#ifndef __NFSSERVER_H_ENV_
#define __NFSSERVER_H_ENV_ 1

#include <map>
#include <memory>

#include <sys/time.h>

#include "nfsv41.h"

class NfsServer {
public:
    static const uint32_t _maxForeSlots = 16;
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
        sessionid4 _sessionId;
        uint16_t _foreSlots;
        uint16_t _backSlots;
        uint32_t _csa_flags;    // from the create

        Session(NfsServer *serverp, Client *clientp);
    };

    // Map client owner, and client ID, to client
    std::map<std::string, Client *> _clientOwnerMap;
    std::map<uint64_t, Client *> _clientIdMap;
    std::map<sessionid4, Session *> _sessionMap;

    uint64_t _nextClientId;
    uint64_t _nextSessionCounter;

    struct timeval _bootTime;

    NfsServer();

    void start();

    int32_t opExchangeId(nfs_argop4 *op, nfs_resop4 *resp, struct svc_req *req);

    int32_t opCreateSession(nfs_argop4 *op, nfs_resop4 *resp, struct svc_req *req);
};

extern "C"  COMPOUND4res *nfsproc4_compound_4_svc(COMPOUND4args *args,
                                                  struct svc_req *req);

extern "C" void *nfsproc4_null_4_svc(void *args,
                                     struct svc_req *req);

extern "C" CB_COMPOUND4res *cb_compound_1_svc(CB_COMPOUND4args *args,
                                              struct svc_req *req);

extern "C" void *cb_null_1_svc(void *args, struct svc_req *req);


#endif /* __NFSSERVER_H_ENV_ */
