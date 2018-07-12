#include <time.h>

#include "cfsms.h"
#include "xapi.h"
#include "buftls.h"

int32_t
CnodeMs::getPath(std::string *pathp, CEnv *envp)
{
    std::string tempPath;
    CnodeMs *tnodep = this;
    CnodeBackEntry *bep;
    while(tnodep) {
        if (tnodep->_isRoot) {
            break;
        }
        bep = tnodep->_backEntriesp;
        if (!bep)
            break;
        tempPath = bep->_name + "/" + tempPath;
        tnodep = bep->_parentp;
    }
    tempPath = "/" + tempPath;

    *pathp = tempPath;

    /* TBD: figure out how to generate paths */
    return 0;
}

int32_t
CnodeMs::parseResults( Json::Node *jnodep,
                       std::string *idp,
                       uint64_t *sizep,
                       uint64_t *modTimep,
                       uint64_t *changeTimep)
{
    Json::Node *tnodep;
    std::string modTimeStr;
    std::string sizeStr;
    int allFound = 1;

    tnodep = jnodep->searchForChild("id", 0);
    if (tnodep) {
        *idp = tnodep->_children.head()->_name;
    }
    else
        allFound = 0;

    tnodep = jnodep->searchForChild("lastModifiedDateTime", 0);
    if (tnodep) {
        time_t secsSince70;
        modTimeStr = tnodep->_children.head()->_name;
        const char *tp = modTimeStr.c_str();
        // USE mktime, gmtime, time to convert to local time_t
        // format is "2016-03-21T20:01:37Z" Z for Zulu time zone
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
    else
        allFound = 0;

    tnodep = jnodep->searchForChild("size", 0);
    if (tnodep) {
        sizeStr = tnodep->_children.head()->_name;
        *sizep = atoi(sizeStr.c_str());
    }
    else
        allFound = 0;

    return (allFound? 0 : -1);
}

int32_t
CnodeMs::lookup(std::string name, Cnode **childpp, CEnv *envp)
{
    /* perform getAttr operation */
    char tbuffer[0x4000];
    XApi *xapip;
    XApi::ClientConn *connp;
    BufGen *bufGenp;
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

    code = getPath(&dirPath, envp);
    if (code != 0)
        return code;

    callbackString = "/v1.0/me/drive/root:" + dirPath + name;
    
    xapip = new XApi();
    bufGenp = new BufTls("");
    bufGenp->init(const_cast<char *>("graph.microsoft.com"), 443);
    
    while(1) {
        connp = xapip->addClientConn(bufGenp);
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
        if (code == 0) {
            jnodep->print();
        }
        
        inPipep->waitForEof();
        delete reqp;
        reqp = NULL;

        code = parseResults(jnodep, &id, &size, &changeTime, &modTime);
        delete jnodep;
        jnodep = NULL;

        if (code == 0) {
            code = _cfsp->getCnodeLinked(this, name, &id, &childp, &lockSet);
            if (code == 0) {
                childp->_valid = 1;
                childp->_attrs._length = size;
                childp->_attrs._ctime = changeTime;
                childp->_attrs._mtime = modTime;
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
CnodeMs::mkdir(std::string name, Cnode **newDirpp, CEnv *envp)
{
    /* perform mkdir operation */
    char tbuffer[0x4000];
    XApi *xapip;
    XApi::ClientConn *connp;
    BufGen *bufGenp;
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
    
    lockSet.add(this);

    if (_isRoot)
        callbackString = "/v1.0/me/drive/root/children";
    else
        callbackString = "/v1.0/me/drive/items/" + _id + "/children";
    
    postData = "{\n";
    postData += "\"name\": \"" + name + "\",\n";
    postData += "\"folder\": {},\n";
    postData += "\"@microsoft.graph.conflictBehavior\": \"fail\"\n";
    postData += "}\n";
    
    xapip = new XApi();
    bufGenp = new BufTls("");
    bufGenp->init(const_cast<char *>("graph.microsoft.com"), 443);
    
    while(1) {
        connp = xapip->addClientConn(bufGenp);
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
        if (code == 0) {
            jnodep->print();
        }
        else {
            break;
        }
        
        code = parseResults(jnodep, &id, &size, &changeTime, &modTime);
        if (code == 0) {
            code = _cfsp->getCnodeLinked(this, name, &id, &childp, &lockSet);
            if (code == 0) {
                childp->_valid = 1;
                childp->_attrs._length = size;
                childp->_attrs._ctime = changeTime;
                childp->_attrs._mtime = modTime;
                *newDirpp = childp;
            }
        }
        else
            break;

        break;
    }

    if (jnodep) {
        delete jnodep;
        jnodep = NULL;
    }
    if (reqp) {
        delete reqp;
        reqp = NULL;
    }
    if (code)
        *newDirpp = NULL;

    return code;
}

/* called with Cnode lock held */
int32_t
CnodeMs::fillAttrs( CEnv *envp)
{
    /* perform getAttr operation */
    char tbuffer[0x4000];
    XApi *xapip;
    XApi::ClientConn *connp;
    BufGen *bufGenp;
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
    
    if (_isRoot)
        callbackString = "/v1.0/me/drive/root";
    else
        callbackString = "/v1.0/me/drive/items/" + _id;
    
    xapip = new XApi();
    bufGenp = new BufTls("");
    bufGenp->init(const_cast<char *>("graph.microsoft.com"), 443);
    
    while(1) {
        connp = xapip->addClientConn(bufGenp);
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
        if (code == 0) {
            jnodep->print();
        }
        
        inPipep->waitForEof();
        delete reqp;

        parseResults(jnodep, &id, &size, &changeTime, &modTime);
        _attrs._mtime = modTime;
        _attrs._ctime = changeTime;
        _attrs._length = size;
        _valid = 1;

        delete jnodep;
        reqp = NULL;
        code = 0;
        break;
    }

    return code;
}

int32_t
CnodeMs::getAttr(CAttr *attrp, CEnv *envp)
{
    int32_t code;
    CnodeLockSet lockSet;
    
    lockSet.add(this);

    if (_valid)
        return 0;
    code = fillAttrs(envp);
    return code;
}

int32_t
CnodeMs::startSession( std::string name,
                       std::string *sessionUrlp)
{
    /* perform mkdir operation */
    char tbuffer[0x4000];
    XApi *xapip;
    XApi::ClientConn *connp;
    BufGen *bufGenp;
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
        callbackString = "/v1.0/me/drive/root:/" + name + ":/createUploadSession";
    else
        callbackString = "/v1.0/me/drive/items/" + _id + ":/" + name + ":/createUploadSession";
    
    postData = "{\n";
    postData = "\"item\": {\n";
    postData += "\"@microsoft.graph.conflictBehavior\": \"replace\"\n";
    postData += "}\n";  /* close item */
    postData += "}\n";
    
    xapip = new XApi();
    bufGenp = new BufTls("");
    bufGenp->init(const_cast<char *>("graph.microsoft.com"), 443);
    
    while(1) {
        connp = xapip->addClientConn(bufGenp);
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
        if (code == 0) {
            jnodep->print();
        }
        else {
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

        break;
    }

    if (jnodep) {
        delete jnodep;
        jnodep = NULL;
    }
    if (reqp) {
        delete reqp;
        reqp = NULL;
    }

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
    XApi *xapip;
    std::string sessionHost;
    std::string sessionRelativeUrl;
    XApi::ClientConn *connp;
    BufGen *bufGenp;
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
    
    Rst::splitUrl(sessionUrlp, &sessionHost, &sessionRelativeUrl);

    osp_assert(byteCount < 4*1024*1024);
    dataBufferp = new char[byteCount];
    xapip = new XApi();
    bufGenp = new BufTls("");
    bufGenp->init(const_cast<char *>(sessionHost.c_str()), 443);
    
    while(1) {
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

        connp = xapip->addClientConn(bufGenp);
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
            break;
        }

        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        printf("sendData received %d bytes from response pipe read\n", (int) code);
        if (code >= 0 && code < (signed) sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        
        inPipep->waitForEof();

        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        if (code == 0) {
            jnodep->print();
        }
        else {
            printf("bad parse data is '%s'\n", tp);
            break;
        }
        
        break;
    }

    if (jnodep) {
        delete jnodep;
        jnodep = NULL;
    }
    if (dataBufferp) {
        delete [] dataBufferp;
    }
    if (reqp) {
        delete reqp;
        reqp = NULL;
    }

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
    if (code)
        return code;
    size = dataAttrs._length;

    /* MS claims in docs that multiples of this are only safe values;
     * commenters don't believe them.
     */
    uint32_t bytesPerPut = 320*1024;
    
    /* this locks the whole directory when uploading a file; it would be nicer if
     * we could get more concurrency here, but it isn't all that obviousl how.
     */
    lockSet.add(this);

    code = startSession( name, &sessionUrl);
    if (code)
        return code;

    remainingBytes = size;
    currentOffset = 0;
    while(remainingBytes > 0) {
        code = sendData( &sessionUrl,
                         sourcep,
                         size,
                         currentOffset,
                         bytesPerPut);
        if (code < 0) {
            abortSession( &sessionUrl);
            return code;
        }
        else if (code < bytesPerPut) {
            /* we've hit EOF, so we're done */
            code = 0;
            break;
        }
        else {
            /* update counters */
            currentOffset += bytesPerPut;
        }
    }

    return code;
}

int32_t
CfsMs::root(Cnode **nodepp, CEnv *envp)
{
    CnodeMs *rootp;
    int32_t code;
    CnodeLockSet lockSet;

    rootp = new CnodeMs();
    lockSet.add(rootp);
    rootp->_cfsp = this;
    rootp->_isRoot = 1;

    code = rootp->fillAttrs(envp);

    if (code == 0)
        *nodepp = rootp;
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
        cp->holdNL();
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
        childp->releaseNL();
        return 0;
    }

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
        parentp->holdNL();
    }

    *cnodepp = childp;
    return 0;
}
