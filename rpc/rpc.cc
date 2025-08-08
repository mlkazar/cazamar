#include <poll.h>
#include <errno.h>
#include <stdio.h>

#include "rpc.h"

/*================RpcListener================*/

void
RpcListener::init(Rpc *rpcp, RpcServer *serverp, uint16_t v4Port)
{
    _rpcp = rpcp;
    _serverp = serverp;
    _v4Port = v4Port;

    _listenerThreadp = new CThreadHandle();
    _listenerThreadp->init((CThread::StartMethod) &RpcListener::listen, this, NULL);
}

/* socket accept listener, for incoming connections */
void
RpcListener::listen(void *contextp)
{
    struct sockaddr_in sockAddr;
    int32_t code;
    int newFd;
    RpcConn *connp;
    int opt;

    _listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (_listenSocket < 0) {
        printf("Rpc: socket call failed %d\n", errno);
        return;
    }

    opt = 1;
    code = setsockopt(_listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (code < 0) {
        printf("Rpc: resueaddr code %d failed\n", errno);
        return;
    }

#ifndef __linux__
    sockAddr.sin_len = sizeof(sockAddr);
#endif
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_addr.s_addr = htonl(0);
    sockAddr.sin_port = htons(_v4Port);
    code = bind(_listenSocket, (struct sockaddr *) &sockAddr, sizeof(sockAddr));
    if (code < 0) {
        printf("Rpc: bind call failed %d\n", errno);
        return;
    }

    code = ::listen(_listenSocket, 10);
    if (code < 0) {
        printf("Rpc: listen failed %d\n", errno);
        return;
    }

    while(1) {
        struct sockaddr taddr;
        socklen_t taddrLen;

        newFd = accept(_listenSocket, (struct sockaddr *) &taddr, &taddrLen);
        if (newFd < 0) {
            printf("Rpc: listener accept failed %d\n", errno);
            return;
        }

        connp = new RpcConn(_rpcp, this);
        connp->initServer(newFd);    /* start conn */
    }
}

/*================RpcConn================*/

void
RpcConn::initBase()
{
    _receiveChain.init(_queueLimit);
    _sendChain.init(_queueLimit);
    _bodyChain.init(_queueLimit);

    _lastActiveMs = osp_time_ms();
    _dqNextp = _dqPrevp = NULL;

    _sendCallActivep = NULL;
    _receiveCallActivep = NULL;

    /* send up a callback from _sendChain to actually deliver data */
    _sendChain.setCallback(&RpcConn::sendData, this);
}

void
RpcConn::initServer(int fd) {
    initBase();
    int32_t code;
    uint32_t peerNameSize;
    int opt;

    peerNameSize = sizeof(_peerAddr);
    code = getpeername(fd, (struct sockaddr *) &_peerAddr, &peerNameSize);
    if (code < 0)
        _peerAddr.sin_addr.s_addr = 0;

    _isClient = 0;
    _fd = fd;

    opt = 1;
    code = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&opt, sizeof(opt));
    if (code == -1) {
        printf("Rpc: setsockopt server NODELAY failed code=%d\n", errno);
    }

    /* bump ref count twice, once for each task; only decremented once
     * corresponding task exits.
     */
    _refCount = 2;

    _listenerDone = 0;
    _listenerThreadp = new CThreadHandle();
    _listenerThreadp->init((CThread::StartMethod) &RpcConn::listenFd, this, NULL);

    _helperDone = 0;
    _helperThreadp = new CThreadHandle();
    _helperThreadp->init((CThread::StartMethod) &RpcConn::helper, this, NULL);
}

void
RpcConn::initClient() {
    initBase();

    _isClient = 1;
}

/* internal callback invoked when data arrives at an outgoing pipe */
/* static */ int32_t
RpcConn::sendData(RpcSdr *sdrp, void *cxp)
{
    OspMBuf *mbufsp;
    OspMBuf *nbufp;
    uint32_t byteCount;
    RpcConn *connp = (RpcConn *) cxp;
    uint32_t tcount;
    int32_t rcode=0;
    int32_t code;

    mbufsp = sdrp->popAll(&byteCount);
    /* write all data from mbufsp */
    while(mbufsp) {
        nbufp = mbufsp->_dqNextp;
        if ((tcount = mbufsp->dataBytes()) > 0) {
            code = (int32_t) write(connp->_fd, mbufsp->data(), tcount);
            if (code != (signed) tcount) {
                printf("Rpc: write returned %d, expected %d\n", code, tcount);
                rcode = -1;
                break;
            }
        }

        /* move to next */
        delete mbufsp;
        mbufsp = nbufp;
    }

    /* if we broke early, free the rest */
    OspMBuf::freeList(mbufsp);

    return rcode;
}

/* called with the RPC lock held */
RpcConn::~RpcConn()
{
    _rpcp->_allConns.remove(this);
}

/* the FD listener gets an FD from the RpcConn constructor; this is one of
 * two threads running in the RpcConn.
 */
void
RpcConn::listenFd(void *contextp)
{
    int32_t code;
    char tbuffer[_listenSize];
    OspMBuf *mbufp;
    struct pollfd pollFd;
    int fd;

    /* from constructor */
    fd = _fd;

    /* loop reading data and pushing into receiveChain */
    while(1) {
        pollFd.fd = fd;
        pollFd.events = POLLIN;
        pollFd.revents = 0;

        code = ::poll(&pollFd, 1, /* wait forever */ -1);
        if (code < 0) {
            /* failed */
            printf("poll terminate fd=%d\n", fd);
            terminate("poll");
            return;
        }

        osp_assert(code != 0);

        code = (int32_t) ::read(fd, &tbuffer, sizeof(tbuffer));
        if (code <= 0) {
            /* EOF or error */
            printf("read failed fd=%d\n", fd);
            terminate("read");
            return;
        }

        mbufp = OspMBuf::alloc(code);
        mbufp->pushNBytes(tbuffer, code);
        _receiveChain.appendMBuf(mbufp);
    }
}

/* context is a null pointer; the second of 2 threads running in a
 * conn.  This one performs the RPC protocol processing for incoming
 * traffic.
 */
void
RpcConn::helper(void *contextp)
{
    RpcClientContext *clientp;
    RpcServerContext *serverContextp;
    int32_t code;
    uint32_t opcode;

    while(1) {
        /* wait for lock allowing reading an entire packet from the socket */
        waitForReceive(&_anonContext);

        /* unmarshal a header's worth of data; terminate socket on failure */
        code = _header.marshal(&_receiveChain, /* unmarshal */ 0);
        if (code) {
            releaseReceive();
            helperDone("bad receive");
            return;
        }

        /* now parse the header and see what's up */
        if (_header._version != RpcHeader::_currentVersion) {
            releaseReceive();
            helperDone("bad version");
            return;
        }

        /* generate a response header from the request header */
        _header.generateResponse(&_responseHeader);

        /* lookup the server involved */
        if (!_isClient) {
            _serverp = _rpcp->getServerById(&_header._serviceId);
            if (!_serverp) {
                releaseReceive();
                _responseHeader._error = RpcHeader::_errBadService;
                sendHeaderResponse(&_responseHeader);
                continue;
            }
        } else {
            osp_assert(_serverp != nullptr);
        }

        switch (_header._opcode) {
            case RpcHeader::_opRequest:
                /* pull out opcode if possible */
                code = _receiveChain.copyLong(&opcode, /* !marshal */ 0);
                if (code) {
                    releaseReceive();
                    helperDone("no opcode");
                    return;
                }

                serverContextp = _serverp->getContext(opcode);
                if (!serverContextp) {
                    _responseHeader._opcode = RpcHeader::_opResponse;
                    _responseHeader._error = RpcHeader::_errBadOpcode;
                    reverseConn();
                    sendHeaderResponse(&_responseHeader);
                    releaseSend();
                    return;
                }

                serverContextp->setServer(_serverp);
                serverContextp->setConn(this);

                /* we didn't have the context until now */
                exchangeReceiveOwner(serverContextp);

                /* the server context is called with a receive locked conn, and reverses
                 * it to become a send locked conn.
                 */
                code = serverContextp->serverMethod(_serverp, &_receiveChain, &_bodyChain);
                _responseHeader._opcode = RpcHeader::_opResponse;
                _responseHeader._error = code;

                if (code >= 0) {
                    _responseHeader._size = _bodyChain.bytes();
                }
                else {
                    _responseHeader._size = 0;
                }

                _responseHeader.marshal(&_sendChain, /* marshal */ 1);
                if (code >= 0) {
                    _sendChain.append(&_bodyChain);
                }
                else {
                    _bodyChain.free();
                }

                /* release the send side of the connection */
                releaseSendNL();

                serverContextp->release();
                break;

            case RpcHeader::_opResponse:
                _rpcp->_lock.take();
                clientp = _serverp->findContext(_header._requestId);
                if (!clientp) {
                    releaseReceiveNL();
                    _rpcp->_lock.release();
                    helperDone("No response");
                    return;
                }

                exchangeReceiveOwnerNL(clientp);

                /* at this point, it is the waiting call's responsibility to release
                 * the receive lock on the connection, once it is done unparsing
                 * the response stream.
                 */
                clientp->_haveResponse = 1;
                clientp->_srLocked = 2;
                if (clientp->_waitingForResponse) {
                    clientp->_waitingForResponse = 0;
                    clientp->_recvResponseCV.broadcast();
                }
                _rpcp->_lock.release();
                break;

            case RpcHeader::_opAbort:
                /* nothing to do yet */
                releaseReceive();
                break;

                /*received by server side */
            case RpcHeader::_opOpen:
                reverseConn();
                _header.generateResponse(&_responseHeader);
                _responseHeader._opcode = RpcHeader::_opOpenResponse;
                sendHeaderResponse(&_responseHeader);
                releaseSend();
                break;

            case RpcHeader::_opOpenResponse:
                /* dequeue any queued client calls */
                _rpcp->_lock.take();

                releaseReceiveNL();

                _serverp->_isOpen = 1;
                osp_assert(_serverp->_opening);
                _serverp->_opening = 0;
                if (_serverp->_openWaitersPresent) {
                    _serverp->_openWaitersCV.broadcast();
                    _serverp->_openWaitersPresent = 0;
                }

                _rpcp->_lock.release();
                break;

            case RpcHeader::_opPing:
                reverseConn();
                updateActivity();

                _responseHeader._opcode = RpcHeader::_opPingResponse;
                _responseHeader._error = 0;
                sendHeaderResponse(&_responseHeader);
                releaseSend();
                break;

            case RpcHeader::_opPingResponse:
                releaseReceive();
                updateActivity();
                break;

            default:
                releaseReceive();
                helperDone("bad opcode");
                return;
        } /* switch on opcode */

        /* here we have a send locked conn */
    } /* loop */
}

void
RpcConn::helperDone(const char *strp)
{
    _rpcp->_lock.take();
    _helperDone = 1;
    _rpcp->_lock.release();

    /* from this point on, we better not reference any variables in RpcConn */

    /* drop ref count from helper thread */
    release();
}

int32_t
RpcConn::sendHeaderResponse(RpcHeader *headerp)
{
    return headerp->marshal(&_sendChain, /* marshal */ 1);
}

void
RpcConn::terminate(const char *whyp)
{
    close(_fd);
    _fd = -1;
    _listenerDone = 1;

    /* a server conn, so no one will ever reuse this */
    _shutdownInProgress = 1;
    _receiveChain.abort();
    _sendChain.abort();

    /* drop reference from listener thread */
    release();
}

void
RpcConn::updateActivity()
{
    _rpcp->_lock.take();
    _lastActiveMs = osp_time_ms();
    _rpcp->_lock.release();
}

void
RpcConn::waitForSendNL(RpcContext *ownerp)
{
    while(_sendCallActivep != NULL) {
        _sendCallCV.wait();
    }
    _sendCallActivep = ownerp;
}

void
RpcConn::releaseSendNL()
{
    osp_assert(_sendCallActivep != NULL);
    _sendCallActivep = NULL;
    _sendCallCV.broadcast();
}

void
RpcConn::waitForReceiveNL(RpcContext *ownerp)
{
    while(_receiveCallActivep != NULL) {
        _receiveCallCV.wait();
    }
    _receiveCallActivep = ownerp;
}

void
RpcConn::releaseReceiveNL()
{
    osp_assert(_receiveCallActivep != NULL);
    _receiveCallActivep = NULL;
    _receiveCallCV.broadcast();
}

void
RpcConn::reverseConnNL()
{
    RpcContext *contextp;

    contextp = _receiveCallActivep;
    osp_assert(contextp != NULL);

    releaseReceiveNL();
    waitForSendNL(contextp);
}

int32_t
RpcConn::setupConn()
{
    int s;
    int32_t code;
    int opt;
    struct sockaddr_in localAddr;

    _rpcp->_lock.take();
    while(1) {
        if (_connecting || _shutdownInProgress) {
            _openCV.wait();
            continue;
        }

        if (_connected) {
            _rpcp->_lock.release();
            return 0;
        }

        _connecting = 1;
        _rpcp->_lock.release();
        break;
    }

    if (_fd != -1) {
        close(_fd);
        _fd = -1;
    }

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        printf("Rpc: addListener - socket call failed %d\n", errno);
        _connecting = 0;
        _openCV.broadcast();
        return errno;
    }

    opt = 1;
    code = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (code < 0) {
        close(s);
        printf("Rpc: resueaddr code %d failed\n", errno);
        _connecting = 0;
        _openCV.broadcast();
        return errno;
    }

#ifndef __linux__
    localAddr.sin_len = sizeof(localAddr);
#endif
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = htonl(0);
    localAddr.sin_port = htons(0);
    code = bind(s, (struct sockaddr *) &localAddr, sizeof(localAddr));
    if (code < 0) {
        close(s);
        printf("Rpc: addClient: bind call failed %d\n", errno);
        _connecting = 0;
        _openCV.broadcast();
        return errno;
    }

    code = connect(s, (struct sockaddr *) &_peerAddr, sizeof(_peerAddr));
    if (code != 0) {
        printf("Rpc: addClient connect failed %d\n", errno);
        close(s);
        _connecting = 0;
        _openCV.broadcast();
        return errno;
    }

    opt = 1;
    code = setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&opt, sizeof(opt));
    if (code == -1) {
        printf("Rpc: setsockopt NODELAY failed code=%d\n", errno);
        close(s);
        _connecting = 0;
        _openCV.broadcast();
        return errno;
    }

    _rpcp->_lock.take();
    osp_assert(_connecting);
    _connecting = 0;
    _connected = 1;
    _fd = s;

    /* reset SDRs */
    _receiveChain.reset();
    _bodyChain.reset();
    _sendChain.reset();

    _rpcp->_lock.release();

    /* restart any helper threads that exited */
    if (_helperDone) {
        _rpcp->_lock.take();
        _helperDone = 0;
        holdNL();       /* for reference from the helper thread */
        _rpcp->_lock.release();

        _helperThreadp = new CThreadHandle();
        _helperThreadp->init((CThread::StartMethod) &RpcConn::helper, this, NULL);
    }

    if (_listenerDone) {
        _rpcp->_lock.take();
        _listenerDone = 0;
        holdNL();       /* for the reference from the listener thread */
        _rpcp->_lock.release();

        _listenerThreadp = new CThreadHandle();
        _listenerThreadp->init((CThread::StartMethod) &RpcConn::listenFd, this, NULL);
    }

    _openCV.broadcast();

    return 0;
}

void
RpcConn::checkShutdownNL() {
    RpcClientContext *ccallp;

    if (_shutdownInProgress) {
        if (_helperDone && _listenerDone) {
            _shutdownInProgress = 0;
            _connected = 0;

            /* wakeup anyone waiting for open / close / shutdown transitions */
            _openCV.broadcast();

            if (_isClient) {
                /* wakeup any client calls waiting for a response */
                for(ccallp = _serverp->_clientCalls.head(); ccallp; ccallp = ccallp->_dqNextp) {
                    if (ccallp->_connp == this) {
                        ccallp->_haveResponse = 1;
                        ccallp->_failed = 1;
                        /* don't set _srLocked to 2, since we didn't pass any connection lock */
                        if (ccallp->_waitingForResponse) {
                            ccallp->_waitingForResponse = 0;
                            ccallp->_recvResponseCV.broadcast();
                        }
                    }
                } /* for loop over client calls */
            } /* if isClient */
        } /* if both processes done */
    } /* if shutdown in progress */
}

void
RpcConn::getPeerAddr(uint32_t *ipAddrp, uint32_t *portp)
{
    uint32_t tempAddr;
    memcpy(&tempAddr, &_peerAddr.sin_addr.s_addr, sizeof(tempAddr));
    *ipAddrp = ntohl(tempAddr);
    if (portp) {
        *portp = ntohs(_peerAddr.sin_port);
    }
}

/*================RpcContext================*/

void
RpcContext::getPeerAddr(uint32_t *ipAddrp, uint32_t *portp)
{
    _connp->getPeerAddr(ipAddrp, portp);
}

/*================RpcSdr================*/

void
RpcSdr::appendMBuf(OspMBuf *appendDatap)
{
    RpcSdr::NotifyProc *notifyProcp = NULL;
    void *notifyContextp = NULL;

    _lock.take();
    _bufs.append(appendDatap);
    _byteCount += appendDatap->dataBytes();

    if (_subType == IsIn) {
        /* wakeup anyone waiting for data to arrive */
        _cv.broadcast();
    }
    else if (_subType == IsOut) {
        /* notify other end data is available for transmission */
        if (_notifyProcp) {
            notifyProcp = _notifyProcp;
            notifyContextp = _notifyContextp;
        }
    }

    _lock.release();

    /* delay until we've dropped our locks, in case someone calls back into this
     * module.
     */
    if (notifyProcp)
        (*notifyProcp)(this, notifyContextp);
}

int32_t
RpcSdr::copyCountedBytes(char *targetp, uint32_t nbytes, int isMarshal)
{
    OspMBuf *mbp;
    char *datap;
    uint32_t tcount;
    RpcSdr::NotifyProc *notifyProcp = NULL;
    void *notifyContextp = NULL;

    _lock.take();
    if (isMarshal) {
        /* copy data into the SDR buffer, or add a new buffer */
        while (nbytes > 0) {
            if (_aborted) {
                _lock.release();
                return -1;
            }

            mbp = _bufs.tail();
            if (!mbp || mbp->bytesAtEnd() == 0) {
                /* add another mbuf */
                mbp = OspMBuf::alloc(OspMBuf::_defaultSize);
                _bufs.append(mbp);
            }

            tcount = mbp->bytesAtEnd();
            if (tcount > nbytes)
                tcount = nbytes;
            mbp->pushNBytes(targetp, tcount);
            _byteCount += tcount;
            nbytes -= tcount;
        }
    }
    else {
        /* unmarshal */
        while(nbytes > 0) {
            if (_aborted) {
                _lock.release();
                return -1;
            }

            mbp = _bufs.head();
            if (!mbp) {
                _blocked = 1;
                _cv.wait();
                continue;
            }

            tcount = mbp->dataBytes();
            if (tcount > nbytes)
                tcount = nbytes;
            if (tcount > 0) {
                datap = mbp->popNBytes(tcount);
                memcpy(targetp, datap, tcount);
                nbytes -= tcount;
                _byteCount -= tcount;
            }

            if (mbp->dataBytes() == 0) {
                mbp = _bufs.pop();
                delete mbp;
            }
        }
    }

    /* wake anyone waiting for data */
    if (_subType == IsIn) {
        if (_blocked) {
            _blocked = 0;
            _cv.broadcast();
        }
    }
    else if (_subType == IsOut) {
        /* outgoing data */
        if (_notifyProcp) {
            notifyProcp = _notifyProcp;
            notifyContextp = _notifyContextp;
        }
    }
    _lock.release();

    if (notifyProcp)
        (*notifyProcp)(this, notifyContextp);

    /* we're done */
    return 0;
}


uint32_t
RpcSdr::bytes()
{
    uint32_t count;

    _lock.take();
    count = _byteCount;
    _lock.release();

    return count;
}

/*================RpcSdrIn================*/

void
RpcSdrIn::doNotify()
{
    _lock.take();
    if (_blocked) {
        _blocked = 0;
        _cv.broadcast();
    }
    _lock.release();
}

/*================RpcSdrOut================*/
void
RpcSdrOut::doNotify()
{
    RpcSdr::NotifyProc *notifyProcp = NULL;
    void *notifyContextp = NULL;

    /* outgoing data */
    _lock.take();
    if (_notifyProcp) {
        notifyProcp = _notifyProcp;
        notifyContextp = _notifyContextp;
    }
    _lock.release();

    if (notifyProcp)
        notifyProcp(this, notifyContextp);
}


/*================Rpc================*/
int32_t
Rpc::addClientConn(struct sockaddr_in *destAddrp, RpcConn **connpp)
{
    RpcConn *connp;

    connp = new RpcConn(this, NULL);
    memcpy(&connp->_peerAddr, destAddrp, sizeof(struct sockaddr_in));
    connp->_isClient = 1;

    /* start receive processing */
    connp->initClient();
    connp->hold();

    *connpp = connp;

    return 0;
}

RpcServer *
Rpc::addServer(RpcServer *aserverp, uuid_t *serviceIdp)
{
    RpcServer *serverp;
    _lock.take();

    if (!aserverp) {
        serverp = new RpcServer(this);
    } else {
        serverp = aserverp;
    }

    memcpy(&serverp->_serviceId, serviceIdp, sizeof(uuid_t));

    if (aserverp != nullptr)
        _allServers.append(serverp);

    _lock.release();

    return serverp;
}

void
Rpc::init()
{
    return;
}

RpcServer *
Rpc::getServerById(uuid_t *idp) {
    RpcServer *serverp;
    _lock.take();
    for(serverp = _allServers.head(); serverp; serverp=serverp->_dqNextp) {
        if (memcmp(&serverp->_serviceId, idp, sizeof(uuid_t)) == 0)
            break;
    }
    _lock.release();
    return serverp;
}

/*================RpcServer================*/
/* called with rpc lock held */
RpcClientContext *
RpcServer::findContext(uint32_t requestId)
{
    RpcClientContext *cxp;

    for(cxp = _clientCalls.head(); cxp; cxp=cxp->_dqNextp) {
        if (cxp->_requestId == requestId)
            break;
    }

    return cxp;
}

/*================RpcClientContext================*/
/* must be called with lock held */
int32_t
RpcClientContext::waitForOpenNL()
{
    RpcServer *serverp = _serverp;
    while (serverp->_opening) {
        if (_connp->failedNL()) {
            printf("Rpc: open reports failed conn\n");
            return -1;
        }
        serverp->_openWaitersPresent = 1;
        serverp->_openWaitersCV.wait();
    }
    return 0;
}

int32_t
RpcClientContext::openServerNL()
{
    RpcServer *serverp = _serverp;
    RpcConn *connp = _connp;
    RpcHeader openHeader;
    int32_t code = 0;
    Rpc *rpcp = _rpcp;

    if (serverp->_isOpen) {
        return 0;
    }

    code = waitForOpenNL();
    if (code)
        return code;

    if (!serverp->_isOpen) {
        serverp->_opening = 1;
        openHeader.setupBasic(serverp);
        openHeader._opcode = RpcHeader::_opOpen;

        rpcp->_lock.release();
        openHeader.marshal(&connp->_sendChain, /* doMarshal */ 1);
        rpcp->_lock.take();

        code = waitForOpenNL();
    }

    return code;
}

/* convention is that if makeCall returns success, you MUST call finishCall to adjust state
 * back to 'no active calls'
 */
int32_t
RpcClientContext::makeCall(RpcConn *connp, uint32_t opcode, RpcSdr **callSdrpp, RpcSdr **respSdrpp)
{
    Rpc *rpcp;
    int32_t code;

    _connp = connp;
    _serverp = connp->_serverp;
    _rpcp = rpcp = connp->_rpcp;
    _appOpcode = opcode;
    _failed = 0;

    rpcp->_lock.take();
    osp_assert(!_counted);
    _serverp->_clientCalls.append(this);
    connp->_activeClientCalls++;
    _counted = 1;
    rpcp->_lock.release();

    code = connp->setupConn();
    if (code) {
        _failed = 1;
        cleanup();
        return code;
    }

    /* make sure server's 'connection' is open */
    rpcp->_lock.take();
    code = openServerNL();
    if (code) {
        _failed = 1;
        rpcp->_lock.release();
        cleanup();
        return code;
    }

    /* init these for context on each call */
    _haveResponse = 0;
    _waitingForResponse = 0;

    /* wait until we're allowed to send on the transmit side before
     * returning pointers to RpcSdrs to our caller.
     */
    connp->waitForSendNL(this);
    _srLocked = 1;

    rpcp->_lock.release();

    *callSdrpp = &connp->_bodyChain;
    *respSdrpp = &connp->_receiveChain;
    return 0;
}

int32_t
RpcClientContext::getResponse()
{
    RpcHeader header;
    Rpc *rpcp;
    int32_t code;
    uint32_t requestId;

    rpcp = _connp->_rpcp;

    rpcp->_lock.take();

    if (_failed) {
        rpcp->_lock.release();
        cleanup();
        return -1;
    }

    requestId = _serverp->_nextRequestId++;

    /* actually send the call */
    header.setupBasic(_serverp);
    header._opcode = RpcHeader::_opRequest;
    header._size = _connp->_bodyChain.bytes();
    header._requestId = requestId;

    /* and tag ourselves with the requestId, so we can match up the response */
    _requestId = requestId;

    header.marshal(&_connp->_sendChain, /* doMarshal */ 1);
    code = _connp->_sendChain.copyLong(&_appOpcode, /* doMarshal */ 1);
    if (code) {
        _failed = 1;
        rpcp->_lock.release();
        printf("Rpc: app opcode marshal fails code=%d\n", code);
        cleanup();
        return code;
    }

    _connp->_sendChain.append(&_connp->_bodyChain);     /* append the body */

    _connp->releaseSendNL();
    _srLocked = 0;

    while (!_haveResponse) {
        /* can't assert !_waitingForResponse, since we may get spurious wakeups */
        _waitingForResponse = 1;
        _recvResponseCV.wait();
    }

    /* once waitForResponse is off, the receive owner has been set to us.  If we succeed,
     * we return with the receive owner set, otherwise we release it now.  Note that the guy
     * who wakes us up and turns off waitingForResponse also sets _srLocked.
     */

    if (_failed || _connp->failedNL()) {
        _failed = 1;
        rpcp->_lock.release();
        cleanup();
        return -1;
    }

    rpcp->_lock.release();
    return 0;
}

void
RpcClientContext::cleanup()
{
    Rpc *rpcp = _connp->_rpcp;

    rpcp->_lock.take();

    if (_counted) {
        osp_assert(_connp->_activeClientCalls > 0);
        --_connp->_activeClientCalls;

        _serverp->_clientCalls.remove(this);
        _counted = 0;
    }

    /* we have a receive lock */
    if (_srLocked == 1)
        _connp->releaseSendNL();
    else if (_srLocked == 2)
        _connp->releaseReceiveNL();
    _srLocked = 0;

    rpcp->_lock.release();
}

int32_t
RpcClientContext::finishCall()
{
    cleanup();
    return 0;
}

/*================RpcHeader================*/

int32_t
RpcHeader::marshal(Sdr *sdrp, int isMarshal)
{
    int32_t code;

    if ((code = sdrp->copyChar(&_version, isMarshal)) != 0)
        return code;
    if ((code = sdrp->copyChar(&_headerSize, isMarshal)) != 0)
        return code;
    if ((code = sdrp->copyChar(&_opcode, isMarshal)) != 0)
        return code;
    if ((code = sdrp->copyChar(&_pad0, isMarshal)) != 0)
        return code;
    if ((code = sdrp->copyLong(&_reserved, isMarshal)) != 0)
        return code;
    if ((code = sdrp->copyUuid(&_serviceId, isMarshal)) != 0)
        return code;
    if ((code = sdrp->copyLong(&_requestId, isMarshal)) != 0)
        return code;
    if ((code = sdrp->copyLong(&_size, isMarshal)) != 0)
        return code;
    if ((code = sdrp->copyLong((uint32_t *) &_error, isMarshal)) != 0)
        return code;
    if ((code = sdrp->copyLong((uint32_t *) &_pad1, isMarshal)) != 0)
        return code;
    return 0;
}

void
RpcHeader::generateResponse(RpcHeader *responseHeaderp)
{
    *responseHeaderp = *this;
    responseHeaderp->_opcode = _opcode+1;
}

void
RpcHeader::setupBasic(RpcServer *serverp)
{
    
    _version = _currentVersion;
    _headerSize = (sizeof(RpcHeader) + 7) >> 3;
    _opcode = 0xff;
    _pad0 = 0;
    _pad1 = 0;
    _reserved = 0;
    memcpy(&_serviceId, &serverp->_serviceId, sizeof(uuid_t));
    /* leave connId 0 for now */
    _requestId = serverp->_nextRequestId++;
    _size = 0;
    _error = 0;
}
