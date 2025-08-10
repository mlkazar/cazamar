#include <ifaddrs.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "rpc.h"
#include "osp.h"
#include "sdr.h"
#include "voter.h"

int32_t
Voter::setPeers(VoterAddr *addrs, uint32_t addrCount, VoterAddr &self) {
    for(uint32_t i=0;i<addrCount;i++) {
        VoterPeer tpeer;
        tpeer._addr._ipAddr = addrs[i]._ipAddr;
        tpeer._addr._port = addrs[i]._port;
        _peers.push_back(tpeer);
    }

    _peerCount = addrCount;
    return 0;
}

uint32_t
Voter::getMyAddr() {
    ifaddrs *addrsp;
    int32_t code;
    uint32_t myAddr;
    ifaddrs *taddrp;

    code = getifaddrs(&addrsp);
    if (code)
        return code;

    myAddr = ~0U;
    for(taddrp = addrsp; taddrp != nullptr; taddrp = taddrp->ifa_next) {
        struct sockaddr_in *sap = (struct sockaddr_in *) taddrp->ifa_addr;
        if (sap->sin_family != AF_INET)
            continue;
        if (ntohl(sap->sin_addr.s_addr) == 0x7f000001)
            continue;
        myAddr = ntohl(sap->sin_addr.s_addr);
        break;
    }

    return myAddr;
}

int32_t
Voter::init(int32_t port) {
    RpcConn *connp;
    RpcServer *serverp;

    _localAddr._ipAddr = getMyAddr();
    _localAddr._port = port;

    // _rpcp is set by constructor

    _peerCount = 0;
    for(auto &x : _peers) {
        uuid_t serviceId;
        Rpc::uuidFromLongId(&serviceId, 7);
        serverp = _rpcp->addServer(NULL, &serviceId);

        struct sockaddr_in destAddr;

        destAddr.sin_family = AF_INET;
        destAddr.sin_addr.s_addr = htonl(x._addr._ipAddr);
        destAddr.sin_port = htons(x._addr._port);
        (void) _rpcp->addClientConn(&destAddr, &connp);

        // Associate the two; the server holds a queue of calls, and
        // also provides the service ID for the connection.
        connp->setServer(serverp);

        x._connp = connp;
        _peerCount++;
    }

    // PeerCount as computed doesn't include ourselves.
    // Values like 4 go to 3, values like 3 go to 2.
    _requiredCount = (_peerCount)/2 + 1;

    _bestEpoch.clear();
    _bestEpochMs = 0;

    _collectorThreadp = new CThreadHandle();
    _collectorThreadp->init((CThread::StartMethod) &Voter::collectVotes, this, NULL);

    uuid_t serviceId;

    Rpc::uuidFromLongId(&serviceId, 7);
    VoterServer *voterServerp = new VoterServer(_rpcp, this);
    _rpcp->addServer(voterServerp, &serviceId);

    /* create an endpoint for the server */
    RpcListener *listenerp = new RpcListener();
    listenerp->init(_rpcp, voterServerp, port);

    return 0;
}

void
Voter::collectVotes(void *contextp) {
    while(true) {
        collectVotesWork();

        sleep(VoterShortMs/1000);
    }
}

void
Voter::collectVotesWork() {
    // If someone else is ahead of us
    printf("\nStarting a new ping collection\n");
    uint64_t now = osp_time_ms();

    {
        char estr[64];
        uuid_unparse(_bestEpoch._epochId, estr);
        printf("port %d best value is %d.%s, %lld ms old\n",
               _localAddr._port, _bestEpoch._counter, estr, osp_time_ms() - _bestEpochMs);
    }

    if (_bestEpoch > _pushState && now - _bestEpochMs < VoterLongMs) {
        printf("port %d abandoning vote collection due to superior candidate\n",
               _localAddr._port);
        return;
    }

    // See if we're best by advertising ourselves.
    for(auto &x : _peers) {
        VoterPingCall pingCall;
        VoterPingResp pingResp;

        pingCall._callData = _pushState;
        pingCall._callingAddr = _localAddr;

        RpcSdr *sendSdrp;
        RpcSdr *recvSdrp;
        int32_t code;

        printf("collectVotes calling with counter=%d committed=%d\n",
               pingCall._callData._counter, pingCall._callData._committed);

        code = makeCall(x._connp, /* opcode */ VoterPingCall::_opcode, &sendSdrp, &recvSdrp);
        printf("===client back from call with code=%d\n", code);
        if (code) {
            printf("call failed with code=%d\n", code);
            continue;
        }

        pingCall.marshal(sendSdrp, /* marshal */ 1);

        code = getResponse();
        if (code) {
            printf("call getresponse failed code=%d\n", code);
            continue;
        }

        pingResp.marshal(recvSdrp, /* !marshal */ 0);

        x._peerState = pingResp._responseData;
        x._lastRespMs = osp_time_ms();

        finishCall();

        if (x._peerState > _bestEpoch) {
            _bestEpoch = x._peerState;
            _bestEpochMs = osp_time_ms();
        }

        {
            char uuidStr[64];
            char uuidSelf[64];
            uuid_unparse(_pushState._epochId, uuidSelf);
            uuid_unparse(pingResp._responseData._epochId, uuidStr);
            printf("= node %d->%d %s.%d rcvd pingResp error=%d id==%s.%d commited=%d\n",
                   _localAddr._port,
                   x._addr._port,
                   uuidSelf,
                   _pushState._counter,
                   pingResp._error,
                   uuidStr,
                   pingResp._responseData._counter,
                   pingResp._responseData._committed);
        }
    } // calls done.

    // When we're collecting votes, we're really just looking to see if we
    // can move to committed state.
    uint32_t preparedCount = 0;
    for(auto &x : _peers) {
        uint64_t now = osp_time_ms();
        if (now - x._lastRespMs > VoterLongMs) {
            // This peer isn't responding so skip counting it.
            continue;
        }

        if (x._peerState == _pushState) {
            preparedCount++;
        }
    }  // scan of results

    if (preparedCount >= _requiredCount ) {
        // The elected state is set only if we pushed committed
        // messages to everyone; this means that everyone knows we're
        // the primary.
        if (_pushState._committed)
            _elected = true;
        _pushState.setCommitted(true);
    }
}

int32_t
Voter::handlePing(VoterAddr *remoteAddrp, VoterPingCall *callp, VoterPingResp *resp) {
    VoterList::iterator it;
    for(it = _peers.begin(); it != _peers.end(); ++it) {
        if (it->_addr == callp->_callingAddr) {
            break;
        }
    }

    if (it == _peers.end()) {
        printf("Ping received by %d can't find peer\n", _localAddr._port);
        return -1;
    }

    {
        char estr[64];
        uuid_unparse(callp->_callData._epochId, estr);
        printf("Ping received by %d %d.%s\n", _localAddr._port,
               callp->_callData._counter, estr);
    }

    // we've found the peer calling us.  See if we can adopt the information
    // stored within

    // Update the received status
    it->_peerState = callp->_callData;
    it->_lastCallMs = osp_time_ms();

    // Figure out the response: we respond with the best epoch we
    // found.  That's either the caller, or our own epoch.
    if (it->_peerState >= _pushState) {
        resp->_responseData = it->_peerState;
    } else if (it->_peerState._committed && !_pushState._committed) {
        // caller is committed, and we're not, so don't disturb the
        // consensus.
        resp->_responseData = it->_peerState;
    } else {
        resp->_responseData = _pushState;
    }

    // If this is an advertisement of a better epoch, use it.
    if (callp->_callData > _bestEpoch) {
        _bestEpoch = callp->_callData;
    }

    // and keep track of how good old the bestEpoch is.
    if (callp->_callData == _bestEpoch)
        _bestEpochMs = osp_time_ms();

    return 0;
}

int32_t
VoterServer::VoterServerContext::serverMethod(RpcServer *serverp, Sdr *inDatap, Sdr *outDatap) {
    VoterAddr remoteAddr;

    VoterPingCall pingCall;
    VoterPingResp pingResp;
    int32_t code;

    printf("== in server method\n");

    // Unmarshal the incoming data
    pingCall.marshal(inDatap, /* !marshal */ 0);

    getPeerAddr(&remoteAddr._ipAddr, &remoteAddr._port);

    code = _voterp->handlePing(&remoteAddr, &pingCall, &pingResp);
    if (code != 0) {
        pingResp._error = code;
    } else {
        pingResp._error = 0;
    }

    getConn()->reverseConn();

    pingResp.marshal(outDatap, /* marshal */ 1);

    return 0;
}
