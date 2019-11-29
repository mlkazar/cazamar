#include <time.h>

#include "cfsms.h"
#include "xapi.h"
#include "buftls.h"

/* called with THIS held; generate a path to it.  Since going up the tree violates
 * our locking hierarchy, we have to be careful to lock only one thing at a time.
 *
 * The structure of the tree is actually protected by the _refLock.
 */
int32_t
CnodeMs::getPath(std::string *pathp, CEnv *envp)
{
    std::string tempPath;
    CnodeMs *tnodep = this;
    CnodeMs *pnodep;
    CnodeBackEntry *bep;

    int tnodeHeld = 0;

    _cfsp->_stats._getPathCalls++;
    while(tnodep) {
        if (tnodep->_isRoot) {
            break;
        }

        /* cnode is held, so bep is won't be freed */
        bep = tnodep->_backEntriesp;
        if (!bep) {
            break;
        }

        /* pnodep is stable as well, due to the hold */
        pnodep = bep->_parentp;

        /* prevent changes to the name from some sort of invalidation */
        pnodep->_lock.take();
        tempPath = bep->_name + "/" + tempPath;
        pnodep->_lock.release();

        /* move our reference up one level, but preserving the reference
         * on the original THIS.
         */
        pnodep->hold();
        if (tnodeHeld) {
            tnodep->release();
        }
        tnodep = pnodep;
        tnodeHeld = 1;
    }
    tempPath = "/" + tempPath;

    /* get rid of any leftover references; any obtained locks have already been released */
    if (tnodeHeld && tnodep) {
        tnodep->release();
    }

    *pathp = tempPath;

    /* TBD: figure out how to generate paths */
    return 0;
}

/* mark all nodes as invalid and clear out their names starting from THIS;
 * cnode must be held by caller.
 */
void
CnodeMs::invalidateTree()
{
    CnodeBackEntry *bep;
    CnodeBackEntry *nbep;
    CfsMs *cfsp = _cfsp;
    CnodeMs *childp;
    
    _lock.take();
    _valid = 0;
    
    cfsp->_refLock.take();
    for(bep = _children.head(); bep; bep=nbep) {
        childp = bep->_childp;
        childp->holdNL();
        bep->_name = "";        /* invalidate the name */
        cfsp->_refLock.release();       /* child hold now protects bep from deallocation */

        /* now recurse */
        _lock.release();
        childp->invalidateTree();
        _lock.take();

        cfsp->_refLock.take();
        nbep = bep->_dqNextp;
        childp->releaseNL();
    }
    cfsp->_refLock.release();

    _lock.release();
}

/* return success if everything was found, or if allFoundp is
 * non-null, if id is present.  Set allFoundp if everything was found.
 */
int32_t
CnodeMs::parseResults( Json::Node *jnodep,
                       std::string *idp,
                       uint64_t *sizep,
                       uint64_t *modTimep,
                       uint64_t *changeTimep,
                       CAttr::FileType *fileTypep,
                       uint8_t *allFoundp)
{
    Json::Node *tnodep;
    std::string modTimeStr;
    std::string sizeStr;
    int allFound = 1;
    int idFound = 1;
    CAttr::FileType fileType;

    tnodep = jnodep->searchForChild("id", 0);
    if (tnodep) {
        *idp = tnodep->_children.head()->_name;
    }
    else {
        allFound = 0;
        idFound = 0;
    }

    tnodep = jnodep->searchForChild("lastModifiedDateTime", 0);
    if (tnodep) {
        time_t secsSince70;
        modTimeStr = tnodep->_children.head()->_name;
        const char *tp = modTimeStr.c_str();
        /* we return this in UTC, since that's what everyone uses for everything;
         * our value is nanoseconds since midnight before 1/1/70 UTC.
         */
        struct tm timeInfo;
        memset(&timeInfo, 0, sizeof(timeInfo));
        timeInfo.tm_year = atoi(tp) - 1900;
        timeInfo.tm_mon = atoi(tp+5) - 1;
        timeInfo.tm_mday = atoi(tp+8);
        timeInfo.tm_hour = atoi(tp+11);
        timeInfo.tm_min = atoi(tp+14);
        timeInfo.tm_sec = atoi(tp+17);
        secsSince70 = timegm(&timeInfo);
        *modTimep = (uint64_t) secsSince70 * 1000000000ULL;
        *changeTimep = *modTimep;
    }
    else {
        allFound = 0;
    }

    tnodep = jnodep->searchForChild("size", 0);
    if (tnodep) {
        sizeStr = tnodep->_children.head()->_name;
        *sizep = atoi(sizeStr.c_str());
    }
    else
        allFound = 0;

    fileType = CAttr::FILE;
    tnodep = jnodep->searchForChild("folder", 0);
    if (tnodep != NULL)
        fileType = CAttr::DIR;
    *fileTypep = fileType;

    if (allFoundp) {
        /* if can provide details, return success if we at least have the ID */
        *allFoundp = allFound;
        return (idFound? 0 : CFS_ERR_INVAL);
    }
    else {
        /* caller doesn't care about details, so return success only iff we have
         * all the info required.
         */
        return (allFound? 0 : CFS_ERR_INVAL);
    }
}

/* called with dir lock held, but not child lock held; returns held child
 * pointer.  Drops refLock periodically to let others in.
 */
int32_t
CnodeMs::nameSearch(std::string name, CnodeMs **childpp)
{
    CnodeBackEntry *backp;
    CnodeMs *childp;
    CfsMs *cfsp = _cfsp;

    cfsp->_refLock.take();
    for(backp = _children.head(); backp; backp=backp->_dqNextp) {
        childp = backp->_childp;
        childp->holdNL();
        cfsp->_refLock.release();

        if (name == backp->_name) {
            /* return this one; already held from above */
            *childpp = childp;
            return 0;
        }
        cfsp->_refLock.take();
        childp->releaseNL();    /* release hold from above */
    }
    cfsp->_refLock.release();
    *childpp = NULL;
    return CFS_ERR_NOENT;
}

int32_t
CnodeMs::lookup(std::string name, int forceBackend, Cnode **childpp, CEnv *envp)
{
    /* perform getAttr operation */
    char tbuffer[0x4000];
    XApi::ClientConn *connp;
    XApi::ClientReq *reqp;
    std::string postData;
    CThreadPipe *inPipep;
    CThreadPipe *outPipep;
    const char *tp;
    Json json;
    Json::Node *jnodep = NULL;
    std::string callbackString;
    std::string authHeader;
    std::string id;
    std::string modTimeStr;
    std::string sizeStr;
    uint64_t size;
    int32_t code;
    uint64_t modTime;
    uint64_t changeTime;
    std::string dirPath;
    CnodeMs *childp;
    CnodeLockSet lockSet;
    CAttr::FileType fileType;
    CfsRetryError retryState;

    /* lock parent */
    lockSet.add(this);

    if (!forceBackend) {
        code = nameSearch(name, (CnodeMs **) childpp);
        if (code == 0) {
            return 0;
        }
    }

    /* temporarily drop lock over getPath call */
    lockSet.remove(this);

    code = getPath(&dirPath, envp);
    if (code != 0) {
        printf("cfs lookup: getpath failed code=%d\n", code);
        return code;
    }

    _cfsp->_stats._lookupCalls++;
    callbackString = "/v1.0/me/drive/root:" + Rst::urlPathEncode(dirPath + name);
    
    while(1) {
        _cfsp->_stats._totalCalls++;
        connp = _cfsp->_xapiPoolp->getConn(std::string("graph.microsoft.com"), 443, /* TLS */ 1);
        reqp = new XApi::ClientReq();
        authHeader = "Bearer " + _cfsp->_loginCookiep->getAuthToken();
        reqp->addHeader("Authorization", authHeader.c_str());
        reqp->addHeader("Content-Type", "application/json");
        reqp->startCall( connp,
                         callbackString.c_str(),
                         /* isPost */ XApi::reqGet);
        
        outPipep = reqp->getOutgoingPipe();
        outPipep->write(postData.c_str(), postData.length());
        outPipep->eof();
        
        code = reqp->waitForHeadersDone();
        if (code) {
            delete reqp;
            reqp = NULL;
            if (_cfsp->retryRpcError(CfsLog::opLookup, code, &retryState))
                continue;
            else {
                code = CFS_ERR_TIMEDOUT;
                break;
            }
        }

        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < (signed) sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        
        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        if (code != 0) {
            printf("json parse failed code=%d\n", code);
            delete reqp;
            reqp = NULL;
            code = CFS_ERR_INVAL;
            break;
        }

        if (_cfsp->retryError(CfsLog::opLookup, reqp, &jnodep, &retryState)) {
            delete reqp;
            reqp = NULL;
            continue;
        }

        /* see if we have a fatal error */
        if ((code = retryState.getCode()) != 0)
            break;

        delete reqp;
        reqp = NULL;

        code = parseResults(jnodep, &id, &size, &changeTime, &modTime, &fileType);
        delete jnodep;
        jnodep = NULL;

        if (code == 0) {
            code = _cfsp->getCnodeLinked(this, name, &id, &childp, &lockSet);
            if (code == 0) {
                childp->_valid = 1;
                childp->_attrs._length = size;
                childp->_attrs._ctime = changeTime;
                childp->_attrs._mtime = modTime;
                childp->_attrs._fileType = fileType;
                *childpp = childp;
            }
            else
                break;
        }
        else
            break;

        code = 0;
        break;
    }

    if (reqp)
        delete reqp;

    return code;
}

int32_t
CnodeMs::sendSmallFile(std::string name, CDataSource *sourcep, CEnv *envp)
{
    /* perform create operation */
    char tbuffer[0x4000];
    XApi::ClientConn *connp;
    XApi::ClientReq *reqp;
    CThreadPipe *inPipep;
    CThreadPipe *outPipep;
    std::string postData;
    const char *tp;
    Json json;
    Json::Node *jnodep = NULL;
    std::string callbackString;
    std::string authHeader;
    int32_t code;
    std::string id;
    uint64_t size;
    uint64_t modTime;
    uint64_t changeTime;
    CnodeMs *childp;
    CnodeLockSet lockSet;
    CAttr dataAttrs;
    char *dataBufferp = NULL;
    uint32_t sendBytes;
    uint8_t allFound;
    CAttr::FileType fileType;
    static const uint32_t dataBufferBytes = 4*1024*1024;
    int32_t httpError;
    CfsRetryError retryState;
    
    if (_cfsp->_verbose)
        printf("sendSmallFile: id=%s name=%s\n", _id.c_str(), name.c_str());

    if (_isRoot) {
        callbackString = "/v1.0/me/drive/root:/" + Rst::urlPathEncode(name) + ":/content";
    }
    else
        callbackString = ("/v1.0/me/drive/items/" + _id + ":/" + Rst::urlPathEncode(name) +
                          ":/content");
    
    _cfsp->_stats._sendSmallFilesCalls++;

    /* no post data */
    dataBufferp = new char[dataBufferBytes];

    while(1) {
        sourcep->getAttr(&dataAttrs);
        sendBytes = dataAttrs._length;

        _cfsp->_stats._totalCalls++;
        connp = _cfsp->_xapiPoolp->getConn(std::string("graph.microsoft.com"), 443, /* TLS */ 1);
        reqp = new XApi::ClientReq();
        reqp->setSendContentLength(sendBytes);
        authHeader = "Bearer " + _cfsp->_loginCookiep->getAuthToken();
        reqp->addHeader("Authorization", authHeader.c_str());
        reqp->addHeader("Content-Type", "text/plain");
        reqp->startCall( connp,
                         callbackString.c_str(),
                         /* isPost */ XApi::reqPut);
        
        outPipep = reqp->getOutgoingPipe();
        code = sourcep->read(0, sendBytes, dataBufferp);
        outPipep->write(dataBufferp, sendBytes);
        outPipep->eof();
        if (code < 0) {
            printf("read code=%d\n", code);
            reqp->waitForHeadersDone();
            delete reqp;
            reqp = NULL;
            code = CFS_ERR_SERVER;
            break;
        }
        
        code = reqp->waitForHeadersDone();
        if (code != 0) {
            printf("sendsmallfile header done %d\n", code);
            delete reqp;
            reqp = NULL;
            if (_cfsp->retryRpcError(CfsLog::opSendFile, code, &retryState))
                continue;
            else {
                code = CFS_ERR_TIMEDOUT;
                break;
            }
        }

        /* wait for a response, and then parse it */
        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < (signed) sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        
        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        if (code != 0) {
            if (reqp) {
                printf("sendsmallfile parsejson %d\n", code);
                delete reqp;
                reqp = NULL;
            }
            printf("json parse failed code=%d\n", code);
            break;
        }
        
        if (_cfsp->retryError(CfsLog::opSendFile, reqp, &jnodep, &retryState)) {
            delete reqp;
            reqp = NULL;
            continue;
        }
        code = retryState.getCode();
        if (code != 0) {
            delete reqp;
            reqp = NULL;
            break;
        }

        if (reqp) {
            httpError = reqp->getHttpError();
            delete reqp;
            reqp = NULL;
        }
        else {
            httpError = 1000;
        }

        /* don't need the lock until we're merging in the results */
        lockSet.add(this);

        code = parseResults(jnodep, &id, &size, &changeTime, &modTime, &fileType, &allFound);
        if (code == 0) {
            code = _cfsp->getCnodeLinked(this, name, &id, &childp, &lockSet);
            if (code == 0) {
                if (allFound) {
                    childp->_valid = 1;
                    childp->_attrs._length = size;
                    childp->_attrs._ctime = changeTime;
                    childp->_attrs._mtime = modTime;
                    childp->_attrs._fileType = fileType;
                }
                else {
                    printf("json analyze failed code=%d\n", code);
                    childp->_valid = 0;
                }
                childp->release();    /* lock held by getCnodeLinked */
                childp = NULL;
            }
        }
        else {
            jnodep->print();
            printf("json parseresults failed code=%d httpError=%d\n", code, httpError);
        }
        break;
    }

    if (reqp)
        delete reqp;

    if (jnodep) {
        delete jnodep;
        jnodep = NULL;
    }

    if (dataBufferp)
        delete [] dataBufferp;
    
    if (_cfsp->_verbose)
        printf("sendSmallFile: done code=%d\n", code);
    return code;
}

int32_t
CnodeMs::mkdir(std::string name, Cnode **newDirpp, CEnv *envp)
{
    /* perform mkdir operation */
    char tbuffer[0x4000];
    XApi::ClientConn *connp;
    XApi::ClientReq *reqp;
    CThreadPipe *inPipep;
    CThreadPipe *outPipep;
    std::string postData;
    const char *tp;
    Json json;
    Json::Node *jnodep = NULL;
    std::string callbackString;
    std::string authHeader;
    int32_t code;
    std::string id;
    uint64_t size;
    uint64_t modTime;
    uint64_t changeTime;
    CnodeMs *childp;
    CnodeLockSet lockSet;
    CAttr::FileType fileType;
    uint32_t httpError;
    int errorOk = 0;
    CfsRetryError retryState;
    
    if (_cfsp->_verbose)
        printf("mkdir: id=%s name=%s\n", _id.c_str(), name.c_str());

    if (_isRoot)
        callbackString = "/v1.0/me/drive/root/children";
    else
        callbackString = "/v1.0/me/drive/items/" + _id + "/children";

    _cfsp->_stats._mkdirCalls++;
    
    postData = "{\n";
    postData += "\"name\": \"" + name + "\",\n";
    postData += "\"folder\": {},\n";
    postData += "\"@microsoft.graph.conflictBehavior\": \"fail\"\n";
    postData += "}\n";
    
    while(1) {
        _cfsp->_stats._totalCalls++;
        connp = _cfsp->_xapiPoolp->getConn(std::string("graph.microsoft.com"), 443, /* TLS */ 1);
        reqp = new XApi::ClientReq();
        reqp->setSendContentLength(postData.length());
        authHeader = "Bearer " + _cfsp->_loginCookiep->getAuthToken();
        reqp->addHeader("Authorization", authHeader.c_str());
        reqp->addHeader("Content-Type", "application/json");
        reqp->startCall( connp,
                         callbackString.c_str(),
                         /* isPost */ XApi::reqPost);
        
        outPipep = reqp->getOutgoingPipe();
        outPipep->write(postData.c_str(), postData.length());
        outPipep->eof();
        
        code = reqp->waitForHeadersDone();
        if (code != 0) {
            delete reqp;
            reqp = NULL;
            if (_cfsp->retryRpcError(CfsLog::opMkdir, code, &retryState))
                continue;
            else {
                code = CFS_ERR_TIMEDOUT;
                break;
            }
        }
        httpError = reqp->getHttpError();

        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < (signed) sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        
        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        if (code != 0) {
            printf("json parse failed code=%d\n", code);
            delete reqp;
            reqp = NULL;
            break;
        }
        
        if (httpError == 409) {
            Json::Node *tnodep;
            tnodep = jnodep->searchForChild("code");
            if (tnodep &&
                tnodep->_children.head() &&
                tnodep->_children.head()->_name == "nameAlreadyExists") {
                code = CFS_ERR_EXIST;
                errorOk = 1;    /* even though we had an error, this is OK */
                delete reqp;
                reqp = NULL;
                break;
            }
        }
        
        if (!errorOk) {
            if (_cfsp->retryError(CfsLog::opMkdir, reqp, &jnodep, &retryState)) {
                delete reqp;
                reqp = NULL;
                continue;
            }
            /* otherwise we have success or a fatal error */
            code = retryState.getCode();
            if (code != 0) {
                delete reqp;
                reqp = NULL;
                break;
            }
        }

        if (reqp) {
            delete reqp;
            reqp = NULL;
        }

        /* this will fail with a 409 but that doesn't hurt us; we just won't fill
         * in the attributes now.
         */
        code = parseResults(jnodep, &id, &size, &changeTime, &modTime, &fileType);
        if (code == 0) {
            code = _cfsp->getCnodeLinked(this, name, &id, &childp, &lockSet);
            if (code == 0) {
                childp->_valid = 1;
                childp->_attrs._length = size;
                childp->_attrs._ctime = changeTime;
                childp->_attrs._mtime = modTime;
                childp->_attrs._fileType = fileType;
                *newDirpp = childp;
            }
        }
        break;
    }

    if (jnodep) {
        delete jnodep;
        jnodep = NULL;
    }

    if (reqp)
        delete reqp;

    if (code)
        *newDirpp = NULL;

    if (_cfsp->_verbose)
        printf("mkdir: done code=%d\n", code);

    return code;
}

/* called without locks held */
int32_t
CnodeMs::fillAttrs( CEnv *envp, CnodeLockSet *lockSetp)
{
    /* perform getAttr operation */
    char tbuffer[0x4000];
    XApi::ClientConn *connp;
    XApi::ClientReq *reqp;
    std::string postData;
    CThreadPipe *inPipep;
    CThreadPipe *outPipep;
    const char *tp;
    Json json;
    Json::Node *jnodep;
    std::string callbackString;
    std::string authHeader;
    std::string id;
    std::string modTimeStr;
    std::string sizeStr;
    uint64_t size;
    int32_t code;
    uint64_t modTime;
    uint64_t changeTime;
    CAttr::FileType fileType;
    CfsRetryError retryState;
    
    if (_isRoot)
        callbackString = "/v1.0/me/drive/root";
    else
        callbackString = "/v1.0/me/drive/items/" + _id;
    
    _cfsp->_stats._fillAttrCalls++;

    while(1) {
        _cfsp->_stats._totalCalls++;
        connp = _cfsp->_xapiPoolp->getConn(std::string("graph.microsoft.com"), 443, /* TLS */ 1);
        reqp = new XApi::ClientReq();
        authHeader = "Bearer " + _cfsp->_loginCookiep->getAuthToken();
        reqp->addHeader("Authorization", authHeader.c_str());
        reqp->addHeader("Content-Type", "application/json");
        reqp->startCall( connp,
                         callbackString.c_str(),
                         /* isPost */ XApi::reqGet);
        
        outPipep = reqp->getOutgoingPipe();
        outPipep->write(postData.c_str(), postData.length());
        outPipep->eof();
        
        code = reqp->waitForHeadersDone();
        if (code) {
            delete reqp;
            reqp = NULL;
            if (_cfsp->retryRpcError(CfsLog::opGetAttr, code, &retryState))
                continue;
            else {
                code = CFS_ERR_TIMEDOUT;
                break;
            }
        }

        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < (signed) sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        
        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        if (code != 0) {
            printf("json parse failed code=%d\n", code);
        }
        
        if (_cfsp->retryError(CfsLog::opGetAttr, reqp, &jnodep, &retryState)) {
            delete reqp;
            reqp = NULL;
            continue;
        }
        code = retryState.getCode();

        delete reqp;
        reqp = NULL;

        if (code)
            break;

        lockSetp->add(this);
        parseResults(jnodep, &id, &size, &changeTime, &modTime, &fileType);
        _attrs._mtime = modTime;
        _attrs._ctime = changeTime;
        _attrs._length = size;
        _attrs._fileType = fileType;
        _valid = 1;

        delete jnodep;
        code = 0;
        break;
    }

    if (reqp)
        delete reqp;

    if (_cfsp->_verbose)
        printf("fillAttrs: id='%s' done code=%d\n", _id.c_str(), code);
    return code;
}

int32_t
CnodeMs::getAttr(CAttr *attrp, CEnv *envp)
{
    int32_t code;
    CnodeLockSet lockSet;
    
    lockSet.add(this);

    if (_valid) {
        *attrp = _attrs;
        return 0;
    }

    _cfsp->_stats._getAttrCalls++;

    lockSet.reset();

    code = fillAttrs(envp, &lockSet);
    if (code == 0) {
        *attrp = _attrs;
    }
    return code;
}

int32_t
CnodeMs::startSession( std::string name,
                       std::string *sessionUrlp)
{
    /* perform mkdir operation */
    char tbuffer[0x4000];
    XApi::ClientConn *connp;
    XApi::ClientReq *reqp;
    CThreadPipe *inPipep;
    CThreadPipe *outPipep;
    std::string postData;
    const char *tp;
    Json json;
    Json::Node *jnodep = NULL;
    Json::Node *tnodep;
    std::string callbackString;
    std::string authHeader;
    int32_t code;
    std::string id;
    CfsRetryError retryState;
    
    /* one per large file transfer */
    _cfsp->_stats._sendLargeFilesCalls++;

    sessionUrlp->erase();

    // perhaps syntax is .../root:/filename:/createUploadSession
    // or .../items/{id}:/filename:/createUploadSession
    // based on a random comment
    if (_isRoot)
        callbackString = "/v1.0/me/drive/root:/" + Rst::urlPathEncode(name) + ":/createUploadSession";
    else
        callbackString = "/v1.0/me/drive/items/" + _id + ":/" + Rst::urlPathEncode(name) + ":/createUploadSession";
    
    postData = "{\n";
    postData = "\"item\": {\n";
    postData += "\"@microsoft.graph.conflictBehavior\": \"replace\"\n";
    postData += "}\n";  /* close item */
    postData += "}\n";
    
    while(1) {
        _cfsp->_stats._totalCalls++;
        connp = _cfsp->_xapiPoolp->getConn(std::string("graph.microsoft.com"), 443, /* TLS */ 1);
        reqp = new XApi::ClientReq();
        reqp->setSendContentLength(postData.length());
        authHeader = "Bearer " + _cfsp->_loginCookiep->getAuthToken();
        reqp->addHeader("Authorization", authHeader.c_str());
        reqp->addHeader("Content-Type", "application/json");
        reqp->startCall( connp,
                         callbackString.c_str(),
                         /* isPost */ XApi::reqPost);
        
        outPipep = reqp->getOutgoingPipe();
        outPipep->write(postData.c_str(), postData.length());
        outPipep->eof();
        
        code = reqp->waitForHeadersDone();
        if (code != 0) {
            delete reqp;
            reqp = NULL;
            if (_cfsp->retryRpcError(CfsLog::opSendFile, code, &retryState))
                continue;
            else {
                code = CFS_ERR_TIMEDOUT;
                break;
            }
        }

        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < (signed) sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        
        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        if (code != 0) {
            printf("json parse failed code=%d\n", code);
            delete reqp;
            reqp = NULL;
            break;
        }
        
        if (_cfsp->retryError(CfsLog::opSendFile, reqp, &jnodep, &retryState)) {
            delete reqp;
            reqp = NULL;
            continue;
        }
        code = retryState.getCode();
        if (code) {
            delete reqp;
            reqp = NULL;
            break;
        }

        /* search for uploadUrl */
        tnodep = jnodep->searchForChild("uploadUrl", 0);
        if (tnodep) {
            *sessionUrlp = tnodep->_children.head()->_name;
            code = 0;
        }
        else {
            code = -1;
        }

        if (reqp) {
            delete reqp;
            reqp = NULL;
        }

        break;
    }

    if (jnodep) {
        delete jnodep;
        jnodep = NULL;
    }

    if (reqp)
        delete reqp;

    return code;
    
}

/* bytesReadp is set only on 0 returns */
int32_t
CnodeMs::sendData( std::string *sessionUrlp,
                   CDataSource *sourcep,
                   uint64_t fileLength,
                   uint64_t byteOffset,
                   uint32_t byteCount,
                   uint32_t *bytesReadp)
{
    /* perform mkdir operation */
    char tbuffer[0x4000];
    char *dataBufferp;
    std::string sessionHost;
    std::string sessionRelativeUrl;
    XApi::ClientConn *connp;
    XApi::ClientReq *reqp = NULL;
    CThreadPipe *inPipep;
    CThreadPipe *outPipep;
    const char *tp;
    Json json;
    Json::Node *jnodep = NULL;
    std::string authHeader;
    int32_t code=0;
    std::string id;
    uint32_t readCount;
    int32_t actuallyReadCount;
    uint16_t port;
    CfsRetryError retryState;
    
    Rst::splitUrl(*sessionUrlp, &sessionHost, &sessionRelativeUrl, &port);

    osp_assert(byteCount < 4*1024*1024);
    dataBufferp = new char[byteCount];
    _cfsp->_stats._sendDataCalls++;

    while(1) {
        _cfsp->_stats._totalCalls++;
        connp = _cfsp->_xapiPoolp->getConn(sessionHost, port, /* TLS */ 1);
        /* read some bytes */
        readCount = (byteOffset + byteCount > fileLength?
                     fileLength - byteOffset :
                     byteCount);
        actuallyReadCount = sourcep->read( byteOffset, readCount, dataBufferp);
        if (actuallyReadCount == 0) {
            connp->setBusy(0);  /* marked busy by getConn */
            break;
        }
        if (actuallyReadCount < 0) {
            connp->setBusy(0);  /* marked busy by getConn */
            code = CFS_ERR_IO;
            break;
        }

        reqp = new XApi::ClientReq();
        reqp->setSendContentLength(actuallyReadCount);
        authHeader = "Bearer " + _cfsp->_loginCookiep->getAuthToken();
        reqp->addHeader("Authorization", authHeader.c_str());
        sprintf(tbuffer, "bytes %ld-%ld/%ld",
                (long) byteOffset,
                (long) byteOffset+actuallyReadCount-1,
                (long) fileLength);
        reqp->addHeader("Content-Range", tbuffer);
        reqp->startCall( connp,
                         sessionRelativeUrl.c_str(),
                         /* isPut */ XApi::reqPut);
        
        outPipep = reqp->getOutgoingPipe();
        outPipep->write(dataBufferp, actuallyReadCount);
        outPipep->eof();
        
        code = reqp->waitForHeadersDone();
        if (code != 0) {
            delete reqp;
            reqp = NULL;
            if (_cfsp->retryRpcError(CfsLog::opSendFile, code, &retryState))
                continue;
            else {
                code = CFS_ERR_TIMEDOUT;
                break;
            }
        }

        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < (signed) sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        
        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        if (code != 0) {
            printf("json parse failed code=%d\n", code);
            delete reqp;
            reqp = NULL;
            break;
        }

        if (_cfsp->retryError(CfsLog::opSendFile, reqp, &jnodep, &retryState)) {
            delete reqp;
            reqp = NULL;
            continue;
        }
        code = retryState.getCode();

        delete reqp;
        reqp = NULL;

        break;
    }

    if (reqp)
        delete reqp;

    if (jnodep) {
        delete jnodep;
        jnodep = NULL;
    }

    if (dataBufferp) {
        delete [] dataBufferp;
    }

    if (code == 0) {
        *bytesReadp = actuallyReadCount;
    }
    else {
        *bytesReadp = 0;
    }
    
    return code;
}

/* static */ int32_t
CnodeMs::abortSession( std::string *sessionUrlp)
{
    /* TBD: write this */
    return 0;
}

int32_t
CnodeMs::sendFile( std::string name,
                   CDataSource *sourcep,
                   uint64_t *bytesCopiedp,
                   CEnv *envp)
{
    int32_t code;
    std::string sessionUrl;
    uint64_t remainingBytes;
    uint64_t currentOffset;
    uint64_t size;
    CnodeLockSet lockSet;
    CAttr dataAttrs;
    uint32_t bytesSent;

    code = sourcep->getAttr(&dataAttrs);
    if (code) {
        printf("sendFile: local getattr failure\n");
        return code;
    }
    size = dataAttrs._length;

    /* we can use simpler put mechanism which can use same connection */
    if (size < 4*1024*1024) {
        code = sendSmallFile(name, sourcep, envp);
        if (bytesCopiedp)
            *bytesCopiedp += size;
        return code;
    }

    if (_cfsp->_verbose)
        printf("sendBigFile: id=%s name=%s\n", _id.c_str(), name.c_str());

    /* MS claims in docs that multiples of this are only safe values;
     * commenters don't believe them.
     */
    uint32_t bytesPerPut = 12*320*1024;
    
    code = startSession( name, &sessionUrl);
    if (code) {
        printf("sendFile: session start failed code=%d\n", code);
        return code;
    }

    remainingBytes = size;
    currentOffset = 0;
    while(remainingBytes > 0) {
        code = sendData( &sessionUrl,
                         sourcep,
                         size,
                         currentOffset,
                         bytesPerPut,
                         &bytesSent);
        if (code) {
            printf("sendFile: sendData failed code=%d\n", code);
            abortSession( &sessionUrl);
            return code;
        }
        else if (bytesSent < bytesPerPut) {
            /* we've hit EOF, so we're done */
            if (bytesCopiedp)
                *bytesCopiedp += bytesSent;
            code = 0;
            break;
        }
        else {
            /* update counters */
            if (bytesCopiedp)
                *bytesCopiedp += bytesSent;
            currentOffset += bytesPerPut;
        }
    }

    if (_cfsp->_verbose)
        printf("sendFile: done code=0\n");

    /* note that non-zero codes have already been handled */
    return 0;
}

void
CnodeMs::hold() {
    _cfsp->_refLock.take();
    holdNL();
    _cfsp->_refLock.release();
}

void
CnodeMs::holdNL() {
    _refCount++;
    if (_inLru) {
        _cfsp->_lruQueue.remove(this);
        _inLru = 0;
    }
}

void
CnodeMs::release()
{
    CfsMs *cfsp = _cfsp;
    cfsp->_refLock.take();
    releaseNL();
    cfsp->_refLock.release();
}

/* if call this, must call check recycle once locks are done */
void
CnodeMs::releaseNL() {
    int didAdd = 0;
    osp_assert(_refCount > 0);
    CfsMs *cfsp;

    cfsp = _cfsp;               /* once ref count hits zero, all bets are off */
    _refCount--;
    if (recyclable() && !_inLru) {
        _inLru = 1;
        cfsp->_lruQueue.append(this);
        didAdd = 1;
    }
}

/* returns held reference to root */
int32_t
CfsMs::root(Cnode **nodepp, CEnv *envp)
{
    CnodeMs *rootp;
    int32_t code;
    CnodeLockSet lockSet;

    rootp = _rootp;
    if (!rootp) {
        while(1) {
            /* allocCnode returns NULL on certain race conditions */
            rootp = allocCnode(NULL);
            if (rootp)
                break;
        }

        rootp->_cfsp = this;
        rootp->_isRoot = 1;
        _rootp = rootp;         /* reference from new goes to _rootp */
    }

    lockSet.add(rootp);

    if (rootp->_valid) {
        code = 0;
    }
    else {
        code = rootp->fillAttrs(envp, &lockSet);
    }

    if (code == 0) {
        rootp->hold();          /* each call increments refCount for csller */
        *nodepp = rootp;
    }
    else
        *nodepp = NULL;

    return code;
}

/* returns null if lost race condition and had to drop hash lock; if it returns a
 * real cnode, it means that the hash lock hadn't been dropped.
 */
CnodeMs *
CfsMs::allocCnode(CThreadMutex *hashLockp)
{
    CnodeMs *cnodep;
    CnodeMs *tnodep;
    CnodeMs **lnodepp;
    CnodeMs *parentp;
    CnodeBackEntry *backp;
    int doRelease;

    _refLock.take();

    /* we just want to create a new one, since we haven't populated the entire
     * cache yet.
     */

    if (_cnodeCount < _maxCnodeCount) {
        cnodep = new CnodeMs();
        cnodep->_cfsp = this;
        _cnodeCount++;
        _refLock.release();
        return cnodep;
    }

    /* otherwise, try LRU queue, and if that doesn't find anyone, just allocate a new
     * one anyway, and bump a counter.
     */
    while(1) {
        /* see if we have an already recycled cnode; currently unused */
        if ((cnodep = _freeListp) != NULL) {
            _freeListp = cnodep->_nextFreep;
            _refLock.release();
            return cnodep;
        }

        /* try to get one from the LRU queue */
        cnodep = _lruQueue.pop();
        if (!cnodep) {
            _cnodeCount++;
            cnodep = new CnodeMs();
            cnodep->_cfsp = this;
            _refLock.release();
            return cnodep;
        }
        cnodep->_inLru = 0;

        if (!cnodep->recyclable()) {
            /* can't recycle this one; they'll get added to LRU queue again
             * once their children disppear or their ref count hits zero
             */
            continue;
        }

        /* here, cnodep is recyclable, but we can't recycle it without adding
         * the hash lock for this entry, so we can hash it out.
         */
        if (hashLockp == &_lock) {
            doRelease = 0;
        }
        else if (_lock.tryTake() == 0) {
            doRelease = 1;
        }
        else {
            /* couldn't get the hash lock directly; because we're going to
             * drop the lock, our caller can't do anything with our cnode anyway.
             * So, we'll just wait for the lock we wanted to get with lock.try
             * above, and then return failure.
             */
            _refLock.release();
            if (hashLockp)
                hashLockp->release();
            _lock.take(); /* if we use multiple hash locks, ensure this one matches cnodep */
            _refLock.take();
            _lock.release();

            /* rather than recheck conditions, just put cnodep in the LRU queue
             * if it isn't, but should be.  And then return failure.  Our caller
             * will retry.
             */
            if (!cnodep->_inLru && cnodep->recyclable()) {
                cnodep->_inLru = 1;
                _lruQueue.append(cnodep);
            }
            _refLock.release();
            return NULL;
        }

        /* if we make it here, we have the hash lock for the old cnode
         * we're trying to recycle.  It should still be recyclable,
         * since we never dropped the _refLock, and we still have our
         * caller's hash lock, if one was held.
         */
        if (cnodep->_inHash) {
            cnodep->_inHash = 0;
            for( lnodepp = &_hashTablep[cnodep->_hashIx], tnodep = *lnodepp;
                 tnodep;
                 lnodepp = &tnodep->_nextHashp, tnodep = *lnodepp) {
                if (tnodep == cnodep) {
                    break;
                }
            }
            osp_assert(tnodep);
            *lnodepp = cnodep->_nextHashp;
        }

        while ((backp = cnodep->_backEntriesp) != NULL) {
            osp_assert(backp->_childp == cnodep);
            parentp = backp->_parentp;
            parentp->_children.remove(backp);
            cnodep->unthreadEntry(backp);
            delete backp;
            /* and see if parent should be in LRU */
            if (parentp->recyclable() && !parentp->_inLru) {
                parentp->_inLru = 1;
                _lruQueue.append(parentp);
            }
        }

        osp_assert(!cnodep->_inLru);
        if (doRelease)
            _lock.release();
        _refLock.release();
        return cnodep;
    } /* loop */

    /* not reached */
    return NULL;
}

int32_t
CfsMs::getCnode(std::string *idp, CnodeMs **cnodepp)
{
    uint64_t hashValue;
    CnodeMs *cp;

    hashValue = Cfs::fnvHash64(idp) % _hashSize;

    while(1) {
        _lock.take();

        for(cp = _hashTablep[hashValue]; cp; cp=cp->_nextHashp) {
            if (cp->_id == *idp)
                break;
        }

        if (!cp) {
            cp = allocCnode(&_lock);
            if (!cp) {
                /* _lockp was released on failure; must reverify hash table search */
                continue;
            }

            cp->_cfsp = this;
            cp->_id = *idp;
            cp->_refCount = 1;

            /* hash in */
            cp->_hashIx = hashValue;
            cp->_inHash = 1;
            cp->_nextHashp = _hashTablep[hashValue];
            _hashTablep[hashValue] = cp;
        }
        else {
            cp->hold();
        }
        *cnodepp = cp;
        break;
    }

    _lock.release();
    return 0;
}

int32_t
CfsMs::getCnodeLinked( CnodeMs *parentp,
                       std::string name,
                       std::string *idp,
                       CnodeMs **cnodepp,
                       CnodeLockSet *lockSetp)
{
    CnodeMs *childp;
    int32_t code;
    CnodeBackEntry *entryp;
    CnodeMs *oldChildp;

    if (!parentp) {
        return 0;
    }

    code = getCnode(idp, &childp);
    if (code)
        return code;

    lockSetp->add(parentp);
    lockSetp->add(childp);

    /* otherwise, thread us in */
    _refLock.take();
    for( entryp = parentp->_children.head(); entryp; entryp=entryp->_dqNextp) {
        if (entryp->_name.length() == 0 && entryp->_childp == childp) {
            /* we found an invalid entry with the same child, probably the
             * result of an earlier invalidateTree call.  Just revalidate.
             */
            entryp->_name = name;
            break;
        }
        if (entryp->_name == name) {
            if (childp != entryp->_childp) {
                /* we have the object with this name, so replace the object and
                 * unsplice the back entry from the old child.
                 *
                 * This could just be one name out of many for the
                 * child, so we have to find the entry in the child's
                 * back pointer list.
                 */
                oldChildp = entryp->_childp;

                /* remove this back entry structure from the wrong child */
                oldChildp->unthreadEntry(entryp);

                /* and add it into the new child's back pointer list */
                osp_assert(childp->_backEntriesp == NULL); /* debugging only */
                entryp->_nextSameChildp = childp->_backEntriesp;
                childp->_backEntriesp = entryp;

                /* and set the downward pointer */
                entryp->_childp = childp;
            } /* child doesn't match */
            break;
        } /* name matches */
    } /* see if name already exists in parent dir */
    _refLock.release();

    if (!entryp) {
        entryp = new CnodeBackEntry();
        _refLock.take();
        entryp->_nextSameChildp = childp->_backEntriesp;
        childp->_backEntriesp = entryp;
        entryp->_name = name;
        entryp->_parentp = parentp;
        entryp->_childp = childp;
        parentp->_children.append(entryp);
        _refLock.release();
    }

    *cnodepp = childp;
    return 0;
}

/* must be called with refLock held */
void
CnodeMs::unthreadEntry(CnodeBackEntry *entryp)
{
    CnodeBackEntry **lentrypp;
    CnodeBackEntry *tentryp;

    for( lentrypp = &_backEntriesp, tentryp = *lentrypp;
         tentryp;
         lentrypp = &tentryp->_nextSameChildp, tentryp = *lentrypp) {
        if (tentryp == entryp) {
            *lentrypp = entryp->_nextSameChildp;
            break;
        }
    }

    osp_assert(tentryp != NULL);
}

int
CfsMs::retryRpcError( CfsLog::OpType type,
                      int32_t error,
                      CfsRetryError *retryStatep)
{       
    if (retryStatep->_retries++ >= CfsRetryError::_maxRetries) {
        _logp->logError(type, error, "RPC Error", "");
        _stats._xapiErrors++;
        sleep(1);
        return 0;
    }
    else
        return 1;
}

/* note that retryError deletes the json structure and nulls out the pointer if
 * it returns 0 (continue).  Otherwise, we're done, and the final return code is
 * returned.
 */
int
CfsMs::retryError( CfsLog::OpType type,
                   XApi::ClientReq *reqp,
                   Json::Node **parsedNodepp,
                   CfsRetryError *retryStatep)
{
    uint32_t httpError;
    httpError = reqp->getHttpError();
    int32_t code;
    Json::Node *parsedNodep = *parsedNodepp;

    /* add in a 1 in the stalling cases below */
    _stalledErrors <<= 1;

    if (reqp->getError() != 0) {
        code = 1000000 + reqp->getError();
        if (_logp)
            _logp->logError(type, code, "RPC error", "");
        _stats._xapiErrors++;
        retryStatep->_finalCode = code;
        return 0;
    }
    else if (httpError >= 200 && httpError < 300) {
        retryStatep->_finalCode = CFS_ERR_OK;
        return 0;
    }
    else if (httpError == 404) {
        retryStatep->_finalCode = CFS_ERR_NOENT;
        return 0;
    }

    std::string shortError;
    std::string longError;
    Json::Node *nodep;

    if (parsedNodep != NULL) {
        nodep = parsedNodep->searchForChild("child", 0);
        if (nodep && nodep->_children.head())
            shortError = nodep->_children.head()->_name;
        nodep = parsedNodep->searchForChild("message", 0);
        if (nodep && nodep->_children.head())
            longError = nodep->_children.head()->_name;
    }

    if (httpError == 400) {
        /* this error claims we sent bad parameters, but it shows up
         * randomly on mkdirs so we retry a few times before giving
         * up.  Actually, it appears to show up even less frequently
         * on other calls, so I've removed the mkdir check.
         */
        if (retryStatep->_retries++ >= CfsRetryError::_maxRetries) {
            _logp->logError(type, httpError, shortError, longError);
            _stats._mysteryErrors++;
            retryStatep->_finalCode = CFS_ERR_SERVER;
            return 0;
        }
        else {
            _stats._bad400++;
            sleep(2);
            delete parsedNodep;
            *parsedNodepp = NULL;
            return 1;
        }
    }
    else if (httpError == 401) {
        std::string refreshToken = _loginCookiep->getRefreshToken();
        _stats._authRequired++;
        if (retryStatep->_retries++ >= CfsRetryError::_maxRetries) {
            _logp->logError(type, httpError, "too many auth errors ", longError);
            retryStatep->_finalCode = CFS_ERR_ACCESS;
            return 0;
        }

        /* authentication expired; use refresh token */
        if (refreshToken.length() == 0) {
            /* some login mechanisms don't have refresh tokens */
            if (_logp)
                _logp->logError(type, httpError, "authentication error " + shortError, longError);
            _loginCookiep->logout();
            retryStatep->_finalCode = CFS_ERR_ACCESS;
            return 0;
        }
        code = _loginCookiep->refresh();
        if (code) {
            if (_logp)
                _logp->logError(type, code, "authentication refresh " + shortError, longError);
            retryStatep->_finalCode = CFS_ERR_ACCESS;
        }
        return (code == 0);
    }
    else if (httpError == 409) {
        _stats._busy409++;
        if (retryStatep->_retries++ >= CfsRetryError::_maxRetries) {
            _logp->logError(type, httpError, "too many 409's " + shortError, longError);
            retryStatep->_finalCode = CFS_ERR_TIMEDOUT;
            return 0;
        }
        else {
            _stalledErrors |= 1;
            sleep(4);
            printf("retrying 409 error\n");
            delete parsedNodep;
            *parsedNodepp = NULL;
            return 1;
        }
    }
    else if ( (httpError >= 500 && httpError <= 504) ||
              httpError == 429) {
        /* overloaded server, or bad choice of server.  Must rebind */
        if (httpError == 429)
            _stats._busy429++;
        else
            _stats._overloaded5xx++;
        _stalledErrors |= 1;
        reqp->resetConn();
        sleep(8);
        delete parsedNodep;
        *parsedNodepp = NULL;
        return 1;
    }
    else {
        _stats._mysteryErrors++;
        if (_logp)
            _logp->logError(type, httpError, shortError, longError);
        retryStatep->_finalCode = CFS_ERR_SERVER;
        return 0;
    }
}

/* return false if this character isn't legal at the start of a file name, even if it
 * is legal in other places in a file name.
 */
int
CfsMs::legalFirst(int tc)
{
    if (tc == ' ' || tc == '~')
        return 0;
    else
        return 1;
}

/* return false if tc is a character that's not legal anywhere in an entry name;
 * otherwise return true.
 */
int
CfsMs::legalAnywhere(int tc)
{
    if ( tc == '*' ||
         tc == '"' ||
         tc == ':' ||
         tc == '<' ||
         tc == '>' ||
         tc == '?' ||
         tc == '\\' ||
         tc == '|')
        return 0;
    else
        return 1;
}

std::string
CfsMs::legalizeIt(std::string ins)
{
    const char *tp = ins.c_str();
    std::string outs;
    char tc;
    int firstComp;
    int failed;

    /* see if we're done without any reallocation */
    failed = 0;
    firstComp = 1;
    while( (tc = *tp++) != 0) {
        if (tc == '/') {
            firstComp = 1;
            continue;
        }

        if ( (firstComp && !legalFirst(tc)) ||
             !legalAnywhere(tc)) {
            failed = 1;
            break;
        }

        firstComp = 0;
    }

    if (!failed) {
        return ins;
    }

    /* otherwise turn all illegal first chars into '_' characters if
     * they start an entry name.
     */
    tp = ins.c_str();
    firstComp = 1;
    while( (tc = *tp++) != 0) {
        if (tc == '/') {
            firstComp = 1;
            outs.push_back(tc);
            continue;
        }

        if ( (firstComp && !legalFirst(tc)) ||
             !legalAnywhere(tc)) {
            outs.push_back('_');
            firstComp = 0;
            continue;
        }

        firstComp = 0;
        outs.push_back(tc);
    }
    

    printf("Converting '%s' to '%s'\n", ins.c_str(), outs.c_str());
    return outs;
}
