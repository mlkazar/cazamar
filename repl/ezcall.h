#ifndef __EZCALL_H_ENV__
#define __EZCALL_H_ENV__ 1

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
 */

class EzCall {
 public:
    static const uin32_t _hashSize = 251;
    static const uint32_t _maxChannels = 1024;
    class CommonConn {
    public:
        CommonReq *_dqNextp;
        CommonReq *_dqPrevp;
        UUID _uuid;
        CommonReq *_hashNextp;
        uint8_t _isClient;
    };

    class ClientConn  : CommonConn {
    public:
        ClientReq *_dqNextp;
        ClientReq *_dqPrevp;
        dqueue<ClientReq> _pendingReqsp;
        ClientReq *currentReqsp[_maxChannels];
    };

    class ServerConn : CommonConn {
        
    };

    dqueue<ClientReq> _activeClientReqs;
    CommonReq *_hashTablep[_hashSize];
};

#endif /* __EZCALL_H_ENV__ */
