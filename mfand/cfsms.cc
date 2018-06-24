#include "cfsms.h"
#include "xapi.h"
#include "json.h"
#include "buftls.h"

int32_t
CnodeMs::getPath(std::string *pathp, Cenv *envp)
{
    pathp->erase();
    /* TBD: figure out how to generate paths */
    return 0;
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
    Json::Node *jnodep;
    std::string callbackString;
    std::string authHeader;
    int32_t code;
    
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
        delete jnodep;
        reqp = NULL;
        break;
    }

    return 0;
}

int32_t
CnodeMs::getAttr(Cattr *attrp, Cenv *envp)
{
    /* perform mkdir operation */
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
    int32_t code;
    
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
        delete jnodep;
        reqp = NULL;
        break;
    }

    return 0;
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
