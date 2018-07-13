#include "xapipool.h"

XApi::ClientConn *
XApiPool::getConn(std::string fullHostName, uint32_t port, uint8_t isTls)
{
    Entry *ep;
    XApi::ClientConn *connp;
    uint16_t loop;
    Entry *randomBusyEp = NULL;
    uint16_t matchingCount;

    _lock.take();
    
    /* randomize which busy conn to use */
    loop = _loop++;
    if (_loop >= _maxConns)
        _loop = 0;

    matchingCount = 0;
    for(ep = _allConns.head(); ep; ep=ep->_dqNextp) {
        if ( *ep->_bufGenp->getFullHostName() == fullHostName &&
             ep->_isTls == isTls &&
             port == ep->_bufGenp->getPort()) {
            matchingCount++;
            if (!ep->_connp->getBusy()) {
                /* we can use this connection */
                break;
            }
            else {
                if (loop-- >= 0) {
                    /* remember one connection to use if all allowed are busy */
                    randomBusyEp = ep;
                }
            } /* not busy */
        } /* matching connection */
    } /* loop over all cached connections */

    if (ep) {
        /* found an idle connection */
        connp = ep->_connp;
    }
    else if (matchingCount < _maxConns) {
        /* all existing conns are busy, but we're allowed to create a new one */
        ep = new Entry();
        ep->_isTls = isTls;
        if (isTls) {
            BufTls *tlsp;
            ep->_bufGenp = tlsp = new BufTls("");
            tlsp->init(const_cast<char *>(fullHostName.c_str()), port);
            connp = ep->_connp = _xapip->addClientConn(tlsp);
        }
        else {
            BufSocket *socketp;
            ep->_bufGenp = socketp = new BufSocket();
            socketp->init(const_cast<char *>(fullHostName.c_str()), port);
            ep->_connp = _xapip->addClientConn(socketp);
            connp = ep->_connp;
        }
        _allConns.append(ep);
    }
    else {
        /* we have to reuse an existing conn */
        connp = randomBusyEp->_connp;
    }
    _lock.release();

    return connp;
}
