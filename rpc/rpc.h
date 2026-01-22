#ifndef __RPC_H_ENV_
#define __RPC_H_ENV_ 1

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include <netinet/tcp.h>

#include "cthread.h"
#include "ospmbuf.h"
#include "sdr.h"
#include "dqueue.h"

/* Basics of RPC protocol.  A connection is opened from client to server, with a
 * unique connection ID.
 *
 * Goal is to provide at most once semantics for requests, with timeouts based on
 * pings.
 *
 * Per RPC lock protecting connection set and servers.  Each conn is
 * associated with a particular server, and a server can have more
 * than one conn open.  Each conn is transmitting or receiving one
 * call at a time, but may have multiple calls outstanding at once.
 *
 * Global lock on the connection (multiplexing multiple servers on a
 * single conn) that coordinates more complex stuff, like managing how
 * recently a connection was accessed.
 * 
 * Per RpcSdr object lock coordinating senders and
 * receivers.   At some point, we may multiplex a single service across
 * multiple conns, at which point we'll have to move to a more global
 * rpc lock.
 */

/* forward dcls */
class RpcSdrIn;
class Rpc;
class RpcSdrOut;
class RpcSdr;
class RpcSdrValve;
class RpcServer;
class RpcListener;
class RpcConn;

class RpcHeader : public SdrSerialize {
 public:
    static const uint8_t _currentVersion = 0xBF;

    static const uint8_t _opRequest = 1;
    static const uint8_t _opResponse = 2;
    static const uint8_t _opOpen = 3;
    static const uint8_t _opOpenResponse = 7;
    static const uint8_t _opAbort = 4;
    static const uint8_t _opPing = 5;
    static const uint8_t _opPingResponse = 6;

    static const int32_t _errNotOpen = 1;
    static const int32_t _errBadOpcode = 2;
    static const int32_t _errBadService = 3;

    uint8_t _version;           /* version number */
    uint8_t _headerSize;        /* in 8 byte units */
    uint8_t _opcode;            /* what type of request */
    uint8_t _pad0;              /* padding */
    uint32_t _reserved;         /* for channel, other stuff */
    uuid_t _serviceId;          /* client generated connection ID */
    uint32_t _requestId;        /* lollypop topology */
    uint32_t _size;             /* in bytes, not counting header */
    int32_t _error;             /* error code on response packets */
    uint32_t _pad1;             /* bring things to a multiple of 8 */

    int32_t marshal(Sdr *sdrp, int isMarshal);

    void generateResponse(RpcHeader *responseHeaderp);

    void setupBasic(RpcServer *serverp);
};

/* a specific incoming or outgoing call.  These are also the entities that
 * 'lock' a connection for transmission (or receiving, probably).
 */
class RpcContext : public CThread {
 public:
    RpcConn *_connp;            /* back pointer to connection */
    RpcServer *_serverp;        /* back pointer to server */
    uint32_t _ageMs;            /* call age in milliseconds */

    void getPeerAddr(uint32_t *ipAddrp, uint32_t *portp = 0);   /* in host order */

    RpcConn *getConn() {
        return _connp;
    }

    RpcServer *getServer() {
        return _serverp;
    }

    void setConn(RpcConn *connp) {
        _connp = connp;
    }

    void setServer(RpcServer *serverp) {
        _serverp = serverp;
    }
};

/* one of these gets instantiated for each incoming socket we want to handle
 * It creates a socket listener for each port requested.  You also add in RpcServers
 * for each server that needs to receive requests..
 */
class Rpc {
    friend class RpcConn;

    bool _shuttingDown;
    bool _shutdown;
    uint32_t _runningThreads;
    CThreadCV _shutdownCV;

 public:
    CThreadMutex _lock;

    dqueue<RpcConn> _allConns;

    dqueue<RpcServer> _allServers;

    void init();

    int32_t addListener(RpcServer *serverp, uint16_t v4Port);

    RpcServer *addServer(RpcServer *serverp, uuid_t *uuidp);

    static void uuidFromLongId(uuid_t *uuidp, uint32_t id) {
        memset(uuidp, 0, sizeof(*uuidp));
        memcpy(uuidp, &id, sizeof(id));
    }

    int32_t addClientConn(struct sockaddr_in *destAddrp, RpcConn **conpp);

    RpcServer *getServerById(uuid_t *idp);

    Rpc() : _shutdownCV(&_lock) {
        _shuttingDown = false;
        _shutdown = false;
        _runningThreads = 0;
    }

    void newThreadCreated();

    bool checkThreadMustExit() {
        bool result;

        _lock.take();
        result = checkThreadMustExitNL();
        _lock.release();

        return result;
    }

    bool checkThreadMustExitNL();

    void threadExiting(const char *whop) {
        _lock.take();
        threadExitingNL(whop);
        _lock.release();
    }

    void threadExitingNL(const char *whop);

    void shutdown();
};

/* this gets subclassed for every server method */
class RpcServerContext : public RpcContext {
 public:
    RpcServerContext *_dqNextp;
    RpcServerContext *_dqPrevp;
    uint32_t _requestId;        /* relative to server */

    /* function called when a call arrives */
    virtual int32_t serverMethod (RpcServer *serverp, Sdr *callDatap, Sdr *respDatap) = 0;

    virtual ~RpcServerContext() {
        return;
    }

    void release() {
        delete this;
    }
};

class RpcClientContext : public RpcContext {
 public:
    RpcClientContext *_dqNextp;
    RpcClientContext *_dqPrevp;
    CThreadCV _recvResponseCV;
    Rpc *_rpcp;
    uint8_t _waitingForResponse;
    uint8_t _haveResponse;
    uint8_t _failed;
    uint8_t _srLocked;          /* 0-none, 1-send, 2-recv */
    uint8_t _counted;           /* if bumped activeClientCount / in clientCalls */
    uint32_t _requestId;
    uint32_t _appOpcode;

    int32_t makeCall(RpcConn *connp, uint32_t opcode, RpcSdr **callSdrpp, RpcSdr **respSdrpp);

    int32_t getResponse();

    int32_t finishCall();

    void cleanup();

    RpcClientContext(Rpc *rpcp) : _recvResponseCV(&rpcp->_lock) {
        _rpcp = rpcp;
        _waitingForResponse = 0;
        _haveResponse = 0;
        _failed = 0;
        _srLocked = 0;
        _counted = 0;
    }

    Rpc *getRpc() {
        return _rpcp;
    }

    ~RpcClientContext() {
        return;
    }

    void release() {
        delete this;
    }

    int32_t waitForOpenNL();

    int32_t openServerNL();
};

/* class represents essentially a buffered pipe of characters, where one set of threads
 * can append characters, up to a count, and another set can remove characters when they're
 * available.  This module will provide sufficient synchronization so that the structures
 * won't get corrupted if people simultaneously read or write the pipe, but data may be
 * intermixed if there are simulataneous readers and writers.
 */
class RpcSdr : public Sdr {
 public:
    typedef int32_t NotifyProc(RpcSdr *sdrp, void *cxp);

    enum SubType {IsUnknown, IsIn, IsOut, IsBuffer};

 public:
    dqueue<OspMBuf> _bufs;      /* queue of buffers waiting to be consumed */
    uint32_t _byteCount;        /* bytes queued */

    /* cv and mutex protecting user's queue; when this is an input
     * chain (data from network to application), the user sleeps here
     * if there's no data available.  If this is an output chain
     * (application sending data over the network), the user sleeps
     * here if the pipe buffer is full, i.e.  too much data is queued
     * already.
     */
    CThreadCV _cv;
    CThreadMutex _lock;
    uint8_t _blocked;
    uint8_t _aborted;

    /* For input chains, this function is called when any data has
     * been read from _bufs; if the file descriptor has been blocked
     * due to excessive queueing, the callback is responsible for
     * unblocking the FD (probably adding it back to the set of polled
     * FDs).
     *
     * For output chains, this function is called when data has been
     * written to the chain, allowing the listener to read data from
     * the chain and write it out.
     */
    NotifyProc *_notifyProcp;
    void *_notifyContextp;

    uint32_t _maxSize;          /* minimum; somewhat advisory */

 public:
    SubType _subType;

    RpcSdr() : _cv(&_lock) {
        _subType = IsUnknown;
    }

    void init(uint32_t maxSize) {
        /* initialize anything that doesn't auto-construct */
        _maxSize = maxSize;
        _notifyProcp = NULL;
        _notifyContextp = NULL;
        _blocked = 0;
        _byteCount = 0;
        _aborted = 0;
        return;
    }

    virtual int32_t copyCountedBytes(char *targetp, uint32_t nbytes, int isMarshal);

    uint32_t bytes();

    void setCallback(NotifyProc *procp, void *contextp) {
        _notifyProcp = procp;
        _notifyContextp = contextp;
    }

    /* alternative wait to push data to a chain */
    virtual void appendMBuf(OspMBuf *appendp);

    virtual void doNotify() = 0;

    void abort() {
        _lock.take();
        _aborted = 1;
        if (_blocked) {
            _blocked = 0;
            _cv.broadcast();
        }
        _lock.release();
    }

    void freeNL() {
        OspMBuf *mbufp;
        OspMBuf *nextMBufp;

        for(mbufp = _bufs.head(); mbufp; mbufp = nextMBufp) {
            nextMBufp = mbufp->_dqNextp;
            delete mbufp;
        }

        _bufs.init();
    }

    void free() {
        _lock.take();
        freeNL();
        _lock.release();
    }

    void reset() {
        _lock.take();
        freeNL();
        _aborted = 0;
        
        if (_blocked) {
            _blocked = 0;
            _cv.broadcast();
        }

        _lock.release();
    }

    OspMBuf *popAll(uint32_t *byteCountp) {
        OspMBuf *mbufp;
        _lock.take();
        mbufp = _bufs.head();
        if (byteCountp)
            *byteCountp = _byteCount;
        _byteCount = 0;
        _bufs.init();
        _lock.release();
        return mbufp;
    }

    void append(RpcSdr *sdrp) {
        dqueue<OspMBuf> bufs;

        sdrp->_lock.take();
        bufs.concat(&sdrp->_bufs);
        sdrp->_bufs.init();
        sdrp->_lock.release();

        _lock.take();
        _bufs.concat(&bufs);
        _lock.release();

        doNotify();
    }

    uint32_t getAvailableBytes() {
        uint32_t count;
        _lock.take();
        count = _byteCount;
        _lock.release();
        return count;
    }
};

/* class representing data flowing into an application from some other entity
 * (typically a socket listener).
 */
class RpcSdrIn : public RpcSdr {
 public:
    RpcSdrIn() {
        _subType = RpcSdr::IsIn;
    }

    void doNotify();
};

class RpcSdrOut : public RpcSdr {
 public:
    RpcSdrOut() {
        _subType = RpcSdr::IsOut;
    }

    void doNotify();
};

class RpcSdrBuffer : public RpcSdr {
 public:
    RpcSdrBuffer() {
        _subType = RpcSdr::IsBuffer;
    }

    void doNotify() {}
};

/* one of these representing a listener for incoming RPC connections */
class RpcListener : public CThread {
    Rpc *_rpcp;
    RpcServer *_serverp;
    int _listenSocket;
    CThreadHandle *_listenerThreadp;

 public:
    uint16_t _v4Port;

    RpcListener() {
        return;
    }

    void listen(void *contextp);

    void init(Rpc *rpcp, RpcServer *serverp, uint16_t v4Port);
};

/* one of these for an incoming or outgoing connection.  Each conn has a RpcServer
 * for which it expects to receive calls.
 */
class RpcConn : public CThread {
    static const uint32_t _listenSize = 4096;
    static const uint32_t _queueLimit = 0x10000;
 public:
    int32_t _refCount;

    uint8_t _shutdownInProgress;
    uint8_t _listenerDone;
    uint8_t _helperDone;
    uint8_t _connected;
    uint8_t _connecting;
    uint32_t _activeClientCalls;
    uint32_t _hardTimeoutMs;
    CThreadCV _openCV;

    Rpc *_rpcp;
    RpcListener *_listenerp;
    int _fd;    /* socket's FD */

    CThreadHandle *_listenerThreadp;
    CThreadHandle *_helperThreadp;

    RpcContext _anonContext;

    RpcHeader _header;
    RpcHeader _responseHeader;
    uint8_t _isClient;
    struct sockaddr_in _peerAddr;

    RpcConn *_dqNextp;
    RpcConn *_dqPrevp;

    /* associated server for this connection */
    RpcServer *_serverp;

    /* active incoming or outgoing call; this is set when a specific
     * call is sending a RPC packet in or out, and prevents other
     * conns from intermixing bytes in the socket.  Null if the
     * connection is available for use.
     */
    RpcContext *_sendCallActivep;        /* outgoing client call or server response */

    /* CVs for above; sleep on this when needing to set one of above pointers */
    CThreadCV _sendCallCV;

    RpcContext *_receiveCallActivep;
    CThreadCV _receiveCallCV;

    /* chain of mbufs received from the socket */
    RpcSdrIn _receiveChain;

    /* when FD last received or transmitted data, including ping /
     * ping response messages.
     */
    uint64_t _lastActiveMs;

    /* chain of mbufs generated by caller / response; we'll add a
     * header to these and then append them to the sendChain for
     * transmission.
     */
    RpcSdrBuffer _bodyChain;

    /* chain of outgoing mbufs to send on the socket when it is
     * clear.
     */
    RpcSdrOut _sendChain;

 public:
    RpcConn(Rpc *rpcp, RpcListener *listenerp) : 
      _openCV(&rpcp->_lock), _sendCallCV(&rpcp->_lock), _receiveCallCV(&rpcp->_lock) {
        _rpcp = rpcp;
        _listenerp = listenerp;
        _fd = -1;
        printf("fd6 %p %d\n", this, _fd);
        _refCount = 0;
        _shutdownInProgress = 0;
        _listenerDone = 1;
        _helperDone = 1;
        _connected = 0;
        _connecting = 0;
        _activeClientCalls = 0;
        _hardTimeoutMs = 60000;

        rpcp->_lock.take();
        rpcp->_allConns.append(this);
        rpcp->_lock.release();
    }

    static int32_t sendData(RpcSdr *sdrp, void *cxp);

    ~RpcConn();

    void abort(const char *strp);

    void listenFd(void *contextp);

    void helperDone(const char *strp);

    void helper(void *argp);

    void holdNL() {
        _refCount++;
    }

    void getPeerAddr(uint32_t *addrp, uint32_t *port = 0);  /* in host order */

    int failedNL() {
        int connFailed;

        if (_shutdownInProgress || !_connected)
            connFailed = 1;
        else
            connFailed = 0;

        return connFailed;
    }

    int failed() {
        int connFailed;
        _rpcp->_lock.take();
        connFailed = failedNL();
        _rpcp->_lock.release();
        
        return connFailed;
    }

    void checkShutdownNL();

    void setHardTimeout(uint32_t ms) {
        _hardTimeoutMs = ms;
    }

    void releaseNL() {
        printf("conn %p release old refct=%d\n", this, _refCount);
        osp_assert(_refCount > 0);

        checkShutdownNL();

        if (--_refCount == 0) {
            printf("deleting conn %p\n", this);
            delete this;
        }
    }

    int32_t setupConn();

    void hold() {
        _rpcp->_lock.take();
        holdNL();
        _rpcp->_lock.release();
    }

    void release() {
        Rpc *rpcp = _rpcp;

        rpcp->_lock.take();
        releaseNL();
        rpcp->_lock.release();
    }

    int32_t sendHeaderResponse(RpcHeader *headerp);

    void waitForSendNL(RpcContext *contextp);

    int32_t waitForReceiveNL(RpcContext *contextp);

    void releaseReceiveNL();

    void releaseSendNL();

    void releaseSend() {
        _rpcp->_lock.take();
        releaseSendNL();
        _rpcp->_lock.release();
    }

    void releaseReceive() {
        _rpcp->_lock.take();
        releaseReceiveNL();
        _rpcp->_lock.release();
    }

    void updateActivity();

    void terminate(const char *whyp);

    int32_t waitForReceive(RpcContext *contextp) {
        int32_t code;
        _rpcp->_lock.take();
        code = waitForReceiveNL(contextp);
        _rpcp->_lock.release();

        return code;
    }

    void exchangeReceiveOwnerNL(RpcContext *serverContextp) {
        osp_assert(serverContextp != NULL);
        osp_assert(_receiveCallActivep == &_anonContext);
        _receiveCallActivep = serverContextp;
    }

    void reverseConnNL();

    void reverseConn() {
        _rpcp->_lock.take();
        reverseConnNL();
        _rpcp->_lock.release();
    }

    void exchangeReceiveOwner(RpcContext *serverContextp) {
        _rpcp->_lock.take();
        exchangeReceiveOwnerNL(serverContextp);
        _rpcp->_lock.release();
    }

    void setServer(RpcServer *serverp) {
        _serverp = serverp;
    }

    void initBase();

    void initServer(int fd);

    void initClient();
};

/* this gets subclassed for an RPC server instance */
class RpcServer {
 public:
    /* wait CV for server waiting for it to be safe to start
     * sending a response.
     */
    CThreadCV _sendResponseCV;

    /* server only structures */
    dqueue<RpcServerContext> _serverCalls; /* queue of all executing calls */

    /* client only structures */
    dqueue<RpcClientContext> _clientCalls; /* first call is active, others are wating for conn */

    RpcSdrOut *_outChainp;

    CThreadCV _openWaitersCV;
    uint8_t _opening;
    uint8_t _openWaitersPresent;
    uint8_t _isOpen;

    uint32_t _nextRequestId;

    uuid_t _serviceId;
    Rpc *_rpcp;

    /* flags */

    RpcServer *_dqNextp;
    RpcServer *_dqPrevp;

    /* called by service initialization code */
    int32_t registerServerProc
        ( uint32_t opcode,
          RpcServerContext *(*contextFactoryp)(RpcServer *serverp));

    virtual RpcServerContext *getContext(uint32_t opcode) {
        /* if not overriden, this is a client-side RpcServer and we shouldn't
         * be calling this, since it is only called when receiving service side
         * calls.
         */
        osp_assert(0);
        return NULL;
    }

    /* called by server context method when a call is finished */
    void callComplete( int32_t responseCode, RpcServerContext *contextp);

    RpcClientContext *findContext(uint32_t requestId);

    /* serviceId is set when addServer is called */
    RpcServer(Rpc *rpcp) : _sendResponseCV(&rpcp->_lock), _openWaitersCV(&rpcp->_lock) {
        _rpcp = rpcp;
        _isOpen = 0;
        _openWaitersPresent = 0;
        _opening = 0;
        _outChainp = NULL;  /* filled in when request arrives */
        _nextRequestId = 2;
        return;
    }
};

#endif /* __RPC_H_ENV_ */
