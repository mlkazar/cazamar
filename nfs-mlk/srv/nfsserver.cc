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
    return std::string("staff");
}

uint32_t
NfsServer::translate_to_uid(std::string uname) {
    // TODO: use C library
    return 0;
}

uint32_t
NfsServer::translate_to_gid(std::string gname) {
    // TODO: use C library
    return 20;  // staff
}

NfsServer::StateId *
NfsServer::allocStateId(Client *clientp) {
    StateId *sp = new StateId();
    generateStateId(clientp, sp->_stateId);
    sp->_isWrite = false;

    clientp->_allStateIds.push_back(sp);

    return sp;
}

NfsServer::StateId *
NfsServer::findStateId(Client *clientp, stateid4 *statep) {
    std::list<StateId *>::iterator it;
    StateId *sp;

    for(it = clientp->_allStateIds.begin(); it != clientp->_allStateIds.end(); it++) {
        sp = *it;
        if (memcmp(&sp->_stateId, statep, sizeof(*statep)) == 0)
            return sp;
    }

    return nullptr;
}

// free internal pointers in an fattr4 structure
void
NfsServer::freeFattr4(fattr4 *ap) {
    free(ap->attr_vals.attrlist4_val);
    free(ap->attrmask.bitmap4_val);
}

// free internal pointers in an entry4 structure
void
NfsServer::freeEntry4(entry4 *ep) {
    free(ep->name.utf8string_val);
    freeFattr4(&ep->attrs);
}

void
NfsServer::stringToNfs4Fh(std::string instr, nfs_fh4 *fhp) {
    uint32_t tlen = instr.length();
    fhp->nfs_fh4_len = tlen;
    fhp->nfs_fh4_val = (char *) malloc(tlen);
    memcpy(fhp->nfs_fh4_val, instr.data(), tlen);
}

void
NfsServer::generateStateId(Client *clientp, stateid4 &stateId) {
    uint64_t newStateCounter = clientp->_stateIdGenerator++;
    stateId.seqid = newStateCounter;
    memset(stateId.other, 0, sizeof(stateId.other));
    memcpy(stateId.other, &newStateCounter, sizeof(newStateCounter));
}

// returns -1 if there are entries present that we don't know how to
// parse.
int32_t
NfsServer::vnAttrsFromNfsAttrs(Vn::SetAttr *vnAttrsp, fattr4 *nfsAttrsp) {
    uint32_t ltemp;
    uint64_t lltemp;
    std::string name;

    vnAttrsp->setMask = 0;
    if (nfsAttrsp == nullptr) {
        return 0;
    }

    // We have to process all the elements, even the ones we don't
    // care about, since there's no length field in the bitmap4
    // entries.
    char *datap = nfsAttrsp->attr_vals.attrlist4_val;
    uint32_t bytesLeft = nfsAttrsp->attr_vals.attrlist4_len;

    for(uint32_t i=0;i<nfsAttrsp->attrmask.bitmap4_len;i++) {
        uint32_t baseIx = 32*i;
        uint32_t mask = nfsAttrsp->attrmask.bitmap4_val[i];
        for(uint32_t j=0;j<32;j++) {
            uint32_t itemIx = baseIx + j;
            if (mask & (1<<j)) {
                // itemIx is present in the data
                switch (itemIx) {
                    case FATTR4_CHANGE:
                        datap += sizeof(uint64_t);

                    case FATTR4_SIZE:
                        memcpy(&lltemp, datap, sizeof(lltemp));
                        datap += sizeof(lltemp);
                        vnAttrsp->attrs.size = htonll(lltemp);
                        vnAttrsp->setMask |= Vn::SetAttr::setSize;
                        break;

                    case FATTR4_NAMED_ATTR:
                        datap += sizeof(uint32_t);

                    case FATTR4_MODE:
                        memcpy(&ltemp, datap, sizeof(ltemp));
                        datap += sizeof(ltemp);
                        vnAttrsp->attrs.mode = htonl(ltemp);
                        vnAttrsp->setMask |= Vn::SetAttr::setMode;
                        break;

                    case FATTR4_OWNER:
                        memcpy(&ltemp, datap, sizeof(ltemp));
                        ltemp = htonl(ltemp);
                        datap += sizeof(ltemp);
                        name = std::string(datap, ltemp);
                        datap += ltemp;
                        vnAttrsp->attrs.uid = translate_to_uid(name);
                        vnAttrsp->setMask |= Vn::SetAttr::setUid;
                        break;

                    case FATTR4_OWNER_GROUP:
                        memcpy(&ltemp, datap, sizeof(ltemp));
                        ltemp = htonl(ltemp);
                        datap += sizeof(ltemp);
                        name = std::string(datap, ltemp);
                        datap += ltemp;
                        vnAttrsp->attrs.gid = translate_to_gid(name);
                        vnAttrsp->setMask |= Vn::SetAttr::setGid;
                        break;

                    case FATTR4_TIME_ACCESS:
                        memcpy(&lltemp, datap, sizeof(lltemp));
                        lltemp = htonll(lltemp);        // secs
                        datap += sizeof(lltemp);
                        memcpy(&ltemp, datap, sizeof(ltemp));
                        ltemp = htonl(ltemp);         // nsecs
                        datap += sizeof(ltemp);
                        vnAttrsp->attrs.atime.secs = lltemp;
                        vnAttrsp->attrs.atime.nsecs = ltemp;
                        vnAttrsp->setMask |= Vn::SetAttr::setATime;
                        break;

                    case FATTR4_TIME_MODIFY:
                        memcpy(&lltemp, datap, sizeof(lltemp));
                        lltemp = htonll(lltemp);        // secs
                        datap += sizeof(lltemp);
                        memcpy(&ltemp, datap, sizeof(ltemp));
                        ltemp = htonl(ltemp);         // nsecs
                        datap += sizeof(ltemp);
                        vnAttrsp->attrs.mtime.secs = lltemp;
                        vnAttrsp->attrs.mtime.nsecs = ltemp;
                        vnAttrsp->setMask |= Vn::SetAttr::setMTime;
                        break;

                    case FATTR4_TIME_BACKUP:
                        datap += sizeof(lltemp) + sizeof(ltemp);
                        break;

                    case FATTR4_TIME_CREATE:
                        datap += sizeof(lltemp) + sizeof(ltemp);
                        break;

                    default:
                        return -1;
                }       // switch
            } // if (mask...)
        } // loop over all bits
    } // loop over all words

    return 0;
}

nfsstat4
NfsServer::opOpen(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    OPEN4args *arg = &op->nfs_argop4_u.opopen;
    OPEN4res *subResp;
    Vn *childVp;
    Client *clientp;
    Vn::Attr dirAttrs;
    changeid4 preChange;
    changeid4 postChange;
    uint32_t *sp;

    subResp = &resp->nfs_resop4_u.opopen;
    subResp->status = NFS4_OK;
    clientp = compStatep->_sessionp->_clientp;
    OPEN4resok *okResp = &subResp->OPEN4res_u.resok4;

    Vn *dirVp;
    Vn::Attr attrs;
    int32_t code;
    code = _vfsp->vget(compStatep->_currentFh, &dirVp);
    if (code) {
        subResp->status = NFS4ERR_STALE;
        printf("nfsserver::open failed with estale\n");
        return NFS4ERR_STALE;
    }

    dirVp->getAttr(&dirAttrs);
    preChange = changeOf(&dirAttrs);

    int doCreate = (arg->openhow.opentype == OPEN4_CREATE);

    if (arg->claim.claim == CLAIM_NULL) {
        std::string name = std::string(arg->claim.open_claim4_u.file.utf8string_val,
                                       arg->claim.open_claim4_u.file.utf8string_len);

        fattr4 *fattrToUse = nullptr;
        if (doCreate) {
            if ( arg->openhow.openflag4_u.how.mode == UNCHECKED4 ||
                 arg->openhow.openflag4_u.how.mode == GUARDED4) {
                fattrToUse = &arg->openhow.openflag4_u.how.createhow4_u.createattrs;
            } else if (arg->openhow.openflag4_u.how.mode == EXCLUSIVE4_1) {
                fattrToUse = &arg->openhow.openflag4_u.how.createhow4_u.ch_createboth.cva_attrs;
            } else {
                printf("nfsserver: unrecognized openhow %d\n",
                       arg->openhow.openflag4_u.how.mode);
            }

            Vn::SetAttr setAttrs;
            vnAttrsFromNfsAttrs(&setAttrs, fattrToUse);
            code = dirVp->create(name, &setAttrs, &childVp);
        } else {
            code = dirVp->lookup(name, &childVp);
        }
    } else if (arg->claim.claim == CLAIM_FH)  {
        // use current file handle
        code = _vfsp->vget(compStatep->_currentFh, &childVp);
        if (code != 0) {
            subResp->status = NFS4ERR_STALE;
            return NFS4ERR_STALE;
        }
    }

    if (code) {
        subResp->status = NFS4ERR_INVAL;
        return NFS4ERR_INVAL;
    }

    // wrap the filehandle in a stateid4, and fill in the rest of the
    // response.
    StateId *statep = allocStateId(clientp);
    statep->_vnp = childVp;

    dirVp->getAttr(&dirAttrs);
    postChange = changeOf(&dirAttrs);

    memcpy(&okResp->stateid, &statep->_stateId, sizeof(stateid4));
    okResp->cinfo.atomic = false;       // change to true when we do our locking properly
    okResp->cinfo.before = preChange;
    okResp->cinfo.after = postChange;

    okResp->rflags = (OPEN4_RESULT_CONFIRM | OPEN4_RESULT_LOCKTYPE_POSIX);

    // fill in the attributes for exclusive creates.  Don't know if it
    // is legal to pass this back.
    okResp->attrset.bitmap4_len = 0;
    okResp->attrset.bitmap4_val = nullptr;

    okResp->delegation.delegation_type = OPEN_DELEGATE_NONE;

    subResp->status = NFS4_OK;
    return NFS4_OK;
}

uint64_t
NfsServer::changeOf(Vn::Attr *attrp) {
    return (((uint64_t)attrp->ctime.secs) << 32) + attrp->ctime.nsecs;
}

nfsstat4
NfsServer::opLookup(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    LOOKUP4args *arg = &op->nfs_argop4_u.oplookup;
    LOOKUP4res *subResp;
    subResp = &resp->nfs_resop4_u.oplookup;
    subResp->status = NFS4_OK;

    Vn *vp;
    Vn::Attr attrs;
    int32_t code;
    code = _vfsp->vget(compStatep->_currentFh, &vp);
    if (code) {
        subResp->status = NFS4ERR_STALE;
        printf("nfsserver::lookup failed with estale\n");
        return NFS4ERR_STALE;
    }

    VnTest *childVp;

    std::string name = std::string(arg->objname.utf8string_val,
                                   arg->objname.utf8string_len);
    code = vp->lookup(name, (Vn **)(&childVp));
    if (code) {
        subResp->status = NFS4ERR_NOENT;
        printf("nfsserver::lookup name lookup failed for '%s'\n", name.c_str());
        return NFS4ERR_NOENT;
    }

    compStatep->_currentFh = childVp->getFh();

    printf("nfsserver::lookup name lookup succeeds for '%s'\n", name.c_str());

    childVp->release();
    vp->release();

    return NFS4_OK;
}


// copy out file attributes into an fattr4.  Return # of bytes copied.
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
        memset(ap, 0, _attrMaskWords * sizeof(uint32_t));
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
        std::string fh = vp->getFh();
        outMaskp[0] |= (1<<19);
        ltemp = htonl(fh.length());
        memcpy(ap, &ltemp, sizeof(ltemp));
        ltemp = fh.length();       // back to host order
        ap += sizeof(ltemp);
        memcpy(ap, fh.data(), ltemp);
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

    // hidden boolean
    supMaskp[0] |= (1<<25);
    if (inMaskp[0] & (1<<25)) {
        outMaskp[0] |= (1<<25);
        ltemp = htonl(0);
        memcpy(ap, &ltemp, sizeof(ltemp));
        ap += sizeof(ltemp);
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

    // User name
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

    // Group name
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

nfsstat4
NfsServer::opReaddir(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    READDIR4args *arg = &op->nfs_argop4_u.opreaddir;
    READDIR4res *subResp;
    subResp = &resp->nfs_resop4_u.opreaddir;
    subResp->status = NFS4_OK;
    READDIR4resok *okResp = &subResp->READDIR4res_u.resok4;
    int32_t code;

    Vn *vp;
    Vn::Attr attrs;
    _vfsp->vget(compStatep->_currentFh, &vp);

    // copy out some entries.  Dircount holds the name and cookie
    // value sizes, after xdr'ing.  So, the name has a 4 byte count
    // and the name bytes rounded up to next 4 bytes, and the cookie
    // value is a fixed 8 bytes.
    //
    // maxBytesLeft has the total bytes in xdr format for the entire
    // response, so it contains 8 bytes of cookie verifier, 4 bytes of
    // EOF flag, the dircount bytes, the per entry attrs and the
    // '*next' field, which is really just an embedded 4 byte boolean
    // value preceding the entry.  There is an extra 4 bytes for the
    // flag indicating that there are no more entries in the linked
    // list.
    uint32_t maxDirBytes = arg->dircount;
    uint32_t maxTotalBytes = arg->maxcount;
    uint32_t usedDirBytes = 0;
    uint32_t usedTotalBytes = 0;
    uint32_t tlen;
    Vn::DirEnt dirEntry;
    entry4 *ep;
    Vn::Attr entryAttrs;
    uint32_t inMaskAll[3];
    entry4 **lastEpp;
    bool atEof;
    VnTest *evp;
    uint64_t nextCookie;

    inMaskAll[0] = inMaskAll[1] = inMaskAll[2] = ~0;

    atEof = false;
    lastEpp = &okResp->reply.entries;
    nextCookie = arg->cookie;
    while (true) {
        // readdir's dirEntry contains the cookie of the next entry to
        // return.
        code = vp->readdir(nextCookie, &dirEntry);
        if (code != 0) {
            code = 0;
            break;
        }
        if (dirEntry._name.length() == 0) {
            // EOF
            code = 0;
            atEof = true;
            break;
        }

        code = vp->lookup(dirEntry._name, (Vn **) &evp);
        if (code != 0)
            break;

        code = evp->getAttr(&entryAttrs);
        if (code)
            break;

        ep = (entry4 *) malloc(sizeof(*ep));
        memset(ep, 0, sizeof(*ep));
        ep->cookie = nextCookie = dirEntry._cookie;
        ep->name.utf8string_len = tlen = dirEntry._name.length();
        ep->name.utf8string_val = (char *) malloc(tlen);
        memcpy(ep->name.utf8string_val, dirEntry._name.data(), tlen);
        copyToFattr(&ep->attrs,
                    inMaskAll,
                    evp,
                    &entryAttrs,
                    &_fsAttrs);

        // adjust usages conservatively.  XDR count of entry is XDR of
        // 8 byte cookie, plus XDR of length field (4 bytes) ) and XDR
        // of name bytes padded to multiple of 4.  We just add 4 bytes
        // instead of rounding, which is generous.
        uint32_t entryDirBytes = sizeof(nfs_cookie4) + 4 + dirEntry._name.length() + 4;
        usedDirBytes += entryDirBytes;

        // XDR of whole entry: the directory info (name and cookie), the
        // fattr4 (4 bytes of length, and then those bytes padded by 4)
        // and bitmap4 (4 bytes of vector length, then 4*vector count)
        usedTotalBytes += (entryDirBytes + ep->attrs.attr_vals.attrlist4_len + 8 +
                           ep->attrs.attrmask.bitmap4_len * 4 + 4);

        if (usedDirBytes > maxDirBytes || usedTotalBytes > maxTotalBytes) {
            freeEntry4(ep);     // only frees internals
            free(ep);
            break;
        } else {
            *lastEpp = ep;
            lastEpp = &ep->nextentry;
        }
    } // while loop

    if (code == 0) {
        memcpy(&okResp->cookieverf, _vfsp->getCookieVerifier().data(), NFS4_VERIFIER_SIZE);
        okResp->reply.eof = atEof;
    }

    if (code == 0) {
        subResp->status = NFS4_OK;
        return NFS4_OK;
    } else {
        subResp->status = NFS4ERR_INVAL;
        return NFS4ERR_INVAL;
    }
}

nfsstat4
NfsServer::opRead(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    READ4args *arg = &op->nfs_argop4_u.opread;
    READ4res *subResp;
    subResp = &resp->nfs_resop4_u.opread;
    subResp->status = NFS4_OK;
    READ4resok *okResp = &subResp->READ4res_u.resok4;
    StateId *idp;
    char *datap;
    Vn::Attr attrs;

    Client *clientp = compStatep->_sessionp->_clientp;
    idp = findStateId(clientp, &arg->stateid);

    if (!idp) {
        subResp->status = NFS4ERR_STALE_STATEID;
        return NFS4ERR_STALE_STATEID;
    }

    Vn *vp = idp->_vnp;

    int32_t code = vp->getAttr(&attrs);
    uint64_t fileSize = attrs.size;

    uint32_t allocBytes = (fileSize < arg->count? fileSize : arg->count);

    datap = (char *) malloc(allocBytes);
    code = vp->read(datap, arg->offset, arg->count);
    if (code < 0) {
        free(datap);
        subResp->status = NFS4ERR_IO;
        return NFS4ERR_IO;
    }

    okResp->data.data_len = code;
    if (code > 0) {
        okResp->data.data_val = datap;
    } else {
        okResp->data.data_val = nullptr;
        free(datap);
    }
    okResp->eof = (arg->offset + arg->count >= fileSize);

    printf("nfsserver::read vp=%p %d bytes at %0llx eof=%d\n",
           vp, code, arg->offset, okResp->eof);
    return NFS4_OK;
}

nfsstat4
NfsServer::opClose(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    CLOSE4args *arg = &op->nfs_argop4_u.opclose;
    CLOSE4res *subResp;
    subResp = &resp->nfs_resop4_u.opclose;
    subResp->status = NFS4_OK;
    StateId *idp;
    char *datap;
    Vn::Attr attrs;

    Client *clientp = compStatep->_sessionp->_clientp;
    idp = findStateId(clientp, &arg->open_stateid);

    if (!idp) {
        subResp->status = NFS4ERR_STALE_STATEID;
        return NFS4ERR_STALE_STATEID;
    }

    clientp->_allStateIds.remove(idp);
    delete idp;

    // already set status.
    return NFS4_OK;
}

nfsstat4
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

    printf("nfsserver: getattr %d words\n", arg->attr_request.bitmap4_len);
    for(i=0;i<arg->attr_request.bitmap4_len;i++) {
        printf("%08x ", arg->attr_request.bitmap4_val[i]);
    }
    printf("\n");

    Vn *vp;
    Vn::Attr attrs;
    _vfsp->vget(compStatep->_currentFh, &vp);

    printf("nfsserver::getattr vp=%p\n", vp);

    code = vp->getAttr(&attrs);
    vp->release();
    if (code != 0) {
        printf("nfsserver: getattr failure code=%d\n", code);
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

    return NFS4_OK;
}

nfsstat4
NfsServer::opGetFh(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    GETFH4res *subResp;
    subResp = &resp->nfs_resop4_u.opgetfh;
    subResp->status = NFS4_OK;
    GETFH4resok *okResp = &subResp->GETFH4res_u.resok4;
    int32_t code;
    uint32_t tlen;
    char *datap;

    tlen = compStatep->_currentFh.length();
    printf("nfsserver::getfh returning %d byte file handle\n", tlen);
    okResp->object.nfs_fh4_len = tlen;
    datap = (char *) malloc(tlen);
    memcpy(datap, compStatep->_currentFh.data(), tlen);
    okResp->object.nfs_fh4_val = datap;
    return NFS4_OK;
}

nfsstat4
NfsServer::opAccess(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    ACCESS4args *arg = &op->nfs_argop4_u.opaccess;
    ACCESS4res *subResp;
    subResp = &resp->nfs_resop4_u.opaccess;
    subResp->status = NFS4_OK;
    ACCESS4resok *okResp = &subResp->ACCESS4res_u.resok4;
    int32_t code;

    Vn *vp;
    Vn::Attr attrs;
    _vfsp->vget(compStatep->_currentFh, &vp);

    printf("nfsserver::access vp=%p\n", vp);

    uint32_t nfsAccessFlags = arg->access;
    uint32_t vnAccessFlags = 0;
    uint32_t vnResultsFlags = 0;
    uint32_t nfsResultsFlags = 0;

    if (nfsAccessFlags & (ACCESS4_LOOKUP | ACCESS4_READ)) {
        vnAccessFlags |= 4;
    }
    if (nfsAccessFlags & (ACCESS4_MODIFY | ACCESS4_EXTEND | ACCESS4_DELETE)) {
        vnAccessFlags |= 2;
    }
    if (nfsAccessFlags & (ACCESS4_EXECUTE)) {
        vnAccessFlags |= 1;
    }

    code = vp->access(vnAccessFlags, &vnResultsFlags);
    vp->release();
    if (code != 0) {
        subResp->status = NFS4ERR_INVAL;
        return NFS4ERR_INVAL;
    }

    // turn the bits back into NFS land bits, being careful not to set
    // any bits in the results that weren't in the original request.
    if (vnResultsFlags & 4) {
        nfsResultsFlags |= (nfsAccessFlags & (ACCESS4_LOOKUP | ACCESS4_READ));
    }
    if (vnResultsFlags & 2) {
        nfsResultsFlags |= (nfsAccessFlags &
                            (ACCESS4_MODIFY | ACCESS4_EXTEND | ACCESS4_DELETE));
    }
    if (vnResultsFlags & 1) {
        nfsResultsFlags |= (nfsAccessFlags & ACCESS4_EXECUTE);
    }

    // send back the results; we support everything.
    okResp->access = nfsResultsFlags;
    okResp->supported = nfsResultsFlags;

    printf("nfs-mlk: returning access flags=%x->%x\n",
           nfsAccessFlags, okResp->access);

    return NFS4_OK;
}

nfsstat4
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

    return NFS4_OK;
}

nfsstat4
NfsServer::opSecinfoNoName(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    SECINFO4res *subResp;
    subResp = &resp->nfs_resop4_u.opsecinfo;
    subResp->status = NFS4_OK;
    SECINFO4resok *okResp = &subResp->SECINFO4res_u.resok4;

    okResp->SECINFO4resok_len = 2;
    okResp->SECINFO4resok_val = (secinfo4 *) malloc(2 * sizeof(secinfo4));
    okResp->SECINFO4resok_val[0].flavor = 0;    // AUTH_NONE
    okResp->SECINFO4resok_val[1].flavor = 1;    // AUTH_SYS

    return NFS4_OK;
}

nfsstat4
NfsServer::opSecinfo(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    SECINFO4res *subResp;
    subResp = &resp->nfs_resop4_u.opsecinfo;
    subResp->status = NFS4_OK;
    SECINFO4resok *okResp = &subResp->SECINFO4res_u.resok4;

    okResp->SECINFO4resok_len = 2;
    okResp->SECINFO4resok_val = (secinfo4 *) malloc(2 * sizeof(secinfo4));
    okResp->SECINFO4resok_val[0].flavor = 0;    // AUTH_NONE
    okResp->SECINFO4resok_val[1].flavor = 1;    // AUTH_SYS

    return NFS4_OK;
}

nfsstat4
NfsServer::opReclaimComplete(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    RECLAIM_COMPLETE4args *arg = &op->nfs_argop4_u.opreclaim_complete;
    RECLAIM_COMPLETE4res *subResp;
    subResp = &resp->nfs_resop4_u.opreclaim_complete;
    subResp->rcr_status = NFS4_OK;
    return NFS4_OK;
}

nfsstat4
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
        return NFS4ERR_SEQ_MISORDERED;
    }

    compStatep->_sessionp = sessionp;

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

    return NFS4_OK;
}

nfsstat4
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
        return NFS4ERR_STALE_CLIENTID;
    }

    if (arg->csa_sequence == clientp->_sessionSequenceId) {
        // TODO: replay cache
    } else if (arg->csa_sequence != clientp->_sessionSequenceId + 1) {
        // failure due to bad ordering
        subResp->csr_status = NFS4ERR_SEQ_MISORDERED;
        return NFS4ERR_SEQ_MISORDERED;;
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

    return NFS4_OK;
}

nfsstat4
NfsServer::opPutRootFh(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    char *tp;
    uint32_t length;
    std::string fh = _vfsRootp->getFh();

    compStatep->_currentFh = fh;

    PUTROOTFH4res *subResp;

    subResp = &resp->nfs_resop4_u.opputrootfh;
    subResp->status = NFS4_OK;

    return NFS4_OK;
}

nfsstat4
NfsServer::opPutFh(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    char *tp;
    uint32_t length;

    PUTFH4args *arg = &op->nfs_argop4_u.opputfh;

    compStatep->_currentFh = std::string(arg->object.nfs_fh4_val,
                                         arg->object.nfs_fh4_len);

    PUTFH4res *subResp;

    subResp = &resp->nfs_resop4_u.opputfh;
    subResp->status = NFS4_OK;

    return NFS4_OK;
}

nfsstat4
NfsServer::opSaveFh(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    char *tp;
    uint32_t length;

    compStatep->_savedFh = compStatep->_currentFh;
    compStatep->_savedStateId = compStatep->_currentStateId;

    SAVEFH4res *subResp;

    subResp = &resp->nfs_resop4_u.opsavefh;
    subResp->status = NFS4_OK;

    return NFS4_OK;
}

nfsstat4
NfsServer::opRestoreFh(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    char *tp;
    uint32_t length;

    compStatep->_currentFh = compStatep->_savedFh;
    compStatep->_currentStateId = compStatep->_savedStateId;

    RESTOREFH4res *subResp;

    subResp = &resp->nfs_resop4_u.oprestorefh;
    subResp->status = NFS4_OK;

    return NFS4_OK;
}

// unimplemented operation.  We need to use *some* union, since the
// status is part of every union instead of in a common structure. So,
// we use DELEGRETURN just because it has a status field and nothing
// else in the return structure.
nfsstat4
NfsServer::opBad(nfs_argop4 *op, nfs_resop4 *resp, CompoundState *compStatep) {
    DELEGRETURN4res *subResp;

    printf("nfs-mlk: bad operation received\n");

    subResp = &resp->nfs_resop4_u.opdelegreturn;
    subResp->status = NFS4ERR_INVAL;

    return NFS4ERR_INVAL;
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
    nfsstat4 code;
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
        code = NFS4_OK;
        switch(op->argop) {
            case OP_ACCESS:
                code = serverp->opAccess(op, resp, &compoundState);
                break;
            case OP_CLOSE:
                code = serverp->opClose(op, resp, &compoundState);
                break;
            case OP_COMMIT:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_CREATE:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_DELEGPURGE:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_DELEGRETURN:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_GETATTR:
                code = serverp->opGetAttr(op, resp, &compoundState);
                break;
            case OP_GETFH:
                code = serverp->opGetFh(op, resp, &compoundState);
                break;
            case OP_LINK:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_LOCK:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_LOCKT:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_LOCKU:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_LOOKUP:
                code = serverp->opLookup(op, resp, &compoundState);
                break;
            case OP_LOOKUPP:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_NVERIFY:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_OPEN:
                code = serverp->opOpen(op, resp, &compoundState);
                break;
            case OP_OPENATTR:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_OPEN_CONFIRM:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_OPEN_DOWNGRADE:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_PUTFH:
                code = serverp->opPutFh(op, resp, &compoundState);
                break;
            case OP_PUTPUBFH:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_PUTROOTFH:
                code = serverp->opPutRootFh(op, resp, &compoundState);
                break;
            case OP_READ:
                code = serverp->opRead(op, resp, &compoundState);
                break;
            case OP_READDIR:
                code = serverp->opReaddir(op, resp, &compoundState);
                break;
            case OP_READLINK:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_REMOVE:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_RENAME:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_RENEW:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_RESTOREFH:
                code = serverp->opRestoreFh(op, resp, &compoundState);
                break;
            case OP_SAVEFH:
                code = serverp->opSaveFh(op, resp, &compoundState);
                break;
            case OP_SECINFO:
                code = serverp->opSecinfo(op, resp, &compoundState);
                break;
            case OP_SETATTR:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_SETCLIENTID:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_SETCLIENTID_CONFIRM:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_VERIFY:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_WRITE:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_RELEASE_LOCKOWNER:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_BACKCHANNEL_CTL:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_BIND_CONN_TO_SESSION:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_EXCHANGE_ID:
                code = serverp->opExchangeId(op, resp, &compoundState);
                break;
            case OP_CREATE_SESSION:
                code = serverp->opCreateSession(op, resp, &compoundState);
                break;
            case OP_DESTROY_SESSION:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_FREE_STATEID:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_GET_DIR_DELEGATION:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_GETDEVICEINFO:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_GETDEVICELIST:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_LAYOUTCOMMIT:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_LAYOUTGET:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_LAYOUTRETURN:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_SECINFO_NO_NAME:
                code = serverp->opSecinfoNoName(op, resp, &compoundState);
                break;
            case OP_SEQUENCE:
                code = serverp->opSequence(op, resp, &compoundState);
                break;
            case OP_SET_SSV:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_TEST_STATEID:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_WANT_DELEGATION:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_DESTROY_CLIENTID:
                code = serverp->opBad(op, resp, &compoundState);
                break;
            case OP_RECLAIM_COMPLETE:
                code = serverp->opReclaimComplete(op, resp, &compoundState);
                break;

            default:
                code = NFS4ERR_INVAL;
                break;
        }
        resp->resop = op->argop;

        // stop on first subrequest error.
        if (code != 0) {
            resps->status = code;
            resps->resarray.resarray_len = i+1;
            printf("nfsserver: setting compound error to %d\n", (int) code);
            break;
        }
    }

    return resps;
}

/* static */
void *nfsproc4_null_4_svc(void *args, struct svc_req *req) {
    printf("in null\n");
    return (char *) "foo";
}
