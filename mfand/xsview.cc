#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "bufsocket.h"
#include "xsview.h"

/* utility function to create an event from an incoming request in a connection */
XSEvent *
XSView::makeEvent( Rst::Request *reqp,
                   dqueue<Rst::Hdr> *rcvHeadersp,
                   dqueue<Rst::Hdr> *sendHeadersp)
{
    XSEvent *eventp;

    eventp = new XSEvent();
    eventp->_viewp = this;
    eventp->_windowp = getWindow();
    eventp->_rcvHdrsp = rcvHeadersp;
    eventp->_sendHdrsp = sendHeadersp;
    eventp->_rcvContentLength = 0;
    eventp->_sendContentLength = 0;
    return eventp;
}

/* return true if we're supposed to make the headers proc callback
 * only after receiving incoming data.
 */
/* static */ int
XSView::callbackAfterRcv(Rst::Request *reqp)
{
#if 0
    int32_t tlength;
    tlength = reqp->getRcvContentLength();
    if (tlength > 0 && tlength <= XSEvent::_maxInline) {
        return 1;
    }
    else {
        return 0;
    }
#else
    return 0;
#endif
}

/**************** XSWindow ****************/

/* called to fill a buffer to send.  If we run out of data from the
 * event's pipe, we do a short write, otherwise we keep reading 
 * from the pipe until we've read *bufferSizep bytes.
 *
 * Return 0 on success, or a negative error code.  Success should be
 * returned on a short read or EOF.
 */
/* static */ int32_t
XSWindow::ReqSendProc( void *contextp,
             Rst::Common *commonp,
             char *bufferp,
             int32_t *bufferSizep,
             uint8_t *morep)
{
    int32_t code = 0;
    XSWindow::ListenConn *listenConnp = (XSWindow::ListenConn *) contextp;
    XSEvent *eventp = listenConnp->_currentEventp;
    int32_t maxBytes;
    int32_t bytesCopied;

    maxBytes = *bufferSizep;
    bytesCopied = 0;
    while(maxBytes > 0) {
        code = eventp->_sendPipe.read(bufferp, maxBytes);
        printf("Sending %d bytes read from app to network\n", code);
        if (code < 0)
            return code;
        else if (code == 0) {
            /* hit EOF */
            break;
        }

        /* otherwise, we've copied some data, try to get more */
        bytesCopied += code;
    }
    *bufferSizep = bytesCopied;
    return 0;
}

/* called with incoming data from an incoming request; if
 * callbackAfterRcv is true, we do a callback after reading the data,
 * and store the data into a string.
 */
 /* static */ int32_t
XSWindow::ReqRcvProc( void *contextp,
                      Rst::Common *commonp,
                      char *bufferp,
                      int32_t *bufferSizep,
                      uint8_t *morep)
{
    XSWindow::ListenConn *listenConnp = (XSWindow::ListenConn *) contextp;
    XSEvent *eventp = listenConnp->_currentEventp;
    Rst::Request *reqp = static_cast<Rst::Request *>(commonp);
    int32_t code;
    int32_t maxBytes;
    
    printf("Received bytes from %s to send to app\n", reqp->getRcvUrl()->c_str());
    maxBytes = *bufferSizep;
    if (maxBytes == 0) {
        /* make sure that the pipe has received EOF indication; note
         * that *bufferSizep is already set to the desired code.
         */
        eventp->_rcvPipe.write(bufferp, 0);
        return 0;
    }

    code = eventp->_rcvPipe.write(bufferp, maxBytes);

    /* transform short write into a successful call with smaller # of
     * bytes written.
     */
    if (code >= 0) {
        *bufferSizep = code;
        code = 0;
    }

    return code;
}

/* static */ void *
XSWindow::eventDispatcher(void *contextp)
{
    XSEvent *eventp = (XSEvent *) contextp;
    XSView *viewp = eventp->_viewp;

    printf("xsview: in eventdispatcher viewp=%p eventp=%p\n", viewp, eventp);
    (viewp->_eventCallbackp)(viewp->_eventContextp, viewp, eventp);

    printf("xsview: dispatcher for event %p exits\n", eventp);
    pthread_exit(NULL);
}

/* called after headers have been received on an incoming request */
void
XSWindow::HeadersProc( void *contextp,
                       Rst::Common *commonp,
                       int32_t errorCode,
                       int32_t httpCode)
{
    Rst::Request *reqp = static_cast<Rst::Request *>(commonp);
    XSWindow::ListenConn *listenConnp = (XSWindow::ListenConn *) contextp;
    XSWindow *windowp;
    XSEvent *eventp;
    const char *urlp;
    char *tp;
    uint32_t tag;
    XSView *viewp;
    pthread_t junkId;
    int32_t code;
    
    /* called after receipt of incoming headers */
    printf("-->in XSWindows HeadersProc with request '%s' for %s from host %s\n",
           reqp->getRcvOp()->c_str(),
           reqp->getRcvUrl()->c_str(),
           reqp->getRcvHost()->c_str());
    windowp = listenConnp->_windowp;

    /* reqp->getRcvUrl()->c_str() == "/" XXXX to do: use hash table
     * on getRcvUrl to find view to call, not just use windowp.
     */
    /* a new window request */
    eventp = windowp->makeEvent( reqp, &listenConnp->_rcvHeaders, &listenConnp->_sendHeaders);
    listenConnp->_currentEventp = eventp;

    /* parse relative URL to retrieve the tag */
    urlp = reqp->getRcvUrl()->c_str();
    tp = strchr(urlp, '/');
    if (tp != NULL) {
        tag = strtoul(tp+1, NULL, 16);
    }
    else
        tag = strtoul(urlp, NULL, 16);

    /* note that event will be deleted by the connection listener, ListenConn,
     * when it goes to receive the next event, and that won't happen until the
     * thread spawned here terminates the RST call.
     */
    printf("Decoded tag=%d\n", tag);
    if (tag == 0) {
        viewp = windowp;
    }
    else {
        /* may return NULL */
        viewp = windowp->findViewByTag(tag);
        windowp->unregisterTag(tag);
    }

    eventp->_viewp = viewp;

    printf("Dispatching view=%p tag=%d\n", viewp, tag);
    if (viewp) {
        /* copy incoming content length to the event */
        eventp->_rcvContentLength = reqp->getRcvContentLength();

        code = (viewp->_syncCallbackp)(viewp->_eventContextp, viewp, eventp);
        printf("xsview: back from sync callback code=%d\n", code);
        if (code == 0) {
            /* sync callback will have set this if we have data to send back */
            reqp->setSendContentLength(eventp->_sendContentLength);
            printf("xsview: setting send content length %d\n", eventp->_sendContentLength);
            pthread_create(&junkId, NULL, &XSWindow::eventDispatcher, eventp);
        }
    }
    else {
        printf("XSWindow: dismissing event for tag 0x%x\n", (int) tag);
    }
}


/* task main loop to listen for incoming connections */
/* static */ void *
XSWindow::listener(void *cxp)
{
    BufSocket *lsocketp;
    BufGen *socketp;
    int32_t code;
    dqueue<Rst::Hdr> rcvHeaders;
    dqueue<Rst::Hdr> sendHeaders;
    XSWindow *windowp = (XSWindow *) cxp;

    lsocketp = new BufSocket((char *) NULL, windowp->_port);
    lsocketp->listen();

    while(1) {
        code = lsocketp->accept(&socketp);
        if (code < 0) {
            printf("listening socket closed!\n");
            return NULL;
        }
        printf("Received incoming socket %p\n", socketp);
        windowp->addListener(socketp);
        printf("xsevent: spawning new listener for new socket\n");
    }

    return NULL;
}

/* thread main loop for receiving incoming requests from an incoming connection */
/* static */ void *
XSWindow::listenConn(void *cxp)
{
    Rst *rstp;
    XSWindow::ListenConn *listenConnp = (XSWindow::ListenConn *) cxp;
    BufGen *bufGenp;
    int32_t code;
    Rst::Request *reqp;
    dqueue<Rst::Hdr> sendHeaders;
    dqueue<Rst::Hdr> rcvHeaders;
    Rst::Hdr *ccHeaderp;

    bufGenp = listenConnp->_bufGenp;

    ccHeaderp = new Rst::Hdr("Cache-Control", "no-store");

    rstp = new Rst();
    rstp->init(bufGenp);

    while(1) {
        if (listenConnp->_currentEventp) {
            delete listenConnp->_currentEventp;
            listenConnp->_currentEventp = NULL;
        }

        reqp = new Rst::Request(rstp);

        /* call to receive and process an incoming request.
         * Rst::Request will execute and copy data into the event
         * pipes.  The XSEvent's callback will be performed in a
         * separate thread, and we'll wait at the top of the loop
         * until the call is all finished.  So, at most one call per
         * connection is active at once.
         */
        sendHeaders.init();
        sendHeaders.append(ccHeaderp);

        code = reqp->init( ReqSendProc,
                           &sendHeaders,
                           ReqRcvProc,
                           &rcvHeaders,
                           &HeadersProc,
                           listenConnp);
        printf("xsevent: incoming request %p from rst code=%d, listen=%p\n",
               reqp, code, listenConnp);
        if (code < 0) {
            delete reqp;
            delete rstp;
            delete bufGenp;
            delete ccHeaderp;

            /* XXX remove listenConn from window with lock */

            pthread_exit(NULL);
            return NULL;
        }

        /* this also frees any receive headers */
        delete reqp;
    }
}

int32_t
XSWindow::addListener(BufGen *socketp)
{
    ListenConn *listenConnp;

    listenConnp = new ListenConn();
    listenConnp->_windowp = this;
    listenConnp->_bufGenp = socketp;

    /* XXX locking needed against another thread removing an item from this list */
    _allListenConns.append(listenConnp);

    pthread_create(&listenConnp->_listenerId, NULL, &listenConn, listenConnp);
    return 0;
}

int32_t
XSWindow::addPort(uint16_t port)
{
    pthread_t listenerId;
    _port = port;

    pthread_create(&listenerId, NULL, &listener, this);
    return 0;
}

XSView *
XSWindow::findViewByTag(uint32_t tag)
{
    XSViewEntry *vep;
    XSView *viewp;

    for(vep = _childViews.head(); vep; vep=vep->_dqNextp) {
        viewp = vep->_viewp;
        if (viewp->_tag == tag) {
            return viewp;
        }
    }
    return NULL;
}

int32_t
XSWindow::registerViewByTag(XSView *aviewp, uint32_t tag)
{
    XSViewEntry *vep;
    XSView *viewp;
    for(vep = _childViews.head(); vep; vep=vep->_dqNextp) {
        viewp = vep->_viewp;
        if (viewp->_tag == tag) {
            if (viewp == aviewp)
                return 0;
            else {
                osp_assert("XSWindow: duplicate tag" == 0);
            }
        }
    }

    vep = new XSViewEntry();
    aviewp->_tag = tag;
    vep->_viewp = aviewp;
    _childViews.append(vep);
    return 0;
}

int32_t
XSWindow::unregisterTag(uint32_t tag)
{
    XSViewEntry *vep;
    XSView *viewp;
    for(vep = _childViews.head(); vep; vep=vep->_dqNextp) {
        viewp = vep->_viewp;
        if (viewp->_tag == tag) {
            _childViews.remove(vep);
            delete vep;
            break;
        }
    }
    return 0;
}

/**************** XSPass ****************/

XSPass::XSPass()
{
    
}
