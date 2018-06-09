#ifndef __MFAND_H_ENV__
#define __MFAND_H_ENV__ 1

#include "cthread.h"
#include "dqueue.h"
#include "xgml.h"

class MfLocation {
 public:
    MfLocation *_dqNextp;
    MfLocation *_dqPrevp;
    uint32_t _ipAddr;
    std::string _locationString;
};

class MfEntry {
 public:
    MfEntry *_dqNextp;
    MfEntry *_dqPrevp;
    char *_whop;
    char *_songp;
    char *_artistp;
    uint32_t _songType;
    uint32_t _acl;

    void freeList() {
        MfEntry *nep;
        MfEntry *ep;

        for(ep = this; ep; ep=nep) {
            nep = ep->_dqNextp;
            delete ep;
        }
    }

    /* takes ownership of storage; storage must be allocated as an array of characters */
    MfEntry(char *whop, char *songp, char *artistp, uint32_t songType, uint32_t acl) {
        _whop = whop;
        _songp = songp;
        _artistp = artistp;
        _songType = songType;
        _acl = acl;
        _dqNextp = NULL;
        _dqPrevp = NULL;
    }

    ~MfEntry() {
        if (_whop)
            delete [] _whop;
        if (_songp)
            delete [] _songp;
        if (_artistp)
            delete [] _artistp;
    }
};

class MfServer : public RpcServer {

    static const uint32_t _maxQueued = 100;

    CThreadMutex _lock;
    dqueue<MfEntry> _allPlaying;
    dqueue<MfLocation> _locationCache;
    Xgml *_xgmlp;

 public:
    class MfAnnounceContext : public RpcServerContext {
        MfServer *_serverp;

        int32_t serverMethod(RpcServer *serverp, Sdr *inDatap, Sdr *outDatap);

    public:
        MfAnnounceContext(MfServer *serverp) {
            _serverp = serverp;
        }
    };

    class MfListAllContext : public RpcServerContext {
        MfServer *_serverp;

        int32_t serverMethod(RpcServer *serverp, Sdr *inDatap, Sdr *outDatap);

    public:
        MfListAllContext(MfServer *serverp) {
            _serverp = serverp;
        }
    };

    RpcServerContext *getContext(uint32_t opcode) {
        RpcServerContext *sp;

        if (opcode == 1)
            sp = new MfAnnounceContext(this);
        else if (opcode == 2)
            sp = new MfListAllContext(this);
        else {
            printf("RpcTest: bad opcode received, op=%d\n", opcode);
            sp = NULL;
        }
        return sp;
    }

    int32_t translateLocation(uint32_t ipAddr, std::string *resultp);

    int32_t findLocation(uint32_t ipAddr, std::string *resultp);

    void addLocation(uint32_t ipAddr, std::string *resultp);

    int32_t getLocation(uint32_t ipAddr, std::string *resultp);

public:
    void checkCount();

    MfServer(Rpc *rpcp) : RpcServer(rpcp) {
        _xgmlp = new Xgml();
    }
};

#endif /* __MFAND_H_ENV__ */
