#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nfsserver.h"
#include "nfsv41.h"

extern "C" {
void nfs4_program_4();
int _rpcsvcdirty = 0;
}

NfsServer *mainNfsServerp;

NfsServer::NfsServer () {
    SVCXPRT *transp;

    pmap_unset(NFS4_PROGRAM, NFS_V4);
    mainNfsServerp = this;

    transp = svcudp_create(RPC_ANYSOCK);
    if (transp == NULL) {
        fprintf(stderr, "Cannot create udp service.\n");
    }
    if (!svc_register(transp, NFS4_PROGRAM, NFS_V4, nfs4_program_4, IPPROTO_UDP)) {
        fprintf(stderr, "Unable to register (NFS4_PROGRAM, NFS_V4, udp).\n");
    }

    transp = svctcp_create(RPC_ANYSOCK, 512*1024, 512*1024);

    if (transp == NULL) {
        fprintf(stderr, "Cannot create tcp service.\n");
    }
    if (!svc_register(transp, NFS4_PROGRAM, NFS_V4, nfs4_program_4, IPPROTO_TCP)) {
        fprintf(stderr, "Unable to register (NFS4_PROGRAM, NFS_V4, tcp).\n");
    }

    _nextClientId = 2000;
    _nextSessionCounter = 3000;      // this is just part of the sessionId

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    _bootTime = tv;
}

void NfsServer::start() {
    printf("Sun RPC Server running...\n");
    svc_run(); 
    printf("svc_run returned\n");
}

/* static */
CB_COMPOUND4res *cb_compound_1_svc(CB_COMPOUND4args *args,
                                   struct svc_req *req) {
    printf("in cb compound\n");
    return nullptr;
}

/* static */
void *cb_null_1_svc(void *args, struct svc_req *req) {
    printf("in cb null\n");
    return nullptr;
}

int32_t
NfsServer::opExchangeId(nfs_argop4 *op, nfs_resop4 *resp, struct svc_req *req) {
    // should implement a hash table
    EXCHANGE_ID4args *arg = &op->nfs_argop4_u.opexchange_id;
    client_owner4 *owner = &arg->eia_clientowner;
    uint32_t flags = arg->eia_flags;
    printf("ownerid='%s' (%d) flags=%x\n",
           owner->co_ownerid.co_ownerid_val,
           owner->co_ownerid.co_ownerid_len,
           flags);

    std::string ownerString = std::string(owner->co_ownerid.co_ownerid_val,
                                          owner->co_ownerid.co_ownerid_len);
    Client *clientp = _clientOwnerMap[ownerString];
    if (clientp == nullptr) {
        clientp = new Client();
        _clientOwnerMap[ownerString] = clientp;
        memcpy(&clientp->_verifier, &owner->co_verifier, sizeof(clientp->_verifier));
        clientp->_clientOwner = ownerString;
    }

    EXCHANGE_ID4res *subResp;
    subResp = &resp->nfs_resop4_u.opexchange_id;

    subResp->eir_status = NFS4_OK;
    EXCHANGE_ID4resok *okResp = &subResp->EXCHANGE_ID4res_u.eir_resok4;

    char *tp;

    // Now allocate a new client ID
    _clientIdMap[_nextClientId] = clientp;
    okResp->eir_clientid = _nextClientId++;

    // according to RFC 8881, the first createSession call will have
    // sequence id equal to the value returned in eir_sequenceid.  Our
    // value in _sessionSequenceId is the last returned Id, so we're
    // expecting the next createSession to have the last returned Id +
    // 1.
    okResp->eir_sequenceid = 1;
    clientp->_sessionSequenceId = okResp->eir_sequenceid - 1;

    okResp->eir_flags = EXCHGID4_FLAG_USE_NON_PNFS;    // supports basic non-PNFS operation
    okResp->eir_state_protect.spr_how = SP4_NONE;
    okResp->eir_server_owner.so_minor_id = 77;
    okResp->eir_server_owner.so_major_id.so_major_id_len = 4;
    okResp->eir_server_owner.so_major_id.so_major_id_val = tp = (char *) malloc(4);
    strncpy(tp, "FUSE", 4);

    okResp->eir_server_scope.eir_server_scope_len = 4;
    okResp->eir_server_scope.eir_server_scope_val = tp = (char *) malloc(4);
    memcpy(tp, "FUSE", 4);

    okResp->eir_server_impl_id.eir_server_impl_id_len = 0;
    okResp->eir_server_impl_id.eir_server_impl_id_val = nullptr;

    return 0;
}

int32_t
NfsServer::opCreateSession(nfs_argop4 *op, nfs_resop4 *resp, struct svc_req *req) {
    CREATE_SESSION4args *arg = &op->nfs_argop4_u.opcreate_session;
    CREATE_SESSION4res *subResp;
    subResp = &resp->nfs_resop4_u.opcreate_session;
    subResp->csr_status = NFS4_OK;
    CREATE_SESSION4resok *okResp = &subResp->CREATE_SESSION4res_u.csr_resok4;

    uint32_t foreSlots;
    uint32_t backSlots;

    Client *clientp = _clientIdMap[arg->csa_clientid];
    if (clientp == nullptr) {
        subResp->csr_status = NFS4ERR_STALE_CLIENTID;
        return -1;
    }

    if (arg->csa_sequence == clientp->_sessionSequenceId) {
        // TODO: replay cache
    } else if (arg->csa_sequence != clientp->_sessionSequenceId + 1) {
        // failure due to bad ordering
        subResp->csr_status = NFS4ERR_SEQ_MISORDERED;
        return -1;
    }

    Session *sessionp = new Session(this, clientp);
    sessionp->_clientp = clientp;
    sessionp->_foreSlots = arg->csa_fore_chan_attrs.ca_maxrequests;
    sessionp->_backSlots = arg->csa_back_chan_attrs.ca_maxrequests;

    foreSlots = arg->csa_fore_chan_attrs.ca_maxrequests;
    if (foreSlots > _maxForeSlots)
        foreSlots = _maxForeSlots;
    sessionp->_backSlots = arg->csa_back_chan_attrs.ca_maxrequests;
    sessionp->_foreSlots = foreSlots;

    memcpy(&okResp->csr_sessionid, &sessionp->_sessionId, sizeof(okResp->csr_sessionid));
    okResp->csr_sequence = ++clientp->_sessionSequenceId;
    okResp->csr_flags = arg->csa_flags;
    sessionp->_csa_flags = arg->csa_flags;
    okResp->csr_fore_chan_attrs = arg->csa_fore_chan_attrs;
    okResp->csr_fore_chan_attrs.ca_maxrequests = foreSlots;
    okResp->csr_back_chan_attrs = arg->csa_back_chan_attrs;
    printf("created session with flags %x\n", arg->csa_flags);

    return 0;
}

NfsServer::Session::Session(NfsServer *serverp, Client *clientp) {
    _clientp = clientp;

    // init a new session ID
    memcpy(&_sessionId, &serverp->_bootTime, 8);
    memcpy(((char *) &_sessionId)+8, &serverp->_nextSessionCounter, 8);
    serverp->_nextSessionCounter++;
}

/* static */
COMPOUND4res *nfsproc4_compound_4_svc(COMPOUND4args *args,
                                      struct svc_req *req) {
    printf("in compound with ct=%d\n", args->argarray.argarray_len);
    nfs_argop4 *op = args->argarray.argarray_val;
    uint32_t tlen;
    char *tp;

    NfsServer *serverp = mainNfsServerp;

    nfs_resop4 *resp;   // an individual response

    COMPOUND4res *resps = (COMPOUND4res *)malloc(sizeof(*resps));
    memset(resps, 0, sizeof(*resps));
    resps->status = NFS4_OK;
    resps->tag.utf8string_len = tlen = args->tag.utf8string_len;
    resps->tag.utf8string_val = tp = (char *) malloc(tlen);
    memcpy(tp, args->tag.utf8string_val, tlen);

    // allocate and zero the response array
    resps->resarray.resarray_len = tlen = args->argarray.argarray_len;
    resp = resps->resarray.resarray_val = (nfs_resop4 *) malloc(tlen * sizeof(nfs_resop4));
    memset(resps->resarray.resarray_val, 0, tlen * sizeof(nfs_resop4));

    for(uint32_t i=0;i<args->argarray.argarray_len; i++, op++, resp++) {
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
                serverp->opExchangeId(op, resp, req);
                break;
            case OP_CREATE_SESSION:
                serverp->opCreateSession(op, resp, req);
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
        resp->resop = op->argop;
    }
    
    return resps;
}

/* static */
void *nfsproc4_null_4_svc(void *args, struct svc_req *req) {
    printf("in null\n");
    return (char *) "foo";
}
