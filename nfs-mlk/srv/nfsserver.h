#ifndef __NFSSERVER_H_ENV_
#define __NFSSERVER_H_ENV_ 1

#include <map>
#include <memory>

#include <sys/time.h>

#include "nfsv41.h"
#include "vn.h"

class NfsServer {
public:
    static const uint32_t _maxForeSlots = 16;
    static const uint32_t _attrMaskWords = 3;

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
        std::string _currentFh;
        std::string _savedFh;
        stateid4 _currentStateId;
        stateid4 _savedStateId;

        CompoundState() {
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

    Vfs *_vfsp;
    Vn *_vfsRootp;
    Vfs::FsAttr _fsAttrs;

    NfsServer(Vfs *vfsp);

    void start();

    std::string translate_uid(uint32_t uid);

    std::string translate_gid(uint32_t gid);

    void copyToFattr( fattr4 *outp,
                      uint32_t *inMaskp,
                      Vn *vp,
                      Vn::Attr *attrsp,
                      Vfs::FsAttr *fsAttrsp);

    void freeFattr4(fattr4 *ap);

    void freeEntry4(entry4 *ep);

    void stringToNfs4Fh(std::string instr, nfs_fh4 *fhp);

    nfsstat4 opExchangeId(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *csp);

    nfsstat4 opCreateSession(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *csp);

    nfsstat4 opReclaimComplete(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *csp);

    nfsstat4 opSequence(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *csp);

    nfsstat4 opPutRootFh(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *csp);

    nfsstat4 opPutFh(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep);

    nfsstat4 opGetAttr(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep);

    nfsstat4 opReaddir(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep);

    nfsstat4 opLookup(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep);

    nfsstat4 opGetFh(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep);

    nfsstat4 opAccess(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep);

    nfsstat4 opBad(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep);

    nfsstat4 opSecinfoNoName(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep);

    nfsstat4 opSecinfo(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep);
};

extern "C"  COMPOUND4res *nfsproc4_compound_4_svc(COMPOUND4args *args,
                                                  struct svc_req *req);

extern "C" void *nfsproc4_null_4_svc(void *args,
                                     struct svc_req *req);

extern "C" CB_COMPOUND4res *cb_compound_1_svc(CB_COMPOUND4args *args,
                                              struct svc_req *req);

extern "C" void *cb_null_1_svc(void *args, struct svc_req *req);


#endif /* __NFSSERVER_H_ENV_ */
