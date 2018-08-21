#ifndef _XAPI_H_ENV__
#define _XAPI_H_ENV__ 1

#include <stdlib.h>
#include "dqueue.h"
#include "cthread.h"

#include <string>

#include "bufgen.h"
#include "rst.h"

/* parent class; instantiated once per listening socket or client call
 * stream.
 */
class XApi : public CThread {
 public:
    class ServerReq;
    class ClientReq;
    class UserThread;

    enum reqType {
        reqGet = 0,
        reqPost = 1,
        reqPut = 2};

    static const uint32_t _numUserThreads = 4;

 private:
    /* protects userThread lists, and associated _userThreadCV for allocating a
     * user thread.
     */
    CThreadMutex _lock;

    /* note that userThreadCV must follow _lock so that the latter
     * gets constructed first, since userThreadCV's constructor is
     * passed the address of the _lock.
     */
    CThreadCV _userThreadCV;    /* waiting for userThread to get freed */

    uint16_t _port;     /* listening port for incoming API requests */
    BufGen *_lsocketp;  /* listening socket */

 public:
    typedef ServerReq *(ServerFactory)(std::string *opcodep);

    /* for delivering requests from user threads */
    class CommonReq {
    public:
        XApi *_xapip;
        CThreadPipe *_incomingDatap;
        CThreadPipe *_outgoingDatap;

        virtual void startMethod() = 0; /* start running here */

        CThreadPipe *getIncomingPipe() {
            return _incomingDatap;
        }

        CThreadPipe *getOutgoingPipe() {
            return _outgoingDatap;
        }

        CommonReq() {
            _xapip = NULL;
            _incomingDatap = NULL;
            _outgoingDatap = NULL;
        }

        virtual ~CommonReq() {
            /* pipes are owned by connection, not by us */
            return;
        }
    };

    class ClientConn : public CThread {
    public:
        CThreadPipe _incomingData;
        CThreadPipe _outgoingData;
        BufGen *_bufGenp;
        XApi *_xapip;
        Rst *_rstp;
        uint8_t _busy;

        CThreadMutex _mutex;

        uint8_t _headersDone;
        CThreadCV _headersDoneCV;
        uint8_t _allDone;
        CThreadCV _allDoneCV;
        CThreadCV _busyCV;

        ClientReq *_activeReqp;

        ClientConn(XApi *xapip, BufGen *bufGenp) :
        _headersDoneCV(&_mutex),
            _allDoneCV(&_mutex),
            _busyCV(&_mutex) {
            _bufGenp = bufGenp;
            _xapip = xapip;

            _rstp = new Rst();
            _rstp->init(bufGenp);

            _headersDone = 0;
            _busy = 0;
            _activeReqp = NULL;
        }

        /* if setting busy on a connection, wait until previous user is done with it;
         * and if clearing busy, signal anyone waiting that they can proceed.
         */
        void setBusy(uint8_t busy) {
            _mutex.take();
            if (busy) {
                while(_busy) {
                    _busyCV.wait();
                }
                _busy = 1;
            }
            else {
                _busy = 0;
                _busyCV.broadcast();
            }
            _mutex.release();
        }

        uint8_t getBusy() {
            /* no locking; busy can change right after this call anyway */
            return _busy;
        }

        void setHeadersDone() {
            _mutex.take();
            _headersDone = 1;
            _mutex.release();
            _headersDoneCV.broadcast();
        }

        void waitForHeadersDone() {
            _mutex.take();
            while(1) {
                if (_headersDone)
                    break;
                _headersDoneCV.wait();
            }
            _mutex.release();
        }

        void setAllDone() {
            _mutex.take();
            _allDone = 1;
            _mutex.release();
            _allDoneCV.broadcast();
        }

        void waitForAllDone() {
            _mutex.take();
            while(1) {
                if (_allDone)
                    break;
                _allDoneCV.wait();
            }
            _mutex.release();
        }
    };

    /* The user begin by creating a new XApi::ClientReq, adding any
     * required headers via addHeader, and calling setSendContentLength
     * if any data needs to be sent with the request; a value of -1 uses
     * chunked encoding for use when the caller doesn't know the length
     * of the data at the start of the call.
     *
     * The call is then started by calling startCall with the client
     * connection, the relative path to the web service (starting with
     * a "/") and a flag that's true iff we're to do an HTTP POST instead of a
     * GET.
     *
     * The user then calls getOutgoingPipe to get the pipe for sending data,
     * and writes data to the pipe (don't forget to call CThreadPipe::eof when
     * done).
     *
     * The client then calls waitForHeadersDone, which waits until the
     * HTTP headers on the response have been received, and returns any
     * error code from HTTP.  0 is returned on success (instead of 200).
     *
     * Then the user can calll getRecvHeaders to get any Rst::Hdr structures
     * showing returned data, and can call getIncomingPipe to get a pipe to
     * use to read the response data.
     *
     * The user *must* call waitForEof on the incoming pipe before
     * deleting the request below, since until that time, the Rst
     * state machine, and thus this state machine, may still be
     * running.  Once we set Eof, we promise not to access any of the
     * Rst or XApi structures again for this request.
     *
     * Finally, when all done, the user calls delete on the
     * XApi::ClientReq.
     */
    class ClientReq : public CommonReq {
    public:
        ClientConn *_connp;
        UserThread *_userThreadp;
        reqType _isPost;
        std::string _relativePath;
        int32_t _error;
        int32_t _httpError;
        int32_t _sendContentLength;
        dqueue<Rst::Hdr> _sendHeaders;
        dqueue<Rst::Hdr> _recvHeaders;
        Rst::Call *_callp;

        ClientReq() {
            _connp = NULL;
            _userThreadp = NULL;
            _isPost = reqGet;
            _error = 0;
            _sendContentLength = 0;
            _callp = NULL;
        }

        virtual ~ClientReq() {
            Rst::Hdr *hdrp;
            Rst::Hdr *nhdrp;
            for(hdrp = _sendHeaders.head(); hdrp; hdrp=nhdrp) {
                nhdrp = hdrp->_dqNextp;
                delete hdrp;
            }
            _sendHeaders.init();

            /* TBD: do we really need to do this and free it from the rst code, too? */
            for(hdrp = _recvHeaders.head(); hdrp; hdrp=nhdrp) {
                nhdrp = hdrp->_dqNextp;
                delete hdrp;
            }
            _recvHeaders.init();

            /* userThread actually self-destructs when the call completes */
            _userThreadp = NULL;

            /* make sure we're done with the call on this connection, and then mark
             * the connection as available for reallocation.  Don't reference connp after
             * this, as it doesn't belong to us any more.
             */
            osp_assert(_connp->_allDone);
            _connp->setBusy(0);
            _connp = NULL;

#if 1
            if (_callp) {
                delete _callp;
                _callp = NULL;
            }
#endif
        }

        Rst::Hdr *getRecvHeaders() {
            return _recvHeaders.head();
        }

        void setSendContentLength(int32_t sendContentLength) {
            _sendContentLength = sendContentLength;
        }

        static void allDoneProc( void *contextp,
                                 Rst::Common *commonp,
                                 int32_t errorCode,
                                 int32_t httpCode);

        int32_t startCall(ClientConn *connp, const char *relativePathp, reqType isPost);

        int32_t waitForHeadersDone();

        int32_t waitForAllDone();

        void resetConn() {
            if (_connp)
                _connp->_bufGenp->disconnect();
        }

        int32_t getError() {
            return _error;
        }

        int32_t getHttpError() {
            return (_error == 0? _httpError : _error);
        }

        void addHeader(const char *keyp, const char *valuep);

    private:
        /* called internally to start helper userThread, which runs the HTTP 
         * state machine.
         */
        void startMethod();

        static int32_t callRecvProc( void *contextp,
                                     Rst::Common *commonp,
                                     char *bufferp,
                                     int32_t *bufferSizep,
                                     uint8_t *morep);

        static int32_t callSendProc( void *contextp,
                                     Rst::Common *commonp,
                                     char *bufferp,
                                     int32_t *bufferSizep,
                                     uint8_t *morep);

        static void headersDoneProc( void *contextp,
                                     Rst::Common *commonp,
                                     int32_t errorCode,
                                     int32_t httpCode);
    };

    /* class representing one connection */
    class ServerConn : public CThread {
    public:
        CThreadPipe _incomingData;
        CThreadPipe _outgoingData;
        BufGen *_bufGenp;
        XApi *_xapip;

        /* for allListenConns */
        ServerConn *_dqNextp;
        ServerConn *_dqPrevp;

        CThreadHandle *_listenerp;
        CThreadMutex _mutex;

        ServerReq *_activeReqp;

        /* state of current call */
        uint8_t _callDone;
        CThreadCV _callDoneCV;  /* associated with conn's _mutex */

        uint8_t _inputDone;
        CThreadCV _inputDoneCV; /* associated with conn's _mutex */

        void listenConn(void *cxp);

    public:
        ServerConn () : _callDoneCV(&_mutex), _inputDoneCV(&_mutex) {
            _callDone = 0;
            _inputDone = 0;
            _xapip = NULL;
        }

        void clearCallDone() {
            _mutex.take();
            _callDone = 0;
            _mutex.release();
        }

        void setCallDone() {
            _mutex.take();
            _callDone = 1;
            _mutex.release();
            _callDoneCV.broadcast();
        }

        void waitForCallDone() {
            _mutex.take();
            while(!_callDone) {
                _callDoneCV.wait();
            }
            _mutex.release();
        }

        void setInputDone() {
            _mutex.take();
            _inputDone = 1;
            _mutex.release();
            _inputDoneCV.broadcast();
        }

        void waitForInputDone() {
            _mutex.take();
            while(!_inputDone) {
                _inputDoneCV.wait();
            }
            _mutex.release();
        }

        void clearInputDone() {
            _mutex.take();
            _inputDone = 0;
            _mutex.release();
        }

        static int32_t ReqRcvProc( void *contextp,
                                   Rst::Common *commonp,
                                   char *bufferp,
                                   int32_t *bufferSizep,
                                   uint8_t *morep);

        static int32_t ReqSendProc( void *contextp,
                                    Rst::Common *commonp,
                                    char *bufferp,
                                    int32_t *bufferSizep,
                                    uint8_t *morep);

        static void HeadersProc( void *contextp,
                                 Rst::Common *commonp,
                                 int32_t errorCode,
                                 int32_t httpCode);
        static void InputDoneProc( void *contextp,
                                   Rst::Common *commonp,
                                   int32_t errorCode,
                                   int32_t httpCode);
    };

    /* The server requests are handled by first registering a factory function
     * with an XApi instance (see xapitest.cc), using XApi::registerFactory,
     * and then setting the service TCP port using initWithPort.
     *
     * The server factory function allocates a new service object, which must
     * be a subclass of XApi::ServerReq; the factory will be called on each
     * new incoming call.
     *
     * The new call's startMethod function will be called, which
     * typically starts by calling XApi::ServerReq::getIncomingPipe
     * and XApi::ServerReq::getOutgoingPipe with itself.  Any incoming
     * headers can be obtained by calling getRecvHeaders to get a pointer to
     * the first Rst::Hdr in the request, and then following the _dqNextp pointer to
     * the remainder of the list.
     *
     * Incoming data can be read from the pipe (EOF is indicated by a
     * zero byte read response).  Once the request is ready to send a
     * response, it calls setSendContentLength to indicate how much
     * data will be sent in the body of the response (optionally; if
     * no data is returned the call need not be made).
     *
     * Any return headers can be added by calling addHeader.  Finally inputReceived
     * is called to indicate that it is time to generate the response header.
     *
     * If any response data is to be sent, it is written to the output
     * pipe at this time, and if any data is sent, the pipe's eof()
     * function must be called when all of the data has been written
     * (it need not be called if there is no data to send at all.
     *
     * Finally requestDone indicates that the call is complete; the
     * server request may be deleted any time after this call returns,
     * so be sure your startMethod doesn't reference any instance
     * variables in the request after this point.
     */
    class ServerReq : public CommonReq {
    public:
        std::string _opcode;            /* name of opcode */
        dqueue<Rst::Hdr> *_rcvHeadersp;
        dqueue<Rst::Hdr> *_sendHeadersp;
        XApi *_xapip;
        XApi::ServerConn *_connp;
        Rst::Request *_rstReqp;

        ServerReq() {
            return;
        }

        virtual ~ServerReq() {
            /* nothing to do, since the CThreadPipes are actually part
             * of the associated connection.  But we want to provide a
             * virtual destructor so that someone who deletes a
             * generic ServerReq will run the subclassed destructor.
             */
            return;
        }

        Rst::Hdr *getRecvHeaders() {
            return _rcvHeadersp->head();
        }

        void addHeader(const char *keyp, const char *valuep) {
            Rst::Hdr *hdrp;

            hdrp = new Rst::Hdr();
            hdrp->_key = std::string(keyp);
            hdrp->_value = std::string(valuep);
            _sendHeadersp->append(hdrp);
        }

        void headerDone(int32_t httpError) {
            return;
        }

        void requestDone() {
            _connp->setCallDone();
        }

        void inputReceived() {
            _connp->setInputDone();
        }

        int32_t getRcvContentLength() {
            return _rstReqp->getRcvContentLength();
        }

        void setSendContentLength(int32_t sendLength, int32_t httpError = 200) {
            _rstReqp->setSendContentLength(sendLength);
        }
    };

    class UserThread : public CThread {
    public:
        /* condition variable for delivering request to a just-woken user thread */
        CThreadCV *_cvp;        /* uses connp's mutex */

        /* filled in on a per-request basis */
        CommonReq *_reqp;

        UserThread *_dqNextp;
        UserThread *_dqPrevp;
        XApi *_xapip;

        UserThread() {
            return;
        }

        void init(XApi *xapip);

        ~UserThread() {
            if (_cvp) {
                delete _cvp;
                _cvp = NULL;
            }
        }

        void deliverReq(CommonReq *reqp);

        void threadInit(void *contextp);

    };

 private:

    dqueue<UserThread> _allUserThreads;
    dqueue<ServerConn> _allListenConns;

    ServerFactory *_requestFactoryProcp;

    void listener(void *cxp);

    UserThread *getUserThread();

    void freeUserThread(XApi::UserThread *up);

 public:
    /* Externally callable functions */
    int32_t registerFactory(ServerFactory *factoryp) {
        _requestFactoryProcp = factoryp;
        return 0;
    }

    int32_t addNewConn(BufGen *socketp);

    void initWithPort(uint16_t port);

    void initWithBufGen(BufGen *lsocketp);

    ClientConn *addClientConn(BufGen *bufGenp);

    static int32_t parseOpFromUrl(std::string *strp, std::string *resultp);

    XApi() : _userThreadCV(&_lock) {
        _lsocketp = NULL;
        return;
    }

 private:
    /* internal functions */
};
#endif /* _XAPI_H_ENV__ */
