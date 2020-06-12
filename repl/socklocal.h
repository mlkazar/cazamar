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

    public:
        void start();

        void ensureRunning();

        void init( std::shared_ptr<SockLocalConn> connp) {
            _connp = std::static_pointer_cast<SockLocalConn>(connp);
            _outgoingSysp = connp->_outgoingSysp;
        }

        DeliveryTask() {
            _running = 0;
            _outgoingSysp = NULL;
            setRunMethod(&SockLocalConn::DeliveryTask::start);
        }
    };
    
    SpinLock _lock;
    DeliveryTask _deliveryTask;
    dqueue<SockBufRef> _deliveryQueue;
    SockLocalSys *_incomingSysp;
    SockLocalSys *_outgoingSysp;
    std::shared_ptr<SockLocalLink> _linkp;
    int _isIncoming;
    
    void init( SockLocalSys *incomingSysp,
               SockLocalSys *outgoingSysp, 
               int isIncoming) {
        _incomingSysp = incomingSysp;
        _outgoingSysp = outgoingSysp;
        _isIncoming = isIncoming;
        _deliveryTask.init( std::static_pointer_cast<SockLocalConn>(shared_from_this()));
    }

    int isClosed() {
        return 0;
    }

    int32_t send(std::shared_ptr<SockBuf> bufp);

    static std::shared_ptr<SockLocalConn> getLocalConn() {
        std::shared_ptr<SockLocalConn> connp;

        connp = std::make_shared<SockLocalConn>();
        return connp;
    }

    SockLocalConn() {
        _incomingSysp = _outgoingSysp = NULL;
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
 private:
    SockLocalOutConnMap _outConnMap;
    SockLocalInConnMap _inConnMap;
    std::string _name;
    int _listening;
    SockLocalNet *_netp;

 public:
#if 0
    std::shared_ptr<SockLocalConn> getLocalInConn( std::string name) {
        std::shared_ptr<SockLocalConn> connp;
        SockLocalInConnMap::iterator it;
        SockLocalSys *incomingSysp;

        it = _inConnMap.find(name);
        if (it != _inConnMap.end()) {
            /* we already have an entry */
            return it->second;
        }

        incomingSysp = _netp->findSysByName(name);
        if (!incomingSysp) {
            return connp;
        }

        /* otherwise, we create a new node */
        connp = SockLocalConn::getLocalConn();
        connp->init(incomingSysp, this, /* isIncoming */ 1);
        _inConnMap[name] = connp;       /* do we need this?  As a map? */

        return connp;
    }
#endif

    void ifconfig(SockNode *nodep) {
        _name = nodep->getName();
        _netp->addHost(this);
    }

    void listen(std::string portInfo) {
        /* no ports, so just mark as listening */
        _listening = 1;
    }

    std::string getName() {
        return _name;
    }

    /* get an outgoing connection to the specified target host */
    std::shared_ptr<SockConn> getConnection(SockNode *nodep);

    SockLocalSys(SockClient *clientp, SockLocalNet *netp) : SockSys(clientp) {
        _listening = 0;
        _netp = netp;
    };
};

#endif /* __SOCK_LOCAL_H_ENV__ */
