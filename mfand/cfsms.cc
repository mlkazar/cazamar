#include <time.h>

#include "cfsms.h"
#include "xapi.h"
#include "buftls.h"

/* called with THIS held; generate a path to it.  Since going up the tree violates
 * our locking hierarchy, we have to be careful to lock only one thing at a time.
 *
 * Note that you can't modify a BackEntry chain without both the parent and child
 * locked, which makes our lives a bit easier.
 */
int32_t
CnodeMs::getPath(std::string *pathp, CEnv *envp)
{
    std::string tempPath;
    CnodeMs *tnodep = this;
    CnodeMs *unodep;
    CnodeBackEntry *bep;
    int tnodeHeld = 0;
    while(tnodep) {
        tnodep->_lock.take();
        if (tnodep->_isRoot) {
            tnodep->_lock.release();
            break;
        }
        bep = tnodep->_backEntriesp;
        if (!bep) {
            tnodep->_lock.release();
            break;
        }
        tempPath = bep->_name + "/" + tempPath;
        unodep = bep->_parentp;
        unodep->hold();
        tnodep->_lock.release();
        if (tnodeHeld) {
            tnodep->release();
        }
        tnodep = unodep;
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
        timeInfo.tm_year = atoi(tp) - 1900;
        timeInfo.tm_mon = atoi(tp+5) - 1;
        timeInfo.tm_mday = atoi(tp+8);
        timeInfo.tm_hour = atoi(tp+11);
        timeInfo.tm_min = atoi(tp+14);
        timeInfo.tm_sec = atoi(tp+17);
        secsSince70 = timegm(&timeInfo);
        *modTimep = secsSince70 * 1000000000;
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
        return (idFound? 0 : -1);
    }
    else {
        /* caller doesn't care about details, so return success only iff we have
         * all the info required.
         */
        return (allFound? 0 : -1);
    }
}

/* called with dir lock held, but not child lock held */
int32_t
CnodeMs::nameSearch(std::string name, CnodeMs **childpp)
{
    CnodeBackEntry *backp;
    CnodeMs *childp;

    for(backp = _children.head(); backp; backp=backp->_dqNextp) {
        if (name == backp->_name) {
            /* return this one */
            childp = backp->_childp;
            childp->hold();
            *childpp = childp;
            return 0;
        }
    }
    return -1;
}

int32_t
CnodeMs::lookup(std::string name, Cnode **childpp, CEnv *envp)
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

    /* lock parent */
    lockSet.add(this);

    code = nameSearch(name, (CnodeMs **) childpp);
    if (code == 0) {
        return 0;
    }

    /* temporarily drop lock over getPath call */
    lockSet.remove(this);

    code = getPath(&dirPath, envp);
    if (code != 0) {
        printf("cfs lookup: getpath failed code=%d\n", code);
        return code;
    }

    callbackString = "/v1.0/me/drive/root:" + Rst::urlPathEncode(dirPath + name);
    
    while(1) {
        connp = _cfsp->_xapiPoolp->getConn(std::string("graph.microsoft.com"), 443, /* TLS */ 1);
        reqp = new XApi::ClientReq();
        authHeader = "Bearer " + _cfsp->_loginp->getAuthToken();
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
            break;
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
        
        inPipep->waitForEof();

        if (code) {
            break;
        }

        if (_cfsp->retryError(reqp, jnodep)) {
            delete reqp;
            reqp = NULL;
            continue;
        }

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
    
    if (_cfsp->_verbose)
        printf("sendSmallFile: id=%s name=%s\n", _id.c_str(), name.c_str());

    if (_isRoot) {
        callbackString = "/v1.0/me/drive/root:/" + Rst::urlPathEncode(name) + ":/content";
    }
    else
        callbackString = ("/v1.0/me/drive/items/" + _id + ":/" + Rst::urlPathEncode(name) +
                          ":/content");
    
    /* no post data */
    dataBufferp = new char[dataBufferBytes];

    while(1) {
        sourcep->getAttr(&dataAttrs);
        sendBytes = dataAttrs._length;

        connp = _cfsp->_xapiPoolp->getConn(std::string("graph.microsoft.com"), 443, /* TLS */ 1);
        reqp = new XApi::ClientReq();
        reqp->setSendContentLength(sendBytes);
        authHeader = "Bearer " + _cfsp->_loginp->getAuthToken();
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
            reqp->waitForHeadersDone();
            delete reqp;
            reqp = NULL;
            break;
        }
        
        code = reqp->waitForHeadersDone();
        if (code != 0) {
            delete reqp;
            reqp = NULL;
            break;
        }

        /* wait for a response, and then parse it */
        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < (signed) sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        
        inPipep->waitForEof();

        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        if (code != 0) {
            if (reqp) {
                delete reqp;
                reqp = NULL;
            }
            printf("json parse failed code=%d\n", code);
            break;
        }
        
        if (_cfsp->retryError(reqp, jnodep)) {
            delete reqp;
            reqp = NULL;
            continue;
        }

        if (reqp) {
            delete reqp;
            reqp = NULL;
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
                    childp->_valid = 0;
                }
                childp->release();    /* lock held by getCnodeLinked */
                childp = NULL;
            }
        }
        break;
    }

    osp_assert(reqp == NULL);

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
    
    if (_cfsp->_verbose)
        printf("mkdir: id=%s name=%s\n", _id.c_str(), name.c_str());

    if (_isRoot)
        callbackString = "/v1.0/me/drive/root/children";
    else
        callbackString = "/v1.0/me/drive/items/" + _id + "/children";
    
    postData = "{\n";
    postData += "\"name\": \"" + name + "\",\n";
    postData += "\"folder\": {},\n";
    postData += "\"@microsoft.graph.conflictBehavior\": \"fail\"\n";
    postData += "}\n";
    
    while(1) {
        connp = _cfsp->_xapiPoolp->getConn(std::string("graph.microsoft.com"), 443, /* TLS */ 1);
        reqp = new XApi::ClientReq();
        reqp->setSendContentLength(postData.length());
        authHeader = "Bearer " + _cfsp->_loginp->getAuthToken();
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
            break;
        }
        httpError = reqp->getHttpError();

        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < (signed) sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        
        inPipep->waitForEof();

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
                code = 17;
                errorOk = 1;    /* even though we had an error, this is OK */
                delete reqp;
                reqp = NULL;
                break;
            }
        }
        
        if (!errorOk && _cfsp->retryError(reqp, jnodep)) {
            delete reqp;
            reqp = NULL;
            continue;
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

    osp_assert(reqp == NULL);

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
    
    if (_isRoot)
        callbackString = "/v1.0/me/drive/root";
    else
        callbackString = "/v1.0/me/drive/items/" + _id;
    
    while(1) {
        connp = _cfsp->_xapiPoolp->getConn(std::string("graph.microsoft.com"), 443, /* TLS */ 1);
        reqp = new XApi::ClientReq();
        authHeader = "Bearer " + _cfsp->_loginp->getAuthToken();
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
            break;
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
        
        inPipep->waitForEof();

        if (_cfsp->retryError(reqp, jnodep)) {
            delete reqp;
            reqp = NULL;
            continue;
        }

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

    if (_cfsp->_verbose)
        printf("fillAtts: id='%s' done code=%d\n", _id.c_str(), code);
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
        connp = _cfsp->_xapiPoolp->getConn(std::string("graph.microsoft.com"), 443, /* TLS */ 1);
        reqp = new XApi::ClientReq();
        reqp->setSendContentLength(postData.length());
        authHeader = "Bearer " + _cfsp->_loginp->getAuthToken();
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
            break;
        }

        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < (signed) sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        
        inPipep->waitForEof();

        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        if (code != 0) {
            printf("json parse failed code=%d\n", code);
            delete reqp;
            reqp = NULL;
            break;
        }
        
        if (_cfsp->retryError(reqp, jnodep)) {
            delete reqp;
            reqp = NULL;
            continue;
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
    osp_assert(reqp == NULL);

    return code;
    
}

int32_t
CnodeMs::sendData( std::string *sessionUrlp,
                   CDataSource *sourcep,
                   uint64_t fileLength,
                   uint64_t byteOffset,
                   uint32_t byteCount)
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
    
    Rst::splitUrl(*sessionUrlp, &sessionHost, &sessionRelativeUrl, &port);

    osp_assert(byteCount < 4*1024*1024);
    dataBufferp = new char[byteCount];

    while(1) {
        connp = _cfsp->_xapiPoolp->getConn(sessionHost, port, /* TLS */ 1);
        /* read some bytes */
        readCount = (byteOffset + byteCount > fileLength?
                     fileLength - byteOffset :
                     byteCount);
        actuallyReadCount = sourcep->read( byteOffset, readCount, dataBufferp);
        if (actuallyReadCount == 0)
            break;
        if (actuallyReadCount < 0) {
            code = -1;
            break;
        }

        reqp = new XApi::ClientReq();
        reqp->setSendContentLength(actuallyReadCount);
        authHeader = "Bearer " + _cfsp->_loginp->getAuthToken();
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
            break;
        }

        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < (signed) sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        
        inPipep->waitForEof();

        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        if (code != 0) {
            printf("json parse failed code=%d\n", code);
            delete reqp;
            reqp = NULL;
            break;
        }

        if (_cfsp->retryError(reqp, jnodep)) {
            delete reqp;
            reqp = NULL;
            continue;
        }

        delete reqp;
        reqp = NULL;

        break;
    }

    if (jnodep) {
        delete jnodep;
        jnodep = NULL;
    }
    if (dataBufferp) {
        delete [] dataBufferp;
    }
    osp_assert(reqp == NULL);

    if (code == 0)
        return actuallyReadCount;
    else
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
    uint32_t bytesPerPut = 320*1024;
    
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
                         bytesPerPut);
        if (code < 0) {
            printf("sendFile: sendData failed code=%d\n", code);
            abortSession( &sessionUrl);
            return code;
        }
        else if (code < bytesPerPut) {
            /* we've hit EOF, so we're done */
            if (bytesCopiedp)
                *bytesCopiedp += code;
            code = 0;
            break;
        }
        else {
            /* update counters */
            if (bytesCopiedp)
                *bytesCopiedp += code;
            currentOffset += bytesPerPut;
        }
    }

    if (_cfsp->_verbose)
        printf("sendFile: done code=%d\n", code);

    return code;
}

void
CnodeMs::hold() {
    _cfsp->_refLock.take();
    _refCount++;
    _cfsp->_refLock.release();
}

void
CnodeMs::release() {
    osp_assert(_refCount > 0);

    _cfsp->_refLock.take();
    _refCount--;
    _cfsp->_refLock.release();
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
        rootp = new CnodeMs();
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

int32_t
CfsMs::getCnode(std::string *idp, CnodeMs **cnodepp)
{
    uint64_t hashValue;
    CnodeMs *cp;

    _lock.take();

    hashValue = Cfs::fnvHash64(idp) % _hashSize;
    for(cp = _hashTablep[hashValue]; cp; cp=cp->_nextIdHashp) {
        if (cp->_id == *idp)
            break;
    }

    if (!cp) {
        cp = new CnodeMs();
        cp->_cfsp = this;
        cp->_id = *idp;
    }
    else {
        cp->hold();
    }
    *cnodepp = cp;

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

    code = getCnode(idp, &childp);
    if (code)
        return code;

    if (!parentp) {
        childp->release();
        return 0;
    }

    lockSetp->add(parentp);
    lockSetp->add(childp);

    /* otherwise, thread us in */
    for(entryp=childp->_backEntriesp; entryp; entryp=entryp->_nextSameChildp) {
        if (entryp->_name == name)
            break;
    }

    if (!entryp) {
        entryp = new CnodeBackEntry();
        entryp->_nextSameChildp = childp->_backEntriesp;
        childp->_backEntriesp = entryp;
        entryp->_name = name;
        entryp->_parentp = parentp;
        entryp->_childp = childp;
        parentp->_children.append(entryp);
        parentp->hold();
    }

    *cnodepp = childp;
    return 0;
}

int
CfsMs::retryError(XApi::ClientReq *reqp, Json::Node *parsedNodep)
{
    uint32_t httpError;
    httpError = reqp->getHttpError();
    int32_t code;

    if (reqp->getError() != 0)
        return 0;
    else if (httpError >= 200 && httpError < 300)
        return 0;
    else if (httpError == 401) {
        std::string refreshToken = _loginp->getRefreshToken();
        /* authentication expired; use refresh token */
        if (refreshToken.length() == 0) {
            /* some login mechanisms don't have refresh tokens */
            _loginp->logout();
            return 0;
        }
        code = _loginp->refresh();
        return (code == 0);
    }
    else if ( (httpError >= 502 && httpError <= 504) ||
              httpError == 429) {
        /* overloaded server, or bad choice of server.  Must rebind */
        reqp->resetConn();
        sleep(1);
        return 1;
    }
    else
        return 0;
}

