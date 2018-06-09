#include <netdb.h>

#include "rpc.h"
#include "cthread.h"
#include "mfclient.h"
#include "dqueue.h"
#include "mfand.h"

void
MfClient::init() {
    struct sockaddr_in destAddr;
    uuid_t serviceId;
    int32_t code;
    struct hostent *hostp;

    memset(&destAddr, 0, sizeof(destAddr));
#ifndef __linux__
    destAddr.sin_len = sizeof(destAddr);
#endif
    destAddr.sin_family = AF_INET;
    destAddr.sin_addr.s_addr = htonl(0x80020a02);       /* vice2 */
    destAddr.sin_port = htons(7711);

    hostp = gethostbyname("djtogoapp.duckdns.org");
    if (hostp) {
        memcpy(&destAddr.sin_addr.s_addr, hostp->h_addr_list[0], 4);
    }
    else {
        return;
    }

    /* uncomment for local debugging XXXXXXXXXXXXXXXX */
    // destAddr.sin_addr.s_addr = htonl(0xc0a8010b);    /* HOME */
    // destAddr.sin_addr.s_addr = htonl(0x0a020553);

    Rpc::uuidFromLongId(&serviceId, 7);
    _serverp = _rpcp->addServer(NULL, &serviceId);

    /* open a conn to the target */
    code = _rpcp->addClientConn(&destAddr, &_connp);
    printf("RpcTest: addclientconn code=%d\n", code);
    if (code)
        return;

    /* bind conn to the server */
    _connp->setServer(_serverp);

    _didInit = 1;
}

int32_t
MfClient::announceSong(char *userp, char *songp, char *artistp, uint32_t songType, uint32_t acl ) {
    int32_t code;
    RpcSdr *sendSdrp;
    RpcSdr *recvSdrp;

    if (!_didInit)
        return -1;

    code = _announceContext.makeCall(_connp, /* opcode */ 1, &sendSdrp, &recvSdrp);
    if (code) {
        printf("RpcTest: makecall fail %d\n", code);
        return code;
    }

    code = sendSdrp->copyString(&userp, /* doMarshal */ 1);
    if (code) {
        _announceContext.finishCall();
        return code;
    }
    code = sendSdrp->copyString(&songp, /* doMarshal */ 1);
    if (code) {
        _announceContext.finishCall();
        return code;
    }
    code = sendSdrp->copyString(&artistp, /* doMarshal */ 1);
    if (code) {
        _announceContext.finishCall();
        return code;
    }
    code = sendSdrp->copyLong(&songType, /* doMarshal */ 1);
    if (code) {
        _announceContext.finishCall();
        return code;
    }
    code = sendSdrp->copyLong(&acl, /* doMarshal */ 1);
    if (code) {
        _announceContext.finishCall();
        return code;
    }

    code = _announceContext.getResponse();
    if (code) {
        printf("RpcTest: call response=%d\n", code);
        _announceContext.finishCall();
        return code;
    }

    _announceContext.finishCall();

    return 0;
}

int32_t
MfClient::getAllPlaying(uint32_t maxEntries, dqueue<MfEntry> *elistp)
{
    uint32_t currentEntries;
    uint32_t i;
    int32_t code;
    RpcSdr *sendSdrp;
    RpcSdr *recvSdrp;
    MfEntry *ep;
    char *whop;
    char *songp;
    char *artistp;
    uint32_t songType;
    uint32_t acl;

    if (!_didInit)
        return -1;

    elistp->init();

    code = _getAllContext.makeCall(_connp, /* opcode */ 2, &sendSdrp, &recvSdrp);
    if (code) {
        printf("RpcTest: makecall fail %d\n", code);
        return code;
    }

    code = sendSdrp->copyLong(&maxEntries, /* doMarshal */ 1);
    if (code) {
        printf("getallplaying: send maxentries fail %d\n", code);
        _getAllContext.finishCall();
        return code;
    }

    code = _getAllContext.getResponse();
    if (code) {
        printf("getAllPlaying getResponse %d\n", code);
        _getAllContext.finishCall();
        return code;
    }

    code = recvSdrp->copyLong(&currentEntries, 0);
    if (code) {
        printf("getAllPlaying/2 %d\n", code);
        _getAllContext.finishCall();
        return code;
    }

    for(i=0;i<currentEntries;i++) {
        code = recvSdrp->copyString(&whop, 0);
        if (code) {
            printf("getAllPlaying/3 %d\n", code);
            _getAllContext.finishCall();
            return code;
        }
        code = recvSdrp->copyString(&songp, 0);
        if (code) {
            printf("getAllPlaying/4 %d\n", code);
            delete [] whop;
            _getAllContext.finishCall();
            return code;
        }
        code = recvSdrp->copyString(&artistp, 0);
        if (code) {
            printf("getAllPlaying/5 %d\n", code);
            delete [] whop;
            delete [] songp;
            _getAllContext.finishCall();
            return code;
        }
        code = recvSdrp->copyLong(&songType, 0);
        if (code) {
            printf("getAllPlaying/6 %d\n", code);
            delete [] whop;
            delete [] songp;
            _getAllContext.finishCall();
            return code;
        }
        code = recvSdrp->copyLong(&acl, 0);
        if (code) {
            printf("getAllPlaying/7 %d\n", code);
            delete [] whop;
            delete [] songp;
            _getAllContext.finishCall();
            return code;
        }

        printf("Received from %s: song=%s artist=%s type=0x%x acl=0x%x\n",
               whop, songp, artistp, (int) songType, (int) acl);

        if (acl) {
            ep = new MfEntry(whop, songp, artistp, songType, acl);
            elistp->append(ep);
        }
    }

    /* call is done */
    _getAllContext.finishCall();

    return 0;
}
