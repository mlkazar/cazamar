#ifndef __SOCK_LOCAL_H_ENV__
#define __SOCK_LOCAL_H_ENV__ 1

#include <pthread.h>
#include <string>
#include <map>
#include <vector>

#include "task.h"
#include "sock.h"

class SockLocalConn;
class SockLocalNet;
class SockLocalSys;

typedef SockNode SockLocalNode;

typedef std::map<std::string,std::shared_ptr<SockLocalConn> > SockLocalOutConnMap;

/* the incoming socket map should be indexed by the channel from which the
 * data was received.  It could be a file descriptor for real sockets, or just
 * the sender's string name, in our testbed system.
 */
typedef std::map<std::string,std::shared_ptr<SockLocalConn> > SockLocalInConnMap;

class SockLocalLink : public std::enable_shared_from_this<SockLocalLink> {
public:
    SpinLock _linkLock;
    std::shared_ptr<SockLocalConn> _clientConnp;
    std::shared_ptr<SockLocalConn> _serverConnp;
};

class SockLocalConn : public SockConn {
 public:
    class DeliveryTask : public Task {
        std::shared_ptr<SockLocalConn> _connp;
        int _running;
        SockLocalSys *_outgoingSysp;
        SockClient *_outgoingClientp;

    public:
        void start();

        void ensureRunning();

        void init( std::shared_ptr<SockLocalConn> connp) {
            _connp = std::static_pointer_cast<SockLocalConn>(connp);
            _outgoingSysp = connp->_outgoingSysp;
            _outgoingClientp = connp->_outgoingClientp;
        }

        DeliveryTask() {
            _running = 0;
            _outgoingSysp = NULL;
            setRunMethod(&SockLocalConn::DeliveryTask::start);
        }
    };
    
    SpinLock _lock;
    DeliveryTask _deliveryTask;
    dqueue<RbufRef> _deliveryQueue;
    SockLocalSys *_outgoingSysp;
    SockClient *_outgoingClientp;
    SockLocalSys *_incomingSysp;
    SockClient *_incomingClientp;
    std::shared_ptr<SockLocalLink> _linkp;
    int _isIncoming;
    
    void init( SockLocalSys *incomingSysp,
               SockClient *incomingClientp,
               SockLocalSys *outgoingSysp, 
               SockClient *outgoingClientp,
               int isIncoming) {
        _incomingSysp = incomingSysp;
        _outgoingSysp = outgoingSysp;
        _incomingClientp = incomingClientp;
        _outgoingClientp = outgoingClientp;
        _isIncoming = isIncoming;
        _deliveryTask.init( std::static_pointer_cast<SockLocalConn>(shared_from_this()));
    }

    int isClosed() {
        return 0;
    }

    int32_t send(std::shared_ptr<Rbuf> bufp);

    static std::shared_ptr<SockLocalConn> getLocalConn() {
        std::shared_ptr<SockLocalConn> connp;

        connp = std::make_shared<SockLocalConn>();
        return connp;
    }

    SockLocalConn() {
        _incomingSysp = _outgoingSysp = NULL;
        _incomingClientp = NULL;
        _isIncoming = 0;
        return;
    }

    ~SockLocalConn() {
        return;
    }
};

class SockLocalNet {
public:
    SpinLock _netLock;
    std::vector<SockLocalSys *> _hosts;

    void addHost(SockLocalSys *sysp) {
        _netLock.take();
        _hosts.push_back(sysp);
        _netLock.release();
    }

    SockLocalSys *findSysByName(std::string hostName);
};

class SockLocalSys : public SockSys {
    class Listener {
    public:
        Listener *_dqNextp;
        Listener *_dqPrevp;
        std::string _port;
        SockClient *_clientp;
    };

 private:
    SockLocalOutConnMap _outConnMap;
    SockLocalInConnMap _inConnMap;
    std::string _name;
    SockLocalNet *_netp;
    dqueue<Listener> _allListeners;

 public:
    void ifconfig(SockNode *nodep) {
        _name = nodep->getName();
        _netp->addHost(this);
    }

    void listen(std::string portInfo, SockClient *clientp) {
        /* no ports, so just mark as listening */
        Listener *listp;
        listp = new Listener();
        listp->_port = portInfo;
        listp->_clientp = clientp;
        _allListeners.append(listp);
    }

    Listener *findListener(std::string portName) {
        Listener *listp;
        for(listp = _allListeners.head(); listp; listp=listp->_dqNextp) {
            if (listp->_port == portName)
                return listp;
        }
        return NULL;
    }

    std::string getName() {
        return _name;
    }

    /* get an outgoing connection to the specified target host */
    std::shared_ptr<SockConn> getConnection(SockNode *nodep, std::string port, SockClient *clp);

    SockLocalSys(SockLocalNet *netp) : SockSys () {
        _netp = netp;
    };
};

#endif /* __SOCK_LOCAL_H_ENV__ */
