#ifndef __VOTER_H_ENV__
#define __VOTER_H_ENV__ 1

#include <list>

#include "cthread.h"
#include "osp.h"
#include "rpc.h"
#include "sdr.h"
#include "votersdr.h"

class Voter;

class VoterAddr {
public:
    uint32_t _ipAddr;   // in host order
    uint32_t _port;     // in host order

    VoterAddr() {
        _ipAddr = 0;
        _port = 0;
    }

    bool operator == (const VoterAddr &rhs) {
        if (_ipAddr == rhs._ipAddr &&
            _port == rhs._port)
            return true;
        else
            return false;
    }

    bool operator < (const VoterAddr &rhs) {
        if (_ipAddr < rhs._ipAddr)
            return true;
        else if (_ipAddr == rhs._ipAddr) {
            if (_port < rhs._port)
                return true;
            else
                return false;
        } else {
            return false;
        }
    }

    bool operator > (const VoterAddr &rhs) {
        if (_ipAddr > rhs._ipAddr)
            return true;
        else if (_ipAddr == rhs._ipAddr) {
            if (_port > rhs._port)
                return true;
            else
                return false;
        } else {
            return false;
        }
    }

    bool operator != (const VoterAddr &rhs) {
        return !((*this) == rhs);
    }

    bool operator <= (const VoterAddr &rhs) {
        return (*this) == rhs || (*this) < rhs;
    }

    bool operator >= (const VoterAddr &rhs) {
        return (*this) == rhs || (*this) > rhs;
    }
};

class VoterPeer {
public:
    VoterAddr _addr;
    VoterData _peerState;
    bool _inQuorum;
    uint64_t _lastRespMs;    // time last response received
    uint64_t _lastCallMs;    // last time a call received from node
    RpcConn *_connp;

    VoterPeer() {
        _lastRespMs = 0;
        _lastCallMs = 0;
        _inQuorum = true;
        _connp = nullptr;
        return;
    }
};

class VoterServer : public RpcServer {
    Voter *_voterp;

 public:
    class VoterServerContext : public RpcServerContext {
        int32_t serverMethod(RpcServer *serverp, Sdr *inDatap, Sdr *outDatap);

    public:
        Voter *_voterp;

        VoterServerContext(Voter *voterp) {
            _voterp = voterp;
        }
    };

    RpcServerContext *getContext(uint32_t opcode) {
        VoterServerContext *sp;

        if (opcode != 1) {
            printf("RpcTest: bad opcode received, op=%d\n", opcode);
            return NULL;
        }
        sp = new VoterServerContext(_voterp);
        return sp;
    }

public:
    // The voter object is passed into the server structure and then
    // copied into each server context created for each incoming call.
    VoterServer(Rpc *rpcp, Voter *voterp) : RpcServer(rpcp) {
        _voterp = voterp;
    }
};

class Voter : public RpcClientContext {
public:
    typedef std::list<VoterPeer> VoterList;
    uint32_t _peerCount;
    uint32_t _requiredCount;
    VoterList _peers;

    // If we're trying to become master, the state we're trying to propagate.
    VoterData _pushState;
    bool _elected;

    // State last received from a valid master, including ourselves
    // when we're collecting votes.
    VoterData _recvdState;

    // Track when we hear someone better, and don't send Pings until
    // they've expired.
    uint64_t _bestEpochMs;      // latest time from this epoch
    VoterData _bestEpoch;       // biggest epoch we've observed elsewhere

    CThreadHandle *_collectorThreadp;

    Rpc *_rpcp;

    int32_t init(int32_t port);

    int32_t setPeers(VoterAddr *addrs, uint32_t addrCount, VoterAddr &localAddr);

    void *collectVotes();

    void collectVotesWork();

    int32_t handlePing(VoterAddr *remoteAddrp, VoterPingCall *callp, VoterPingResp *resp);

    static uint32_t getMyAddr();

    Voter() : RpcClientContext(new Rpc()) {
        _rpcp = getRpc();       // from subclass
        _rpcp->init();

        _elected = false;
        _collectorThreadp = nullptr;
    }
};

#endif // __VOTER_H_ENV__
