#ifndef _SAPI_H_ENV__
#define _SAPI_H_ENV__ 1

#include <stdlib.h>
#include "dqueue.h"
#include "cthread.h"

#include <string>

#include "bufgen.h"
#include "rst.h"
#include "dqueue.h"

/* parent class; instantiated once per listening socket or client call
 * stream.
 */
class SApi : public CThread {
 public:
    class ServerReq;
    class ClientReq;
    class UserThread;
    class CookieEntry;
    class CookieKey;

    typedef SApi::ServerReq *UrlCallback(std::string *urlp, SApi *sapip);

    static const uint32_t _numUserThreads = 4;

 public:
    typedef ServerReq *(ServerFactory)(std::string *opcodep, SApi *sapip);

    class UrlEntry {
    public:
        std::string _urlPath;
        UrlCallback *_callback;
        UrlEntry *_dqNextp;
        UrlEntry *_dqPrevp;
    };

    class Dict {
        class Elt {
        public:
            std::string _key;
            std::string _value;
            Elt *_dqNextp;
            Elt *_dqPrevp;
        };

        dqueue<Elt> _all;

    public:
        void erase();

        int32_t add(std::string instr, std::string outstr);

        int32_t lookup(std::string instr, std::string *outp);
    };

    class CookieEntry {
    public:
        CookieEntry *_dqNextp;
        CookieEntry *_dqPrevp;
        dqueue<CookieKey> _allKVs;
        std::string _cookieId;

        CookieEntry() {
            _dqNextp = NULL;
            _dqPrevp = NULL;
        }
    };

    class CookieKey {
    public:
        CookieEntry *_cookieEntryp;
        std::string _key;
        void *_valuep;
        CookieKey *_dqNextp;
        CookieKey *_dqPrevp;

        CookieKey() {
            _cookieEntryp = NULL;
            _dqPrevp = NULL;
            _dqPrevp = NULL;
            _valuep = NULL;
        }
    };

    /* for delivering requests from user threads */
    class CommonReq {
    public:
        SApi *_sapip;
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
            _sapip = NULL;
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
        SApi *_sapip;
        Rst *_rstp;

        CThreadMutex _mutex;

        uint8_t _headersDone;
        CThreadCV _headersDoneCV;

        ClientReq *_activeReqp;

        ClientConn(SApi *sapip, BufGen *bufGenp) : _headersDoneCV(&_mutex) {
            _bufGenp = bufGenp;
            _sapip = sapip;

            _rstp = new Rst();
            _rstp->init(bufGenp);

            _headersDone = 0;
            _activeReqp = NULL;
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
    };

    /* class representing one connection */
    class ServerConn : public CThread {
    public:
        CThreadPipe _incomingData;
        CThreadPipe _outgoingData;
        BufGen *_bufGenp;
        SApi *_sapip;

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
            _sapip = NULL;
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

        SApi *getSApi() {
            return _sapip;
        }

        static int32_t interpretFile(char *fileNamep, Dict *dictp, std::string *responsep);

        static int32_t interpretParm( int opcode,
                                      std::string *parmNamep,
                                      std::string *outParmp,
                                      SApi::Dict *dictp);

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

    /* The server requests are handled by first registering a URL dispatch
     * with an SApi instance (see sapitest.cc), using SApi::registerUrl
     * and then setting the service TCP port using initWithPort.
     *
     * The server factory function allocates a new service object, which must
     * be a subclass of SApi::ServerReq; the factory will be called on each
     * new incoming call.
     *
     * The new call's startMethod function will be called, which
     * typically starts by calling SApi::ServerReq::getIncomingPipe
     * and SApi::ServerReq::getOutgoingPipe with itself.  Any incoming
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
        SApi *_sapip;
        std::string _opcode;            /* name of opcode */
        dqueue<Rst::Hdr> *_rcvHeadersp;
        dqueue<Rst::Hdr> *_sendHeadersp;
        SApi::ServerConn *_connp;
        Rst::Request *_rstReqp;
        std::string _cookieId;
        CookieEntry *_cookieEntryp;  /* looked up based on incoming cookie */

        ServerReq *_dqNextp;    /* in allServerReqs */
        ServerReq *_dqPrevp;

        ServerReq(SApi *sapip) {
            _cookieEntryp = NULL;
            _sapip = sapip;
            sapip->_allServerReqs.append(this);
            return;
        }

        virtual ~ServerReq() {
            _sapip->_allServerReqs.remove(this);
            _sapip = NULL;

            /* nothing else to do, since the CThreadPipes are actually
             * part of the associated connection.  But we want to
             * provide a virtual destructor so that someone who
             * deletes a generic ServerReq will run the subclassed
             * destructor.
             */
            return;
        }

        Rst::Hdr *getRecvHeaders() {
            return _rcvHeadersp->head();
        }

        void *getCookieKey(std::string key);

        void setCookieKey(std::string key, void *contextp);

        CookieEntry *getCookie() {
            return _cookieEntryp;
        }

        CookieEntry *setCookie();

        SApi *getSApi() {
            return _sapip;
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

        Rst::Request *getRstReq() {
            return _rstReqp;
        }
        
        ServerConn *getConn() {
            return _connp;
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
        SApi *_sapip;

        UserThread() {
            return;
        }

        void init(SApi *sapip);

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
    dqueue<ServerReq> _allServerReqs;
    dqueue<CookieEntry> _allCookieEntries;

    dqueue<UrlEntry> _allUrls;

    ServerFactory *_requestFactoryProcp;

    void listener(void *cxp);

    UserThread *getUserThread();

    void freeUserThread(SApi::UserThread *up);

    CookieEntry *addCookieState(std::string cookieId);

 public:
    /* Externally callable functions */
    void registerUrl(const char *relativePathp, SApi::UrlCallback procp);

    ServerReq *dispatchUrl(std::string *urlp, SApi::ServerConn *connp, SApi *sapip);

    int32_t addNewConn(BufGen *socketp);

    static int32_t parseOpFromUrl(std::string *strp, std::string *resultp);

    void initWithPort(uint16_t port);

    ClientConn *addClientConn(BufGen *bufGenp);

    void useTls() {
        _useTls = 1;
    }

    SApi() : _userThreadCV(&_lock) {
#ifdef __linux__
        _randomBuf.state = NULL;
        initstate_r(time(0) + getpid(),
                    _randomState,
                    sizeof(_randomState),
                    &_randomBuf);
#else
        srandomdev();
#endif

        _useTls = 0;
        return;
    }

    CookieEntry *findCookieEntry(std::string *strp) {
        CookieEntry *ep;
        for(ep = _allCookieEntries.head(); ep; ep=ep->_dqNextp) {
            if (ep->_cookieId == *strp) {
                return ep;
            }
        }
        return NULL;
    }

    void *getContext() {
        return _contextp;
    }

    void setContext(void *acontextp) {
        _contextp = acontextp;
    }

 private:
    /* protects userThread lists, and associated _userThreadCV for allocating a
     * user thread.
     */
    CThreadMutex _lock;

#ifdef __linux__
    random_data _randomBuf;
    char _randomState[64];
#endif

    /* note that userThreadCV must follow _lock so that the latter
     * gets constructed first, since userThreadCV's constructor is
     * passed the address of the _lock.
     */
    CThreadCV _userThreadCV;    /* waiting for userThread to get freed */

    uint16_t _port;     /* listening port for incoming API requests */

    uint8_t _useTls;

    void *_contextp;

    /* internal functions */
};

#endif /* _SAPI_H_ENV__ */
