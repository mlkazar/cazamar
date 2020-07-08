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
 * The target of a call is first described by a node name; these may
 * include the equivalent of an TCP port number.  The target is
 * further qualified by a service ID (a UUID).  At the time a call is
 * made, we search the connection pool for a usable connection,
 * creating one if none is found.
 *
 * A connection is subdivided into a number of channels, where each
 * channel allows a single outstanding call at any instant.
 *
 * Before a client connection can be used, it must be opened, which
 * means we must establish shared connection state for each channel.
 * A connection must be opened after it is first created, but a
 * connection can also be reset due to communications problems or
 * server restarts, in which case a ping or call will return
 * ERR_CLOSED, and the client must reopen the connection, passing the
 * state of each channel to the server again.
 *
 * Because our ezcall connections can be reused, and may use multiple
 * underlying transport streams, we really only need one ezcall
 * connection for each target node.
 *
 * The callId field holds the currently executing call, if one is
 * running, otherwise it indicates the last executed call.  It is
 * initialized to 0 when a new connection is created, so we expect the
 * first call to start at 1.
 *
 * Reference counting:
 *
 * CommonReq holds a reference on its associated connection.  It has
 * an unheld reference from the CommonConn's channel array, which must
 * be cleared when the request's reference is removed.
 *
 * CommonConn structures don't have any other long term references,
 * but are have unheld references from the node and ID hash table.
 *
 * All of these structures may be temporarily held when upcalled.
 *
 * CommonReq and CommonConn are actually deleted when their reference counts
 * hit zero and their deleted flags are set (via ::delete methods).
 *
 * ServerTasks keep a reference on their ServerReq until sendResponse
 * completes.
 */

#include <stddef.h>
#include "task.h"
#include "sock.h"
#include "lock.h"
#include "osp.h"
#include <uuid/uuid.h>

class EzCall;

class EzCallSockClient : public SockClient  {
public:
    SockSys *_sockSysp;
    EzCall *_ezCallp;

    EzCallSockClient() {
        _sockSysp = NULL;
        _ezCallp = NULL;
    }

    void indicatePacket( std::shared_ptr<SockConn> connp,
                         std::shared_ptr<Rbuf> bufp);

    void setSys(EzCall *ezCallp, SockSys *sysp) {
        _ezCallp = ezCallp;
        _sockSysp = sysp;
    }
};

class EzCall {
 public:
    static const uint32_t _hashSize = 251;
    static const int32_t _maxChannels = 4;

    /* opcodes for call and response packets */
    static const uint32_t _rpcCallOp = 1;
    static const uint32_t _rpcResponseOp = 2;

    class CommonConn;
    class ClientConn;
    class ServerConn;
    class CommonReq;
    class ClientReq;
    class ServerReq;
    class ServerTask;

    class Header {
    public:
        char _opcode;
        char _padding;
        int16_t _channelId;
        uint32_t _callId;
        uuid_t _clientId;
    };

    /* connections are permanently held by requests */
    class CommonConn {
    public:
        uint32_t _refCount;
        CommonConn *_dqNextp;
        CommonConn *_dqPrevp;
        CommonConn *_idHashNextp;
        SockNode _sockNode;
        std::string _port;
        uuid_t _uuid;
        uint8_t _isClient;
        uint8_t _deleted;
        uint8_t _inIdHash;
        uint8_t _inNodeHash;
        
        dqueue<ClientReq> _pendingReqs;         /* reqs waiting for a slot */
        CommonReq *_currentReqsp[_maxChannels]; /* active reqs, by channel # */
        int32_t _callId[_maxChannels];
        uint16_t _idHash;
        uint16_t _freeChannels;
        EzCall *_sysp;

        /* should really be in ServerConn, but its inconvenient to cast after
         * searching in generic code.
         */
        std::shared_ptr<SockConn> _serverConn;

        CommonConn() {
            uint32_t i;

            _sysp = NULL;
            _refCount = 1;
            _deleted = 0;
            _inIdHash = 0;
            _inNodeHash = 0;
            uuid_generate(_uuid);
            _isClient = 0;
            _idHash = idHash(&_uuid);
            _idHashNextp = NULL;
            _freeChannels = _maxChannels;
            for(i=0;i<_maxChannels;i++) {
                _callId[i] = 0;
                _currentReqsp[i] = 0;
            }

            /* most of the initialization is handled in getConnectionByX, including
             * inserting the connections in the appropriate hash tables.
             */
        }

        virtual ~CommonConn() {
            /* remove from hash table */
            CommonConn *treqp;
            CommonConn **lreqpp;
            if (_inIdHash) {
                for( lreqpp = &_sysp->_idHashTablep[_idHash], treqp = *lreqpp;
                     treqp;
                     lreqpp = &treqp->_idHashNextp, treqp = *lreqpp) {
                    *lreqpp = treqp->_idHashNextp;
                    break;
                }
                osp_assert(treqp);
                _inIdHash = 0;
            }

            /* should have been removed by ClientConn's destructor before CommonConn's
             * destructor is called.
             */
            osp_assert(!_inNodeHash);
        }

        void hold() {
            _refCount++;
        }

        void del() {
            _deleted = 1;
            if (_refCount == 0)
                delete this;
        }

        void release() {
            if (--_refCount == 0 && _deleted) {
                delete this;
            }
        }
    };

    class ClientConn : public CommonConn {
    public:
        uint16_t _nodeHash;
        ClientConn *_nodeHashNextp;

        ClientReq *getClientReq();

        ClientConn() {
            /* inNodeHash is cleared in CommonConn's constructor */
            _nodeHashNextp = NULL;
            _nodeHash = 0;
        }

        ~ClientConn() {
            ClientConn *treqp;
            ClientConn **lreqpp;
            if (_inNodeHash) {
                for( lreqpp = &_sysp->_nodeHashTablep[_idHash], treqp = *lreqpp;
                     treqp;
                     lreqpp = &treqp->_nodeHashNextp, treqp = *lreqpp) {
                    *lreqpp = treqp->_nodeHashNextp;
                    break;
                }
                osp_assert(treqp);
                _inNodeHash = 0;
            }
            return;
        }

        void checkPending();
};

    /* inherit from this for new tasks that receive incoming server requests */
    class ServerConn : public CommonConn {
        /* when a server connection is created, we remember the socket from which
         * the last request was received.  We send responses back to this guy.
         *
         * By doing this instead of reconnecting to the peer address,
         * we can function in a NAT environment, since in these
         * environments we may be unable to open new sockets.
         */
    public:
        ~ServerConn() {
            return;
        }
    };

    /* requests are held by their presence in the currentReqsp channel array */
    class CommonReq {
    public:
        uint32_t _refCount;
        std::shared_ptr<Rbuf> _inBufp;
        std::shared_ptr<Rbuf> _outBufp;
        int16_t _channel;
        uint32_t _callId;
        EzCall *_sysp;
        CommonConn *_connp;
        uint8_t _isClient;
        uint8_t _deleted;
        uint8_t _headerAdded;

        CommonReq() {
            _callId = 0;
            _sysp = NULL;
            _connp = NULL;
            _isClient = 0;
            _deleted = 0;
            _headerAdded = 0;
            _channel = -1;
        }

        ~CommonReq() {
            if (_connp) {
                _connp->release();
                _connp = NULL;
            }
        }

        int32_t prepareHeader();

        void init(int16_t channel, EzCall *ezp, ServerConn *connp, uint32_t callId) {
            _channel = channel;
            _sysp = ezp;
            _connp = connp;
            _isClient = 0;
            _callId = callId;
        }

        void hold() {
            _refCount++;
        }

        void del() {
            _deleted = 1;
            if (_refCount == 0)
                delete this;
        }

        void release() {
            if (--_refCount == 0 && _deleted) {
                delete this;
            }
        }
    };

    class ClientReq : public CommonReq {
        uint32_t _lastSendMs;

    public:
        ClientReq *_dqPrevp;
        ClientReq *_dqNextp;
        Task *_responseTaskp;

        ClientReq() {
            _lastSendMs = 0;
            _responseTaskp = NULL;
        }

        int32_t allocateChannel();

        std::shared_ptr<Rbuf> getResponseBuffer() {
            return _inBufp;
        }

        void doSend();
    };

    /* when there's an outstanding task, we keep an extra reference on this */
    class ServerReq : public CommonReq {
        uint8_t _sentResponse;

    public:
        ServerTask *_serverTaskp;

        int sentResponse() {
            return _sentResponse;
        }

        ServerReq() {
            _sentResponse = 0;
            _serverTaskp= NULL;
        }

        void init(int16_t channel, EzCall *ezp, ServerConn *connp, uint32_t callId) {
            CommonReq::init(channel, ezp, connp, callId);
            _isClient = 0;
        }

        void sendResponse(std::shared_ptr<Rbuf> rbufp);

        void doSend(std::shared_ptr<Rbuf> rbufp);
    };

public:
    EzCall();

    int32_t call( SockNode *nodep,
                  std::string port,
                  std::shared_ptr<Rbuf> rbufp, 
                  ClientReq **reqpp, 
                  Task *responseTaskp);

    class ServerTask : public Task {
        ServerConn *_connp;
    public:
        virtual void init(std::shared_ptr<Rbuf> rbufp, ServerReq *reqp, void *contextp) = 0;

        void sendResponse(std::shared_ptr<Rbuf> rbufp);
    };

    int32_t init(SockSys *sockSysp, std::string port);

    typedef ServerTask *(ServerFactoryProc)();

    void setFactoryProc( ServerFactoryProc *procp) {
        _factoryProcp = procp;
    }

    CommonConn *getConnectionByClientId( uuid_t *uuidp,
                                         int32_t channelId,
                                         uint32_t callId,
                                         std::shared_ptr<SockConn> aconnp);

    ClientConn *getConnectionByNode(SockNode *nodep, std::string port);

    void indicatePacket(std::shared_ptr<SockConn> aconnp, std::shared_ptr<Rbuf> rbufp);

    static uint32_t nodeHash(SockNode *nodep) {
        const char *tp = (char *) nodep->getName().c_str();
        uint32_t len;
        uint32_t i;
        uint32_t hashValue = 0x811c9dc5;
        len = nodep->getName().size();

        for(i=0;i<len;i++) {
            hashValue *= 0x01000193;
            hashValue = hashValue ^ (*tp++);
        }
        return hashValue % _hashSize;
    }

    static int loopCmp(int32_t a, int32_t b) {
        if (a-b < 0)
            return -1;
        else if (a == b)
            return 0;
        else return 1;
    }

    /* fnv1 */
    static uint32_t idHash(uuid_t *uuidp) {
        char *tp = (char *) uuidp;
        uint32_t i;
        uint32_t hashValue = 0x811c9dc5;
        for(i=0;i<16;i++) {
            hashValue *= 0x01000193;
            hashValue = hashValue ^ (*tp++);
        }
        return hashValue % _hashSize;
    }

private:
    ServerFactoryProc *_factoryProcp;
    SockSys *_sockSysp;
    EzCallSockClient _sockClient;
    dqueue<ClientReq> _activeClientReqs;
    CommonConn *_idHashTablep[_hashSize];
    ClientConn *_nodeHashTablep[_hashSize];
    SpinLock _lock;
};

#endif /* __EZCALL_H_ENV__ */
