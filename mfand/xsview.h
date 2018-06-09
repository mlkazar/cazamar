#ifndef _XSVIEW_H_ENV__
#define _XSVIEW_H_ENV__ 1

#include <pthread.h>

#include <dqueue.h>
#include <cthread.h>

#include "bufgen.h"
#include "rst.h"

class XSWindow;
class XSView;

/* is this the right abstraction for a web-based screen?  Or should the coordinates
 * be part of it?
 */
class XSScreen {
 public:
    /* send raw HTML to the renderer */
    int32_t send(std::string *datap);
};

/* mainly virtual class handling event driven notifications; you
 * typically subclass this type to get your own type of event.
 */
class XSEvent {
 public:
    static const uint32_t _maxInline = 16384;

    dqueue<Rst::Hdr> *_rcvHdrsp;
    dqueue<Rst::Hdr> *_sendHdrsp;
    XSWindow *_windowp;
    XSView *_viewp;

    Rst::Hdr *find(std::string key);

    Rst::Hdr *findNext(Rst::Hdr *prevp);

    /* pipe for xsview to write to to deliver data to user; user app
     * reads from this pipe.  This will be filled in by XSEvent by
     * the time the sync callback is made.
     */
    int32_t _rcvContentLength;  /* 0, count, or -1 for chunked */
    CThreadPipe _rcvPipe;

    /* pipe for xsview to read to obtain data to send; user app writes
     * outgoing data to this pipe.  This must be filled in by the sync
     * callback if there is any data to transfer.
     */
    int32_t _sendContentLength;  /* 0, count, or -1 for chunked */
    CThreadPipe _sendPipe;

    XSEvent() {
        _rcvHdrsp = NULL;
        _sendHdrsp = NULL;
        _windowp = NULL;
        _viewp = NULL;
    }

    ~XSEvent() {
        Rst::Hdr *hdrp;
        Rst::Hdr *nhdrp;

        for(hdrp = _rcvHdrsp->head(); hdrp; hdrp = nhdrp) {
            nhdrp = hdrp->_dqNextp;
            delete hdrp;
        }
        for(hdrp = _sendHdrsp->head(); hdrp; hdrp = nhdrp) {
            nhdrp = hdrp->_dqNextp;
            delete hdrp;
        }
    }

    void setSendContentLength(int32_t sendLength) {
        _sendContentLength = sendLength;
    }

    int32_t getRcvContentLength() {
        return _rcvContentLength;
    }
};

#if 0
/* typically gets instantiated as an embedded object, or subclassed by
 * those who enjoy puzzling out multiple inheritance.
 */
class XSViewDelegate {
    /* return true if you accept the event, false if you want to continue propagating
     * the event up the view tree.
     */
    virtual int32_t hit(XSEvent *evp) = 0;
};
#endif

class XSPoint {
 public:
    float _x;
    float _y;

    XSPoint(float x, float y) : _x(x), _y(y) {
        return;
    }

    XSPoint() {
        _x = 0.0;
        _y = 0.0;
    }
};

class XSSize {
 public:
    float _width;
    float _height;

    XSSize(float width, float height) : _width(width), _height(height) {
        return;
    }

    XSSize() {
        _width = 0.0;
        _height = 0.0;
    }
};

/* coordinate system has origin at upper left */
class XSFrame {
    XSPoint _origin;
    XSSize _size;

 public:
    XSFrame(float x, float y, float w, float h) : _origin(x,y), _size(w,h){
        return;
    };

    XSFrame() : _origin(0.0, 0.0), _size(0.0, 0.0) {
        return;
    }
};

class XSView {
    friend class XSWindow;

 public:
    typedef int32_t Callback(void *contextp, XSView *viewp, XSEvent *eventp);

 protected:
    Callback *_eventCallbackp;
    void *_eventContextp;
    Callback *_syncCallbackp;
    void *_syncContextp;
    XSWindow *_windowp;
    XSFrame _frame;
    uint32_t _tag;

 public:
    // int32_t setDelegate(XSViewDelegate *delegatep);
    int32_t setFrame(XSFrame *framep) {
        /* XXX todo */
        return 0;
    }

    static int callbackAfterRcv(Rst::Request *reqp);

    XSWindow *getWindow() {
        return _windowp;
    }

    void setEventCallback(Callback *procp, void *contextp) {
        _eventCallbackp = procp;
        _eventContextp = contextp;
    }

    void setSyncCallback(Callback *procp, void *contextp) {
        _syncCallbackp = procp;
        _syncContextp = contextp;
    }

    XSEvent *makeEvent( Rst::Request *reqp,
                        dqueue<Rst::Hdr> *rcvHeadersp,
                        dqueue<Rst::Hdr> *sendHeadersp);

    XSView() {
        _windowp = NULL;
        _eventCallbackp = NULL;
        _eventContextp = NULL;
        _syncCallbackp = NULL;
        _syncContextp = NULL;
        _tag = -1;
    }
};

class XSViewEntry {
 public:
    XSViewEntry *_dqNextp;
    XSViewEntry *_dqPrevp;
    XSView *_viewp;

    XSViewEntry() {
        _viewp = NULL;
    }
};

class XSWindow : public XSView {
    class ListenConn {
    public:
        XSWindow *_windowp;
        BufGen *_bufGenp;
        XSEvent *_currentEventp;
        ListenConn *_dqNextp;
        ListenConn *_dqPrevp;
        pthread_t _listenerId;
        dqueue<Rst::Hdr> _sendHeaders;
        dqueue<Rst::Hdr> _rcvHeaders;

        ListenConn() {
            _windowp = NULL;
            _bufGenp = NULL;
            _currentEventp = NULL;
            _dqNextp = _dqPrevp = NULL;
        }
    };

    uint16_t _port;
    dqueue<ListenConn> _allListenConns;
    dqueue<XSViewEntry> _childViews;

    static int32_t ReqSendProc( void *contextp,
                                Rst::Common *commonp,
                                char *bufferp,
                                int32_t *bufferSizep,
                                uint8_t *morep);

    static int32_t ReqRcvProc ( void *contextp, /* XSWindow */
                                Rst::Common *commonp,
                                char *bufferp,
                                int32_t *bufferSizep,
                                uint8_t *morep);

    static void HeadersProc( void *contextp, /* XSWindow */
                             Rst::Common *commonp,
                             int32_t errorCode,
                             int32_t httpCode);

    static void *listener(void *cxp); /* cxp is XSWindow structure */

    static void *listenConn(void *cxp); /* cxp is ListenConn */

    static void *eventDispatcher(void *contextp);

 public:
    int32_t addPort(uint16_t port);

    int32_t addListener(BufGen *socketp);

    int32_t registerViewByTag(XSView *viewp, uint32_t tag);

    XSView *findViewByTag(uint32_t tag);

    int32_t unregisterTag(uint32_t tag);

    XSWindow() {
        _windowp = this;        /* this assigns to the XSView subclass */
    }
};

class XSPass : public XSView {
 public:
    XSPass();
};

#endif /* _XSVIEW_H_ENV__ */
