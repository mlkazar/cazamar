#ifndef _VOTER_H_ENV__
#define _VOTER_H_ENV__ 1

#include "task.h"
#include "sock.h"
#include "sdr.h"
#include <string>
#include <uuid/uuid.h>

/* the Voter class represents a voter contacting a set of nodes, of
 * which this voter is an instance, and participating in an election
 * with the other nodes in the network with them, with the network
 * defined by the SockSys object.
 *
 * The state of the election amongst the nodes is upcalled to the
 * VoterClient object(s) set within the voter.
 *
 * The state transitions for a voter notify the client on every new
 * epoch, and in that same state notification, indicate whether this
 * specific voter is now the primary or not.
 *
 * The client acknowledges that it has now stopped all operations for
 * any previous epoch by calling back into the voter with the new
 * epoch that it is now acknowleging.  Once an epoch is acknowledged as
 * current, the client may receive notifications of newer epoch
 * changes.  Note that the voter may skip epochs in its notifications
 * if more than one epoch has passed between the previous notification
 * and the time the next notification is performed.
 *
 * Notifications are guaranteed to be serialized, so that there's at
 * most one notification upcall outstanding at any instant.  And in
 * the case of a newEpoch upcall, upcalls will be blocked until the
 * upcall is acknowledged.
 *
 * Note that epochs are comprised of a timestamp (ms since Unix time 0) and a
 * node UUID to disambiguate ties in the timestamp field.  They're ordered by
 * the dictionary ordering by timestamp first and then node UUID.
 */

/* The protocol works as follows:
 *
 * The protocol's goal is to find a node with the largest node UUID, and
 * get it a majority of the votes of the nodes in the cluster.  It does
 * this by first sending Scout messages to find the node with the best (largest)
 * UUID, and then having that node send Ping messages to all nodes proposing
 * a value for an epoch that's larger than any previously agreed up epoch.
 *
 * The Scout phase is a convergence optimization: it helps quickly
 * squelch an attempt to propose a better epoch when there's already a
 * good elected primary node with a usable epoch, and it helps quickly
 * choose a candidate to win an election in the case where there is no
 * primary already present.
 *
 * A node begins by sending all of its peers a Scout message,
 * proposing a potential new epoch.  A node receiving a Scout message
 * responds negatively to it if the node is still part of a live epoch
 * owned by another node, or is part of a live epoch owned by the same
 * node having a larger timestamp field (in which case this is
 * probably a very delayed Scout message).  It also responds
 * negatively if it not part of a live epoch, but its own node UUID is
 * larger than the sender's UUID, or it has recently seen a Scout or
 * Ping message from a node with larger UUID.  Otherwise, it responds
 * positively (return code 0) to the Scout message.
 *
 * Once a scouting node gets positive responses from a majority of
 * nodes, it moves on to the Propose phase.  It sends Propose messages
 * with its proposed epoch to all of its peer nodes.  These Propose
 * messages are accepted by every node that hasn't already accepted a
 * larger epoch proposal from another proposing node (this is
 * basically the part that comes from Paxos).  Once the proposer
 * receives responses from a majority of the nodes, it becomes primary
 * and then continues to send out proposals for the same epoch.  As
 * long as it continues to receive positive responses for the same
 * epoch, it continues as primary.  If it loses the votes for
 * continuing as primary, it chooses a new epoch and goes back to
 * Scout state.
 */

class VoterSdr : public Sdr {
    std::string _data;

public:
    int32_t copyCountedBytes(char *targetp, uint32_t nbytes, int isMarshal);

    uint32_t bytes() {
        return _data.size();
    }

    std::string getString() {
        return _data;
    }

    void setString(std::string a) {
        _data = a;
    }

    VoteSdr() {
        _datap = NULL;
    }

    int32_t sdrScoutReq( Sdr *sdrp, 
                         UUID *uuidp,
                         uint8_t *flagsp,
                         VoterEpoch *epochp,
                         int isMarshal) {
        sdrUUID(sdrp, uuidp, isMarshal);
        sdrChar(sdrp, flagsp, isMarshal);
        sdrEpoch(sdrp, epochp, isMarshal);
        return 0;
    }

    int32_t sdrScoutResp( Sdr *sdrp, 
                          int32_t *codep,
                          VoterEpoch *epochp,
                          UUID *nodep,
                          int isMarshal) {
        sdrInt32(sdrp, codep, isMarshal);
        sdrEpoch(sdrp, epochp, isMarshal);
        sdrUUID(sdrp, uuidp, isMarshal);
        return 0;
    }

    int32_t sdrProposeReq( Sdr *sdrp, 
                           UUID *uuidp,
                           uint8_t *flagsp,
                           VoterEpoch *epochp,
                           int isMarshal) {
        sdrUUID(sdrp, uuidp, isMarshal);
        sdrChar(sdrp, flagsp, isMarshal);
        sdrEpoch(sdrp, epochp, isMarshal);
        return 0;
    }

    int32_t sdrProposeResp( Sdr *sdrp, 
                          int32_t *codep,
                          VoterEpoch *epochp,
                          UUID *nodep,
                          int isMarshal) {
        sdrInt32(sdrp, codep, isMarshal);
        sdrEpoch(sdrp, epochp, isMarshal);
        sdrUUID(sdrp, uuidp, isMarshal);
        return 0;
    }
};

/* The format of a Propose message is:
 *
 * Opcode -- 1 for ping.
 * 
 * Sender's Node UUID
 *
 * Flags - 8 bits, where bit 1 is 'sender is primary'
 *
 * Epoch - 16 bytes of UUID (node UUID) followed by 8 bytes of timestamp, giving
 * ms since Unix time 0.
 *
 * Number of milliseconds primary is valid, if message comes from primary.
 *
 * The response is:
 *
 * 32 bit return code.  A return code of 0 means that the proposer's
 * epoch has been accepted, and no smaller epoch will be accepted by
 * this node going forward.  A non-zero response means that the called
 * node has already accepted a larger epoch.  A response of 1 also returns the
 * node and epoch who proposed the best epoch accepted by this node.
 *
 * Epoch -- the best epoch
 *
 * Node -- the node with the best epoch.
 */

/* The format of a Scout message is:
 *
 * Opcode -- 2 for Scout.
 *
 * Sender's node UUID.
 *
 * Epoch -- proposed epoch.
 *
 * The response is:
 *
 * 32 bit return code -- 0 means the sender should propose the epoch, 1 means another
 * primary is active, 2 means called node will propose a better epoch.
 *
 * Epoch -- the best epoch the called node has seen.
 *
 */

int32_t 
VoteSdr::copyCountedBytes(char *targetp, uint32_t nbytes, int isMarshal)
{
    uint32_t inLen;

    if (isMarshal) {
        /* copy data to the end of the string */
        _data.append(targetp, nbytes);
        return 0;
    }
    else {
        inLen = _data.size();
        if (nbytes > inLen)
            return -1;
        memcpy(targetp, _data.c_str(), nbytes);
        /* erase the bytes from the start */
        _data.erase(0, nbytes);
        return 0;
    }
}

class VoterClient;

class VoterEpoch {
 public:
    uuid_t _uuid;
    uint64_t _counter;

    void init() {
        uuid_generate(&_uuid);
        _counter = 0;
    }

    void next(Epoch *epochp) {
        _counter = epochp->_counter+2;
    }

    void setCounter(uint64_t counter) {
        _counter = counter;
    }

    int cmp(const VoterEpoch &a, const VoterEpoch &b) {
        if (a._counter < b._counter)
            return -1;
        else if (a._counter > b._counter)
            return 1;
        else 
            return memcmp(&a._uuid, &b._uuid, sizeof(uuid_t));
    }

};

class Voter : public Task {
 public:
    static const uint32_t _recentMs = 10000;
    static const uint32_t _pingMs = 3000;
    static const uint32_t _maxPeers = 64;
    static const uint32_t _voteLifetimeMs = 20000;

    class Peer : public Task {
    public:
        SockNode *_nodep;
        uint8_t _isMe;
        uint8_t _isPrimary;
        uint64_t _lastReceiveMs;        /* time we last heard from this node for any reason */
        uint64_t _isPrimaryUntil;       /* when the primary's election is over */
        VoterEpoch _declaredEpoch;      /* epoch spec'd by this node */
        Voter *_voterp;

        void init(Voter *voterp) {
            _nodep = NULL;
            _isMe = 0;
            _isPrimary = 0;
            _lastReceivedMs = 0;
            _isPrimaryUntil = 0;
            _voterp = voterp;
        }
    } _peers[_maxPeers];

    /* how long after we last heard from a node that we think it might
     * be dead, and resume pinging other nodes, if we're now the best candidate
     * to be a new primary.
     */
    uint32_t _peerCount;
    SockNode *_myNodep;
    VoterClient *_clientp;
    SockSys *_netSysp;
    VoterEpoch _epoch;

    /* times in milliseconds since Unix time 0 */
    uint64_t _lastBetterReceived;

    Voter() {
        uint32_t i;
        for(i=0;i<_maxPeers;i++) {
            _peers[i].init(this);
        }
        _peerCount = 0;
        _myNodep = NULL;
        _clientp = NULL;
        _netSysp = NULL;
    }

    enum Event { noop = 0,
                 newEpoch = 1
    };

    void init( SockSys *sockSysp,
               std::vector<SockNode *>,
               SockNode *myNodep, 
               VoterClient *clientp);

    void acknowledgeFinished(Epoch *epochp);
};

class VoterClient {
    void stateChange(Voter::Event event, int isPrimary, VoterEpoch *epochp);
};

#endif /* _VOTER_H_ENV__ */
