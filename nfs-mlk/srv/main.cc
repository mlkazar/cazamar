#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nfsv41.h"

extern "C" {
void nfs4_program_4();
int _rpcsvcdirty = 0;
}

int
main(int argc, char **argv) {
    SVCXPRT *transp;

    pmap_unset(NFS4_PROGRAM, NFS_V4);

    transp = svcudp_create(RPC_ANYSOCK);
    if (transp == NULL) {
        fprintf(stderr, "Cannot create udp service.\n");
        return 1;
    }
    if (!svc_register(transp, NFS4_PROGRAM, NFS_V4, nfs4_program_4, IPPROTO_UDP)) {
        fprintf(stderr, "Unable to register (NFS4_PROGRAM, NFS_V4, udp).\n");
        return 1;
    }

    transp = svctcp_create(RPC_ANYSOCK, 512*1024, 512*1024);
    if (transp == NULL) {
        fprintf(stderr, "Cannot create tcp service.\n");
        return 1;
    }
    if (!svc_register(transp, NFS4_PROGRAM, NFS_V4, nfs4_program_4, IPPROTO_TCP)) {
        fprintf(stderr, "Unable to register (NFS4_PROGRAM, NFS_V4, tcp).\n");
        return 1;
    }

    printf("Sun RPC Server running...\n");
    svc_run(); 
    fprintf(stderr, "svc_run returned\n");
    return 1;

}

CB_COMPOUND4res *cb_compound_1_svc(CB_COMPOUND4args *args, struct svc_req *req) {
    printf("in cb compound\n");
    return nullptr;
}

void *cb_null_1_svc(void *args, struct svc_req *req) {
    printf("in cb null\n");
    return nullptr;
}

COMPOUND4res *opExchangeId(nfs_argop4 *op, struct svc_req *req) {
    // should implement a hash table
    EXCHANGE_ID4args *arg = &op->nfs_argop4_u.opexchange_id;
    client_owner4 *owner = &arg->eia_clientowner;
    uint32_t flags = arg->eia_flags;
    printf("ownerid='%s' flags=%x\n", owner->co_ownerid.co_ownerid_val, flags);

    COMPOUND4res *resp = (COMPOUND4res *) malloc(sizeof(COMPOUND4res));
    memset(resp, 0, sizeof(*resp));
    return resp;
}

COMPOUND4res *nfsproc4_compound_4_svc(COMPOUND4args *args, struct svc_req *req) {
    printf("in compound with ct=%d\n", args->argarray.argarray_len);
    nfs_argop4 *op = args->argarray.argarray_val;
    for(uint32_t i=0;i<args->argarray.argarray_len; i++, op++) {
        printf("operation %d\n", op->argop);
        switch(op->argop) {
            case OP_ACCESS:
                break;
            case OP_CLOSE:
                break;
            case OP_COMMIT:
                break;
            case OP_CREATE:
                break;
            case OP_DELEGPURGE:
                break;
            case OP_DELEGRETURN:
                break;
            case OP_GETATTR:
                break;
            case OP_GETFH:
                break;
            case OP_LINK:
                break;
            case OP_LOCK:
                break;
            case OP_LOCKT:
                break;
            case OP_LOCKU:
                break;
            case OP_LOOKUP:
                break;
            case OP_LOOKUPP:
                break;
            case OP_NVERIFY:
                break;
            case OP_OPEN:
                break;
            case OP_OPENATTR:
                break;
            case OP_OPEN_CONFIRM:
                break;
            case OP_OPEN_DOWNGRADE:
                break;
            case OP_PUTFH:
                break;
            case OP_PUTPUBFH:
                break;
            case OP_PUTROOTFH:
                break;
            case OP_READ:
                break;
            case OP_READDIR:
                break;
            case OP_READLINK:
                break;
            case OP_REMOVE:
                break;
            case OP_RENAME:
                break;
            case OP_RENEW:
                break;
            case OP_RESTOREFH:
                break;
            case OP_SAVEFH:
                break;
            case OP_SECINFO:
                break;
            case OP_SETATTR:
                break;
            case OP_SETCLIENTID:
                break;
            case OP_SETCLIENTID_CONFIRM:
                break;
            case OP_VERIFY:
                break;
            case OP_WRITE:
                break;
            case OP_RELEASE_LOCKOWNER:
                break;
            case OP_BACKCHANNEL_CTL:
                break;
            case OP_BIND_CONN_TO_SESSION:
                break;
            case OP_EXCHANGE_ID:
                opExchangeId(op, req);
                break;
            case OP_CREATE_SESSION:
                break;
            case OP_DESTROY_SESSION:
                break;
            case OP_FREE_STATEID:
                break;
            case OP_GET_DIR_DELEGATION:
                break;
            case OP_GETDEVICEINFO:
                break;
            case OP_GETDEVICELIST:
                break;
            case OP_LAYOUTCOMMIT:
                break;
            case OP_LAYOUTGET:
                break;
            case OP_LAYOUTRETURN:
                break;
            case OP_SECINFO_NO_NAME:
                break;
            case OP_SEQUENCE:
                break;
            case OP_SET_SSV:
                break;
            case OP_TEST_STATEID:
                break;
            case OP_WANT_DELEGATION:
                break;
            case OP_DESTROY_CLIENTID:
                break;
            case OP_RECLAIM_COMPLETE:
                break;

            default:
                break;
        }
    }
    
    return nullptr;
}

void *nfsproc4_null_4_svc(void *args, struct svc_req *req) {
    printf("in null\n");
    return (char *) "foo";
}
