#include "ezcall.h"

EzCall::EzCall()
{
    return;
}

void
EzCall::init(SockSys *sockSysp)
{
    _sockClient.setSys(this, sockSysp);
    sockSysp->listen("");
}

/* EXTERNAL: always runs asynchronously; callback made iff return code is 0 */
int32_t
EzCall::call(SockNode *nodep, std::shared_ptr<Rbuf> rbufp, Task *responseTaskp)
{
    ClientConn *connp;
    Header h;
    ClientReq *reqp;
    uint32_t nextCallId;

    memset(&h, 0, sizeof(h));

    _lock.take();
    connp = getConnection(nodep);
    reqp = connp->getClientReq();
    nextCallId = reqp->_callId++;
    _lock.release();

    h._opcode = _rpcCallOp;
    h._clientId = connp->_uuid;

    _lock.take();
    h._callId = nextCallId;
    _lock.release();

    h._channelId = reqp->_channelId;

    /* now prepend the header; fix to use sdr */
    rbufp->prepend(&h, sizeof(h));

    reqp->_rbufp = rbufp;

    reqp->doSend(rbufp);
}

void
EzCall::ClientReq::doSend(std::shared_ptr<Rbuf> rbufp)
{
    std::shared_ptr<SockConn> sockConnp;
    
    _sysp->_lock.take();
    sockConnp = _sysp->_sockSysp->getConnection(_connp->_sockNode);
    _sysp->_lock.release();
    _lastSendMs = osp_time_ms();

    sockConnp->send(rbufp);
}

void
EzCall::ServerReq::doSend(std::share_ptr<Rbuf> rbufp)
{
    std::shared_ptr<SockConn> sockConnp;
    
    _sysp->_lock.take();
    sockConnp = _sysp->_sockSysp->getConnection(_connp->_sockNode);
    _sysp->_lock.release();

    sockConnp->send(rbufp);
}

void
EzCall::indicatePacket(std::shared_ptr<SockConn> connp, std::shared_ptr<Rbuf> rbufp)
{
    int32_t code;
    Header hdr;
    uint32_t channel;
    int newCall;
    
    code = bufp->read(&hdr, sizeof(hdr));
    if (code != sizeof(hdr))
        return;
    bufp->pop(sizeof(hdr));

    /* find and/or create a connection structure; for responses, we don't
     * create a new connection if not found.
     */
    connp = getConnectionByClientId(&h._clientId, (code == _rpcCallOp));
    if (!connp)
        return;

    if (channel >= _maxChannels) {
        return;
    }

    if (h._opcode == _rpcCallOp) {
        /* incoming call */
        newCall = 0;
        /* if we haven't setup our server side handler, ignore calls */
        if (!_factoryp)
            return;

        serverReqp = static_cast<EzCopy::ServerReq *>(_currentReqsp[channel]);
        if (serverReqp == NULL) {
            /* create a server request */
            serverReqp = new ServerReq();
            serverReqp->init(channel, this, connp);
            _currentReqsp[channel] = serverReqp;
            newCall = 1;
            /* request reference is transferred to currentReqsp */
        }
        else if (serverReqp->_sentResponse) {
            if (h._callId == serverReqp->_callId) {
                /* retransmitted call after we sent a response; resend the response */
                doSend(serverReqp->_outBufp);
            }
            else if (h._callId == serverReqp->_callId+1) {
                _currentReqsp[channel] = NULL;
                serverReqp->release();
                serverReqp = new ServerReq();
                serverReqp->init(channel, this, connp, h._callId);
                newCall = 1;
            }
            else {
                /* old request, hopefully; just ignore */
            }
            if (newCall) {
                /* attach a task to the server request */
                if (_factoryp) {
                    serverTaskp = _factoryProcp();
                    serverTaskp->init(inBufp, connp, NULL);
                    /* note that the serverTask will call its own (inherited) sendResponse
                     * method when done.
                     */
                }
            }
        }
        else {
            /* didn't send a response yet because a call is still executing, we
             * should send back a busy message, but right now, this is a TODO
             */
        }
    }
    else if (h._opcode == _rpcResponseOp) {
        /* we received a response packet, so see if we have an active client
         * waiting for this response.
         */
        /* otherwise, we have a channel */
        channel = h._channel;
        clientReqp = static_cast<EzCopy::ClientReq *>(_currentReqsp[channel]);
        if (!clientReqp)
            return;
        if (clientReqp->_callId != h._callId)
            return;
        clientReqp->_responseTaskp->queueForceAvoidDeadlock();
        _currentReqsp[channel] = NULL;
        clientReqp->release();
    }

    /* next, find the connection specified by the UUID in the header */
}

/* bit of glue code to get us to ezcall */
void
EzCallSockClient::indicatePacket( std::shared_ptr<SockConn> connp,
                                  std::shared_ptr<Rbuf> bufp)
{
    _ezCallp->indicatePacket(connp, bufp);
}

void
EzCallSockClient::indicatePacket( std::shared_ptr<SockConn> connp,
                                  std::shared_ptr<Rbuf> bufp)
{
    _ezCallp->indicatePacket(connp, bufp);
}

EzCall::ServerReq::sendResponse(std::shared_ptr<Rbuf> rbufp)
{
    Header h;

    /* marshall in the header */
    h._opcode = _rpcResponseOp;
    h._padding = 0;
    h._channelId = _channel;
    h._callId = _callId;
    memcpy(&h._clientId, &_connp->_uuid, sizeof(h._clientId)__);

    /* save a copy so we can retransmit it */
    _outBufp = rbufp;

    rbufp->prepend(&h, sizeof(h));
    doSend(rbufp);
}
