/*

Copyright 2016-2020 Cazamar Systems

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "bufsocket.h"
#include "buftls.h"
#include "sapi.h"

/* task main loop to listen for incoming connections.
 *
 * The basic model is that a connection arrives and we create a
 * listener thread for the connection, executing at
 * SApi::ServerConn::ListenConn; the ServerConn structure is created
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
 * variable in the SApi::ServerConn structure before freeing the
 * Rst::Request structure.  Of course, we also must handle the case
 * where the server thread finishes before the listening thread.
 */

void
SApi::listener(void *cxp)
{
    BufGen *lsocketp;
    BufGen *socketp;
    int32_t code;
    int32_t errorCount = 0;

    while(1) {
        if (_useTls)
            lsocketp = new BufTls(_pathPrefix);
        else
            lsocketp = new BufSocket();
    
        lsocketp->init((char *) NULL, _port);
        lsocketp->listen();

        while(1) {  
            code = lsocketp->accept(&socketp);
            if (code < 0) {
                perror("accept");
                sleep(1);
                errorCount++;
                if (errorCount > 4) {
                    printf("SApi::listener reset listen socket\n");
                    lsocketp->disconnect();
                    delete lsocketp;
                    break;
                }
                continue;
            }
            printf("Received incoming socket %p (%d)\n", socketp, _port);
            errorCount = 0;
            addNewConn(socketp);
            printf("sapi: spawning new listener for new socket\n");
        }
    }
}

/* external call to initialize a SApi object with an incoming port at which to
 * listen.
 */
void
SApi::initWithPort(uint16_t port)
{
    CThreadHandle *handlep;
    UserThread *utp;
    uint32_t i;

    _port = port;

    handlep = new CThreadHandle();
    handlep->init((CThread::StartMethod) &SApi::listener, this, NULL);

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
SApi::ServerConn::ReqSendProc( void *contextp,
                               Rst::Common *commonp,
                               char *bufferp,
                               int32_t *bufferSizep,
                               uint8_t *morep)
{
    int32_t maxSize = *bufferSizep;
    int32_t code;
    SApi::ServerConn *serverConnp = (SApi::ServerConn *) contextp;

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
SApi::ServerConn::ReqRcvProc( void *contextp,
                              Rst::Common *commonp,
                              char *bufferp,
                              int32_t *bufferSizep,
                              uint8_t *morep)
{
    SApi::ServerConn *serverConnp = (SApi::ServerConn *) contextp;
    int32_t dataSize = *bufferSizep;

    if (dataSize == 0) {
        serverConnp->_incomingData.eof();
        return 0;
    }

    serverConnp->_incomingData.write(bufferp, dataSize);
    return 0;
}

/* static */
int32_t
SApi::parseOpFromUrl(std::string *strp, std::string *resultp)
{
    *resultp = *strp;
    return 0;
}

/* This function is called as soon as the headers have been transferred
 * from the request.  At this point, we fire up the helper thread with a new
 * call.
 */
/* static */ void
SApi::ServerConn::HeadersProc( void *contextp,
                               Rst::Common *commonp,
                               int32_t errorCode,
                               int32_t httpCode)
{
    SApi::ServerConn *serverConnp = (SApi::ServerConn *) contextp;
    SApi::UserThread *userThreadp;
    SApi *sapip;
    Rst::Request *rstReqp = static_cast<Rst::Request *>(commonp);
    std::string result;;
    SApi::ServerReq *reqp;

    /* we received a call, so pass the request to a worker thread */
    sapip = serverConnp->_sapip;
    userThreadp = sapip->getUserThread();

    (void) parseOpFromUrl(rstReqp->getRcvUrl(), &result);

    /* use the registered factory to create a request, and then pass the request to the
     * helper thread to execute things.
     */
    reqp = sapip->dispatchUrl(rstReqp->getBaseUrl(), serverConnp, sapip);
    if (!reqp) {
        rstReqp->setHttpError(404);
        serverConnp->setInputDone();
        serverConnp->setCallDone();
        sapip->freeUserThread(userThreadp);
        return;
    }

    reqp->_opcode = *rstReqp->getRcvOp();
    reqp->_rstReqp = rstReqp;
    reqp->_incomingDatap = &serverConnp->_incomingData;
    reqp->_outgoingDatap = &serverConnp->_outgoingData;
    reqp->_rcvHeadersp = rstReqp->_rcvHeadersp;
    reqp->_sendHeadersp = rstReqp->_sendHeadersp;
    reqp->_connp = serverConnp;
    reqp->_sapip = sapip;

    /* see if we got an "id" cookie */
    if (strcasecmp(rstReqp->_cookieId.c_str(), "id") == 0) {
        reqp->_cookieEntryp = sapip->findCookieEntry(&rstReqp->_cookieValue);
    }

    /* if no data is coming in, we should mark the input done, and set
     * the input stream so that it looks like we're at EOF, in case
     * someone reads it anyway.
     */
    if (rstReqp->getRcvContentLength() == 0) {
        reqp->_incomingDatap->eof();
    }

    /* now wakeup the thread waiting for an incoming request */
    userThreadp->deliverReq(reqp);
}

/* called by Rst once it finishes putting incoming data into the pipe */
void
SApi::ServerConn::InputDoneProc( void *contextp,
                                 Rst::Common *commonp,
                                 int32_t errorCode,
                                 int32_t httpCode)
{
    SApi::ServerConn *serverConnp = (SApi::ServerConn *) contextp;

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
SApi::ServerConn::listenConn(void *cxp)
{
    Rst *rstp;
    BufGen *bufGenp = _bufGenp;
    int32_t code;
    Rst::Request *reqp;
    dqueue<Rst::Hdr> sendHeaders;
    dqueue<Rst::Hdr> rcvHeaders;
    Rst::Hdr *ccHeaderp;

    ccHeaderp = new Rst::Hdr("Cache-Control", "no-store, no-cache");

    sendHeaders.init();
    sendHeaders.append(ccHeaderp);

    rstp = new Rst();
    rstp->init(bufGenp);

    while(1) {
        reqp = new Rst::Request(rstp);

        /* call to receive and process an incoming request.
         * Rst::Request will execute and copy data into the event
         * pipes.  The SApi's callback will be performed in a
         * separate thread, and we'll wait at the top of the loop
         * until the call is all finished.  So, at most one call per
         * connection is active at once.
         */
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
        if (code < 0) {
            delete reqp;
            delete rstp;
            delete bufGenp;
            delete ccHeaderp;

            setCallDone();

            _sapip->_lock.take();
            _sapip->_allListenConns.remove(this);
            _sapip->_lock.release();

            pthread_exit(NULL);
            return;
        }

        waitForCallDone();

        /* this also frees any receive headers */
        delete reqp;
    } /* loop forever */
}

/* static */ int32_t
SApi::ServerConn::interpretFile(const char *fileNamep, SApi::Dict *dictp, std::string *responsep)
{
    FILE *filep;
    int tc;
    int inDollar;
    char opcode;
    std::string parm;
    std::string interpretedParm;
    int32_t code;

    responsep->erase();
    filep = fopen(fileNamep, "r");
    if (!filep)
        return -1;

    inDollar = 0;
    while(1) {
        tc = fgetc(filep);
        if (tc < 0)
            break;
        if (!inDollar) {
            if (tc == '$') {
                inDollar = 1;
                opcode = 0;
            }
            else {
                responsep->append(1, tc);
            }
        }
        else {
            /* in a dollar sign region */
            if (opcode == 0) {
                /* about to read the opcode */
                opcode = tc;
                if (opcode == '$') {
                    /* this is a '$$' string, which just turns into a $ character */
                    responsep->append(1, '$');
                    inDollar = 0;
                    continue;
                }

                tc = fgetc(filep);
                if (tc != ':') {
                    /* this is all we expect with 1 char opcodes */
                    fclose(filep);
                    return -2;
                }
                parm.erase();
            }
            else {
                /* opcode parsed, get the rest of the string */
                if (tc == '$') {
                    /* we're done parsing the parameter string */
                    code = interpretParm(opcode, &parm, &interpretedParm, dictp);
                    if (code) {
                        fclose(filep);
                        return code;
                    }
                    responsep->append(interpretedParm);
                    inDollar = 0;
                }
                else {
                    /* just another character in the parameter string */
                    parm.append(1, tc);
                }
            } /* saw opcode after '$' */
        } /* if '$' */
    } /* while */

    fclose(filep);
    return 0;
}

/* static */ int32_t
SApi::ServerConn::interpretParm( int opcode,
                                 std::string *parmp,
                                 std::string *outParmp,
                                 SApi::Dict *dictp)
{
    if (opcode == 'v') {
        if (dictp->lookup(*parmp, outParmp) == 0) {
            return 0;
        }
        else
            return -4;
    }
    else return -3;
}

void
SApi::ServerReq::setCookieKey(std::string key, void *cxp)
{
    CookieKey *cookieKeyp;
    
    if (_cookieEntryp == NULL) {
        _cookieEntryp = setCookie();
    }

    for( cookieKeyp = _cookieEntryp->_allKVs.head(); 
         cookieKeyp;
         cookieKeyp = cookieKeyp->_dqNextp) {
        if (key == cookieKeyp->_key) {
            cookieKeyp->_valuep = cxp;
            return;
        }
    }

    cookieKeyp = new CookieKey();
    cookieKeyp->_key = key;
    cookieKeyp->_valuep = cxp;
    _cookieEntryp->_allKVs.append(cookieKeyp);
}

void *
SApi::ServerReq::getCookieKey(std::string key)
{
    CookieKey *cookieKeyp;

    if (_cookieEntryp == NULL)
        return NULL;
    for(cookieKeyp = _cookieEntryp->_allKVs.head(); 
        cookieKeyp;
        cookieKeyp = cookieKeyp->_dqNextp) {
        if (key == cookieKeyp->_key)
            return cookieKeyp->_valuep;
    }

    return NULL;
}

SApi::CookieEntry *
SApi::ServerReq::setCookie()
{
    int32_t partA;
    int32_t partB;
    char idBuffer[32];
    char cookieBuffer[1024];
    SApi::CookieEntry *entryp;

#ifdef __linux__
    random_r(&_sapip->_randomBuf, &partA);
    random_r(&_sapip->_randomBuf, &partB);
#else
    partA = random();
    partB = random();
#endif

    snprintf(idBuffer, sizeof(idBuffer), "%08x%08x", partA, partB);
    _cookieId = idBuffer;

    snprintf(cookieBuffer, sizeof(cookieBuffer), "id=%s", idBuffer);

    addHeader("Set-Cookie", cookieBuffer);
    entryp = _sapip->addCookieState(_cookieId);

    return entryp;
}

SApi::CookieEntry *
SApi::addCookieState(std::string cookieId)
{
    CookieEntry *ep;

    ep = new CookieEntry();
    ep->_cookieId = cookieId;
    _lock.take();
    _allCookieEntries.append(ep);
    _lock.release();

    return ep;
}

int32_t
SApi::Dict::lookup(std::string inStr, std::string *outp)
{
    Elt *ep;

    for(ep = _all.head(); ep; ep=ep->_dqNextp) {
        if (ep->_key == inStr) {
            *outp = ep->_value;
            return 0;
        }
    }
    return -1;
}

void
SApi::Dict::erase()
{
    Elt *ep;
    Elt *nep;

    for(ep=_all.head(); ep; ep=nep) {
        nep = ep->_dqNextp;
        delete ep;
    }
}

int32_t
SApi::Dict::add(std::string inStr, std::string newStr)
{
    Elt *ep;

    for(ep = _all.head(); ep; ep=ep->_dqNextp) {
        if (ep->_key == inStr) {
            /* update the value */
            ep->_value = newStr;
            return 0;
        }
    }

    ep = new Elt;
    ep->_key = inStr;
    ep->_value = newStr;
    _all.append(ep);
    return 0;
}

int32_t
SApi::addNewConn(BufGen *socketp)
{
    ServerConn *serverConnp;

    serverConnp = new ServerConn();
    serverConnp->_sapip = this;
    serverConnp->_bufGenp = socketp;
    serverConnp->_activeReqp = NULL;

    _lock.take();
    _allListenConns.append(serverConnp);
    _lock.release();

    serverConnp->_listenerp = new CThreadHandle();
    serverConnp->_listenerp->init( (CThread::StartMethod) &SApi::ServerConn::listenConn,
                                   serverConnp,
                                   NULL);
    return 0;
}

SApi::UserThread *
SApi::getUserThread()
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
SApi::freeUserThread(SApi::UserThread *up)
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
SApi::UserThread::deliverReq(ServerReq *reqp)
{
    SApi *sapip = _sapip;

    sapip->_lock.take();
    osp_assert(!_reqp);
    _reqp = reqp;
    _cvp->broadcast();
    sapip->_lock.release();
}

void
SApi::UserThread::init(SApi *sapip)
{
    CThreadHandle *ctp;

    _sapip = sapip;
    _cvp = new CThreadCV(&sapip->_lock);
    _reqp = NULL;

    ctp = new CThreadHandle();
    ctp->init((CThread::StartMethod) &SApi::UserThread::threadInit, this, NULL);
}

/* userthread task executes this code */
void
SApi::UserThread::threadInit(void *contextp)
{
    SApi *sapip = _sapip;
    ServerReq *reqp;

    /* we start off putting ourselves in the free list */
    sapip->freeUserThread(this);

    while(1) {
        sapip->_lock.take();

        while(!_reqp) {
            _cvp->wait();
        }

        reqp = _reqp;
        _reqp = NULL;
        sapip->_lock.release();

        /* run the method set for us */
        (reqp->*(reqp->_startMethodp))();

        /* put us back in the available thread pool; any time after this, we may get
         * a new request put into our _reqp field, and then get _cvp signalled.
         */
        sapip->freeUserThread(this);
    }
}

void
SApi::registerUrl( const char *urlp,
                   SApi::RequestFactory *requestFactoryp,
                   SApi::StartMethod startMethodp)
{
    UrlEntry *urlEntryp;

    _lock.take();
    for(urlEntryp = _allUrls.head(); urlEntryp; urlEntryp=urlEntryp->_dqNextp) {
        if (strcmp(urlEntryp->_urlPath.c_str(), urlp) == 0) {
            urlEntryp->_startMethodp = startMethodp;
            urlEntryp->_requestFactoryp = requestFactoryp;
            _lock.release();
            return;
        }
    }

    urlEntryp = new UrlEntry();
    urlEntryp->_urlPath = std::string(urlp);
    urlEntryp->_requestFactoryp = requestFactoryp;
    urlEntryp->_startMethodp = startMethodp;
    _allUrls.append(urlEntryp);
    _lock.release();
}

SApi::ServerReq *
SApi::dispatchUrl(std::string *urlp, SApi::ServerConn *connp, SApi *sapip)
{
    UrlEntry *entryp;
    SApi::ServerReq *reqp;

    _lock.take();
    for(entryp = sapip->_allUrls.head(); entryp; entryp = entryp->_dqNextp) {
        if (*urlp == entryp->_urlPath) {
            _lock.release();
            reqp = entryp->_requestFactoryp( sapip);
            reqp->_startMethodp = entryp->_startMethodp;
            return reqp;
        }
    }
    _lock.release();
    return NULL;
}
