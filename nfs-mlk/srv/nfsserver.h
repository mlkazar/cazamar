#ifndef __NFSSERVER_H_ENV_
#define __NFSSERVER_H_ENV_ 1

#include <map>
#include <memory>

#include <sys/time.h>

#include "nfsv41.h"

class NfsServer {
public:
    static const uint32_t _maxForeSlots = 16;
    typedef std::array<char, NFS4_SESSIONID_SIZE> StdSessionId4;
    class Client {
        friend class NfsServer;
    public:
        std::string _clientOwner;
        char _verifier[NFS4_VERIFIER_SIZE];
        uint64_t clientId;
        uint32_t _sessionSequenceId;
    };

    class Session {
        friend class NfsServer;
        Client *_clientp;
        StdSessionId4 _sessionId;
        uint16_t _foreSlots;
        uint16_t _backSlots;
        uint32_t _csa_flags;    // from the create
        uint32_t _nextCallSequence[_maxForeSlots];  // next slot

        Session(NfsServer *serverp, Client *clientp);
    };

    // This is per-compound operation state.
    class CompoundState {
    public:
        nfs_fh4 _currentFh;
        nfs_fh4 _savedFh;
        stateid4 _currentStateId;
        stateid4 _savedStateId;

        CompoundState() {
            memset(&_currentFh, 0, sizeof(_currentFh));
            memset(&_savedFh, 0, sizeof(_savedFh));
            memset(&_currentStateId, 0, sizeof(stateid4));
            memset(&_savedStateId, 0, sizeof(stateid4));
        }
    };

    // Map client owner, and client ID, to client
    std::map<std::string, Client *> _clientOwnerMap;
    std::map<uint64_t, Client *> _clientIdMap;
    std::map<StdSessionId4, Session *> _sessionMap;

    uint64_t _nextClientId;
    uint64_t _nextSessionCounter;

    struct timeval _bootTime;

    NfsServer();

    void start();

    int32_t opExchangeId(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *csp);

    int32_t opCreateSession(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *csp);

    int32_t opReclaimComplete(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *csp);

    int32_t opSequence(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *csp);

    int32_t opPutRootFh(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *csp);

    int32_t opGetAttr(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep);
};

extern "C"  COMPOUND4res *nfsproc4_compound_4_svc(COMPOUND4args *args,
                                                  struct svc_req *req);

extern "C" void *nfsproc4_null_4_svc(void *args,
                                     struct svc_req *req);

extern "C" CB_COMPOUND4res *cb_compound_1_svc(CB_COMPOUND4args *args,
                                              struct svc_req *req);

extern "C" void *cb_null_1_svc(void *args, struct svc_req *req);


#endif /* __NFSSERVER_H_ENV_ */
