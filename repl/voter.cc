#include "voter.h"

void
Voter::init( SockSys *sockSysp,
              std::vector<SockNode *> vectorNodes,
              SockNode *myNodep, 
              VoterClient *clientp)
{
    _myNodep = myNodep;
    _nodeArray = vectorNodes;
    _clientp = clientp;
    _netSysp = sockSysp;

    setRunMethod(&Voter::sendPings);
    queue();
}

void
Voter::sendProposals()
{
    /* We send pings here, sometimes.  A ping goes to all the other
     * nodes in the cluster.
     *
     * Here, we consider sending pings.  If we're primary and we
     * haven't sent a ping out for a while, we send one now.  If we're
     * not primary, but we haven't heard from the primary in a while,
     * then if we haven't heard from a better candidate to replace the
     * primary in a while, we send out a ping.
     */
    for(i=0; i<_peerCount; i++) {
        Peer *peerp;
        peerp = &_peers[i];
        setRunMethod(&Voter::proposalResponse);
        sendProposal(peerp->_nodep,     /* target */
                     &_nodeId,          /* parms */
                     _isPrimary,
                     &_epoch
                     _voteLifetimeMs,
                     this);
    }
}

void
Voter::proposalResponse()

void
Voter:: acknowledgeFinished(Epoch *epochp)
{
}

