#include "ezcall.h"

EzCall::EzCall()
{
    return;
}

/* EXTERNAL: call to setup system */
int32_t
EzCall::init(SockSys *sockSysp)
{
    _sockSysp = sockSysp;
    _sockClient.setSys(this, sockSysp);
    sockSysp->setClient(&_sockClient);
    sockSysp->listen("");
    return 0;
}

int32_t
EzCall::ClientReq::allocateChannel()
{
    uint32_t i;
    ClientConn *connp = static_cast<ClientConn *>(_connp);

    /* channel may already be allocated */
    if (_channel >= 0)
        return _channel;

    for(i=0;i<_maxChannels;i++) {
        if (connp->_currentReqsp[i] == NULL) 
            break;
    }
    if (i >= _maxChannels) {
        _channel = -1;
        return -1;
    }

    _channel = i;
    connp->_currentReqsp[i] = this;
    return i;
}

/* don't do this unless channel allocated */
int32_t
EzCall::CommonReq::prepareHeader()
{
    Header h;
    EzCall::ClientConn *connp = static_cast<EzCall::ClientConn *>(_connp);
    int32_t channel;

    if (!_headerAdded) {
        _headerAdded = 1;
        
        h._opcode = _rpcCallOp;
        memcpy(&h._clientId, connp->_uuid, sizeof(uuid_t));

        channel = _channel;
        osp_assert(channel >= 0);

        h._channelId = channel;
        h._callId = ++connp->_callId[channel];

        /* now prepend the header; fix to use sdr */
        _outBufp->prepend((char *) &h, sizeof(h));
    }

    return 0;
}

/* EXTERNAL: always runs asynchronously; callback made iff return code is 0;
 * ClientReq is stored before call has chance of waking our task, even if we
 * haven't returned yet.  Reference is passed along with ClientReq pointer.
 */
int32_t
EzCall::call(SockNode *nodep, std::shared_ptr<Rbuf> rbufp, ClientReq **reqpp, Task *responseTaskp)
{
    ClientConn *connp;
    ClientReq *reqp;
    uint32_t nextCallId;
    Header h;

    /* just in case */
    *reqpp = NULL;

    memset(&h, 0, sizeof(h));

    _lock.take();
    connp = getConnectionByNode(nodep);
    reqp = connp->getClientReq();
    nextCallId = reqp->_callId++;
    _lock.release();

    reqp->_outBufp = rbufp;

    _lock.take();
    *reqpp = reqp;
    reqp->doSend();
    connp->release();
    _lock.release();
    return 0;
}

/* internal: send packet with client request */
void
EzCall::ClientReq::doSend()
{
    std::shared_ptr<SockConn> sockConnp;
    int32_t channel;
    std::shared_ptr<Rbuf> rbufp = _outBufp;
    
    if ((channel = allocateChannel()) < 0) {
        /* couldn't allocate channel, so queue the request */
        _connp->_pendingReqs.append(this);
        return;
    }

    /* otherwise, we can create the header */
    prepareHeader();

    sockConnp = _sysp->_sockSysp->getConnection(&_connp->_sockNode);
    sockConnp->send(rbufp);
    _lastSendMs = osp_time_ms();
}

/* internal: send packet with server request */
void
EzCall::ServerReq::doSend(std::shared_ptr<Rbuf> rbufp)
{
    std::shared_ptr<SockConn> sockConnp;
    
    sockConnp = _sysp->_sockSysp->getConnection(&_connp->_sockNode);
    sockConnp->send(rbufp);
}

/* process incoming packet */
void
EzCall::indicatePacket(std::shared_ptr<SockConn> aconnp, std::shared_ptr<Rbuf> rbufp)
{
    int32_t code;
    Header hdr;
    int32_t channel;
    int newCall;
    CommonConn *connp;
    ClientConn *clientConnp;
    ServerReq *serverReqp;
    ServerTask *serverTaskp;
    ClientReq *clientReqp;
    
    /* pop the header off the request; read advances the read pointer, but
     * doesn't actually remove the bytes, so we still have to pop them.
     */
    code = rbufp->read((char *) &hdr, sizeof(hdr));
    if (code != sizeof(hdr))
        return;
    rbufp->pop(sizeof(hdr));

    channel = hdr._channelId;

    if (hdr._channelId >= _maxChannels) {
        return;
    }

    /* if we haven't setup our server side handler, ignore calls */
    if (!_factoryProcp) {
        return;
    }

    _lock.take();
    /* find and/or create a connection structure; for responses, we don't
     * create a new connection if not found.
     */
    if (hdr._opcode == _rpcCallOp) {
        /* incoming call -- create a new connection if none found, and 
         * set the call and channel up
         */
        connp = getConnectionByClientId(&hdr._clientId, hdr._channelId, hdr._callId);
    }
    else {
        /* incoming response -- connection should already exist, or discard the packet */
        connp = getConnectionByClientId(&hdr._clientId, -1, 0);
    }
    if (!connp) {
        _lock.release();
        return;
    }

    if (hdr._opcode == _rpcCallOp) {
        /* incoming call */
        newCall = 0;

        serverReqp = static_cast<EzCall::ServerReq *>(connp->_currentReqsp[channel]);
        if (serverReqp == NULL) {
            /* create a server request */
            serverReqp = new ServerReq();
            serverReqp->init(channel, this, static_cast<ServerConn *>(connp), hdr._callId);
            connp->_currentReqsp[channel] = serverReqp;
            connp->hold();      /* for reference from serverReqp */
            newCall = 1;
            /* request reference is transferred to currentReqsp */
        }
        else if (serverReqp->sentResponse()) {
            if (hdr._callId == serverReqp->_callId) {
                /* retransmitted call after we sent a response; resend the response */
                serverReqp->doSend(serverReqp->_outBufp);
            }
            else if (hdr._callId == serverReqp->_callId+1) {
                connp->_currentReqsp[channel] = NULL;
                serverReqp->del();
                serverReqp = new ServerReq();
                connp->hold();  /* for reference from serverReqp */

                /* drop lock over init call, in case it calls back to ezcall */
                serverReqp->init(channel, this, static_cast<ServerConn *>(connp), hdr._callId);

                newCall = 1;
            }
            else {
                /* old request, hopefully; just ignore */
            }
        }
        else {
            /* didn't send a response yet because a call is still executing, we
             * should send back a busy message, but right now, this is a TODO
             */
        }

        /* if incoming new call, fire up a task to handle it */
        if (newCall) {
            /* attach a task to the server request */
            if (_factoryProcp) {
                serverTaskp = _factoryProcp();
                /* note that the serverTask will call its own (inherited) sendResponse
                 * method when done, which will call ::ServerReq::sendResponse.
                 */
                serverReqp->_serverTaskp = serverTaskp;
                serverReqp->hold();

                /* now start the child */
                _lock.release();
                serverTaskp->init(serverReqp->_inBufp, serverReqp, NULL);
                _lock.take();
            }
        }
    }
    else if (hdr._opcode == _rpcResponseOp) {
        /* we received a response packet, so see if we have an active client
         * waiting for this response.
         */
        /* otherwise, we have a channel */
        channel = hdr._channelId;
        clientConnp = static_cast<EzCall::ClientConn *>(connp);
        clientReqp = static_cast<EzCall::ClientReq *>(connp->_currentReqsp[channel]);
        if (!clientReqp)
            return;
        if (clientReqp->_callId != hdr._callId)
            return;
        clientReqp->_responseTaskp->queueForce();
        connp->_currentReqsp[channel] = NULL;
        clientReqp->del();
        clientReqp->release();
        clientConnp->checkPending();
    }

    _lock.release();
}

void
EzCall::ClientConn::checkPending()
{
    ClientReq *reqp;
    int32_t channel;

    while(_pendingReqs.head() != NULL) {
        reqp = _pendingReqs.head();
        channel = reqp->allocateChannel();
        if (channel < 0) {
            return;
        }

        /* otherwise we have a request and an available channel, so now that
         * the channel has been set, we can do the resend.
         */
        _pendingReqs.pop();
        reqp->doSend();
    }
}


/* bit of glue code to get us to ezcall */
void
EzCallSockClient::indicatePacket( std::shared_ptr<SockConn> connp,
                                  std::shared_ptr<Rbuf> bufp)
{
    _ezCallp->indicatePacket(connp, bufp);
}

void
EzCall::ServerReq::sendResponse(std::shared_ptr<Rbuf> rbufp)
{
    Header h;

    /* marshall in the header */
    h._opcode = _rpcResponseOp;
    h._padding = 0;
    h._channelId = _channel;
    h._callId = _callId;
    memcpy(&h._clientId, &_connp->_uuid, sizeof(h._clientId));

    _sysp->_lock.take();

    /* save a copy so we can retransmit it */
    _outBufp = rbufp;

    rbufp->prepend((char *) &h, sizeof(h));
    doSend(rbufp);

    if (_serverTaskp) {
        _serverTaskp = NULL;
        release();      /* release hold created when serverTask connected to request */
    }

    _sysp->_lock.release();
}

EzCall::CommonConn *
EzCall::getConnectionByClientId(uuid_t *uuidp, int32_t channelId, uint32_t callId)
{
    uint32_t hash = idHash(uuidp);
    CommonConn *connp;
    for(connp = _idHashTablep[hash]; connp; connp = connp->_idHashNextp) {
        if (memcmp(uuidp, &connp->_uuid, sizeof(uuid_t)) == 0) {
            if (channelId >= 0) {
                if (connp->_callId[channelId] <= 0)
                    connp->_callId[channelId] = callId;
            }
            return connp;
        }
    }

    if (channelId < 0)
        return NULL;

    /* only server connections provide us real channel IDs */
    connp = new ServerConn();
    connp->_isClient = 0;
    if (connp->_callId[channelId] <= 0) {
        /* adjust the expected call ID */
        connp->_callId[channelId] = callId;
    }

    /* use incoming UUID */
    memcpy(connp->_uuid, uuidp, sizeof(uuid_t));
    connp->_idHash = hash;

    connp->_idHashNextp = _idHashTablep[hash];
    _idHashTablep[hash] = connp;
    connp->_inIdHash = 1;
    return connp;
}


EzCall::ClientConn *
EzCall::getConnectionByNode(SockNode *nodep)
{
    uint32_t hash = nodeHash(nodep);
    ClientConn *connp;
    for(connp = _nodeHashTablep[hash]; connp; connp = connp->_nodeHashNextp) {
        if (connp->_sockNode.getName() == nodep->getName()) {
            connp->hold();
            return connp;
        }
    }

    connp = new ClientConn();
    connp->_sockNode.setName(nodep->getName());
    connp->_sysp = this;
    connp->_isClient = 1;

    /* and hash in */
    connp->_nodeHash = hash;
    connp->_nodeHashNextp = _nodeHashTablep[connp->_nodeHash];
    _nodeHashTablep[connp->_nodeHash] = connp;
    connp->_inNodeHash = 1;

    connp->_idHashNextp = _idHashTablep[connp->_idHash];
    _idHashTablep[connp->_idHash] = connp;
    connp->_inIdHash = 1;

    return connp;
}


EzCall::ClientReq *
EzCall::ClientConn::getClientReq()
{
    ClientReq *reqp;
    reqp = new ClientReq();
    reqp->_refCount = 1;
    reqp->_channel = -1;
    reqp->_sysp = _sysp;
    reqp->_connp = this;
    reqp->_isClient = 1;
    reqp->_channel = ~0;
    reqp->_responseTaskp = NULL;
    reqp->_sysp = _sysp;

    return reqp;
}
