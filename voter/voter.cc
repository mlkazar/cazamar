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
    VoterAddr localAddr;
    RpcConn *connp;

    localAddr._ipAddr = getMyAddr();
    localAddr._port = port;

    // _rpcp is set by constructor

    _peerCount = 0;
    for(auto x : _peers) {
        struct sockaddr_in destAddr;

        destAddr.sin_family = AF_INET;
        destAddr.sin_addr.s_addr = htonl(x._addr._ipAddr);
        destAddr.sin_port = htons(x._addr._port);
        (void) _rpcp->addClientConn(&destAddr, &connp);
        x._connp = connp;
        _peerCount++;
    }

    // PeerCount as computed doesn't include ourselves.
    // Values like 4 go to 3, values like 3 go to 2.
    _requiredCount = (_peerCount)/2 + 1;

    _bestEpoch.generate();
    _bestEpochMs = osp_time_ms();

    _collectorThreadp = new CThreadHandle();
    _collectorThreadp->init((CThread::StartMethod) &Voter::collectVotes, this, NULL);

    uuid_t serviceId;

    Rpc::uuidFromLongId(&serviceId, 7);
    VoterServer *voterServerp = new VoterServer(_rpcp, this);
    _rpcp->addServer(voterServerp, &serviceId);

    /* create an endpoint for the server */
    RpcListener *listenerp = new RpcListener();
    listenerp->init(_rpcp, voterServerp, 7711);

    return 0;
}

void *
Voter::collectVotes() {
    while(true) {
        collectVotesWork();

        sleep(VoterShortMs/1000);
    }

    return nullptr;
}

void
Voter::collectVotesWork() {
    // If someone else is ahead of us
    uint64_t now = osp_time_ms();
    if (now - _bestEpochMs < VoterLongMs) {
        // Best guy is still better than us.
        return;
    }

    for(auto &x : _peers) {
        VoterPingCall pingCall;
        VoterPingResp pingResp;
        pingCall._callData = _pushState;

        RpcSdr *sendSdrp;
        RpcSdr *recvSdrp;
        int32_t code;

        code = makeCall(x._connp, /* opcode */ VoterPingCall::_opcode, &sendSdrp, &recvSdrp);
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
    }

    // First, if a majority of the responses indicate peers in the
    // committed state (possibly including our own state), all with the
    // same paxosCounter, then we just adopt that consensus.
    uint32_t preparedCount = 0;
    for(auto &x : _peers) {
        uint64_t now = osp_time_ms();
        if (now - x._lastRespMs > VoterLongMs) {
            // This peer isn't responding so skip counting it.
        }

        // See if we should stop sending requests because someone proposing a
        // better epoch responded recently.
        if (x._peerState > _pushState) {
            if (x._peerState > _bestEpoch) {
                _bestEpoch = x._peerState;
                _bestEpochMs = x._lastRespMs;
            } else if (x._peerState == _bestEpoch) {
                // Update _bestEpochMs
                if (x._lastRespMs > _bestEpochMs) {
                    _bestEpochMs = x._lastRespMs;
                }
            }
        }

        if (_pushState._committed) {
            // We're trying to send commits to everyone
            if (x._peerState  == _pushState) {
                // This is a valid response
                if (x._peerState._committed) {
                    preparedCount++;
                    x._inQuorum = true;
                } else
                    printf("got uncommitted response from a prepared node\n");
            }
        } else {
            // We're sending prepares to everyone;
            if (x._peerState  == _pushState) {
                // This is a valid response
                preparedCount++;
            } else {
                // We might be seeing someone who's already doing commits,
                // in which case we check
            }
        }
    }

    if (preparedCount >= _requiredCount && _pushState._committed) {
        _elected = true;
    }
}

int32_t
Voter::handlePing(VoterAddr *remoteAddrp, VoterPingCall *callp, VoterPingResp *resp) {
    VoterList::iterator it;
    for(it = _peers.begin(); it != _peers.end(); ++it) {
        if (it->_addr == *remoteAddrp) {
            break;
        }
    }

    if (it == _peers.end()) {
        return -1;
    }

    // we've found the peer calling us.  See if we can adopt the information
    // stored within

    if (callp->_callData == it->_peerState) {
        // Received a duplicate incoming call.  Update last contact time
        // and record commit flag
        it->_lastCallMs = osp_time_ms();
        if (callp->_callData._committed)
            it->_peerState._committed = true;
    } else if (callp->_callData > it->_peerState) {
        // New data
        it->_lastCallMs = osp_time_ms();
        it->_peerState = callp->_callData;
    } else {
        // We're going to reject this update.
    }

    resp->_responseData = it->_peerState;

    return 0;
}

int32_t
VoterServer::VoterServerContext::serverMethod(RpcServer *serverp, Sdr *inDatap, Sdr *outDatap) {
    VoterAddr remoteAddr;

    VoterPingCall pingCall;
    VoterPingResp pingResp;
    int32_t code;

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
