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

#include "xapipool.h"
#include "buftls.h"

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
            ep->_bufGenp = tlsp = new BufTls(_pathPrefix);
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

    /* we mark it as busy before releasing the pool lock so that noone else finds the
     * same connection when there are better non-busy conns.  We release the busy
     * flag in xapi as soon as the call is done.
     */
    connp->setBusy(1);
    _lock.release();

    return connp;
}

void
XApiPool::getStats(XApiPoolStats *statsp)
{
    Entry *ep;
    XApi::ClientConn *connp;
    uint32_t totalActiveMs;
    uint32_t longestActiveMs;
    uint32_t healthyCount;
    uint32_t now;
    int32_t delta;
    uint32_t activeCount;

    _lock.take();

    totalActiveMs = 0;
    longestActiveMs = 0;
    healthyCount = 0;
    activeCount = 0;
    now = osp_time_ms();

    for(ep = _allConns.head(); ep; ep=ep->_dqNextp) {
        connp = ep->_connp;
        if (connp && connp->getBusy()) {
            activeCount++;
            delta = now - connp->getStartMs();
            totalActiveMs += delta;
            if (delta > longestActiveMs)
                longestActiveMs = delta;
            if (delta < 20000)
                healthyCount++;
        }
    }
    
    _lock.release();

    statsp->_longestMs = longestActiveMs;
    if (activeCount > 0)
        statsp->_averageMs = totalActiveMs / activeCount;
    else
        statsp->_averageMs = 0;
    statsp->_healthyCount = healthyCount;
    statsp->_activeCount = activeCount;
}

