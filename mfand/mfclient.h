#ifndef __MFCLIENT_H_ENV__
#define __MFCLIENT_H_ENV__ 1
#include "rpc.h"
#include "cthread.h"
#include "mfand.h"

class MfClient
{
 public:
    Rpc *_rpcp;
    RpcConn *_connp;
    RpcServer *_serverp;
    int _didInit;

    static const uint32_t _aclAll = 1;

    RpcClientContext _announceContext;
    RpcClientContext _getAllContext;

    void init();

    int32_t announceSong(char *userp, char *artistp, char *songp, uint32_t songType, uint32_t acl);

    int32_t getAllPlaying(uint32_t maxEntries, dqueue<MfEntry> *elistp);

    MfClient(Rpc *rpcp) : _announceContext(rpcp), _getAllContext(rpcp) {
        _rpcp = rpcp;
        _didInit = 0;
    }

};

#endif /* __MFCLIENT_H_ENV__ */

