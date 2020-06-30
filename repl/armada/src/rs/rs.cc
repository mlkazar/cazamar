#include <deque>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sstream>

#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <cmdctl/datadump/DataDump.h>
#include <cmdctl/datadump/XmlStream.h>
#include <osp/ospnet.h>
#include <osp/osptimer.h>
#include <platform/platform.h>
#include <util/BoostTimer.h>
#include <util/Connection.h>
#include <util/DeDupLogger.h>
#include <util/NetworkUtils.h>
#include <util/Node.h>
#include <util/StringTrace.h>
#include <util/StringTraceLogger.h>
#include <util/UUID.h>
#include <util/Util.xdr.h>
#include <util/WatchDog.h>
#include <rs/rs.h>

class DataDumpSearch;

using namespace std;
using namespace CallbackStreamNS;

MBUF_ALLOC_TAG(MB_RS_TAG,"rs","rs");

set<RsSocket *> Rs::_activeSockets;
MutexLock Rs::_activeSocketsLock;
Rs *Rs::_rsListHead = NULL;
bool Rs::_allowTracing = false;
uint32_t Rs::_globalNumSockets = 0;

static const uint32_t RsPeriodicCleanupInterval = 600; /* 600 seconds, 10 min*/
static const uint32_t RsConnMaxIdleTime = 7200; /* 2 hours */
static const uint32_t RsSocketMaxIdleTime = 86400; /* 1 day */

// These are timeous to be used when there are too many connections
// or too many sockets open.
static const uint32_t RsConnCongestedNumConnections = 1000;
static const uint32_t RsConnCongestedIdleTime = 1200; /* 20 minutes */
static const uint32_t RsSocketCongestedNumSockets = 400;
static const uint32_t RsSocketCongestedIdleTime = 3600; /* 1 hour */

static const uint32_t RsTraceSize = 4096;
StringTraceLogger RsTrace(RsTraceSize, true,
                          RollingLogSink::factory("RsTrace"));

Rs::Rs(bool noSharePreferredAllocator,
       bool useWatchDog)
{
    static SpinLock l;

    _noSharePreferredAllocator = noSharePreferredAllocator;
    _useWatchDog = useWatchDog;
    _refCount = 0;
    _sockfd = -1;
    _port = 0;
    _vlanp = 0;
    _runningCalls.init();
    _activeCalls.init();
    _allSocketsp = NULL;
    memset(_connHashp, 0, sizeof(RsConn *) * _hashSize);
    _allServicesp = 0;
    _watchDogp.reset();

    l.take();
    _nextRs = _rsListHead;
    _rsListHead = this;
    l.release();

    /* now create a service thread for incoming packets */
    {
        pthread_attr_t pthreadAttrs;
        pthread_attr_t* pthreadAttrsPtr = &pthreadAttrs;
        pthread_t junkId;
        int ret;

        memset(pthreadAttrsPtr, 0, sizeof(*pthreadAttrsPtr));
        ret = pthread_attr_init(&pthreadAttrs);
        if (ret) {
            fprintf(stderr, "Rs::Rs cannot init pthreadAttrs ret=%d\n", ret);
            pthreadAttrsPtr = NULL;
        }
        if (_noSharePreferredAllocator) {
            osp_extra_new_setsp->adjustAutoAssignOffCnt(true);
        }
        ret = pthread_create( &junkId,
                              pthreadAttrsPtr,
                              incomingWorker,
                              this);
        if (ret) {
            fprintf(stderr, "Rs::Rs cannot create thread ret=%d\n", ret);
        }
    }

    initPeriodicCleanup();
}

Rs::~Rs()
{
}

RsService *
Rs::findService( uint32_t serviceId)
{
    RsService *servicep;

    Monitor<MutexLock> m(_mutex);
    for(servicep = _allServicesp; servicep; servicep = servicep->_nextp) {
	if (servicep->_serviceId == serviceId) {
	    ++servicep->_refCount;
	    return servicep;
	}
    }
    return NULL;
}

/* called with global lock held */
RsConn *
Rs::findConnection( UUID *connIdp)
{
    RsConn *connp;
    uint32_t ix;

    osp_assert(_mutex.isLocked());
    
    ix = RsConn::hashUUID(connIdp);
    for(connp = _connHashp[ix]; connp; connp = connp->_hashNextp) {
	if (memcmp(&connp->_connId, connIdp, sizeof(UUID)) == 0) {
            #if RS_CONN_TRACING
            *connp->trace() += " findConnectionHold";
            #endif
	    connp->_refCount++;
	    return connp;
	}
    }
    return NULL;
}

void
RsServerCall::dispatch()
{
    RsService *servicep = _servicep;
    OspNetMBuf *mbufp = _mbufp;

    /* note that the RsServerCall (this) will be freed by sendResponse */
    _mbufp = NULL;
    servicep->_serviceProcp( servicep->_serviceContextp, this, mbufp);
}

/* send a response to a call */
void
RsServerCall::sendResponse( OspNetMBuf *mbufp, int32_t rcode)
{
    RsConn *connp = _connp;
    RsHeader *hp;
    RsPacket *p;
    uint32_t seq;

    osp_assert(mbufp);
    hp = (RsHeader *) mbufp->prepend(&mbufp, sizeof(RsHeader),MB_RS_TAG);
    if (!hp) {
	// no mbufs available now, retry in 1/2 second
	new BoostTimer(500, boost::bind(&RsServerCall::sendResponse,
					this, mbufp, rcode));
	return;
    }
    p = _rsp->getPacket();

    /* conn is held by reference from server call */
    Monitor<MutexLock> m(connp->_mutex);
    memcpy(&hp->_connId, &connp->_connId, sizeof(hp->_connId));
    hp->_magic = RSHEADER_MAGIC;
    hp->_ptype = RSHEADER_PTYPE_RESP;
    hp->_hwords = sizeof(RsHeader)>>2;
    hp->_flags = 0;
    seq = connp->_nextSeq;
    hp->_seq = osp_htonl(seq);
    connp->_nextSeq = seq+1;
    hp->_ack = osp_htonl(connp->_flow._rLastReceived);
    hp->_window = osp_htons(RSHEADER_WINDOW);
    hp->_clientId = osp_htonl(_clientId);
    hp->_u._resp._code = osp_htonl(rcode);

    p->_seq = seq;
    p->_headerp = hp;
    p->_mbufp = mbufp;

    connp->_flow._xmitQ.append(p);

    connp->sendPackets();

    RsService *servicep = _servicep;
    Rs *rsp = _rsp;

    /* we're done with this server call object, so put it back on a free
     * list.
     */
    connp->freeSCall(this);

    /* we also release the extra connection reference we held start when the 
     * call arrived.
     */
     m.release();
    connp->release();

    Monitor<MutexLock> m2(rsp->_mutex);
    if ((--servicep->_refCount == 0) && (servicep->_unregisterCallback)) {
	BoostTask::autoQueue(boost::bind(servicep->_unregisterCallback,
					 servicep->_unregisterContextp));
    }
}

/* called with conn lock held to update the flow control information
 * associated with a connection.
 *
 * Frees acknowledged packets, but you may still need to call
 * sendPackets to resend packets that were queued for newly opened
 * window.
 */
void
RsConn::processAckInfo( RsHeader *hp, int willRespond)
{
    uint32_t ackPos;
    uint32_t seq;
    RsPacket *p;
    Rs *rsp = _rsp;

    /* our acknowledged position is the max of what we've seen so far,
     * and the new info.
     */
    ackPos = osp_ntohl(hp->_ack);
    if (RsCmp( _flow._xAcknowledged, ackPos) < 0) 
	_flow._xAcknowledged = ackPos;

    /* compute what we can send to based on best packet seen so far,
     * and most recent window update.
     */
    _flow._xFirstNoSend = _flow._xAcknowledged + osp_ntohs( hp->_window);

    /* figure out the last we've seen coming into our system, so we can
     * generate acks ourselves.
     */
    seq = osp_ntohl( hp->_seq);

    /* if this is a packet that consumes a spot in the stream, we should advance
     * the "largest received" pointer.
     */
    if (hp->_ptype == RSHEADER_PTYPE_CALL ||
        hp->_ptype == RSHEADER_PTYPE_RESP) {
        if (RsCmp(_flow._rLastReceived, seq) == 0)
            _flow._rLastReceived = seq+1;   /* received everything < seq+1 */
    }

    if (!_socketp) {
	getConnectedSocket();
	return;
    }
    
    /* It's possible that there are packets that haven't been sent yet because
     * the window was closed when they were originally queued. So let's check
     * for those....
     */
    while((p = _flow._xmitQ.head())) {
	if (p->_seq >= _flow._xFirstNoSend) break;
	/* otherwise, we're allowed to send this one; note that we
	 * delayed filling in the "_ack" field until now to ensure that it has
	 * the most up-to-date information available.
	 */

	p->_headerp->_ack = osp_htonl(_flow._rLastReceived);
    
	trackRemoteWindow();

	/* send takes ownership of the packet */
	OspMbuf *mbufp = p->_mbufp;
	p->_mbufp = NULL;
	_flow._xmitQ.pop();
	rsp->freePacket(p);

	if (_socketp->send( mbufp)) {
	    socketError();
	    return;
	}
    }

    /* It's possible that we haven't sent a window update to the remote guy
     * for quite a while, for example, when the remote end is sending a long
     * series of response calls, and we're receiving them but don't have to generate
     * a new packet in response.  So, if we get near the end of the remote guy's
     * window, send an ack.
     */
    if ( !willRespond &&
         RsCmp(_flow._rLastReceived + RSHEADER_WINDOW/8, _flow._rFirstNoSend) > 0) {
        /* we should send an ack to update rFirstNoSend, i.e. tell remote guy
         * he has more window to send.
         */
        rsp->sendAck(_socketp, this);
    }
}

/* called with global lock held to create a new connection */
RsConn *
Rs::getConnection( UUID *connIdp, OspNetVLAN *vlanp, int incoming)
{
    RsConn *connp;
    uint32_t ix;

    osp_assert(_mutex.isLocked());
    
    ix = RsConn::hashUUID( connIdp);
    for(connp = _connHashp[ix]; connp; connp = connp->_hashNextp) {
	if (memcmp(&connp->_connId, connIdp, sizeof(UUID)) == 0) {
            #if RS_CONN_TRACING
            *connp->trace() += " getConnectionFind";
            #endif
	    connp->_refCount++;
	    return connp;
	}
    }

    /* create a new connection, looking idle, but hashed in at least */
    connp = new RsConn();
    connp->_refCount = 1;
    #if RS_CONN_TRACING
    *connp->trace() += " getConnection";
    #endif
    connp->_hardTimeout = 0;
    connp->_hardTimerp = NULL;
    connp->_vlanp = vlanp;
    connp->_rsp = this;
    /* inline hashIn w/o recomputing hash value */
    connp->_hashNextp = _connHashp[ix];
    _connHashp[ix] = connp;
    connp->_flow.init();
    memcpy( &connp->_connId, connIdp, sizeof(*connIdp));
    connp->_nextSeq = 0;
    connp->_socketp = NULL;
    connp->_getSocketTimerp = NULL;
    connp->_freeSCallsp = NULL;
    connp->_freeCCallsp = NULL;
    connp->_ccallCount = 0;
    memset(connp->_ccallTablep, 0, sizeof(connp->_ccallTablep));
    connp->_bindTimerp = NULL;
    connp->_bound = 0;
    connp->_binding = 0;
    connp->_deleted = 0;
    connp->_aborted = false;
    connp->_incoming = incoming;
    connp->_serviceName = incoming ? "inbound" : "unknown";
    return connp;
}

/* called with locked connection to remove a client call object; finds
 * the call and removes it from the conn's active call table.  This gives
 * the caller exclusive access to the call object.  The caller must
 * still free the call object.
 */
RsClientCall *
RsConn::takeCCall( uint32_t clientId)
{
    uint16_t index = clientId & 0xffff;
    uint16_t seq = (clientId >> 16) & 0xffff;
    RsClientCall *callp;
    if ( index >= _maxCCalls) return NULL;
    callp = _ccallTablep[index];
    if (!callp) return NULL;
    if (callp->_sequence != seq) return NULL;
    _ccallTablep[index] = NULL;
    return callp;
}

/* called with no locks held, to process incoming packet */
void
Rs::processPacket( RsSocket *sp, OspNetMBuf *mbufp)
{
    RsHeader *hp;
    RsHeader hpSpace;
    OspNetMBuf *newBufp;
    RsHeader *newHp;
    OspNetVLAN *vlanp = sp->_vlanp;
    uint32_t seq;
    RsConn *connp;
    RsService *servicep;
    RsServerCall *scallp = NULL;
    RsClientCall *ccallp;
    int droppedLock;
    int32_t rcode;
    UUID peerNode(UUID::uuidNone);

    hp = (RsHeader *) mbufp->pullUp(sizeof(hpSpace), (char *) &hpSpace);

    if (hp->_magic != RSHEADER_MAGIC) {
	sp->abort();
	if (mbufp) mbufp->mfreem();
	return;
    }

    /** Mark socket as recently used. */
    if (sp->_idleCount > 0) {
	_mutex.take();
	sp->_idleCount = 0;
	_mutex.release();
    }

    switch (hp->_ptype) {
	case RSHEADER_PTYPE_BIND:
	    // Verify that the bind request is received on the correct node.
	    if (mbufp->lenm() >= (hp->_hwords<<2) + 2 * sizeof(UUID)) {
		// The src / dest UUID pair appears to be present.
		// Expected format:
		// <UUID of node initiating bind> <UUID of this node>
		uint8_t nodeIdSpace[sizeof(UUID)];
		uint8_t *uuidp =
		    (uint8_t *)mbufp->pullUp(sizeof(nodeIdSpace),
					     hp->_hwords<<2,
					     (char *) &nodeIdSpace,
					     NULL, NULL);
		peerNode.convertFrom(sizeof(UUID), uuidp);
		uuidp = (uint8_t *)mbufp->pullUp(sizeof(nodeIdSpace),
						 (hp->_hwords<<2)+sizeof(UUID),
						 (char *) &nodeIdSpace,
						 NULL, NULL);
		UUID nodeId(uuidp);
		if (!Node::LocalNode || (Node::LocalNode->Id() != nodeId)) {
		    DeDupLogger::lerr << "Rs bind node id mismatch" << DeDupLogger::lendl;
		    if (mbufp) mbufp->mfreem();
		    return;
		}
	    }

	    /* find connection, and lock it */
	    _mutex.take();
	    connp = getConnection(&hp->_connId, vlanp, /* inward */ 1);
	    _mutex.release();
	    connp->_mutex.take();
	    connp->_idleCount = 0;

	    /* server-side connection is bound on receipt of bind */
	    connp->_bound = 1;
	    if (connp->_peerNode == UUID::uuidNone) {
		connp->_peerNode = peerNode;
	    }

	    if ((connp->_socketp != sp) && (!sp->_deleted)) {
		/* rebinding on a new socket, remember last socket
		 * we received data for this connection on.
		 */
		if (connp->_socketp)
		    connp->_socketp->release();
		sp->hold();
		connp->_socketp = sp;

		/* and copy addr */
		memcpy( &connp->_peerAddr,
			&sp->_sockAddr,
			sizeof(connp->_peerAddr));
	    }

	    connp->processAckInfo(hp, 1);

	    /* if the connection looks unused (no calls or responses
	     * sent), then send a bind response.  Otherwise, this is 
	     * probably an old bind request that we can ignore.
	     */
	    if (connp->_flow._rLastReceived == 0 &&
		connp->_flow._xAcknowledged == 0) {
		/* and send a bind response packet */
		newBufp = vlanp->get(0);
                if (newBufp) {
                    newHp = (RsHeader *) newBufp->append(sizeof(*hp),MB_RS_TAG);
                    memcpy(newHp, hp, sizeof(RsHeader));
                    newHp->_ptype = RSHEADER_PTYPE_BIND_RESP;
                    newHp->_window = osp_htons(RSHEADER_WINDOW);
                    newHp->_hwords = sizeof(RsHeader)>>2;

                    connp->trackRemoteWindow();

		    /* takes ownership of mbuf chain */
                    if (sp->send(newBufp)) {
                        connp->socketError();
                    }
                }
	    }

	    /* release our lock and reference */
	    connp->_mutex.release();
	    connp->release();

	    break;
	    
	case RSHEADER_PTYPE_BIND_RESP:
	    _mutex.take();
	    connp = findConnection( &hp->_connId);
	    _mutex.release();
	    if (connp) {

		/* connection should be in binding state, or else we
		 * should just ignore this packet.  Turn off the binding
		 * flag and restart any queued packet transmissions.
		 */
		connp->_mutex.take();

		connp->_idleCount = 0;
            
		connp->processAckInfo(hp, 1);

		if (connp->_binding) {
		    connp->_binding = 0;
		    connp->_bound = 1;
		    connp->sendPackets();
		}
		bool mutexHeld = true;
		if (connp->_bindTimerp) {
		    if (connp->_bindTimerp->cancel() == 0) {
			// successfully cancelled timer
			connp->_bindTimerp = NULL;
			connp->_mutex.release();
			mutexHeld = false;

			// release the reference held by the timer.
			connp->release();
		    }
		}
		if (mutexHeld) connp->_mutex.release();
		connp->release();
	    }
	    break;

	case RSHEADER_PTYPE_CALL:
	    _mutex.take();
	    connp = findConnection( &hp->_connId);
	    _mutex.release();
	    if (connp) {
		droppedLock = 0;
		connp->_mutex.take();

		connp->_idleCount = 0;
            
                /* we pass 0 for 'will respond' below, since we may not respond *soon* */
		connp->processAckInfo(hp, 0);      /* advances rLastReceived */

                /* watch for next expected; note that rLastReceived was already updated
                 * by processAckInfo above.
                 */
		seq = osp_ntohl(hp->_seq);
		if (seq+1 == connp->_flow._rLastReceived) {
		    if ( (servicep =
			  findService( osp_ntohs(hp->_u._call._serviceId)))
			 != NULL) {

			if (!servicep->_serviceName.empty()) {
			    connp->_serviceName = servicep->_serviceName;
			}

			scallp = connp->getSCall();
			scallp->_rsp = this;
			scallp->_refCount = 1;
			scallp->_clientId = osp_ntohl(hp->_clientId);
			scallp->_connp = connp; /* holds a reference */
			scallp->_servicep = servicep;

			connp->_mutex.release();
			droppedLock = 1;
			/* pop off the RsHeader */
			mbufp = mbufp->pop(hp->_hwords<<2);
			/* passes ownership of mbufp and scallp to
			 * called procedure.
			 */
			uint64_t startTime = 0;
			if (_allowTracing) {
			    startTime = osp_get_monotonic_ms();
			    RsTrace << "Issuing call for service "
				    << connp->_serviceName << lendl;
			}
                        servicep->_serviceProcp( servicep->_serviceContextp,
                                                 scallp,
                                                 mbufp);
			if (_allowTracing) {
			    uint64_t timeDiff = osp_get_monotonic_ms() - startTime;
			    RsTrace << "Done issuing call for "
				    << connp->_serviceName << lendl;
			    if (timeDiff > 100) {
				RsTrace << "Slow call for "
					<< connp->_serviceName << ": "
					<< timeDiff << " ms" << lendl;
			    }
			}
			mbufp = NULL;
		    }
		    else {
			DeDupLogger::lout.log("No service found.");
		    }
		    /*****TBD: send bad serviceId response *****/
		}
		if (!droppedLock) connp->_mutex.release();
		if (!scallp) {
		    /* release connection from findConnection (but only if
		     * there was no scallp, in which case the reference was
		     * passed to the scallp struct).
		     */
		    connp->release();
		}
	    }
	    break;

	case RSHEADER_PTYPE_RESP:
	    _mutex.take();
	    connp = findConnection( &hp->_connId);
	    _mutex.release();
	    if (connp) {
		connp->_mutex.take();
		connp->_idleCount = 0;
		connp->processAckInfo(hp, 0);
		ccallp = connp->takeCCall(osp_ntohl(hp->_clientId));
		if (ccallp) {
		    rcode = osp_ntohl(hp->_u._resp._code);
		    mbufp = mbufp->pop(hp->_hwords<<2);
		    connp->_mutex.release();
		    uint64_t startTime = 0;
		    if (_allowTracing) {
			startTime = osp_get_monotonic_ms();
			RsTrace << "Issuing response for service "
				<< connp->_serviceName << lendl;
		    }
		    ccallp->_responseProcp( ccallp->_dispatchProcp,
					    ccallp->_responseContextp,
					    mbufp,
					    rcode);
		    if (_allowTracing) {
			uint64_t timeDiff = osp_get_monotonic_ms() - startTime;
			RsTrace << "Finished response for "
				<< connp->_serviceName << lendl;
			if (timeDiff > 100) {
			    RsTrace << "Slow response for "
				    << connp->_serviceName << ": "
				    << timeDiff << " ms" << lendl;
			}
		    }

		    /* response procedure also takes ownership of the
		     * packet.
		     */
		    mbufp = NULL;

		    connp->_mutex.take();
		    connp->freeCCall( ccallp);
		}
		connp->_mutex.release();
		connp->release();	/* from find */
	    }
	    else {
		sendAbort(sp, hp);
	    }
	    break;

        case RSHEADER_PTYPE_ACK:
            _mutex.take();
            connp = findConnection( &hp->_connId);
            _mutex.release();
	    if (connp) {
		connp->_mutex.take();
		connp->_idleCount = 0;
		connp->processAckInfo(hp, 0);
                connp->_mutex.release();
                connp->release();       /* from find */
            }
            break;

	case RSHEADER_PTYPE_ABORT:
	    _mutex.take();
	    connp = findConnection( &hp->_connId);
	    _mutex.release();
	    if (connp) {
		connp->handleAbort();
                connp->release();
	    }
	    break;

	default:
	    DeDupLogger::lout << "received unknown packet type=" <<  hp->_ptype << DeDupLogger::lendl;
	    break;
    }
	    
    if (mbufp) {
	if ( hp->_ptype == RSHEADER_PTYPE_CALL ||
	     hp->_ptype == RSHEADER_PTYPE_RESP)
	    DeDupLogger::lout.log("retried call/resp");
	mbufp->mfreem();
    }
}

void
RsConn::handleAbort()
{
    Monitor<MutexLock> m(_mutex);
    handleAbortNL();
}

void
RsConn::handleAbortNL()
{
    osp_assert(_mutex.isLocked());
    UUID newUUID;

#if RS_CONN_TRACING
    *trace() += " handleAbortNL()";
#endif

    _aborted = true;
    _deleted = 1;
    _bound = 0;

    /* change the connection ID to ensure that no old call responses
     * look like responses to any new calls.
     */
    _rsp->_mutex.take();
    hashOut();
    _connId = newUUID;
    hashIn();
    _rsp->_mutex.release();

    freeQueuedPackets();

    abortWaitingCalls(-1);
}

/* called with locked connection, when we receive a packet for a
 * connection we've never heard of (probably because we restarted
 * since the other side created this connection).  Sends an abort to
 * the other side, aborts any pending calls, and frees any queued
 * packets.
 */
void
Rs::sendAbort(RsSocket *sp, RsHeader *hp)
{
    OspNetMBuf *mbufp;
    RsHeader *newHp;

    mbufp = _vlanp->get(16);
    if (mbufp) {
        newHp = (RsHeader *) mbufp->append(sizeof(RsHeader),MB_RS_TAG);

        memcpy(&newHp->_connId, &hp->_connId, sizeof(UUID));
        newHp->_magic = RSHEADER_MAGIC;
        newHp->_ptype = RSHEADER_PTYPE_ABORT;
        newHp->_hwords = sizeof(RsHeader)/4;
        newHp->_flags = 0;

        newHp->_seq = 0;
        newHp->_ack = 0;
        newHp->_window = 0;

        newHp->_u._abort._code = RS_ERR_NOCONN;

	/* takes ownership of mbuf */
        sp->send(mbufp);
    }
}

void
Rs::sendAck(RsSocket *sp, RsConn *connp)
{
    OspNetMBuf *mbufp;
    RsHeader *newHp;

    mbufp = _vlanp->get(16);
    if (mbufp) {
        newHp = (RsHeader *) mbufp->append(sizeof(RsHeader),MB_RS_TAG);

        memcpy(&newHp->_connId, &connp->_connId, sizeof(UUID));
        newHp->_magic = RSHEADER_MAGIC;
        newHp->_ptype = RSHEADER_PTYPE_ACK;
        newHp->_hwords = sizeof(RsHeader)/4;
        newHp->_flags = 0;

        newHp->_seq = osp_htonl(connp->_nextSeq);
        newHp->_ack = osp_htonl(connp->_flow._rLastReceived);
        newHp->_window = osp_htons(RSHEADER_WINDOW);

        /* keep track of the remote guy's idea of where he has to stop sending */
        connp->trackRemoteWindow();

	/* send takes ownership of mbuf */
        if (sp->send(mbufp)) connp->socketError();
    }
}

void
RsConn::freeQueuedPackets()
{
    RsPacket *p;
    RsPacket *np;

    for(p = _flow._xmitQ.head(); p; p = np) {
	np = p->_dqNextp;
	_rsp->freePacket(p);
    }
    _flow._xmitQ.init();
}

/* Free all pending calls.  Note that since we upcall to each response
 * proc while we're processing all waiting calls, the upcalled guy
 * can queue a new call that we don't want to abort.  So, we first mark
 * all calls that we want to abort, and abort only those.
 */
void
RsConn::abortWaitingCalls(int32_t code)
{
    uint32_t i;
    RsClientCall *ccallp;

    /* mark all calls that we're going to abort */
    for(i=0;i<_maxCCalls;i++) {
	ccallp = _ccallTablep[i];
	if (!ccallp) continue;
	ccallp->_doAbort = 1;
    }

    /* abort marked calls */
    for(i=0;i<_maxCCalls;i++) {
	ccallp = _ccallTablep[i];
	if (!ccallp || !ccallp->_doAbort)
	    continue;
	_ccallTablep[i] = NULL;

	/* we have a call to abort: Don't do it from the calling thread 
           because the caller might be holding a lock that the response 
           handler needs as well, leading to a dead-lock */
        /* NOTE: This object is auto-deleted */
        BoostTask* pBoostTask = new BoostTask(boost::bind(&RsConn::abortWaitingCallsThread, this, ccallp, code));
	DispatcherPolicy *pPolicy = pBoostTask->getDispatcherPolicy();
	if (pPolicy && pPolicy->getPinned()) {
	    pBoostTask->setDispatcherPolicy(NULL);
	}
	pBoostTask->queue();
        pBoostTask = NULL;
    }
}

void
RsConn::abortWaitingCallsThread(RsClientCall *ccallp, int32_t code)
{
    ccallp->_responseProcp( ccallp->_dispatchProcp,
                            ccallp->_responseContextp,
                            NULL,
                            code);  
                            
    hold();
    _mutex.take();
    freeCCall(ccallp);
    _mutex.release();
    release();
}

/* Entry from network driver with a block of TCP data.  Called with an
 * incoming packet on a particular socket; no Rs locks are held at
 * this time.  We assume that data from a given TCP socket is upcalled
 * serially (otherwise, how would we know which block of data comes first
 * in the TCP stream?)
 */
/* static */ void
Rs::incomingProc( void *contextp, OspNetMBuf *mbufp)
{
    int32_t code;
    uint32_t nbytes;
    OspNetMBuf *newBufp;
    OspNetMBuf *remainderBufp;
    RsSocket *sp = (RsSocket *) contextp;

    /* test whether or not this is still a valid socket */
    if (holdSocket(sp)) {
	// not valid
	if (mbufp) mbufp->mfreem();
	return;
    }

    /* sp is valid, and we have a reference on it. */

    if (!mbufp) {
	/* we hit EOF; note that this path grabs connection locks that
         * shouldn't really be held here (if we hold one over a packet
         * send that could block due to a full TCP connection).
         * Probably not an issue since the connection is being closed.
         */
	sp->abort();
	sp->release();
	return;
    }

    osp_assert(mbufp->lenm() >= 0);

    newBufp = sp->_pktBufp;
    if (newBufp)
	newBufp->concat(mbufp);
    else
	newBufp = mbufp;

    nbytes = newBufp->lenm();

    /* start off by pulling the remainder of the count from the packet,
     * if there's any more bytes required for the frame, and we have
     * bytes in the packet.
     */
    code = sp->absorbHeader(&nbytes, &newBufp);
    if (code) {
	/* failed */
	newBufp->mfreem();
	newBufp = NULL;
	DeDupLogger::lout.log("****absorb failed");
	sp->abort();
	sp->release();
	return;
    }
    sp->_pktBufp = newBufp;
    int numPkts = 0;

    /* now see if we have a complete packet we can upcall */
    while (sp->_pktHdrValid == 4 && nbytes >= sp->_pktCount) {
	numPkts++;
	/* we have a complete packet, so split the packet into two
	 * pieces, and process the first piece.
	 */
	remainderBufp = newBufp->split(sp->_pktCount,MB_RS_TAG);
	sp->_pktHdrValid = 0;

	if (remainderBufp) {
	    /* now, we may have some # of count bytes at the head of 
	     * the remainder packet, and we have to process up to 4
	     * bytes from the head, as well.
	     */
	    nbytes = remainderBufp->lenm();
	    code = sp->absorbHeader( &nbytes, &remainderBufp);
	    if (code) {
		remainderBufp->mfreem();
		remainderBufp = NULL;
		DeDupLogger::lout.log("****absorb failed(2)");
		sp->abort();
		sp->release();
		return;
	    }
	}
	else {
	    /* no buffer */
	    nbytes = 0;
	}
	sp->_pktBufp = remainderBufp;

	/* deliver the newBufp packet to the next stage */
	sp->_rsp->_incomingQueue.queue(sp, newBufp);

	/* and reload newBufp for code above */
	newBufp = remainderBufp;
    }

    sp->release();
}

/* called when a new socket is opened to our system; called with 
 * no locks held, by networking layer.
 */
/* static */ void
Rs::listenProc( void *contextp, OspNetSocketTCP *socketp)
{
    Rs *rsp = (Rs *)contextp;
    RsSocket *rsocketp;

    rsocketp = new RsSocket();
    rsocketp->_refCount = 0;
    rsocketp->_deleted = 0;
    rsocketp->_sockp = socketp;
    rsocketp->_rsp = rsp;
    rsocketp->_vlanp = rsp->_vlanp;
    rsocketp->_connecting = 0;
    rsocketp->_connected = 1;
    rsocketp->_sockAddr = *socketp->getForeignAddr();
    rsocketp->_pktHdrValid = 0;
    rsocketp->_pktCount = 0;
    rsocketp->_pktBufp = NULL;
    rsocketp->_numConnectionErrors = 0;
    addToActive(rsocketp);

    rsp->_mutex.take();

    /* all sockets are directly connected when they are created */
    socketp->setIncomingProc(incomingProc, rsocketp);
    rsp->_mutex.release();
}

/* EXTERNAL: called to start a call with data in datap; asynchronously
 * calls back via responseProcp.  No locks are held on entry.
 */
int32_t
RsConn::call2( OspNetMBuf *datap,
	       uint32_t serviceId,
	       uint32_t opcode,
	       RsResponseProc *responseProcp,
	       void *responseContextp,
	       void (*dispatchProcp)())
{
    RsHeader *headerp;
    RsClientCall *callp;
    OspNetMBuf *headBufp;
    RsPacket *p;
    Rs *rsp = _rsp;

    if (!datap) {
	// return generic error if no message to send
	return -1;
    }

    headerp = (RsHeader *) datap->prepend( &headBufp, sizeof(*headerp),MB_RS_TAG);
    if (!headerp) {
	// no mbufs for header, fail the call
	datap->mfreem();
	return -1;
    }

    _mutex.take();
    _idleCount = 0;
    callp = getCCall();
    _mutex.release();
    if (!callp) {
        if(headBufp){
            headBufp->mfreem();
        }
	return -1;
    }
    
    p = rsp->getPacket();

    /* getCCall bumped ref to conn for callp */

    callp->_responseProcp = responseProcp;
    callp->_responseContextp = responseContextp;
    callp->_ttl = _hardTimeout * 1000;  /* in MS */
    osp_assert(callp->_clientId < _maxCCalls);
    osp_assert(callp->_connp == this);
    callp->_refCount = 1;
    callp->_dispatchProcp = dispatchProcp;

    /* store is atomic, so no lock needed here */
    _ccallTablep[callp->_clientId] = callp;

    /* before sending the request, make sure we can make sense of the
     * response by inserting the call into the ccallTablep array.
     * Note that the clientId field is already initialized to the
     * array slot that we're using for this entry (there are no
     * duplicates / conflicts).
     */
    osp_assert(callp->_clientId < _maxCCalls);

    memcpy(&headerp->_connId, &_connId, sizeof(UUID));
    headerp->_magic = RSHEADER_MAGIC;
    headerp->_ptype = RSHEADER_PTYPE_CALL;
    headerp->_hwords = sizeof(*headerp) >> 2;
    headerp->_flags = 0;
    headerp->_window = osp_htons(RSHEADER_WINDOW);
    headerp->_u._call._serviceId = osp_htons(serviceId);
    headerp->_u._call._opcode = osp_htons(opcode);
    headerp->_clientId = osp_htonl(callp->_clientId |
				   ((uint32_t)callp->_sequence << 16));
    /* headerp->_ack will be filled in at transmit time */

    _mutex.take();

    /* do this under the lock so we don't generate duplicate sequence IDs, and
     * keep things in order in the queue.
     */
    headerp->_seq = osp_htonl(_nextSeq);

    p->_headerp = headerp;
    p->_seq = _nextSeq;
    p->_mbufp = headBufp;

    _nextSeq++;

    _flow._xmitQ.append( p);

    sendPackets();

    // Check if an abort occurred around the time that the call was added.
    // If so, the new call may not have been replied to, in which case we
    // should try to abort again.
    if (_aborted) {
	handleAbortNL();
    }

    _mutex.release();

    return 0;
}

int32_t
RsConn::abortConn()
{
    /* ****TBD: more here */
    handleAbortNL();
    return -1;
}

/* called from a timer when we don't get a bind response within a reasonable
 * timer period.  Resend the bind request.
 * Called with a reference held.
 */
void
RsConn::retryBindTimer(OspTimerEvent *evp)
{
    _mutex.take();
    if (evp->cancelled() || _deleted) {
	if (evp == _bindTimerp) {
	    _bindTimerp = NULL;
	}
	_mutex.release();
	release();
	return;
    }

    _binding = 0;
    _bindTimerp = NULL;

    startBind();

    _mutex.release();

    release();
}

/* called with the connection locked to start bind process */
int32_t
RsConn::startBind()
{
    OspNetMBuf *mbufp;
    RsHeader *hp;
    RsSocket *sp;

    if (_binding) return 0;

    int timeout = 3000;

    /* send a bind packet out */
    mbufp = _vlanp->get(4);
    if (mbufp) {
        hp = (RsHeader *) mbufp->append(sizeof(RsHeader),MB_RS_TAG);
        setupControl(hp, RSHEADER_PTYPE_BIND);

	if (Node::LocalNode && (_peerNode != UUID::uuidNone)) {
	    UUIDWire *uuidp = (UUIDWire *) mbufp->append(sizeof(UUIDWire),MB_RS_TAG);
	    if (uuidp) {
		UUID::CopyUUIDToUUIDWire(*uuidp, Node::LocalNode->Id());
	    }
	    uuidp = (UUIDWire *) mbufp->append(sizeof(UUIDWire),MB_RS_TAG);
	    if (uuidp) {
		UUID::CopyUUIDToUUIDWire(*uuidp, _peerNode);
	    }
	}

        /* find a usable socket for this connection */
        sp = findSocket();

        trackRemoteWindow();


        /* we lose ownership of the packet */
        if (sp->send(mbufp)) {
            // force the retry bind timer to start immediately
            socketError();
            timeout = 0;
        }
    }
    else {
        timeout = 0;
    }

    if (_aborted) return 1;

    _binding = 1;

    /* start a timer for bind retries (timer holds a reference) */
    hold();
    _bindTimerp = new OspTimerEvent();
    _bindTimerp->init(timeout, this, (OspTimerEvent::TimerMethod) &RsConn::retryBindTimer);

    return 0;
}

/* called with locked conn, to get a valid socket, or move in 
 * that direction.  Need guarantee that iff 0 is returned we have
 * a socket with the connected flag set.
 */
int32_t 
RsConn::getConnectedSocket()
{
    RsSocket *socketp;
    int32_t code = 1;

    if (_socketp && _socketp->_deleted) {
	// The socket is currently being deleted, this RsConn has not yet
	// been updated, just clear _socketp (the thread deleting the socket
	// would eventually do the same).
	_socketp->release();
	_socketp = NULL;
    }

    socketp = _socketp;

    if (_incoming) {
	if (socketp && socketp->_connected)
	    return 0;
	else
	    return 1;
    }

    if (!socketp) {
	_socketp = socketp = _rsp->getSocket( _vlanp, &_peerAddr, _bindAddr);
	if (_aggressiveSocketDelete) _socketp->_autoDelete = 1;
	if (!socketp->_connected && !_socketp->_connecting) {
	    if (socketError()) return 1;
	    startGetSocketTimer();
	    return 1;
	}
    }

    Monitor<MutexLock> m(_rsp->_mutex);

    if (socketp->_connected) {
	/* synchronous success */
	return 0;
    }

    if (socketp->_connecting) return 1;

    // !connected && !connecting
    code = socketp->_sockp->connectx( &socketp->_sockAddr,
				      Rs::socketConnected,
				      socketp,
				      _bindAddr);

    if (code == 0) {
	socketp->_connecting = 1;
	return 1;
    }
    else {
	m.release();
	if (socketError()) return code;
	startGetSocketTimer();
	return code;
    }
}

void
RsConn::startGetSocketTimer()
{
    if (!_getSocketTimerp) {
	/* timer will hold a reference */
	hold();
	_getSocketTimerp = new OspTimerEvent();
	_getSocketTimerp->init( 3000,
				this,
				((OspTimerEvent::TimerMethod)
				 &RsConn::retryGetSocketTimer));
    }
}

/* Called with a reference held. */
void
RsConn::retryGetSocketTimer(OspTimerEvent *evp)
{
    int32_t code;

    Monitor<MutexLock> m(_mutex);

    if (_deleted || evp->cancelled()) {
	if (evp == _getSocketTimerp) {
	    _getSocketTimerp = NULL;
	}
	m.release();
	release();
	return;
    }

    /* no timer running any more */


    /* returns 0 if connected, <0 if something went wrong, and >0 if
     * it should get better on its own (asynchronously connecting).
     */
    code = getConnectedSocket();
    if (code == 0) {
	/* send packets on newly connected socket */
	sendPackets();
    }

    m.release();
    release();
}

/* internal -- send packets from the head of the xmit queue until the
 * sequence # is past the window.
 *
 * Called with connection lock held.
 */
int32_t
RsConn::sendPackets() {
    RsPacket *p;
    int32_t code;

    if (!_socketp || !_socketp->_connected) {
	/* if we don't have a socket, create one if we're an outbound
	 * connection.  For incoming connections, we wait for the client
	 * to open a connection back to us (and we'll eventually just clean
	 * things up if we don't hear from the other side in "a while").
	 */
	code = getConnectedSocket();
	if (code) return code;
    }

    if (!_bound) {
	if (!_binding)
	    return startBind();
	else
	    return 0;
    }

    /* send all packets we have transmit window for */
    while((p = _flow._xmitQ.head())) {
	if (p->_seq >= _flow._xFirstNoSend) break;
	/* otherwise, we're allowed to send this one; note that we
	 * delayed filling in the "_ack" field until now to ensure that it has
	 * the most up-to-date information available.
	 */
	p->_headerp->_ack = osp_htonl(_flow._rLastReceived);
	
	/* track the window we've advertised to the remote guy */
	trackRemoteWindow();

	/* send takes ownership of the packet */
	OspMbuf *mbufp = p->_mbufp;
	p->_mbufp = NULL;
	_flow._xmitQ.pop();
	_rsp->freePacket(p);
	if (_socketp->send( mbufp)) {
	    socketError();
	    return 1;
	}
    }
    return 0;
}

Rs* Rs::_instanceServer = NULL;
Rs* Rs::_instanceClient = NULL;

Rs* 
Rs::createServer(uint16_t port, bool noSharePreferredAllocator, bool useWatchDog)
{
    Rs* pRs = new Rs(noSharePreferredAllocator, useWatchDog);
    
    int code = 0;
    pRs->_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (pRs->_sockfd >= 0) {
        code = pRs->initWithSocket(OspNetVLAN::create(true), port, pRs->_sockfd);
        if (code == 0) {
            return pRs;
        } else {
            close(pRs->_sockfd);
            pRs->_sockfd = -1;
        }
    }
    
    delete pRs;
    return NULL;
}

void Rs::resetWatchdog(uint32_t watchdogTime)
{
    _watchDogp.reset(new WatchDog(watchdogTime, "Rs watchdog timeout."));
}

/* static */ void *
Rs::incomingWorker(void *contextp)
{
    Rs *rsp = (Rs *) contextp;
    OspMbuf *mbufp;
    OspMbuf *nextMbufp;
    RsSocket *socketp;
    pthread_set_name_np(pthread_self(), "rs:worker");
    // assign this thread to an allocator
    if(rsp->_noSharePreferredAllocator)
    {
        osp_extra_new_setsp->adjustAutoAssignOffCnt(false);
        osp_extra_new_setsp->assignThreadToSet(true);
    }

    uint32_t watchdogTime = 120000; /* 120 seconds */
    if (!rsp->_useWatchDog) {
	watchdogTime = ~0;
    } else if(Platform::GetPlatform()->HasAttr(PlatformDef::ATTR_SIMULATOR)) {
	watchdogTime = 180000; /* 180 seconds */
    }

    while(1) {
        /* get an entire chain of pending work; pthread sleeps if
         * there are no packets waiting in any sockets.  Note that it
         * returns a single socket's worth of packets at a time.
         */
        rsp->_incomingQueue.getAll(&socketp, &mbufp);

        /* dispatch each item; they're freed by sendResponse */
        rsp->_watchDogp.reset(new WatchDog(watchdogTime, "Rs watchdog timeout."));
	if (_allowTracing) {
	    RsTrace << "Starting processing of packets for Rs "
		    << rsp << lendl;
	}
        while(mbufp) {
            nextMbufp = mbufp->getNextPkt();
            rsp->processPacket(socketp, mbufp);
            mbufp = nextMbufp;
	    if (mbufp) rsp->_watchDogp->restart();
        }
	if (_allowTracing) {
	    RsTrace << "Finished processing of packets for Rs "
		    << rsp << lendl;
	}

	rsp->_watchDogp->stop();

        /* hold was done when socket went into the _incoming queue,
         * and it has been removed by getAll.  We want to hold the
         * socket until we're back from all of the processPacket calls
         * above.
         */
        socketp->release();
    }
    return NULL;
}

int32_t
Rs::initWithSocket(OspNetVLAN *vlanp, uint16_t port, int sockfd)
{
    int32_t code;

    _port = port;
    _vlanp = vlanp;
    code = vlanp->tcpListen(port, sockfd, listenProc, this);
    if (code) return code;

    if (port) return 0;

    // port wasn't specified, we need to discover
    // the actual port number assigned

    struct sockaddr addr;
    socklen_t socklen = sizeof(addr);
    if (getsockname(sockfd, &addr, &socklen) < 0) {
	return 1;
    }

    if (addr.sa_family != AF_INET) {
	// can't handle this yet
	return 1;
    }

    sockaddr_in *sinp = ConnectionClient::sockaddrToSin(&addr);
    _port = sinp->sin_port;

    return 0;
}

RsSocket::RsSocket():
    _autoDelete(0),
    _dqNextp(NULL),
    _dqPrevp(NULL),
    _idleCount(0)
{
    // Most initialization is handled in Rs::getSocket or Rs::listenProc.
    memset(&_incoming, 0, sizeof(_incoming));
    memset(&_connectingQueue, 0, sizeof(_connectingQueue));
    memset(&_sockAddr, 0, sizeof(_sockAddr));
}

void
RsSocket::handleAutoDelete() {
    abort();
    release();
}

/* called with global lock held */
void
RsSocket::releaseNL() {
    Rs *rsp = _rsp;
    RsPacket *p;
    RsSocket *tnextp;
    RsSocket **lnextpp;

    osp_assert( rsp->_mutex.isLocked());

    osp_assert(_refCount > 0);
    if (--_refCount == 0 && (_deleted || _autoDelete)) {
	if (_autoDelete && !_deleted) {
	    // Need to abort this socket.
	    // Unfortunately we can't do this from here (because Rs lock held).
	    // So, we use BoostTask to handle the remove.
	    // Marking the inode as deleted with one ref in the mean time
	    // (deleted will prevent reuse of this socket, one ref so the
	    //  BoostTask callback can just call release() again.
	    _autoDelete = 0;
	    _deleted = 1;
	    ++_refCount;
	    BoostTask::autoQueueForce(boost::bind(&RsSocket::handleAutoDelete,
						  this));
	    return;
	}

	/* this is the only time that it is safe to damage an RsSocket, 
	 * since the owner of a connection may take any socket held
	 * by the connection, and send a packet on it (the send may
	 * fail, of course).
	 */
	if (_sockp != NULL) {
	    _sockp->closex();
	    _sockp = NULL;
	}
	while((p = _connectingQueue.pop()) != NULL) {
	    rsp->freePacket(p);
	}
	if (_pktBufp != NULL) {
	    _pktBufp->mfreem();
	    _pktBufp = NULL;
	}

        /* unthread from the list of all known sockets */
        for(lnextpp = &_rsp->_allSocketsp, tnextp = *lnextpp;
            tnextp;
            lnextpp = &tnextp->_allNextp, tnextp = *lnextpp) {
            if (tnextp == this) {
                *lnextpp = tnextp->_allNextp;
		osp_atomic_sub_32(&Rs::_globalNumSockets, 1);
            }
        }

	delete this;
    }
}

void
RsSocket::hold()
{
    Rs *rsp = _rsp;
    rsp->_mutex.take();
    _refCount++;
    rsp->_mutex.release();
}

void
RsSocket::holdNL()
{
    // coverity[missing_lock]
    _refCount++;
}

/* initializer for service */
RsService::RsService( Rs *rsp,
		      uint32_t serviceId,
		      RsServerProc *procp,
		      void *contextp) {
    _serviceId = serviceId;
    _serviceProcp = procp;
    _serviceContextp = contextp;
    _rsp = rsp;
    _doDispatch = 0;
    _refCount = 0;
    _unregisterCallback = 0;

    rsp->_mutex.take();
    _nextp = rsp->_allServicesp;
    rsp->_allServicesp = this;
    rsp->_mutex.release();
}

void
RsService::unregister(UnregisterCallback *callback, void *contextp)
{
    Monitor<MutexLock> m(_rsp->_mutex);
    for(RsService **servicepp = &_rsp->_allServicesp ;
	*servicepp ; servicepp = &(*servicepp)->_nextp) {
	if (*servicepp == this) {
	    *servicepp = _nextp;
	    _unregisterCallback = callback;
	    _unregisterContextp = contextp;
	    if (_refCount == 0) {
		BoostTask::autoQueue(boost::bind(callback, contextp));
	    }
	    return;
        }
    }

    osp_assert(0);  // Should have been found.
}

/* callback that occurs from OspNet layer when an outgoing socket
 * is actually connected and is now usable.
 */
/* static */ void
Rs::socketConnected(void *contextp, int32_t code)
{
    RsSocket *sockp = (RsSocket *) contextp;

    // Try to get a reference on this socket
    if (holdSocket(sockp)) return;

    Rs *rsp = sockp->_rsp;
    RsConn *connp;
    uint32_t i;


    rsp->_mutex.take();

    sockp->_connecting = 0;

    sockp->_connected = (code) ? 0 : 1;

    deque<RsConn *> connections;
    for(i=0;i<_hashSize;i++) {
	for(connp = rsp->_connHashp[i]; connp; connp = connp->_hashNextp) {
	    connp->holdNL();
	    connections.push_back(connp);
	}
    }
    rsp->_mutex.release();

    BOOST_FOREACH(connp, connections) {
	connp->_mutex.take(); 
	if (connp->_socketp == sockp) {
	    if (code) {
		connp->socketError();
	    } else {
		connp->sendPackets();
	    }
	}
	connp->_mutex.release();
	connp->release();
    }

    sockp->release();
}

/* create a new outgoing socket, and connect it; return it referenced;
 * if we already have an existing connection to the target, return it 
 * instead.
 */
RsSocket *
Rs::getSocket( OspNetVLAN *vlanp,
	       OspNetSockAddr *peerAddrp,
	       const std::string &bindAddr)
{
    RsSocket *sockp;
    OspNetSocketTCP *tcpSocketp;
    int32_t code;

    _mutex.take();
    for(sockp = _allSocketsp; sockp; sockp = sockp->_allNextp) {
	if ( !sockp->_deleted &&
	     sockp->_vlanp == vlanp &&
	     sockp->_bindAddr == bindAddr &&
	     memcmp( &sockp->_sockAddr,
		     peerAddrp,
		     sizeof(OspNetSockAddr)) == 0) {
	    if (bindAddr.empty()) {
		// Make sure the locally bound address is a specific address.
		struct sockaddr_in sin;
		socklen_t len = sizeof(sin);
		if ((getsockname(sockp->_sockp->getSock(),
				 (struct sockaddr *)&sin, &len) == 0)) {
		    uint32_t addr = ntohl(sin.sin_addr.s_addr);
		    if ((addr != INADDR_ANY) && (addr != INADDR_NONE)) {
			// It's not a special address, convert to string.
			string boundAddr(NetworkUtils::addrNtoA(addr));
			if (!boost::starts_with(boundAddr, "127.") &&
			    !Node::FindLocalIPV4Addresses().count(boundAddr)) {
			    // The IP that we were bound to locally went away.
			    // Don't reuse this socket.
			    continue;
			}
		    }
		}
	    }
	    sockp->_refCount++;
	    _mutex.release();
	    return sockp;
	}
    }
        
    /* otherwise we need a new socket */
    sockp = new RsSocket;
    sockp->_refCount = 1;
    sockp->_deleted = 0;
    sockp->_rsp = this;
    sockp->_bindAddr = bindAddr;
    tcpSocketp = new OspNetSocketTCP( vlanp, -1);
    sockp->_sockp = tcpSocketp;
    sockp->_sockAddr = *peerAddrp;
    sockp->_vlanp = vlanp;
    sockp->_pktHdrValid = 0;
    sockp->_pktCount = 0;
    sockp->_pktBufp = NULL;
    sockp->_connecting = 1;	/* will do so below */
    sockp->_connected = 0;
    sockp->_numConnectionErrors = 0;
    tcpSocketp->setIncomingProc(incomingProc, sockp);

    _mutex.release();
    addToActive(sockp);
    _mutex.take();

    /* add to list of all sockets */
    sockp->_allNextp = _allSocketsp;
    _allSocketsp = sockp;
    osp_atomic_add_32(&_globalNumSockets, 1);

    /* we get an async callback with code == 0, otherwise we never
     * get a callback at all, and have had a synchronous error.  We never
     * get synchronous success.
     */
    code = tcpSocketp->connectx( peerAddrp, socketConnected, sockp, bindAddr);
    if (code) {
	sockp->_connecting = 0;
	sockp->_connected = 0;
    }
    _mutex.release();

    return sockp;
}

/* initialize an RsConn as a client connection, communicating with the
 * specified peer on the specified vlan.
 */
RsConn *
Rs::getClientConn(OspNetSockAddr *peerAddrp,
		  const UUID &destNode,
		  const std::string &serviceName,
		  const std::string &bindAddr,
		  bool aggressiveSocketDelete)
{
    RsSocket *socketp;
    RsConn *connp;
    UUID connId;
    OspNetVLAN* vlanp = OspNetVLAN::getInstance();
    
    _mutex.take();
    connp = getConnection( &connId, vlanp, /* !inward */ 0);
    ++_recentConnections[serviceName];
    _mutex.release();

    socketp = getSocket(vlanp, peerAddrp, bindAddr);
    if (aggressiveSocketDelete) socketp->_autoDelete = 1;
    connp->_peerAddr = *peerAddrp;
    osp_assert(!connp->_socketp);
    connp->_socketp = socketp;
    connp->_serviceName = serviceName;
    connp->_peerNode = destNode;
    connp->_bindAddr = bindAddr;
    connp->_aggressiveSocketDelete = aggressiveSocketDelete;

    return connp;
}

void
RsConn::hashOut()
{
    RsConn *tconnp;
    RsConn **lconnpp;
    uint32_t ix = hashUUID(&_connId);

    for( lconnpp = &_rsp->_connHashp[ix], tconnp = *lconnpp;
	 tconnp;
	 lconnpp = &tconnp->_hashNextp, tconnp = *lconnpp) {
	if (tconnp == this) break;
    }
    osp_assert(tconnp != NULL);
    *lconnpp = _hashNextp;
}

void
RsConn::hashIn()
{
    uint32_t ix;
    Rs *rsp = _rsp;

    ix = RsConn::hashUUID(&_connId);
    _hashNextp = rsp->_connHashp[ix];
    rsp->_connHashp[ix] = this;
    
}

/* release a reference to a connection, while holding the global
 * lock.
 *
 * At present, user created (!_incoming) connection can be destroyed
 * by just releasing them, but you can/should call destroy instead,
 * which will guarantee that _deleted is set.
 */
void
RsConn::releaseNL() {
    RsClientCall *ccallp;
    RsClientCall *nccallp;
    RsServerCall *scallp;
    RsServerCall *nscallp;

    #if RS_CONN_TRACING
    *trace() += " releaseNL";
    #endif
    
    osp_assert(_refCount > 0);
    if (--_refCount == 0 && (!_incoming || _deleted)) {
	/* no references, so no one should either have a lock or be
	 * trying to get a lock, on this object.
	 */
	// osp_assert(!_mutex.isLocked()); this assert is never right -- someone else could just have it;
        
        /* remove ourselves from the hash table */
        hashOut();

	if (_socketp) {
	    _socketp->releaseNL();
	    _socketp = NULL;
	}

	for(ccallp = _freeCCallsp; ccallp; ccallp = nccallp) {
	    nccallp = ccallp->_dqNextp;
	    delete(ccallp);
	}
	_freeCCallsp = NULL;
	for(scallp = _freeSCallsp; scallp; scallp = nscallp) {
	    nscallp = scallp->_dqNextp;
	    delete(scallp);
	}
	_freeSCallsp = NULL;
        freeQueuedPackets();
	delete this;
    }
}

/* Called with a reference held. */
void
RsConn::hardTimerTick( OspTimerEvent *eventp)
{
    uint32_t i;
    RsClientCall *ccallp;

    _mutex.take();
    if (eventp->cancelled() || _deleted) {
	if (eventp == _hardTimerp) {
	    _hardTimerp = NULL;
	}
	_mutex.release();
	release();
        return;
    }

    /* timer has fired */
    _hardTimerp = NULL;

    for(i=0;i<_maxCCalls;i++) {
        ccallp = _ccallTablep[i];
        if (ccallp) {
            ccallp->_ttl -= _msPerTick;
            if (ccallp->_ttl < 0) {
                _ccallTablep[i] = NULL;
                _mutex.release();

                /* we have a call to abort */
                ccallp->_responseProcp( ccallp->_dispatchProcp,
                                        ccallp->_responseContextp,
                                        NULL,
                                        -1);
                _mutex.take();
		freeCCall(ccallp);

		if (_socketp) {
		    Monitor<MutexLock> m(_rsp->_mutex);
		    if (++_socketp->_numConnectionErrors
			>= RsSocket::_maxConnectionErrors) {

			m.release();
			socketError();
		    }
		}

		// For current users of Rs, it is okay
		// (and sometimes necessary) to abort
		// the connection if any call times out.
		handleAbortNL();
            }
        }
    }

    /* check _deleted again (may have been set from one of the callbacks) */
    if (!_deleted && _hardTimeout && !_hardTimerp) {
	/* keeping the reference that we started with */
        _hardTimerp = new OspTimerEvent();
        _hardTimerp->init(_msPerTick, this, (OspTimerEvent::TimerMethod)&RsConn::hardTimerTick);
	_mutex.release();
    } else {
	/* release the reference we held and exit */
	_mutex.release();
	release();
    }
}

void
RsConn::setHardTimeout(uint32_t timeoutSecs)
{
    osp_debug_assert(timeoutSecs > 0);
    _hardTimeout = MAX(timeoutSecs, (_msPerTick + 999) / 1000);
    _mutex.take();
    if (!_hardTimerp) {
	/* the timer needs a reference. */
	hold();
        _hardTimerp = new OspTimerEvent();
        _hardTimerp->init(_msPerTick, this, (OspTimerEvent::TimerMethod)&RsConn::hardTimerTick);
    }
    _mutex.release();
}

int
RsConn::socketError()
{
    osp_assert(_mutex.isLocked());

    if (_aborted) return 1;

    // abort the socket
    RsSocket *sp = _socketp;
    if (sp) {
	sp->hold();
        _mutex.release();
        sp->abort();
	sp->release();
        _mutex.take();
    }

    return _aborted ? 1 : 0;
}

#if RS_CONN_TRACING
StringTrace RSTrace(2048, false, DatadumpLogSink::factory(""));
string *RsConn::trace() {
    string *retval = RSTrace.get();
    ostringstream info;
    CallStack cs(10);
    cs.Capture();
    info << "(RsConn *)" << this << " time " << osp_get_time_ms() << " "
	 << " service name " << _serviceName
	 << " refcount " << _refCount << " Stack " << cs.toString() << " ";
    *retval = info.str();
    return retval;
}
#endif

void
RsSocket::release() {
    Rs *rsp = _rsp;
    rsp->_mutex.take();
    releaseNL();
    rsp->_mutex.release();
}

/* Set deleted flag which will cause the socket to be freed when its
 * reference count goes to zero.  Since connections have socket
 * references, we get rid of those as well here.  If someone's in the
 * middle of using a socket, they'll have temporarily held the socket,
 * or locked the connection, as well.
 */
void
RsSocket::abort() {
    Rs *rsp = _rsp;
    uint32_t i;
    RsConn *connp;

    Rs::rmFromActive(this);

    _rsp->_mutex.take();
    _deleted = 1;

    /* get rid of all socket references from all connections */
    deque<RsConn *> connections;
    for(i=0;i<Rs::_hashSize;i++) {
	for(connp = rsp->_connHashp[i]; connp; connp = connp->_hashNextp) {
	    connp->holdNL();
	    connections.push_back(connp);
	}
    }
    rsp->_mutex.release();

    BOOST_FOREACH(connp, connections) {
	connp->_mutex.take(); 

	/* have connection locked here, so fix sp pointer */
	if (connp->_socketp == this) {
	    /* get rid of the reference to the old socket */
	    release();	/* works, since this == connp->_socketp */
	    connp->_socketp = NULL;

	    /* mark the connection as deleted, so that it goes away */
	    connp->_deleted = 1;

	    /* abort any pending calls on this socket */
	    connp->abortConn();
	}
	connp->_mutex.release();
	connp->release();
    }
}

int32_t
RsSocket::absorbHeader(uint32_t *nbytesp, OspMbuf **newBufpp)
{
    uint8_t tc;
    OspMbuf *newBufp = *newBufpp;

    while (_pktHdrValid < 4 && (*nbytesp) > 0) {
        if (newBufp == NULL) {
            break;
        }

        tc = *(newBufp->data());

        if ( _pktHdrValid == 0) {
            if (tc != RSSOCKET_MAGIC) {
                *newBufpp = newBufp;
                return -1;
            }
            _pktCount = 0;
        }
        else {
            _pktCount <<= 8;
            _pktCount |= (tc & 0xFF);
        }

        newBufp = newBufp->pop(1);
        _pktHdrValid++;
        (*nbytesp)--;
    }

    *newBufpp = newBufp;
    return 0;
}

int32_t
RsSocket::send(OspMbuf* bufp)
{
    char *datap;
    OspMbuf *newBufp;
    uint32_t len;

    len = bufp->lenm();
    newBufp = _vlanp->get(32);
    if (!newBufp) {
        // Out of mbufs, drop packet and return an error.
        bufp->mfreem();
        return 1;
    }
    datap = newBufp->append(4,MB_RS_TAG);
    newBufp->concat(bufp);
    datap[0] = RSSOCKET_MAGIC;
    datap[1] = (len>>16) & 0xff;
    datap[2] = (len >> 8) & 0xff;
    datap[3] = len & 0xff;

    /* takes ownership of mbuf chain */
    int32_t ret = _sockp->sendx(newBufp);
    return ret;
}

void
Rs::addToActive(RsSocket *sockp)
{
    Monitor<MutexLock> m(_activeSocketsLock);
    _activeSockets.insert(sockp);
}

void
Rs::rmFromActive(RsSocket *sockp)
{
    Monitor<MutexLock> m(_activeSocketsLock);
    set<RsSocket *>::iterator si = _activeSockets.find(sockp);
    if (si != _activeSockets.end()) _activeSockets.erase(si);
}

/* static */
Rs*
Rs::getInstanceServer()
{
    /* Singleton design: thread-safe. Getting an rs instance is rare, so
     * protecting it with a lock is not a performance drain.
     */
    static SpinLock l;

    l.take();
    if(_instanceServer == NULL) {
        _instanceServer = createServer(0, true);
    }
    l.release();
    return _instanceServer;
}

/* static */
Rs*
Rs::getInstanceClient()
{
    /* Singleton design: thread-safe. Getting an rs instance is rare, so
     * protecting it with a lock is not a performance drain.
     */

    static SpinLock l;

    l.take();
    if(_instanceClient == NULL) {
        _instanceClient = new Rs(true);
    }
    l.release();
    return _instanceClient;
}

int
Rs::holdSocket(RsSocket *sockp)
{
    Monitor<MutexLock> m(_activeSocketsLock);
    set<RsSocket *>::iterator si = _activeSockets.find(sockp);
    if (si != _activeSockets.end()) {
	(*si)->hold();
	return 0;
    }

    return 1;
}

RsConn::RsConn() : _peerNode(UUID::uuidNone)
{
    static SpinLock l;
    static uint32_t rsNum = 0;
    Monitor<SpinLock> m(l);
    _connNum = rsNum++;
    _idleCount = 0;
}

void
Rs::initPeriodicCleanup()
{
    new BoostTimer(RsPeriodicCleanupInterval * 1000,
		   boost::bind(&Rs::periodicCleanupCheck, this));
}

void
Rs::periodicCleanupCheck()
{
    new BoostTimer(RsPeriodicCleanupInterval * 1000,
		   boost::bind(&Rs::periodicCleanupCheck, this));
    _mutex.take();

    deque<RsConn *> allConnections;
    for(size_t i = 0 ; i < _hashSize ; ++i) {
	for(RsConn *connp = _connHashp[i]; connp; connp = connp->_hashNextp) {
	    connp->holdNL();
	    allConnections.push_back(connp);
	}
    }

    uint32_t maxSockIdleCount =RsSocketMaxIdleTime / RsPeriodicCleanupInterval;
    if (_globalNumSockets > RsSocketCongestedNumSockets) {
	maxSockIdleCount=RsSocketCongestedIdleTime / RsPeriodicCleanupInterval;
    }

    deque<RsSocket *> socketsToAbort;
    for(RsSocket *sockp = _allSocketsp; sockp; sockp = sockp->_allNextp) {
	if (!sockp->_deleted) {
	    if (sockp->_idleCount >= maxSockIdleCount) {
		++sockp->_refCount;
		socketsToAbort.push_back(sockp);
	    } else {
		++sockp->_idleCount;
	    }
	}
    }
    _mutex.release();

    uint32_t maxConnIdleCount = RsConnMaxIdleTime / RsPeriodicCleanupInterval;
    if (allConnections.size() > RsConnCongestedNumConnections) {
	maxConnIdleCount = RsConnCongestedIdleTime / RsPeriodicCleanupInterval;
    }
    BOOST_FOREACH(RsConn *connp, allConnections) {
	connp->_mutex.take(); 
	if (!connp->_deleted) {
	    if (connp->_idleCount >= maxConnIdleCount) {
		connp->handleAbortNL();
	    } else {
		++connp->_idleCount;
	    }
	}
	connp->_mutex.release();
    }

    _mutex.take();
    BOOST_FOREACH(RsConn *connp, allConnections) {
	connp->releaseNL();
    }
    _mutex.release();

    BOOST_FOREACH(RsSocket *sockp, socketsToAbort) {
	sockp->abort();
	sockp->release();
    }

    _mutex.take();
    uint32_t totalRecentConnections = 0;
    uint32_t maxConnections = 0;
    string maxConnectionName;
    pair<string,uint32_t> recentConnection;
    BOOST_FOREACH(recentConnection, _recentConnections) {
	totalRecentConnections += recentConnection.second;
	if (recentConnection.second > maxConnections) {
	    maxConnections = recentConnection.second;
	    maxConnectionName = recentConnection.first;
	}
    }
    if (totalRecentConnections > 1000) {
	cerr << "Large number of outbound Rs connections("
	     << totalRecentConnections << "), largest source is "
	     << maxConnectionName << " with " << maxConnections
	     << " connections in " << RsPeriodicCleanupInterval
	     << " seconds." << endl;
    }
    _recentConnections.clear();
    _mutex.release();
}

void
Rs::traceSnapshot()
{
    RsTrace.snapshot(10);
}

static void
dumpRsConnections(DataDumpSearch &search,
		  uint64_t cookie,
		  bool summaryOnly,
		  ostream &summary,
		  uint32_t verbosityFlags,
		  XmlStream &header,
		  XmlStream &details,
		  uint64_t *pCookieOut)
{
    size_t numRs = 0;
    size_t numInConns = 0;
    size_t numOutConns = 0;

    /* Create the header: Should only ever be 1 record long */
    header << "Rs" << "RsConn" << "OspNetVLAN"
	   << "ConnId" << "serviceName"
	   << "connectionNum"  << "incoming" << "bound"
	   << "deleted" << "aborted" << "refs" << "pktsPending"
	   << "idleTime"
	   << "RsSocket" << "sockDeleted" << "sockfd"
	   << "nextSeq" << "rcvSeq" << "lastXmitAck"
	   << "peerNode" << "peerAddr"
	   << "peerPort" << "pendingOut"
	   << "bytesOut" << "bytesIn"
	   << endr;

    Rs *rsp = Rs::_rsListHead;
    while (rsp) {
	numRs++;
	Monitor<MutexLock> rsMonitor(rsp->_mutex);
	for(size_t i = 0 ; i<Rs::_hashSize ; ++i) {
	    RsConn *connp = rsp->_connHashp[i];
	    while (connp) {
		connp->holdNL();
		rsMonitor.release();
		Monitor<MutexLock> connMonitor(connp->_mutex);
		if (connp->_incoming) {
		    ++numInConns;
		} else {
		    ++numOutConns;
		}
		if (!summaryOnly) {
		    string peerAddr =
			NetworkUtils::addrNtoA(connp->_peerAddr._addr);
		    RsSocket *sockp = connp->_socketp;
		    details << Hex();
		    details << label("Rs") << rsp
			    << label("RsConn") << connp
			    << label("OspNetVLAN") << connp->_vlanp
			    << label("ConnId") << connp->_connId.toString()
			    << label("serviceName") << connp->_serviceName
			    << label("connectionNum") << connp->_connNum
			    << label("incoming")
			    << ((connp->_incoming) ? "true" : "false")
			    << label("bound")
			    << ((connp->_bound) ? "true" : "false")
			    << label("deleted")
			    << ((connp->_deleted) ? "true" : "false")
			    << label("aborted")
			    << ((connp->_aborted) ? "true" : "false")
			    << Dec()
			    << label("refs") << connp->_refCount
			    << label("pktsPending")
			    << connp->_flow._xmitQ._queueCount
			    << label("idleTime")
			    << (connp->_idleCount * RsPeriodicCleanupInterval)
			    << Hex()
			    << label("RsSocket") << sockp
			    << Dec();
		    if (connp->_socketp) {
			details << label("sockDeleted")
				<< (sockp->_deleted ? "true" : "false");
			details << label("sockfd") << sockp->_sockp->getSock();
		    }
		    details << label("nextSeq") << connp->_nextSeq
			    << label("rcvSeq")
			    << connp->_flow._rLastReceived
			    << label("lastXmitAck")
			    << connp->_flow._xAcknowledged;
		    if (connp->_peerNode != UUID::uuidNone) {
			details << label("peerNode")
				<< Node::getNodeName(connp->_peerNode);
		    }
		    details << label("peerAddr") << peerAddr
			    << label("peerPort") << connp->_peerAddr._port;
		    if (connp->_socketp) {
			details << label("pendingOut")
				<< connp->_socketp->_sockp->pendingWriteSize()
				<< label("bytesOut")
				<< connp->_socketp->_sockp->bytesWritten
				<< label("bytesIn")
				<< connp->_socketp->_sockp->bytesRead;
		    } else {
			details << label("pendingOut") << 0
				<< label("bytesOut") << 0
				<< label("bytesIn") << 0;
		    }

		    details << endr;
		}
		connMonitor.release();
		rsMonitor.take();
		RsConn *nconnp = connp->_hashNextp;
		connp->releaseNL();
		connp = nconnp;
	    }
	}
	rsp = rsp->_nextRs;
    }

    summary << numRs << " Rs instances, "
	    << numInConns << " inbound RsConn instances, "
	    << numOutConns << " outbound RsConn instances.";
}

static void
dumpRsCalls(DataDumpSearch &search,
	    uint64_t cookie,
	    bool summaryOnly,
	    ostream &summary,
	    uint32_t verbosityFlags,
	    XmlStream &header,
	    XmlStream &details,
	    uint64_t *pCookieOut)
{
    size_t numCalls = 0;

    /* Create the header: Should only ever be 1 record long */
    header << "Rs" << "RsConn" << "OspNetVLAN" << "RsClientCall"
	   << "ttl" << "slot"
	   << "seq" << "context" << "sockfd" << "peerAddr" << "peerPort"
	   << endr;

    Rs *rsp = Rs::_rsListHead;
    while (rsp) {
	Monitor<MutexLock> rsMonitor(rsp->_mutex);
	for(size_t i = 0 ; i<Rs::_hashSize ; ++i) {
	    RsConn *connp = rsp->_connHashp[i];
	    while (connp) {
		if (connp->_incoming) {
		    connp = connp->_hashNextp;
		    continue;  // only outgoing have calls
		}
		connp->holdNL();
		rsMonitor.release();
		Monitor<MutexLock> connMonitor(connp->_mutex);
		for (size_t j = 0 ; j < connp->_ccallCount ; ++j) {
		    RsClientCall *ccallp = connp->_ccallTablep[j];
		    if (!ccallp) continue;
		    ++numCalls;
		    if (!summaryOnly) {
			string peerAddr =
			    NetworkUtils::addrNtoA(connp->_peerAddr._addr);
			details << Hex();
			details << label("Rs") << rsp
				<< label("RsConn") << connp
				<< label("OspNetVLAN") << connp->_vlanp
				<< label("RsClientCall") << ccallp
				<< Dec()
				<< label("ttl") << ccallp->_ttl
				<< label("slot") << ccallp->_clientId
				<< label("seq") << ccallp->_sequence
				<< Hex()
				<< label("context")
				<< ccallp->_responseContextp
				<< Dec()
				<< label("sockfd")
				<< (connp->_socketp ?
				    connp->_socketp->_sockp->getSock() : -1)
				<< label("peerAddr") << peerAddr
				<< label("peerPort") << connp->_peerAddr._port;

			details << endr;
		    }
		}
		connMonitor.release();
		rsMonitor.take();
		RsConn *nconnp = connp->_hashNextp;
		connp->releaseNL();
		connp = nconnp;
	    }
	}
	rsp = rsp->_nextRs;
    }

    summary << numCalls << " RsClientCalls outstanding.";
}

int
Rs::initDataDump(void *noarg)
{
    DataDump::registerData("RsConnections", dumpRsConnections);
    DataDump::registerData("RsCalls", dumpRsCalls);
    return 0;
}
SYS_INIT(RsDump,Rs::initDataDump,NULL);

int
Rs::shutdown(void *arg)
{
    /* TODO: Kill all connections */
    return 0;
}
SYS_SHUTDOWN(rs,Rs::shutdown,NULL);
