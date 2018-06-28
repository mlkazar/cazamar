#include <time.h>

#include "cfsms.h"
#include "xapi.h"
#include "buftls.h"

int32_t
CnodeMs::getPath(std::string *pathp, Cenv *envp)
{
    pathp->erase();
    /* TBD: figure out how to generate paths */
    return 0;
}

int32_t
CnodeMs::parseResults(Json::Node *jnodep, std::string *idp, uint64_t *sizep, time_t *modTimep)
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
        *modTimep = secsSince70;
        printf("ctime is %s\n", ctime(&secsSince70));
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
CnodeMs::mkdir(std::string name, Cnode **newDirpp, Cenv *envp)
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
    time_t modTime;
    CnodeMs *childp;
    
    if (_parentp == NULL)
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
        
        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        if (code == 0) {
            jnodep->print();
        }
        else {
            break;
        }
        
        code = parseResults(jnodep, &id, &size, &modTime);
        if (code == 0) {
            _cfsp->getCnode(&id, &childp);
            *newDirpp = childp;
        }

        inPipep->waitForEof();
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

int32_t
CnodeMs::getAttr(Cattr *attrp, Cenv *envp)
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
    time_t modTime;
    
    if (_parentp == NULL)
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

        parseResults(jnodep, &id, &size, &modTime);

        delete jnodep;
        reqp = NULL;
        code = 0;
        break;
    }

    return code;
}

int32_t
CfsMs::root(Cnode **nodepp, Cenv *envp)
{
    CnodeMs *rootp;

    rootp = new CnodeMs();
    rootp->_cfsp = this;
    *nodepp = rootp;

    return 0;
}

int32_t
CfsMs::getCnode(std::string *idp, CnodeMs **cnodepp)
{
    /* TBD: hash table for finding existing cnodes; valid flag for attrs */
    CnodeMs *cp;

    cp = new CnodeMs();
    cp->_cfsp = this;
    cp->_id = *idp;
    *cnodepp = cp;
    cp->_refCount = 1;
    return 0;
}
