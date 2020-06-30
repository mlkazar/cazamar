#ifndef _RS_H_ENV__
#define _RS_H_ENV__ 1

/* The basic protocol uses a transport, possibly TCP, to deliver a
 * sequence of packets to the remote end.  Multiple connections can
 * share the same transport connection (e.g. TCP connection).
 *
 * Each Rs connection is identified by a UUID present in every request
 * packet.  Before the connection can be used, a bind/bindAck pair
 * must be exchanged -- this allows us to provide at most once
 * semantic guarantees.  Otherwise, the first real request could be
 * transmitted, executed, and then the server could crash, and
 * re-execute a retransmitted request.  With a bind handshake included
 * in the protocol, this can't occur: once the bind handshake has
 * happened, if a crash occurs after executing the first request, but
 * before sending a response, then any retransmission will trigger a
 * "unbound connection" error, and the client will know that all calls
 * that were ever transmitted may have executed, and shouldn't be
 * retransmitted.
 *
 * Each call within a connection includes a client-specified context,
 * and some flow control information as well.  This flow control
 * information includes a window (in packets) and a sequence (in
 * packets); this enables us to bound the amount of state received by
 * connection, while keeping the connection flowing smoothly, and also
 * allows us to detect a duplicate transmitted packet on a newly
 * established *tcp* connection, if a single connection fails.
 *
 * Locking rules: there are locks (probably spin locks) on both the Rs
 * structure and individual RsConn connections.  The per-connection
 * locks protect the basic fields on RsConn and RsCall structures,
 * while the global locks protect the remaining structures.  Note that
 * next and prev fields in a structure are protected by the lock of
 * the object holding the queue, not the containing structure, and
 * that refCount and deleted fields are all protected by the Rs lock,
 * except for RsCall reference counts, which are protected by the
 * containing connection lock.
 *
 * Reference counts are maintained on most structures.  They need to
 * be held over asynchronous calls, of course.  In addition, RsCalls
 * hold their containing RsConns, RsConns hold their cached associated
 * RsSocket, and both RsConns and RsSockets hold the Rs object itself.
 * Rs and RsSocket objects both stick around with reference counts of
 * 0, until the deleted flag is set on them.  RsClientCall and
 * RsServerCall structures hang off of, and hold a reference to, their
 * corresponding client RsConn or server RsConn structures, and are
 * protected by the corresponding connection locks.
 */
 
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <osp/ospalloc.h>
#include <osp/ospmbuf.h>
#include <osp/dqueue.h>
#include <osp/squeue.h>
#include <dispatcher/Dispatcher.h>
#include <dispatcher/Task.h>
#include <rs/rsfwds.h>
#include <util/Lock.h>
#include <util/UUID.h>
#include <util/PoolPerDispatcher.h>
#include <util/SysInit.h>
#include <pthread.h>
#include <boost/scoped_ptr.hpp>

class OspTimerEvent;
class WatchDog;

#define RSSOCKET_MAGIC		0xee

#define RSHEADER_MAGIC		0xab
#define RSHEADER_WINDOW		32

MBUF_ALLOC_TAG_DECLARE(MB_RS_TAG);

/* return -1 if a<b, 0 if they're equal, otherwise 1 */
#define Rs_seqCmp(a,b) ((int32_t)((a) - (b)))

/* one of these for each packet in a queue */
class RsPacket {
 public:
    RsPacket *_dqNextp;
    RsPacket *_dqPrevp;	/* points into mbuf */
    RsHeader *_headerp; /* xmit only: points into mbuf for us to patch */
    uint32_t _seq;	/* xmit only from header, in host border */
    OspMbuf *_mbufp;
};

/* header is transmitted in network byte order of course; total size is
 * 10 words, or 40 bytes.
 */
#define RSHEADER_PTYPE_BIND		1
#define RSHEADER_PTYPE_BIND_RESP	2
#define RSHEADER_PTYPE_CALL		3
#define RSHEADER_PTYPE_RESP		4
#define RSHEADER_PTYPE_ACK		5
#define RSHEADER_PTYPE_ABORT		6

#define RS_ERR_NOCONN			1

/* compare two integers that may wrap */
#define RsCmp(a,b)	(((signed long)a) - ((signed long)b))

class RsHeader {
 public:
    UUID _connId;	/* connection UUID */
    uint8_t _magic;	/* magic # to check framing */
    uint8_t _ptype;	/* packet type */
    uint8_t _hwords;	/* header size in 32 bit words */
    uint8_t _flags;	/* for future use */
    uint32_t _seq;	/* in window */
    uint32_t _ack;	/* all packets w/seq < ack have been received */
    uint16_t _window;	/* for opposing direction flow */

    /* some per-call information -- a clientId giving the specific
     * call's details, and application level info of a service ID and
     * an opcode within that service
     */
    uint32_t _clientId;	/* cookie identifying call on client side */
    union {
	struct {
	    uint16_t _opcode;
	    uint16_t _serviceId;
	} _call;;
	struct {
	    uint32_t _code;
	} _resp;
	struct {
	    uint32_t _code;
	} _abort;
    } _u;

    RsHeader() : _connId(UUID::uuidNone) {};
};

class RsService {
 public:
    typedef void UnregisterCallback(void *contextp);

    RsService *_nextp;		/* next in hash bucket */
    uint32_t _serviceId;
    uint8_t _doDispatch;
    RsServerProc *_serviceProcp;
    void *_serviceContextp;
    Rs *_rsp;
    std::string _serviceName;
    size_t _refCount;
    UnregisterCallback *_unregisterCallback;
    void *_unregisterContextp;

    RsService( Rs *rsp,
	       uint32_t serviceId,
	       RsServerProc *procp,
	       void *contextp);

    void unregister(UnregisterCallback *callback, void *contextp);

    /* interface specifies target policy, but we run on separate
     * dispatcher for now.
     */
    void setDispatchPolicy(DispatcherPolicy::ePolicy policy) {
        _doDispatch = 1;
    }

    void setServiceName (const std::string &name) {
	_serviceName = name;
    }
};

/* object passed to server calls to get response back to specific
 * client call.
 */
class RsServerCall {
 public:
    uint32_t _refCount;
    Rs *_rsp;
    RsServerCall *_dqNextp;
    RsServerCall *_dqPrevp;
    uint32_t _clientId;
    RsConn *_connp;	/* reference held */
    RsService *_servicep;
    OspNetMBuf *_mbufp;
    uint8_t _free;

    void sendResponse( OspMbuf *mbufp, int32_t errorCode);

    void dispatch();
};

/* object tracking a single call on a connection; identified by its
 * clientID + Rs connection.
 */
class RsClientCall {
 public:
    uint32_t _refCount;
    RsClientCall *_dqNextp;
    RsClientCall *_dqPrevp;
    RsConn *_connp;		/* reference held */
    uint16_t _clientId;
    uint16_t _sequence;
    RsResponseProc *_responseProcp;
    void *_responseContextp;
    void (*_dispatchProcp)();
    uint8_t _free;
    uint8_t _doAbort;
    int32_t _ttl;
};

/* sockets are locked by the Rs lock, and stick around with 0 reference
 * counts until deleted is set.
 */
class RsSocket {
 public:
    uint32_t _refCount;
    uint8_t _deleted;
    uint8_t _autoDelete;
    RsSocket *_allNextp;                /* in allSocketsp */
    Rs *_rsp;
    OspNetSocketTCP *_sockp;
    OspNetSockAddr _sockAddr;
    OspNetVLAN *_vlanp;
    uint8_t _connecting;
    uint8_t _connected;
    static const uint32_t _maxConnectionErrors = 5;
    uint32_t _numConnectionErrors;
    std::string _bindAddr;

    /* these fields (dqNextp, dqPrevp and incoming) are protected by
     * the Rs::_incomingQueue mutex; if the socket is in the incoming
     * queue, it has an extra reference, as well.
     */
    RsSocket *_dqNextp;                 /* in Rs::_incoming queue */
    RsSocket *_dqPrevp;
    class {
    public:
        OspMbuf *_headp;        /* linked through setNextPkt() / getNextPkt() */
        OspMbuf *_tailp;
        uint8_t _inIncoming;

        /* get initialized by RsSocket's memset */

        OspMbuf *getAll() {
            OspMbuf *p;
            p = _headp;
            _headp = NULL;
            _tailp = NULL;
            return p;
        }

        /* must be called with incoming lock held */
        void append(OspMbuf *p) {
            p->setNextPkt(NULL);
            if (_tailp) {
                _tailp->setNextPkt(p);
            }
            else {
                _headp = p;
            }
            _tailp = p;
        }

    } _incoming;

    /* queue of packets waiting to be sent until a connection is
     * established.
     */
    dqueue<RsPacket> _connectingQueue;

    /* each packet, in a TCP encapsulation, is preceded by a 4 byte
     * count of the # of bytes present in the packet, with a 1 byte
     * frame of 0xee at the start, and then a 24 bit count field.  If
     * we read part of the packet.  The count is in network byte order
     * (MSB first), and we accumulate the count in pktCount.
     * pktHdrValid gives the count of the # of bytes of the header
     * that have been seen.  We pop off the count bytes as we encounter
     * them, and put them in pktCount.  Any partial packet data is in 
     * pktMBufp.
     *
     * Note that these three fields are updated only from the TCP
     * upcall, and so we don't need the connection lock while
     * modifying these fields.
     */
    uint8_t _pktHdrValid;
    uint32_t _pktCount;
    OspMbuf *_pktBufp;

    uint32_t _idleCount;

    RsSocket();

    void handleAutoDelete();

    void releaseNL();

    void release();

    void hold();

    void holdNL();

    /* abort the use of a socket because of an error */
    void abort();

    /* pop the TCP record label from a packet, or as much of the label
     * as we have (and record what we have got so far).  No real locking
     * required since we're running this from the socket receive dispatch
     * caller.
     */
    int32_t absorbHeader(uint32_t *nbytesp, OspMbuf **newBufpp);

    int32_t send(OspMbuf *bufp);
};

class RsIncomingQueue {
 public:
    /* list of all sockets with incoming packets; each socket has an additional
     * reference.
     */
    squeue<RsSocket> _sockets;

    pthread_mutex_t _mutex;
    pthread_cond_t _cv;
    uint8_t _blocking;  /* true if worker thread is sleeping */

    RsIncomingQueue() {
        pthread_mutex_init(&_mutex, NULL);
        pthread_cond_init(&_cv, NULL);
        _sockets.init();
        _blocking = 0;
    }

    /* adds reference to socket that will be released when the 
     * mbufs are dispatched.
     */
    void queue(RsSocket *socketp, OspMbuf *p) {
        int doWakeup = 0;
        pthread_mutex_lock(&_mutex);
        socketp->_incoming.append(p);

        /* do wakeup operation after dropping lock to target's avoid
         * hitting lock immediately on wakeup.
         */
        if (_blocking) {
            _blocking = 0;
            doWakeup = 1;
        }

        /* and make sure socket is in active sockets queue */
        if (!socketp->_incoming._inIncoming) {
            socketp->hold();
            socketp->_incoming._inIncoming = 1;
            _sockets.append(socketp);
        }

        pthread_mutex_unlock(&_mutex);

        if (doWakeup) {
            pthread_cond_broadcast(&_cv);
        }
    }

    /* socket is returned referenced, and caller must release */
    void getAll(RsSocket **osocketpp, OspMbuf **ombufpp) {
        RsSocket *socketp;
        OspMbuf *p;

        pthread_mutex_lock(&_mutex);
        while(1) {
            if ((_sockets.head()) != NULL)
                break;

            /* nothing left */
            _blocking = 1;
            pthread_cond_wait(&_cv, &_mutex);
        }

        /* return entire list from a socket */
        socketp = _sockets.pop();
        socketp->_incoming._inIncoming = 0;
        p = socketp->_incoming.getAll();
        osp_assert(p != NULL);

        *osocketpp = socketp;
        *ombufpp = p;

        pthread_mutex_unlock(&_mutex);
    }
};

/* one of these per Rs server, typically */
class Rs {

 private:

    Rs(bool noSharePreferredAllocator, bool useWatchDog=true);

    /* for initWithSocket, sockfd port should be created (via socket)
     * but not yet bound.
     */
    int32_t initWithSocket(OspNetVLAN *vlanp, uint16_t port, int sockfd);
    
 public:
    static const uint32_t _hashSize = 2048;
    static Rs* _instanceServer;
    static Rs* _instanceClient;
    static Rs* _rsListHead;
    Rs * _nextRs;
    int32_t _refCount;
    MutexLock _mutex;
    int _sockfd;
    uint16_t _port;	/* listening port, in host byte order */
    OspNetVLAN *_vlanp;	/* listening port's vlan */
    dqueue<RsClientCall> _runningCalls;
    dqueue<RsServerCall> _activeCalls;
    static uint32_t _globalNumSockets;
    RsSocket *_allSocketsp;
    RsIncomingQueue _incomingQueue;
    RsConn *_connHashp[_hashSize];
    std::map<std::string,uint32_t> _recentConnections;
    bool _noSharePreferredAllocator;
    bool _useWatchDog;
    RsService *_allServicesp;
    boost::scoped_ptr<WatchDog> _watchDogp;
    
    PoolPerDispatcher<RsPacket> _freePackets __attribute__((__aligned__(OSP_CACHELINE)));
    
    static std::set<RsSocket *> _activeSockets;
    static MutexLock _activeSocketsLock;

    ~Rs();

    /* never call these two while holding an rs lock! */
    static void addToActive(RsSocket *);
    static void rmFromActive(RsSocket *);

    /* returns 0 if a reference is held, else non-zero
     * never call while holding an rs lock!
     * If the object is still present, rs lock will be grabbed via 'hold'.
     */
    static int holdSocket(RsSocket *);
    
    static Rs* getInstanceServer();
    static Rs* getInstanceClient();
    
    // Should only be called if a seperate Rs instance is needed
    static Rs* createServer(uint16_t port, bool noSharePreferredAllocator, bool useWatchDog=true);
    
    int getSocket() const
    {
        return _sockfd;
    }

    void processPacket( RsSocket *sp, OspMbuf *mbufp);

    OspNetVLAN *getVLAN() {
	return _vlanp;
    }

    void resetWatchdog(uint32_t watchDogTime);

    /* free a packet from external world */
    void freePacket( RsPacket *p) {
	if (p->_mbufp) {
	    p->_mbufp->mfreem();
	    p->_mbufp = NULL;
	}
        _freePackets.free(p);
    }

    /* get a packet; note that this is a packet wrapper, not an actual
     * mbuf.
     */
    RsPacket *getPacket() {
	return (RsPacket*)_freePackets.malloc();
    }

    static void incomingProc( void *contextp, OspMbuf *mbufp);

    static void listenProc( void *contextp, OspNetSocketTCP *socketp);

    RsSocket * getSocket( OspNetVLAN *vlanp,
			  OspNetSockAddr *peerAddrp,
			  const std::string &bindAddr);

    // findService bumps the service's refcount (if the service is found).
    RsService * findService( uint32_t serviceId);

    static void socketConnected(void *contextp, int32_t code);

    RsConn * getConnection( UUID *connIdp, OspNetVLAN *vlanp, int incoming);

    void sendAbort(RsSocket *sp, RsHeader *hp);

    void sendAck(RsSocket *sp, RsConn *connp);

    RsConn * findConnection( UUID *connIdp);

    RsConn * getClientConn(OspNetSockAddr *peerAddrp,
			   const UUID &destNode = UUID::uuidNone,
			   const std::string &serviceName = "unknown",
			   const std::string &bindAddr = "",
			   bool aggressiveSocketDelete = false);

    static void * incomingWorker(void *contextp);

    static int initDataDump(void *arg);

    void initPeriodicCleanup();

    void periodicCleanupCheck();

    static bool _allowTracing;
    static void setTracing(bool enable) {
	_allowTracing = enable;
    }
    static void traceSnapshot();

    static int shutdown(void *);
};

/* maintain flow control state for a connection within one or more
 * TCP connnections.  Variables with prefix x describe outgoing 
 * (transmitted) stream, while variables with prefix r describe
 * the incoming (received) packet stream.
 */
class RsFlow {
 public:
    uint32_t _xFirstNoSend;	/* ID of first packet we can't send because of
				 * window limitations.
				 */
    uint32_t _rLastReceived;	/* all less have been received */
    uint32_t _xAcknowledged;	/* all less have been acked for xmit stream */
    uint32_t _rFirstNoSend;     /* first remote not allowed to send us */
    dqueue<RsPacket> _xmitQ;	/* queue of packets waiting for quota,
				 * or other things that prevent packet
				 * sending.
				 */

    void init() {
	_xFirstNoSend = 0;	/* window starts off closed */
	_rLastReceived = 0;	/* all less than this have been acked */
	_xAcknowledged = 0;	/* haven't received any acks yet */
        _rFirstNoSend = 0;      /* window starts off closed */
	_xmitQ.init();
    }
};


/* connection structure */
struct RsConn : public Task{
    static const uint32_t _maxCCalls = 1024;
    static const uint32_t _msPerTick = 2000;
 public:
    RsConn();

    UUID _peerNode;
    uint32_t _connNum;
    uint32_t _refCount;
    MutexLock _mutex;
    OspNetVLAN *_vlanp;
    OspNetSockAddr _peerAddr;
    Rs *_rsp;
    RsConn *_hashNextp;
    RsConn *_dqNextp;
    RsConn *_dqPrevp;
    RsFlow _flow;
    UUID _connId;
    uint32_t _nextSeq;		/* next seq to assign to an outgoing pkt */
    RsSocket *_socketp;		/* may be unbound */
    OspTimerEvent *_getSocketTimerp;
    bool _aborted;
    uint32_t _idleCount;
    std::string _bindAddr;      /* If not empty, try to bind to this addr. */
    std::string _serviceName;

    /* server-side call state */
    RsServerCall *_freeSCallsp;

    /* client-side call state, including table indexed by call ID
     * to find the context quickly.
     */
    RsClientCall *_freeCCallsp;
    uint32_t _ccallCount;
    RsClientCall *_ccallTablep[_maxCCalls];
    OspTimerEvent *_bindTimerp;
    OspTimerEvent *_hardTimerp; /* non-null if connection has a call timeout associated with it */
    uint32_t _hardTimeout;

    /* flags */
    uint8_t _bound;
    uint8_t _binding;
    uint8_t _deleted;
    uint8_t _incoming;
    uint8_t _aggressiveSocketDelete;

    static uint32_t hashUUID( UUID *uuidp) {
	uint32_t *ap = (uint32_t *) uuidp;
	return (1021 * (ap[0] + ap[1] + ap[2] + ap[3])) & (Rs::_hashSize-1);
    }
    
    #ifdef DEBUG
    #define RS_CONN_TRACING 0
    #else
    #define RS_CONN_TRACING 0
    #endif
    #if RS_CONN_TRACING
    std::string *trace();
    #endif

    void handleAbort();

    /* Do not call this externally. */
    void handleAbortNL();

    void freeQueuedPackets();

    void abortWaitingCalls(int32_t code);
    void abortWaitingCallsThread(RsClientCall *ccallp, int32_t code);
    
    void setDeleted() {
	osp_assert(_refCount > 0);
	_mutex.take();
	_deleted = 1;
	_mutex.release();
    }

    void trackRemoteWindow() {
        uint32_t newValue = _flow._rLastReceived + RSHEADER_WINDOW;
        if (RsCmp(newValue, _flow._rFirstNoSend) > 0) {
            _flow._rFirstNoSend = newValue;
        }
    }

    void holdNL() {
        #if RS_CONN_TRACING
	*trace() += " holdNL";
        #endif
	_refCount++;
    }

    void hold() {
	Rs *rsp = _rsp;
	rsp->_mutex.take();
	holdNL();
	rsp->_mutex.release();
    }

    void releaseNL();

    void setHardTimeout(uint32_t timeoutSecs);

    void hashIn();

    void hashOut();

    void processAckInfo( RsHeader *hp, int willRespond);

    /* drops ref count, but doesn't destroy object */
    void release() {
	Rs *rsp = _rsp;
	rsp->_mutex.take();
	releaseNL();
	rsp->_mutex.release();
    }

    void destroy() {
        osp_assert(!_incoming);
        setDeleted();
        release();
    }

    /* this function returns an *unheld* socket pointer, assuming that
     * the connection is locked, and so the socket can't disappear on
     * us, and is still held by the connection.  This function is
     * called with the connection locked, and the global lock *not*
     * locked.
     */
    RsSocket *findSocket() {
	if (_socketp && !_socketp->_deleted) {
	    return _socketp;
	}
	else {
	    RsSocket *sp;
	    if (_socketp) {
		_socketp->release();
		_socketp = NULL;
	    }

	    /* The socket is returned held */
	    sp = _rsp->getSocket(_vlanp, &_peerAddr, _bindAddr);
	    _socketp = sp;
	    return sp;
	}
    }

    int32_t getConnectedSocket();

    /* internal function to setup the core fields in a Rs header */
    void setupCore(RsHeader *hp, uint8_t ptype) {
	memcpy(&hp->_connId, &_connId, sizeof(UUID));
	hp->_magic = RSHEADER_MAGIC;
	hp->_ptype = ptype;
	hp->_hwords = sizeof(RsHeader)/4;
	hp->_flags = 0;
    }

    /* setup a basic control packet that doesn't need the flow control
     * information (though it does advertize the window size).
     */
    void setupControl(RsHeader *hp, uint8_t ptype) {
	setupCore(hp, ptype);
	hp->_seq = 0;
	hp->_ack = 0;
	hp->_clientId = 0;
	hp->_window = osp_htons(RSHEADER_WINDOW);
	hp->_u._call._opcode = 0;
	hp->_u._call._serviceId = 0;
    }

    /* setup a data or ack packet that also contains the packet flow
     * information.  Caller must still fill in _clientId, opcode and
     * service.
     */
    void setupData(RsHeader *hp, uint8_t ptype) {
	setupCore(hp, ptype);
	hp->_seq = osp_htonl(_nextSeq);
	hp->_ack = osp_htonl(_flow._rLastReceived);
	hp->_window = osp_htons(RSHEADER_WINDOW);
    }

    int32_t sendPackets();

    void startGetSocketTimer();

    void retryGetSocketTimer(OspTimerEvent *evp);

    void hardTimerTick( OspTimerEvent *eventp);


    int32_t startBind();

    /* If non-zero is returned, an error occurred and the callback
       will not be activated. 
       Ownership of datap is always passed. On error it will be freed too */
    int32_t call2( OspMbuf *datap,
		   uint32_t serviceId,
		   uint32_t opcode,
		   RsResponseProc *responseProcp,
		   void *responseContextp,
		   void (*dispatchProcp)());

    int32_t call( OspMbuf *datap,
		  uint32_t serviceId,
		  uint32_t opcode,
		  RsResponseProc *responseProcp,
		  void *responseContextp) {
	return call2( datap,
		      serviceId,
		      opcode,
		      responseProcp,
		      responseContextp,
		      NULL);
    }

    RsClientCall *getCCall() {
	osp_assert(_mutex.isLocked());
	RsClientCall *p = _freeCCallsp;
	if (!p) {
	    if (_ccallCount >= _maxCCalls) return NULL;
	    p = new RsClientCall;
	    p->_clientId = _ccallCount++;
	    p->_sequence = 0;
	    p->_connp = this;
	}
	else {
	    _freeCCallsp = p->_dqNextp;
	    osp_assert(p->_free);
	}
	p->_free = 0;
	p->_doAbort = 0;

	hold();	/* all allocated ccalls hold their client */
	return p;
    }

    RsClientCall * takeCCall( uint32_t clientId);

    void freeCCall(RsClientCall *p) {
	osp_assert(_mutex.isLocked());
	osp_assert( !p->_free);
	p->_free = 1;
	p->_sequence++;
	p->_dqNextp = _freeCCallsp;
	_freeCCallsp = p;
	release();
    }

    RsServerCall *getSCall() {
	osp_assert(_mutex.isLocked());
	RsServerCall *p = _freeSCallsp;
	if (!p) {
	    p = new RsServerCall;
	}
	else {
	    _freeSCallsp = p->_dqNextp;
	}
	return p;
    }

    void freeSCall(RsServerCall *p) {
	osp_assert(_mutex.isLocked());
	p->_dqNextp = _freeSCallsp;
	_freeSCallsp = p;
    }

    int32_t abortConn();

    void retryBindTimer(OspTimerEvent *evp);
    
    void releaseTask() {}

    /* Report a socket error.
     * Returns 0 if the connection is being reconnected.
     * Returns 1 if the connection has been aborted.
     */
    int socketError();
    
    bool isAborted() const
    {
        return _aborted;
    }
};

EXPORT_SYS_INIT(RsDump)
EXPORT_SYS_SHUTDOWN(rs);
#endif /* _RS_H_ENV__ */
