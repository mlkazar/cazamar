#include <unistd.h>
#include <stdio.h>

#include "socklocal.h"

void
SockLocalConn::DeliveryTask::start()
{
    Rbuf *bufp;
    std::shared_ptr<SockLocalConn> otherConnp;
    std::shared_ptr<SockLocalLink> linkp;
    RbufRef *trefp;

    setRunMethod(&SockLocalConn::DeliveryTask::start);
    _connp->_lock.take();
    linkp = _connp->_linkp;
    otherConnp = (_connp->_isIncoming? linkp->_clientConnp : linkp->_serverConnp);
    while(1) {
        /* deliveryQueue is a linked list of shared pointers stored with
         * the Rbuf (which is how they also have dqNext/dqPrev pointers)
         *
         * So, we pop off a combo structure, and then copy out the
         * actual shared pointer.
         */
        if ((trefp = _connp->_deliveryQueue.pop()) != NULL) {
            bufp = trefp->_rbufp;
            delete trefp;
            trefp = NULL;

            _connp->_lock.release();
            _outgoingSysp->_clientp->indicatePacket
                (std::static_pointer_cast<SockConn>(otherConnp), bufp);
            _connp->_lock.take();
        }
        else {
            _running = 0;
            _connp->_lock.release();
            return;
        }
    }
    /* not reached, really */
    _connp->_lock.release();
}

/* called with connection lock held */
void
SockLocalConn::DeliveryTask::ensureRunning()
{
    if (!_running) {
        _running = 1;
        queue();
    }
}

/* send a request on a connection */
int32_t
SockLocalConn::send(Rbuf *bufp)
{
    RbufRef *refp;

    refp = new RbufRef();
    _lock.take();
    refp->_rbufp = bufp;
    _deliveryQueue.append(refp);
    _deliveryTask.ensureRunning();
    _lock.release();
    return 0;
}

/* get an outgoing connection to a remote node */
std::shared_ptr<SockConn> 
SockLocalSys::getConnection(SockNode *nodep)
{
    std::shared_ptr<SockLocalConn> connp;
    std::shared_ptr<SockLocalConn> peerConnp;
    std::shared_ptr<SockLocalLink> linkp;
    std::string nodeName = nodep->getName();
    SockLocalOutConnMap::iterator it;
    SockLocalSys *outgoingSysp;

    it = _outConnMap.find(nodeName);
    if (it != _outConnMap.end()) {
        /* we already have an entry */
        return it->second;
    }

    /* otherwise, we create a new node, with us as the source sys, and
     * the target as the destination one.  We also create an incoming
     * connection in the target system, with us as the destination, so
     * the client can respond to us.  We make the packet appear to
     * arrive from that incoming connection, so the server knows who
     * to respond to.
     */
    outgoingSysp = _netp->findSysByName(nodeName);
    if (!outgoingSysp || !outgoingSysp->_listening) {
        return connp;       /* compares equal to null */
    }

    /* create our connection, in our system, with the target sys as the outgoing
     * system.
     */
    connp = SockLocalConn::getLocalConn();
    connp->init(this, outgoingSysp, /* !isIncoming */ 0);
    _outConnMap[nodeName] = connp;

    /* now create the connection's other side which looks like another connection, but
     * one owned by the other side.
     */
    peerConnp = SockLocalConn::getLocalConn();
    peerConnp->init(outgoingSysp, this, /* isIncoming */ 1);

    /* link the connections together before we start using them.  To destroy these, you
     * first reset the pointers from the link back to the connections, and then
     * discard the pointers to the link from the connections.
     */
    linkp = std::make_shared<SockLocalLink>();
    linkp->_clientConnp = connp;
    linkp->_serverConnp = peerConnp;
    peerConnp->_linkp = linkp;
    connp->_linkp = linkp;

    return std::static_pointer_cast<SockConn>(connp);
}

SockLocalSys *
SockLocalNet::findSysByName(std::string hostName) {
    SockLocalSys *sysp;
    uint32_t i;

    _netLock.take();
    sysp = NULL;
    for(i=0;i<_hosts.size();i++) {
        if (_hosts[i]->getName() == hostName) {
            sysp = _hosts[i];
            break;
        }
    }
    _netLock.release();
    return sysp;
}
