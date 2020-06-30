#ifndef __EZCALL_H_ENV__
#define __EZCALL_H_ENV__ 1

/* the packet header contains 1 byte of opcode, 3 bytes reserved, n
 * a 32 bit call number, the client UUID.  All fields are in Intel byte
 * order (little endian).
 *
 */

/* The basic idea is that a client makes a call to a node using a shared pool
 * of connections.
 *
 * The target of a call is first described by a node name; these may include the equivalent of
 * an TCP port number.  The target is further qualified by a service ID (a UUID).  At the
 * time a call is made, we search the connection pool  for a usable connection, creating one
 * if none is found.
 *
 * A connection is subdivided into a number of channels, where each channel allows a single
 * outstanding call at any instant.
 *
 * Before a client connection can be used, it must be opened, which means we must establish
 * shared connection state for each channel.  A connection must be opened after it is first
 * created, but a connection can also be reset due to communications problems or server 
 * restarts, in which case a ping or call will return ERR_CLOSED, and the client must
 * reopen the connection, passing the state of each channel to the server again.
 *
 * Because our ezcall connections can be reused, and may use multiple underlying transport
 * streams, we really only need one ezcall connection for each target node.
 *
 * The callId field holds the currently executing call, if one is running, otherwise
 * it indicates the last executed call.  It is initialized to 0 when a new connection
 * is created, so we expect the first call to start at 1.
 */

#include "sock.h"
#include "lock.h"
#include <uuid/uuid.h>

class EzCall;

class EzCallSockClient : public SockClient  {
public:
    SockSys *_sockSysp;
    EzCall *_ezCallp;

    void indicatePacket( std::shared_ptr<SockConn> connp,
                         std::shared_ptr<Rbuf> bufp);
};

class EzCall {
 public:
    static const uin32_t _hashSize = 251;
    static const uint32_t _maxChannels = 4;

    /* opcodes for call and response packets */
    static const uint32_t _rpcCallOp = 1;
    static const uint32_t _rpcResponseOp = 2;

    class CommonConn;
    class ClientConn;
    class ServerConn;
    class ClientReq;
    class ServerReq;
    class ServerTask;

    class Header {
    public:
        char _opcode;
        char _padding[1];
        uint16_t _channelId;
        uint32_t _callId;
        uuid_t _clientId;
    };

    /* connections are permanently held by requests */
    class CommonConn {
    public:
        uint32_t _refCount;
        CommonReq *_dqNextp;
        CommonReq *_dqPrevp;
        SockNode _sockNode;
        uuid_t _uuid;
        uint8_t _isClient;
        
        dqueue<ClientReq> _pendingReqsp;
        ClientReq *_currentReqsp[_maxChannels];
        uint32_t _callId[_maxChannels];
        CommonConn *_idHashNextp;
        uint16_t _idHash;

        CommonConn() {
            uint32_t i;

            _refCount = 1;
            uuid_generate(_uuid);
            _isClient = 0;
            _idHash = idHash(&_uuid);
            _idHashNextp = NULL;
            for(i=0;i<_maxChannels;i++) {
                _callId[i] = 0; /* used? */
                _currentReqsp[i] = 0;
            }
        }

        virtual ~CommonConn() {
            /* remove from hash table */
            CommonConn *treqp;
            CommonConn **lreqpp;
            for( lreqpp = &_sysp->_idHashp[_idHash], treqp = *lreqpp;
                 treqp;
                 lreqpp = &treqp->_idHashNextp, treqp = *lreqpp) {
                *lreqpp = treqp->_idHashNextp;
                break;
            }
            osp_assert(treqp);
        }

        void release() {
            if (--_refCount == 0) {
                delete this;
            }
        }
    };

    class ClientConn  : CommonConn {
    public:
        ClientConn *_nodeHashNextp;

        ~ClientConn() {
            return;
        }
    };

    /* inherit from this for new tasks that receive incoming server requests */
    class ServerConn : public CommonConn {
    public:
        ~ClientConn() {
            return;
        }
    };

    /* requests are held by their presence in the currentReqsp channel array */
    class CommonReq {
    public:
        uint32_t _refCount;
        std::shared_ptr<Rbuf> _inBufp;
        std::shared_ptr<Rbuf> _outBufp;
        uint16_t _channel;
        uint32_t _callId;
        EzCall *_sysp;
        CommonConn *_connp;
        uint8_t _isClient;

        CommonReq() {
            _inBufp = NULL;
            _outBufp = NULL;b
            _callId = 0;
            _sysp = NULL;
            _connp = NULL;
            _isClient = 0;
        }

        void init(uint16_t channel, EzCall *ezp, ServerConn *connp, uint32_t callId) {
            _channel = channel;
            _sysp = ezp;
            _connp = connp;
            _isClient = 0;
            _callId = callId;
        }

        virtual ~CommonReq() {
            return;
        }

        void release() {
            if (--_refCount == 0) {
                delete this;
            }
        }
    };

    class ClientReq : public CommonReq {
        Task *_responseTaskp;
    };

    class ServerReq : public CommonReq {
        ServerTask *_serverTaskp;
        uint8_t _sentResponse;

        ServerReq() {
            _sentResponse = 0;
            _serverTaskp= NULL;
        }

        void init(uint16_t channel, EzCall *ezp, ServerConn *connp, uint32_t callId) {
            CommonReq::init(channel, ezp, connp, callId);
            _isClient = 0;
        }
    };

public:
    int32_t call(SockNode *nodep, std::shared_ptr<Rbuf> rbufp, Task *responseTaskp)

    class ServerTask : public Task {
        ServerConn *_connp;
    public:
        virtual void init(std::shared_ptr<Rbuf> rbufp, ServerConn *connp, void *contextp) = 0;

        void sendResponse(std::shared_ptr<Rbufp> rbufp);
    };

    typedef ServerTask *(ServerFactoryProc)();

    void setFactoryProc(ServerFactoryProc *procp, void *contextp);

private:
    ServerFactoryProc *_factoryProcp;
    EzCallSockClient _sockClient;
    dqueue<ClientReq> _activeClientReqs;
    CommonConn *_hashTablep[_hashSize];
    SpinLock _lock;
};

#endif /* __EZCALL_H_ENV__ */
