#include <string>
#include <memory>

#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>

#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nfsserver.h"
#include "nfsv41.h"
#include "vn.h"
#include "vntest.h"

extern "C" {
void nfs4_program_4();
int _rpcsvcdirty = 0;
}

NfsServer *mainNfsServerp;

NfsServer::NfsServer (Vfs *vfsp) {
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

    _vfsp = vfsp;
}

void NfsServer::start() {
    printf("Sun RPC Server running...\n");
    _vfsp->root(&_vfsRootp);
    _vfsp->getFsAttr(&_fsAttrs);
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

std::string
NfsServer::translate_uid(uint32_t uid) {
    // TODO: use C library
    return std::string("root");
}

std::string
NfsServer::translate_gid(uint32_t gid) {
    // TODO: use C library
    return std::string("root");
}

// copy out file attributes into an fattr4
void
NfsServer::copyToFattr( fattr4 *outp,
                        uint32_t *inMaskp,
                        Vn *vp,
                        Vn::Attr *attrsp,
                        Vfs::FsAttr *fsAttrsp) {
    static const uint32_t attrListBytes = 2048;
    uint32_t i;
    uint32_t ltemp;
    uint64_t lltemp;
    uint32_t *outMaskp;
    uint32_t *supMaskp; // where we generate the supported mask, if requested
    uint32_t dummySup[_attrMaskWords];
    ////////////////////////////////////////////////////////////////
    // CHANGE THIS COMMENT IF WE SUPPORT MORE ATTRIBUTES!
    ////////////////////////////////////////////////////////////////
    // attrListDatap is supposed to contain the XDR data for each
    // marshalled field, all concatenated.  We summarize the space used
    // for all the fields we might output:
    //  0:  4 words for attrMask
    //  1:  1 word type bits
    //  2:  1 word persist handle
    //  3:  1 word change data
    //  4:  2 words size
    //  5:  1 word link-p
    //  6:  1 word symlink-p
    //  7:  1 word namedattr-p
    //  8:  4 words fsdev
    //  9:  1 word unique handles-p
    // 10:  1 word lease time
    // 11:  1 word
    // 15:  1 word can set time-p
    // 16:  1 word case insensitive-p
    // 17:  1 word case preserving-p
    // 18:  1 word chown restricted-p
    // 19: *1 word plus rounded FH len
    // 20:  2 words fileid
    // 27:  2 words max file size
    // 28:  1 word max links
    // 29:  1 word max name size
    // 30:  1 word max read bytes
    // 31:  1 word max write bytes
    // 33:  1 word unix mode bits
    // 35:  1 word nlinks
    // 36: *1 word plus user name
    // 37: *1 word plus group name
    // 47:  2 words atime
    // 53:  2 words mtime
    // 76:  1 words fs char set
    // (*) means add in length of opaque data.
    // TOTAL: 41 words + rounded_lengthOf{fh,user,group}

    // Setup output bytes for fattr.attr_vals
    char *ap;
    char *attrListDatap = (char *) malloc(attrListBytes);
    memset(attrListDatap, 0, attrListBytes);
    ap = attrListDatap;       // for appending

    // Setup output bitmap in outMaskp, where we'll collect the bits
    // to return.  It'll be a subset of the bits requested by the
    // caller in inMaskp.
    uint32_t *bitmapDatap = (uint32_t *) malloc(12);
    memset(bitmapDatap, 0, 12);
    outp->attrmask.bitmap4_len = 3;     // 3 words
    outp->attrmask.bitmap4_val = bitmapDatap;
    outMaskp = bitmapDatap;

    // generate supported attributes
    if (inMaskp[0] & (1<<0)) {
        outMaskp[0] |= (1<<0);
        ltemp = htonl(_attrMaskWords);
        memcpy(ap, &ltemp, sizeof(ltemp));

        // Remember where to patch the returned attributes
        ap += sizeof(ltemp);
        supMaskp = (uint32_t *) ap;
        ap += _attrMaskWords * sizeof(uint32_t);
        supMaskp[0] |= (1<<0);  // do it here since supMask is set above
    } else {
        // setup a dummy supMask; easier than checking each time if
        // supMaskp is null
        supMaskp = dummySup;
    }

    // file type
    supMaskp[0] |= (1<<1);
    if (inMaskp[0] & (1<<1)) {
        outMaskp[0] |= (1<<1);
        if (attrsp->ftype == Vn::VN_DIR)
            ltemp = htonl(NF4DIR);
        else if (attrsp->ftype == Vn::VN_REG)
            ltemp = htonl(NF4REG);
        else if (attrsp->ftype == Vn::VN_SYMLINK)
            ltemp = htonl(NF4LNK);
        else
            ltemp = htonl(NF4REG);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
    }

    // file handle expiration type: persistent file handles
    supMaskp[0] |= (1<<2);
    if (inMaskp[0] & (1<<2)) {
        outMaskp[0] |= (1<<2);
        ltemp = htonl(FH4_PERSISTENT);
        memcpy(ap, &ltemp, sizeof(uint32_t));
        ap += sizeof(ltemp);
    }

    // change field -- we use ctime-derived
    supMaskp[0] |= (1<<3);
    if (inMaskp[0] & (1<<3)) {
        outMaskp[0] |= (1<<3);
        lltemp = (attrsp->ctime.secs << 32) | attrsp->ctime.nsecs;
        lltemp = htonll(lltemp);
        memcpy(ap, &lltemp, sizeof(uint64_t));
        ap += sizeof(uint64_t);
    }

    // file size
    supMaskp[0] |= (1<<4);
    if (inMaskp[0] & (1<<4)) {
        outMaskp[0] |= (1<<4);
        lltemp = htonll(attrsp->size);
        memcpy(ap, &lltemp, sizeof(uint64_t));
        ap += sizeof(lltemp);
    }

    // is link supported
    supMaskp[0] |= (1<<5);
    if (inMaskp[0] & (1<<5)) {
        outMaskp[0] |= (1<<5);
        ltemp = htonl(1);
        memcpy(ap, &ltemp, sizeof(uint32_t));
        ap += sizeof(uint32_t);
    }

    // is symlink supported
    supMaskp[0] |= (1<<6);
    if (inMaskp[0] & (1<<6)) {
        outMaskp[0] |= (1<<6);
        ltemp = htonl(1);
        memcpy(ap, &ltemp, sizeof(uint32_t));
        ap += sizeof(uint32_t);
    }

    // is named_attr supported
    supMaskp[0] |= (1<<7);
    if (inMaskp[0] & (1<<7)) {
        outMaskp[0] |= (1<<7);
        ltemp = htonl(0);
        memcpy(ap, &ltemp, sizeof(uint32_t));
        ap += sizeof(uint32_t);
    }

    // fsid
    supMaskp[0] |= (1<<8);
    if (inMaskp[0] & (1<<8)) {
        outMaskp[0] |= (1<<8);
        lltemp = htonll(attrsp->fsid.major);
        memcpy(ap, &lltemp, sizeof(lltemp));
        ap += sizeof(lltemp);
        lltemp = htonll(attrsp->fsid.minor);
        memcpy(ap, &lltemp, sizeof(lltemp));
        ap += sizeof(lltemp);
    }

    // unique handles
    supMaskp[0] |= (1<<9);
    if (inMaskp[0] & (1<<9)) {
        outMaskp[0] |= (1<<9);
        ltemp = htonl(1);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
    }

    // lease time in seconds
    supMaskp[0] |= (1<<10);
    if (inMaskp[0] & (1<<10)) {
        outMaskp[0] |= (1<<10);
        ltemp = htonl(fsAttrsp->leaseSecs);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
    }

    // rdattr error
    supMaskp[0] |= (1<<11);
    if (inMaskp[0] & (1<<11)) {
        outMaskp[0] |= (1<<11);
        ltemp = htonl(0);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
    }

    // can set time
    supMaskp[0] |= (1<<15);
    if (inMaskp[0] & (1<<15)) {
        outMaskp[0] |= (1<<15);
        ltemp = htonl(1);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
    }

    // case insensitive
    supMaskp[0] |= (1<<16);
    if (inMaskp[0] & (1<<16)) {
        outMaskp[0] |= (1<<16);
        ltemp = htonl(fsAttrsp->caseInsensitive);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
    }

    // case preserving
    supMaskp[0] |= (1<<17);
    if (inMaskp[0] & (1<<17)) {
        outMaskp[0] |= (1<<17);
        ltemp = htonl(1);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
    }

    // chown restricted is true
    supMaskp[0] |= (1<<18);
    if (inMaskp[0] & (1<<18)) {
        outMaskp[0] |= (1<<18);
        ltemp = htonl(1);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
    }

    // fh
    supMaskp[0] |= (1<<19);
    if (inMaskp[0] & (1<<19)) {
        outMaskp[0] |= (1<<19);
        ltemp = htonl(vp->_fh.length());
        memcpy(ap, &ltemp, sizeof(ltemp));
        ltemp = vp->_fh.length();       // back to host order
        ap += sizeof(ltemp);
        memcpy(ap, vp->_fh.data(), ltemp);
        ap += ((ltemp - 1) | 3) + 1;       // padded length
    }

    // file ID field
    supMaskp[0] |= (1<<20);
    if (inMaskp[0] & (1<<20)) {
        outMaskp[0] |= (1<<20);
        lltemp = htonll(attrsp->fileid);
        memcpy(ap, &lltemp, sizeof(lltemp));
        ap += sizeof(lltemp);
    }

    // max file size (27)
    supMaskp[0] |= (1<<27);
    if (inMaskp[0] & (1<<27)) {
        outMaskp[0] |= (1<<27);
        lltemp = htonll(fsAttrsp->maxFileSize);
        memcpy(ap, &lltemp, sizeof(lltemp));
        ap += sizeof(lltemp);
    }

    // max link count(28)
    supMaskp[0] |= (1<<28);
    if (inMaskp[0] & (1<<28)) {
        outMaskp[0] |= (1<<28);
        ltemp = htonl(fsAttrsp->maxLink);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
    }

    // max name size (29)
    supMaskp[0] |= (1<<29);
    if (inMaskp[0] & (1<<29)) {
        outMaskp[0] |= (1<<29);
        ltemp = htonl(fsAttrsp->maxName);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
    }

    // max read bytes (30)
    supMaskp[0] |= (1<<30);
    if (inMaskp[0] & (1<<30)) {
        outMaskp[0] |= (1<<30);
        lltemp = htonll(fsAttrsp->maxReadBytes);
        memcpy(ap, &lltemp, sizeof(lltemp));
        ap += sizeof(lltemp);
    }

    // max write bytes (31)
    supMaskp[0] |= (1<<31);
    if (inMaskp[0] & (1<<31)) {
        outMaskp[0] |= (1<<31);
        lltemp = htonll(fsAttrsp->maxWriteBytes);
        memcpy(ap, &lltemp, sizeof(lltemp));
        ap += sizeof(lltemp);
    }

    // mode bits (33)
    supMaskp[1] |= (1<<(33-32));
    if (inMaskp[1] & (1<<(33-32))) {
        outMaskp[1] |= (1<<(33-32));
        ltemp = htonl(attrsp->mode);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
    }

    // num links bits (35)
    supMaskp[1] |= (1<<(35-32));
    if (inMaskp[1] & (1<<(35-32))) {
        outMaskp[1] |= (1<<(35-32));
        ltemp = htonl(attrsp->nlinks);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
    }

    supMaskp[1] |= (1<<(36-32));
    if (inMaskp[1] & (1<<(36-32))) {
        outMaskp[1] |= (1<<(36-32));
        std::string tname = translate_uid(attrsp->uid);
        ltemp = htonl(tname.length());
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
        ltemp = tname.length(); // host order
        memcpy(ap, tname.data(), ltemp);
        ltemp = ((ltemp - 1) | 3) + 1;
        ap += ltemp;
    }

    supMaskp[1] |= (1<<(37-32));
    if (inMaskp[1] & (1<<(37-32))) {
        outMaskp[1] |= (1<<(37-32));
        std::string tname = translate_gid(attrsp->gid);
        ltemp = htonl(tname.length());
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
        ltemp = tname.length();
        memcpy(ap, tname.data(), ltemp);
        ltemp = ((ltemp - 1) | 3) + 1;
        ap += ltemp;
    }

    // access time (47)
    supMaskp[1] |= (1<<(47-32));
    if (inMaskp[1] & (1<<(47-32))) {
        outMaskp[1] |= (1<<(47-32));
        lltemp = htonll(attrsp->atime.secs);
        memcpy(ap, &lltemp, sizeof(lltemp));
        ap += sizeof(lltemp);
        ltemp = htonl(attrsp->atime.nsecs);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
    }

    // mod time (53)
    supMaskp[1] |= (1<<(53-32));
    if (inMaskp[1] & (1<<(53-32))) {
        outMaskp[1] |= (1<<(53-32));
        lltemp = htonll(attrsp->mtime.secs);
        memcpy(ap, &lltemp, sizeof(lltemp));
        ap += sizeof(lltemp);
        ltemp = htonl(attrsp->mtime.nsecs);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
    }

    // fs charset cap should be 2 -- utf8 supported
    supMaskp[2] |= (1<<(76-64));
    if (inMaskp[2] & (1<<(76-64))) {
        outMaskp[2] |= (1<<(76-64));
        ltemp = htonl(2);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
    }

    // fill in fattr fields
    outp->attr_vals.attrlist4_len = (ap-attrListDatap);
    assert(outp->attr_vals.attrlist4_len <= attrListBytes);
    outp->attr_vals.attrlist4_val = attrListDatap;
}

int32_t
NfsServer::opGetAttr(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    GETATTR4args *arg = &op->nfs_argop4_u.opgetattr;
    GETATTR4res *subResp;
    subResp = &resp->nfs_resop4_u.opgetattr;
    subResp->status = NFS4_OK;
    GETATTR4resok *okResp = &subResp->GETATTR4res_u.resok4;

    int32_t code;
    uint32_t i;
    uint32_t inMask[_attrMaskWords];
    for(i=0;i<_attrMaskWords;i++) {
        if (i < arg->attr_request.bitmap4_len)
            inMask[i] = arg->attr_request.bitmap4_val[i];
        else
            inMask[i] = 0;
    }

    std::string currentFh = std::string(compStatep->_currentFh.nfs_fh4_val,
                                        compStatep->_currentFh.nfs_fh4_len);
    Vn *vp;
    Vn::Attr attrs;
    _vfsp->vget(currentFh, &vp);

    code = vp->getAttr(&attrs);
    vp->release();
    if (code != 0) {
        subResp->status = NFS4ERR_INVAL;
        return NFS4ERR_INVAL;
    }

    copyToFattr(&okResp->obj_attributes, inMask, vp, &attrs, &_fsAttrs);

    printf("getattr attrlen=%d %x.%x.%x.%x\n",
           arg->attr_request.bitmap4_len,
           arg->attr_request.bitmap4_val[0],
           arg->attr_request.bitmap4_val[1],
           arg->attr_request.bitmap4_val[2],
           arg->attr_request.bitmap4_val[3]);

    return 0;
}

int32_t
NfsServer::opExchangeId(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
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
NfsServer::opReclaimComplete(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    RECLAIM_COMPLETE4args *arg = &op->nfs_argop4_u.opreclaim_complete;
    RECLAIM_COMPLETE4res *subResp;
    subResp = &resp->nfs_resop4_u.opreclaim_complete;
    subResp->rcr_status = NFS4_OK;
    return 0;
}

int32_t
NfsServer::opSequence(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    SEQUENCE4args *arg = &op->nfs_argop4_u.opsequence;
    SEQUENCE4res *subResp;
    subResp = &resp->nfs_resop4_u.opsequence;
    subResp->sr_status = NFS4_OK;
    SEQUENCE4resok *okResp = &subResp->SEQUENCE4res_u.sr_resok4;

    // This is better in C++20, but let's aim for the previous decade.
    StdSessionId4 sessionKey;
    memcpy(sessionKey.data(), arg->sa_sessionid, NFS4_SESSIONID_SIZE);

    Session *sessionp = _sessionMap[sessionKey];
    if (sessionp == nullptr) {
        subResp->sr_status = NFS4ERR_SEQ_MISORDERED;
        return -1;
    }

    uint32_t nextCallSequence = sessionp->_nextCallSequence[arg->sa_slotid];
    if (nextCallSequence == arg->sa_sequenceid) {
        sessionp->_nextCallSequence[arg->sa_slotid]++;
        memcpy(&okResp->sr_sessionid, &arg->sa_sessionid, sizeof(sessionid4));
        okResp->sr_sequenceid = arg->sa_sequenceid;
        okResp->sr_slotid = arg->sa_slotid;
        okResp->sr_highest_slotid = _maxForeSlots;
        okResp->sr_target_highest_slotid = _maxForeSlots;
        okResp->sr_status_flags = 0;
    } else if (arg->sa_sequenceid == nextCallSequence-1) {
        // TODO: retransmission -- figure out how to resend a
        // response.
    } else {
        // bad sequence
        subResp->sr_status = NFS4ERR_SEQ_MISORDERED;
        return NFS4ERR_SEQ_MISORDERED;
    }

    return 0;
}

int32_t
NfsServer::opCreateSession(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
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

int32_t
NfsServer::opPutRootFh(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    char *tp;
    uint32_t length;

    compStatep->_currentFh.nfs_fh4_len = length = _vfsRootp->_fh.length();
    compStatep->_currentFh.nfs_fh4_val = tp = (char *) malloc(4);
    memcpy(tp, _vfsRootp->_fh.data(), length);

    PUTROOTFH4res *subResp;

    subResp = &resp->nfs_resop4_u.opputrootfh;
    subResp->status = NFS4_OK;

    return 0;
}

NfsServer::Session::Session(NfsServer *serverp, Client *clientp) {
    _clientp = clientp;

    // init a new session ID
    StdSessionId4 stdSession;
    char *tp = stdSession.data();

    memcpy(tp, &serverp->_bootTime, 8);
    memcpy(tp+8, &serverp->_nextSessionCounter, 8);
    serverp->_nextSessionCounter++;
    _sessionId = stdSession;
    serverp->_sessionMap[stdSession] = this;

    uint32_t i;
    for(i=0;i<_maxForeSlots;i++) {
        _nextCallSequence[i] = 1;
    }
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

    // and state for the operation
    NfsServer::CompoundState compoundState;

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
                serverp->opGetAttr(op, resp, &compoundState);
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
                serverp->opPutRootFh(op, resp, &compoundState);
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
                serverp->opExchangeId(op, resp, &compoundState);
                break;
            case OP_CREATE_SESSION:
                serverp->opCreateSession(op, resp, &compoundState);
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
                serverp->opSequence(op, resp, &compoundState);
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
                serverp->opReclaimComplete(op, resp, &compoundState);
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
