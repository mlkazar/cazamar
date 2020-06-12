#ifndef _SOCK_H_ENV__
#define _SOCK_H_ENV__ 1

#include "dqueue.h"

class SockConn;

/* Archticture: an application creates a SockSys object, and registers
 * its identity by calling 'listen' if it expects to be contacted by
 * remote users, rather than only initiating outgoing sends.
 * 
 * Using this as a server requires first creating a SockSys object and
 * calling its listen method with a node description (IP address +
 * port for the IP subclass).
 *
 * If a connArrived method is provided, that function will be called
 * with a connection every time that a new incoming connection is
 * created.  If it is not provided, connections will still be created
 * on receipt, and the requestArrived method will be called as new
 * requests arrive.
 *
 * If a requestArrived method is provided, that function will be
 * called with a SockBuf including the request/packet, and a SockConn
 * indicating from whom the request was sent, every time that a new
 * packet arrives.  The SockConn's send method can be used to send
 * messages back to the originator of the received message.
 *
 * Note that all objects enable shared_from_this, so that callbacks
 * can hold references to buffers or connections.
 */

/* one of these per remote node; specialization is responsible for adding
 * enough information to actually contact the node.
 *
 * This superclass providess the reference count
 *
 * Typically subclassed.
 */
class SockNode {
public:
    std::string _nodeName;
    
    virtual void setName(std::string name) {
        _nodeName = name;
    }

    virtual std::string getName() {
        return _nodeName;
    }

    SockNode(std::string aname) {
        _nodeName = aname;
    }

    SockNode() {
        return;
    }

    /* this needs to be virtual so that a delete of this object will
     * actually destroy the subclass properly.
     */
    virtual ~SockNode() {
        return;
    }
};

/* one of these per data packet; buffers can be stored in a queue.
 *
 * Not typically subclassed.
 */
class SockBuf {
 public:
    std::string _data;

    /* provide a static factory to ensure shared ptr always created */
    static std::shared_ptr<SockBuf> getSockBuf() {
        return std::make_shared<SockBuf>();
    }

    SockBuf() {
        return;
    }
};

class SockBufRef {
public:
    SockBufRef *_dqNextp;
    SockBufRef *_dqPrevp;
    std::shared_ptr<SockBuf> _bufp;

    SockBufRef() {
        _dqNextp = _dqPrevp = NULL;
    }

    static SockBufRef *wrapBuf(std::shared_ptr<SockBuf> bufp) {
        SockBufRef *brefp;
        brefp = new SockBufRef();
        brefp->_bufp = bufp;
        return brefp;
    }
};

/* one of these per operational socket; always subclassed */
class SockConn : public std::enable_shared_from_this<SockConn> {
 public:
    virtual int32_t send( std::shared_ptr<SockBuf> bufp) = 0;

    virtual int isClosed() = 0;

    SockConn() {
        return;
    }

    virtual ~SockConn() {};
};

/* typically subclassed; one of these per user of SockSys.  Virtual
 * interface for callbacks.
 */
class SockClient {
protected:
    /* declare this protected to prevent from calling new instead of make_shared */
    SockClient() {
        return;
    }

public:
    /* required method */
    virtual void indicatePacket( std::shared_ptr<SockConn> connp,
                                 std::shared_ptr<SockBuf> bufp) = 0;

    /* optional */
    virtual void connArrived(std::shared_ptr<SockConn> connp) {
        return;
    };

    virtual ~SockClient() {};
};

class SockSys {
public:
    SockClient *_clientp;
    /* calling listen allows incoming requests */
    virtual void listen(std::string portInfo) = 0;

    /* if we're not listening, we still need a function that we can call to label
     * our SockSys with an address so it can receive responses.
     */
    virtual void ifconfig(SockNode *myNodep) {
        return;
    };

    /* you can use null, but have to set it before doing anything else */
    SockSys(SockClient *clientp) {
        _clientp = clientp;
    }

    void setClient( SockClient *clientp) {
        _clientp = clientp;
    }

    /* get an outgoing connection */
    virtual std::shared_ptr<SockConn> getConnection(SockNode *nodep) = 0;

    virtual ~SockSys() {};
};

#endif /*  _SOCK_H_ENV__ */
