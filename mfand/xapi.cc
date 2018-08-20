#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "bufgen.h"
#include "bufsocket.h"
#include "xapi.h"

/* task main loop to listen for incoming connections.
 *
 * The basic model is that a connection arrives and we create a
 * listener thread for the connection, executing at
 * XApi::ServerConn::ListenConn; the ServerConn structure is created
 * when the connection arrives.
 *
 * The thread interprets the incoming data by creating an Rst::Request
 * structure and calling its init function.  No data transfers are
 * started until the HeadersProc callback is performed (from this
 * thread).  At that point, the listening thread pops off a
 * UserThread, calls a registered factory to allocate an
 * operation-specific context, and passes that context to the server
 * thread.
 *
 * To give the server thread time to run before the Rst::Request
 * structure is freed, the listening thread sleeps on a condition
 * variable in the XApi::ServerConn structure before freeing the
 * Rst::Request structure.  Of course, we also must handle the case
 * where the server thread finishes before the listening thread.
 */

void
XApi::listener(void *cxp)
{
    BufGen *socketp;
    int32_t code;

    while(1) {
        code = _lsocketp->accept(&socketp);
        if (code < 0) {
            printf("listening socket closed!\n");
            return;
        }
        printf("Received incoming socket %p\n", socketp);
        addNewConn(socketp);
        printf("xapi: spawning new listener for new socket\n");
    }
}

/* external call to initialize a XApi object with an incoming port at which to
 * listen.
 */
void
XApi::initWithPort(uint16_t port)
{
    CThreadHandle *handlep;
    UserThread *utp;
    uint32_t i;

    _port = port;
    _lsocketp = new BufSocket();
    _lsocketp->init((char *) NULL, _port);
    _lsocketp->listen();

    handlep = new CThreadHandle();
    handlep->init((CThread::StartMethod) &XApi::listener, this, NULL);

    for(i=0;i<_numUserThreads;i++) {
        utp = new UserThread();
        utp->init(this);
    }
}

void
XApi::initWithBufGen(BufGen *lsocketp)
{
    CThreadHandle *handlep;
    UserThread *utp;
    uint32_t i;

    _port = 0;
    _lsocketp = lsocketp;
    _lsocketp->listen();

    handlep = new CThreadHandle();
    handlep->init((CThread::StartMethod) &XApi::listener, this, NULL);

    for(i=0;i<_numUserThreads;i++) {
        utp = new UserThread();
        utp->init(this);
    }
}

/* called by Rst to fill the buffer in bufferp/bufferSizep with data from
 * our application.  We copy the data from the supplied pipe to the data being
 * send back from a GET.
 *
 * Return 0 on success, or a negative error code.  Success should be
 * returned on a short read or EOF.
 *
 * End of data is actually indicated by returning 0 bytes, not by
 * setting *morep to 0.
 */
/* static */ int32_t
XApi::ServerConn::ReqSendProc( void *contextp,
                               Rst::Common *commonp,
                               char *bufferp,
                               int32_t *bufferSizep,
                               uint8_t *morep)
{
    int32_t maxSize = *bufferSizep;
    int32_t code;
    XApi::ServerConn *serverConnp = (XApi::ServerConn *) contextp;

    code = serverConnp->_outgoingData.read(bufferp, maxSize);
    if (code < 0) {
        *bufferSizep = 0;
        return code;
    }

    /* EOF is indicated when code == 0 */
    *bufferSizep = code;
    return 0;
}

/* called by Rst to deliver a buffer of data delivered via PUT or POST.
 * The end of the data is signalled by a call with *bufferSizep being
 * zero.
 */
/* static */ int32_t
XApi::ServerConn::ReqRcvProc( void *contextp,
                              Rst::Common *commonp,
                              char *bufferp,
                              int32_t *bufferSizep,
                              uint8_t *morep)
{
    XApi::ServerConn *serverConnp = (XApi::ServerConn *) contextp;
    int32_t dataSize = *bufferSizep;

    if (dataSize == 0) {
        serverConnp->_incomingData.eof();
        return 0;
    }

    serverConnp->_incomingData.write(bufferp, dataSize);
    return 0;
}

/* static */ int32_t
XApi::parseOpFromUrl(std::string *strp, std::string *resultp)
{
    /* XXXX should parse URL */
    *resultp = std::string("test");
    return 0;
}

/* This function is called as soon as the headers have been transferred
 * from the request.  At this point, we fire up the helper thread with a new
 * call.
 */
/* static */ void
XApi::ServerConn::HeadersProc( void *contextp,
                               Rst::Common *commonp,
                               int32_t errorCode,
                               int32_t httpCode)
{
    XApi::ServerConn *serverConnp = (XApi::ServerConn *) contextp;
    XApi::UserThread *userThreadp;
    XApi *xapip;
    Rst::Request *rstReqp = static_cast<Rst::Request *>(commonp);
    int32_t code;
    std::string result;;
    XApi::ServerReq *reqp;

    /* we received a call, so pass the request to a worker thread */
    xapip = serverConnp->_xapip;
    userThreadp = xapip->getUserThread();

    code = parseOpFromUrl(rstReqp->getRcvUrl(), &result);

    /* use the registered factory to create a request, and then pass the request to the
     * helper thread to execute things.
     */
    reqp = xapip->_requestFactoryProcp(&result);
    reqp->_opcode = *rstReqp->getRcvOp();
    reqp->_rstReqp = rstReqp;
    reqp->_incomingDatap = &serverConnp->_incomingData;
    reqp->_outgoingDatap = &serverConnp->_outgoingData;
    reqp->_rcvHeadersp = rstReqp->_rcvHeadersp;
    reqp->_sendHeadersp = rstReqp->_sendHeadersp;
    reqp->_connp = serverConnp;
    reqp->_xapip = xapip;

    /* if no data is coming in, we should mark the input done, and set
     * the input stream so that it looks like we're at EOF, in case
     * someone reads it anyway.
     */
    if (rstReqp->getRcvContentLength() == 0) {
        serverConnp->setInputDone();
        reqp->_incomingDatap->eof();
    }

    /* now wakeup the thread waiting for an incoming request */
    userThreadp->deliverReq(reqp);
}

/* called by Rst once it finishes putting incoming data into the pipe */
void
XApi::ServerConn::InputDoneProc( void *contextp,
                                 Rst::Common *commonp,
                                 int32_t errorCode,
                                 int32_t httpCode)
{
    XApi::ServerConn *serverConnp = (XApi::ServerConn *) contextp;

    /* all data has been received; Rst calls us before generating a
     * response header, and we in turn wait for the user thread to
     * read the data.  If at this point the user thread knows how much
     * data is in the response, we'll be able to generate the exact right
     * content length.
     */
    serverConnp->waitForInputDone();
}
        
/* thread main loop for receiving incoming requests from an incoming connection */
void
XApi::ServerConn::listenConn(void *cxp)
{
    Rst *rstp;
    BufGen *bufGenp = _bufGenp;
    int32_t code;
    Rst::Request *reqp;
    dqueue<Rst::Hdr> sendHeaders;
    dqueue<Rst::Hdr> rcvHeaders;
    Rst::Hdr *ccHeaderp;

    ccHeaderp = new Rst::Hdr("Cache-Control", "no-store");

    rstp = new Rst();
    rstp->init(bufGenp);

    while(1) {
        reqp = new Rst::Request(rstp);

        /* call to receive and process an incoming request.
         * Rst::Request will execute and copy data into the event
         * pipes.  The XApi's callback will be performed in a
         * separate thread, and we'll wait at the top of the loop
         * until the call is all finished.  So, at most one call per
         * connection is active at once.
         */
        sendHeaders.init();
        sendHeaders.append(ccHeaderp);
        _incomingData.reset();
        _outgoingData.reset();

        clearCallDone();
        clearInputDone();

        /* HeadersProc will allocate and wakeup a UserThread from the pool, which
         * can read or write the pipes in the ServerConn.  When it is done, it will
         * signal our call done semaphore, and we'll wakeup  and delete the
         * Rst::Request we've allocated here, and then wait for a new request to come
         * in from the network.
         */
        code = reqp->init( ReqSendProc,
                           &sendHeaders,
                           ReqRcvProc,
                           &rcvHeaders,
                           &HeadersProc,
                           &InputDoneProc, /* called when data input all done */
                           this);
        printf("xapi: incoming request %p from rst code=%d, listen=%p\n",
               reqp, code, this);
        if (code < 0) {
            delete reqp;
            delete rstp;
            delete bufGenp;
            delete ccHeaderp;

            setCallDone();

            _xapip->_lock.take();
            _xapip->_allListenConns.remove(this);
            _xapip->_lock.release();

            pthread_exit(NULL);
            return;
        }

        printf("xapi: about to wait for request done\n");
        waitForCallDone();
        printf("xapi: back from request done\n");

        /* this also frees any receive headers */
        delete reqp;
    } /* loop forever */
}

int32_t
XApi::addNewConn(BufGen *socketp)
{
    ServerConn *serverConnp;

    serverConnp = new ServerConn();
    serverConnp->_xapip = this;
    serverConnp->_bufGenp = socketp;
    serverConnp->_activeReqp = NULL;

    _lock.take();
    _allListenConns.append(serverConnp);
    _lock.release();

    serverConnp->_listenerp = new CThreadHandle();
    serverConnp->_listenerp->init( (CThread::StartMethod) &XApi::ServerConn::listenConn,
                                   serverConnp,
                                   NULL);
    return 0;
}

XApi::UserThread *
XApi::getUserThread()
{
    UserThread *up;

    _lock.take();
    while(1) {
        up = _allUserThreads.pop();
        if (up != NULL) {
            break;
        }
        _userThreadCV.wait();
    }
    _lock.release();
    return up;
}

void
XApi::freeUserThread(XApi::UserThread *up)
{
    int needWakeup;

    _lock.take();
    needWakeup = (_allUserThreads.count() == 0);
    _allUserThreads.append(up);
    _lock.release();

    if (needWakeup)
        _userThreadCV.broadcast();
}

void
XApi::UserThread::deliverReq(CommonReq *reqp)
{
    XApi *xapip = _xapip;

    xapip->_lock.take();
    osp_assert(!_reqp);
    _reqp = reqp;
    _cvp->broadcast();
    xapip->_lock.release();
}

void
XApi::UserThread::init(XApi *xapip)
{
    CThreadHandle *ctp;

    _xapip = xapip;
    _cvp = new CThreadCV(&xapip->_lock);
    _reqp = NULL;

    ctp = new CThreadHandle();
    ctp->init((CThread::StartMethod) &XApi::UserThread::threadInit, this, NULL);
}

/* userthread task executes this code */
void
XApi::UserThread::threadInit(void *contextp)
{
    XApi *xapip = _xapip;
    CommonReq *reqp;

    /* we start off putting ourselves in the free list */
    xapip->freeUserThread(this);

    while(1) {
        xapip->_lock.take();

        while(!_reqp) {
            _cvp->wait();
        }

        reqp = _reqp;
        _reqp = NULL;
        xapip->_lock.release();

        /* run the method set for us */
        reqp->startMethod();

        /* put us back in the available thread pool; any time after this, we may get
         * a new request put into our _reqp field, and then get _cvp signalled.
         */
        xapip->freeUserThread(this);
    }
}

/* to create a client side connection, create a null XApi and then call addClientConn
 * with the socket.
 */
XApi::ClientConn *
XApi::addClientConn(BufGen *bufGenp)
{
    ClientConn *connp;
    XApi::UserThread *utp;

    /* create a client thread for each outstanding connection; perhaps this is overkill? */
    utp = new UserThread();
    utp->init(this);

    connp = new ClientConn(this, bufGenp);
    connp->_xapip = this;
    connp->_bufGenp = bufGenp;

    return connp;
}

/* to make a call, you create a ClientReq and then call its startCall
 * function; the ClientReq type's comments describe the full usage.
 *
 * On the server side, a request comes in on the listener thread,
 * which runs the HTTP state machine, and a new user thread is
 * allocated to run the server code straight through (without
 * callbacks).
 *
 * On the client side, things work the other way around.  An outgoing
 * request allocates a new thread to run the HTTP state machine, and
 * the calling thread keeps running straight through without
 * callbacks.
 */
int32_t
XApi::ClientReq::startCall(ClientConn *connp, const char *relativePathp, reqType isPost)
{
    XApi *xapip = connp->_xapip;

    connp->setBusy(1);

    _userThreadp = xapip->getUserThread();
    _relativePath = std::string(relativePathp);
    _isPost = isPost;
    _connp = connp;
    _error = 0;

    /* bind the client connection */
    _incomingDatap = &connp->_incomingData;
    _outgoingDatap = &connp->_outgoingData;

    /* reset the state of the pipes, basically emptying out any unread data
     * and turning off EOF.
     */
    _incomingDatap->reset();
    _outgoingDatap->reset();

    connp->_mutex.take();
    connp->_headersDone = 0;
    connp->_activeReqp = this;
    connp->_bufGenp->reopen();
    connp->_mutex.release();

    /* this will allocate a thread and run its startMethod on it */
    _userThreadp->deliverReq(this);
    return 0;
}

void
XApi::ClientReq::addHeader(const char *keyp, const char *valuep)
{
    Rst::Hdr *hdrp;

    hdrp = new Rst::Hdr(keyp, valuep);
    _sendHeaders.append(hdrp);
}

int32_t
XApi::ClientReq::waitForHeadersDone()
{
    _connp->waitForHeadersDone();

    return _error;
}

/* static */ int32_t
XApi::ClientReq::callSendProc( void *contextp,
                               Rst::Common *commonp,
                               char *bufferp,
                               int32_t *bufferSizep,
                               uint8_t *morep)
{
    int32_t maxSize = *bufferSizep;
    int32_t code;
    XApi::ClientReq *clientReqp = (XApi::ClientReq *) contextp;

    code = clientReqp->_outgoingDatap->read(bufferp, maxSize);
    if (code < 0) {
        *bufferSizep = 0;
        return code;
    }

    /* EOF is indicated when code == 0 */
    *bufferSizep = code;
    return 0;
}

/* called by Rst to deliver a buffer of data delivered via PUT or POST.
 * The end of the data is signalled by a call with *bufferSizep being
 * zero.
 */
/* static */ int32_t
XApi::ClientReq::callRecvProc( void *contextp,
                               Rst::Common *commonp,
                               char *bufferp,
                               int32_t *bufferSizep,
                               uint8_t *morep)
{
    XApi::ClientReq *clientReqp = (XApi::ClientReq *) contextp;
    int32_t dataSize = *bufferSizep;

    if (dataSize == 0) {
        clientReqp->_incomingDatap->eof();
        return 0;
    }

    clientReqp->_incomingDatap->write(bufferp, dataSize);
    return 0;
}

/* static */ void
XApi::ClientReq::headersDoneProc( void *contextp,
                                  Rst::Common *commonp,
                                  int32_t errorCode,
                                  int32_t httpCode)
{
    XApi::ClientReq *reqp = (XApi::ClientReq *) contextp;
    XApi::ClientConn *connp = reqp->_connp;

    /* save errors */
    reqp->_error = errorCode;
    reqp->_httpError = httpCode;

    connp->setHeadersDone();
}


/* called with a client request to start an outgoing call */
void
XApi::ClientReq::startMethod()
{
    int32_t code;
    Rst::Call *callp;

    _callp = callp = new Rst::Call(_connp->_rstp);

    /* set parameters */
    callp->setSendContentLength(_sendContentLength);
    if (_isPost == reqPost)
        callp->doPost();
    else if (_isPost == reqPut)
        callp->doPut();

    /* this call won't return until the entire request and response have
     * been processed.
     */
    code = callp->init( _relativePath.c_str(),
                        callSendProc,
                        &_sendHeaders,
                        callRecvProc,
                        &_recvHeaders,
                        headersDoneProc,
                        this);
    if (code != 0) {
        _error = code;
        if (!_connp->_headersDone) {
            _connp->setHeadersDone();
        }
        _connp->_incomingData.eof();
        _connp->_outgoingData.eof();
    }
}
