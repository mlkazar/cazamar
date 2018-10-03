#include <stdio.h>
#include <string>

#include "sapi.h"
#include "xapi.h"
#include "bufsocket.h"
#include "buftls.h"
#include "json.h"

void client(int argc, char **argv, int port);
void server(int argc, char **argv, int port);

std::string main_webAuthToken;

int
main(int argc, char **argv)
{
    int port;

    if (argc <= 1) {
        printf("usage: sapitest <port>\n");
        return 1;
    }

    port = atoi(argv[1]);

    /* peel off command name and port */
    server(argc-2, argv+2, port);

    return 0;
}

class SApiTestReq : public SApi::ServerReq {
public:
    static SApiTestReq *factory( SApi *sapip) {
        return new SApiTestReq(sapip);
    }

    SApiTestReq(SApi *sapip) : ServerReq(sapip) {
        return;
    }

    void AppleLoginKeyDataMethod();

    void AppleLoginMethod();

    void HomeScreenMethod();

    void WriteTestMethod();

    void ReadTestMethod();

    void DeleteTestMethod();
};

void
SApiTestReq::AppleLoginKeyDataMethod()
{
    char tbuffer[4096];
    int32_t code;
    // Rst::Hdr *hdrp;
    std::string response;
    SApi::Dict dict;
    Json json;
    const char *urlp;
    
    CThreadPipe *inPipep = getIncomingPipe();
    
    printf("server: in loginKeyData startMethod\n");
#if 0
    for(hdrp = getRecvHeaders(); hdrp; hdrp=hdrp->_dqNextp) {
        printf("Header %s:%s\n", hdrp->_key.c_str(), hdrp->_value.c_str());
    }
#endif
    printf("reading data...\n");
    while(1) {
        code = inPipep->read(tbuffer, sizeof(tbuffer)-1);
        if (code <= 0)
            break;
        if (code >= sizeof(tbuffer))
            printf("server: bad count from pipe read\n");
        tbuffer[code] = 0;
    }
    response = std::string(tbuffer);
    main_webAuthToken = Rst::urlEncode(&response);
    printf("\nReads done\n");
    printf("webAuthToken token: %s\n", main_webAuthToken.c_str());
    
    urlp = const_cast<char *>(_rstReqp->getRcvUrl()->c_str());
    printf("post url=%s\n", urlp);
    
    inputReceived();
    requestDone();
}

void
SApiTestReq::AppleLoginMethod()
{
    char tbuffer[4096];
    char *obufferp;
    int32_t code;
    // Rst::Hdr *hdrp;
    std::string response;
    SApi::Dict dict;
    const char *tp;
    Json::Node *jnodep;
    Json::Node *redirNodep;
    Json json;

    CThreadPipe *inPipep = getIncomingPipe();
    CThreadPipe *outPipep = getOutgoingPipe();
    printf("server: in applelogin startMethod\n");
#if 0
    for(hdrp = getRecvHeaders(); hdrp; hdrp=hdrp->_dqNextp) {
        printf("Header %s:%s\n", hdrp->_key.c_str(), hdrp->_value.c_str());
    }
#endif
    printf("reading applelogin data...\n");
    while(1) {
        code = inPipep->read(tbuffer, sizeof(tbuffer)-1);
        if (code <= 0)
            break;
        if (code >= (signed) sizeof(tbuffer))
            printf("server: bad count from pipe read\n");
        tbuffer[code] = 0;
        printf("%s", tbuffer);
    }
    printf("\nReads (applelogin) done\n");
    
    {
        XApi *xapip;
        XApi::ClientConn *connp;
        BufGen *bufGenp;
        XApi::ClientReq *reqp;
        CThreadPipe *inPipep;
        char tbuffer[16384];
        char *urlp;
        
        xapip = new XApi();
        bufGenp = new BufTls("");
        bufGenp->init(const_cast<char *>("api.apple-cloudkit.com"), 443);
        connp = xapip->addClientConn(bufGenp);
        reqp = new XApi::ClientReq();
        urlp = const_cast<char *>(_rstReqp->getRcvUrl()->c_str());
        printf("url=%s\n", urlp);
        reqp->setSendContentLength(0);
        reqp->startCall( connp,
                         "/database/1/iCloud.com.Cazamar.Web/development/public/users/caller?ckAPIToken=ef0651510f74629ed41bf76d81df7c0be2f3e5bcd532291f8b8f3671e5d9310b",
                         /* !isPost */ XApi::reqGet);
        code = reqp->waitForHeadersDone();
        printf("waitforheaders (applelogin) code is %d\n", code);
        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < (signed) sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        printf("Received (applelogin) %d bytes of data:'%s'\n", code, tbuffer);
        
        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        printf("json parse code=%d\n", code);
        tp = (char *) "Default junk";
        if (code == 0) {
            redirNodep = jnodep->searchForChild("redirectURL", 0);
            if (redirNodep)
                tp = redirNodep->_children.head()->_name.c_str();
        }

        delete reqp;
        reqp = NULL;
        
        dict.add("redir", tp);
        code = getConn()->interpretFile((char *) "sapi-apple.html", &dict, &response);
        if (code != 0) {
            sprintf(tbuffer, "Oops, interpretFile code is %d\n", code);
            obufferp = tbuffer;
        }
        else {
            obufferp = const_cast<char *>(response.c_str());
        }
    }
    
    setSendContentLength(strlen(obufferp));
    
    /* reverse the pipe -- must know length, or have set content length to -1 */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    printf("server: (applelogin) sent %d/%d bytes\n", code, (int) strlen(obufferp));
    outPipep->eof();
    
    requestDone();
    printf("server (applelogin): all done\n");
}

void
SApiTestReq::HomeScreenMethod()
{
    char tbuffer[16384];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
        
    CThreadPipe *outPipep = getOutgoingPipe();
    code = getConn()->interpretFile((char *) "home-screen.html", &dict, &response);
    if (code != 0) {
        sprintf(tbuffer, "Oops, interpretFile code is %d\n", code);
        obufferp = tbuffer;
    }
    else {
        obufferp = const_cast<char *>(response.c_str());
    }
    
    setSendContentLength(strlen(obufferp));
    
    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    printf("server(homescreen): sent %d/%d bytes\n", code, (int) strlen(obufferp));
    outPipep->eof();
    
    requestDone();
    printf("server(homescreen): all done\n");
}

void
SApiTestReq::WriteTestMethod()
{
    char tbuffer[4096];
    char *obufferp;
    int32_t code;
    // Rst::Hdr *hdrp;
    std::string response;
    SApi::Dict dict;
    const char *tp;
    Json::Node *jnodep = NULL;
    Json::Node *redirNodep = NULL;
    Json::Node *uploadRecordValueNodep = NULL;
    Json::Node *fieldsNodep = NULL;
    Json::Node *dataObjNodep = NULL;
    std::string uploadUrl;
    std::string tstring;
    Json json;

    CThreadPipe *inPipep = getIncomingPipe();
    CThreadPipe *outPipep = NULL;
    printf("server: in objtest startMethod\n");
#if 0
    for(hdrp = getRecvHeaders(); hdrp; hdrp=hdrp->_dqNextp) {
        printf("Header %s:%s\n", hdrp->_key.c_str(), hdrp->_value.c_str());
    }
#endif
    printf("reading (objtest) data...\n");
    while(1) {
        code = inPipep->read(tbuffer, sizeof(tbuffer)-1);
        if (code <= 0)
            break;
        if (code >= (signed) sizeof(tbuffer))
            printf("server: bad count from pipe read\n");
        tbuffer[code] = 0;
        printf("%s", tbuffer);
    }
    printf("\nReads (objtest) done\n");
    
    XApi *xapip;
    XApi::ClientConn *connp;
    BufGen *bufGenp;

    xapip = new XApi();
    bufGenp = new BufTls("");
    bufGenp->init(const_cast<char *>("api.apple-cloudkit.com"), 443);
    connp = xapip->addClientConn(bufGenp);

    /* send upload request; returns URL to use */
    {
        XApi::ClientReq *reqp;
        CThreadPipe *inPipep;
        char tbuffer[16384];
        char *urlp;
        Json::Node *nameNodep;
        Json::Node *leafNodep;
        Json::Node *arrayNodep;
        Json::Node *tokensNodep;
        Json::Node *uploadNodep;
        std::string jsonData;

        uploadNodep = new Json::Node();
        uploadNodep->initStruct();

        arrayNodep = new Json::Node();
        arrayNodep->initArray();
        tokensNodep = new Json::Node();
        tokensNodep->initStruct();

        leafNodep = new Json::Node();
        leafNodep->initString("100", 1);   /* object name */
        nameNodep = new Json::Node();
        nameNodep->initNamed("recordName", leafNodep);
        tokensNodep->appendChild(nameNodep);

        leafNodep = new Json::Node();
        leafNodep->initString("bucketType", 1);   /* object name */
        nameNodep = new Json::Node();
        nameNodep->initNamed("recordType", leafNodep);
        tokensNodep->appendChild(nameNodep);

        leafNodep = new Json::Node();
        leafNodep->initString("dataObj", 1);   /* object name */
        nameNodep = new Json::Node();
        nameNodep->initNamed("fieldName", leafNodep);
        tokensNodep->appendChild(nameNodep);

        arrayNodep->appendChild(tokensNodep);

        nameNodep = new Json::Node();
        nameNodep->initNamed("tokens", tokensNodep);
        uploadNodep->appendChild(nameNodep);

        uploadNodep->unparse(&jsonData);
        printf("unparsed upload request to:\n%s\n", jsonData.c_str());

        reqp = new XApi::ClientReq();
        urlp = const_cast<char *>(_rstReqp->getRcvUrl()->c_str());
        printf("url=%s\n", urlp);
        reqp->setSendContentLength(jsonData.length());
        sprintf(tbuffer, "/database/1/iCloud.com.Cazamar.Web/development/private/assets/upload?ckWebAuthToken=%s&ckAPIToken=ef0651510f74629ed41bf76d81df7c0be2f3e5bcd532291f8b8f3671e5d9310b", main_webAuthToken.c_str());
        printf("Doing put call (upload)with '%s'\n", tbuffer);
        reqp->startCall( connp,
                         tbuffer,
                         /* isPost */ XApi::reqPost);

        outPipep = reqp->getOutgoingPipe();

        outPipep->write(jsonData.c_str(), jsonData.length());
        outPipep->eof();

        code = reqp->waitForHeadersDone();
        printf("waitforheaders (objtest) code is %d\n", code);
        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        printf("Received (objtest2) %d bytes of data:'%s'\n", code, tbuffer);
        
        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        printf("json (objtest) parse code=%d\n", code);
        if (code == 0) {
            redirNodep = jnodep->searchForChild("url", 0);
            if (redirNodep)
                uploadUrl = redirNodep->_children.head()->_name;
        }
        delete reqp;
        reqp = NULL;
    }
        
    /* perform upload to returned URL, retrieving 5 element key */
    {
        XApi::ClientReq *reqp;
        CThreadPipe *inPipep;
        char tbuffer[16384];
        std::string jsonData;
        std::string urlHost;
        std::string urlPath;
        XApi::ClientConn *uploadConnp;
        BufGen *uploadSocketp;
        uint16_t port;

        const char *fileData = "This is a test for Jello Biafra\n";

        reqp = new XApi::ClientReq();
        printf("**sapitest req %p allocated\n", reqp);

        /* we should really be getting the default port from splitUrl */
        reqp->setSendContentLength(strlen(fileData));
        Rst::splitUrl(uploadUrl, &urlHost, &urlPath, &port);
        uploadSocketp = new BufTls("");
        uploadSocketp->init(const_cast<char *>(urlHost.c_str()), port);
        uploadConnp = xapip->addClientConn(uploadSocketp);
        
        strcpy(tbuffer, urlPath.c_str());

        printf("Doing put call (upload)with '%s'\n", tbuffer);
        printf("**sapitest req startcall %p\n", reqp);
        reqp->startCall( uploadConnp,
                         tbuffer,
                         /* isPost */ XApi::reqPost);

        outPipep = reqp->getOutgoingPipe();

        outPipep->write(fileData, strlen(fileData));
        outPipep->eof();
        printf("**data written to outgoing pip req=%p pipe=%p count=%d atEof=%d\n",
               reqp, outPipep, outPipep->count(), outPipep->atEof());

        code = reqp->waitForHeadersDone();
        printf("waitforheaders (objtest upload data) code is %d\n", code);
        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        printf("Received (objtest2) %d bytes of data:'%s'\n", code, tbuffer);
        
        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        printf("json (objtest) parse code=%d\n", code);
        if (code == 0) {
            redirNodep = jnodep->searchForChild("singleFile", 0);
            if (redirNodep) {
                uploadRecordValueNodep = redirNodep->_children.head();
                uploadRecordValueNodep->printToCPP(&tstring);
                printf("target value='%s'\n", tstring.c_str());
            }
        }
        delete reqp;
        reqp = NULL;
    }

    /* create record using 5 element key */
    {
        XApi::ClientReq *reqp;
        CThreadPipe *inPipep;
        char tbuffer[16384];
        char *urlp;
        Json::Node *opNodep;
        Json::Node *nameNodep;
        Json::Node *leafNodep;
        Json::Node *recordNodep;
        std::string jsonData;
        Json::Node *uploadNodep = NULL;

        opNodep = new Json::Node();
        opNodep->initStruct();

        /* create an operations record, starting with 'operationType' */
        leafNodep = new Json::Node();
        leafNodep->initString("forceUpdate", 1);
        nameNodep = new Json::Node();
        nameNodep->initNamed("operationType", leafNodep);
        opNodep->appendChild(nameNodep);

        /* record is a peer of operationType */
        recordNodep = new Json::Node();
        recordNodep->initStruct();
        nameNodep = new Json::Node();
        nameNodep->initNamed("record", recordNodep);
        opNodep->appendChild(nameNodep);

        /* add in desiredKeys */
        leafNodep = new Json::Node();
        leafNodep->initArray();
        nameNodep = new Json::Node();
        nameNodep->initNamed("desiredKeys", leafNodep);
        recordNodep->appendChild(nameNodep);

        /* add in recordName */
        leafNodep = new Json::Node();
        leafNodep->initString("100", 1);
        nameNodep = new Json::Node();
        nameNodep->initNamed("recordName", leafNodep);
        recordNodep->appendChild(nameNodep);

        leafNodep = new Json::Node();
        leafNodep->initString("bucketType", 1);
        nameNodep = new Json::Node();
        nameNodep->initNamed("recordType", leafNodep);
        recordNodep->appendChild(nameNodep);

        /* add in fields struct, containing a named dataObj field that has
         * a value of the uploaded file description.  Then add 'fields' to
         * the operation we're constructing.
         */
        fieldsNodep = new Json::Node();
        fieldsNodep->initStruct();
        nameNodep = new Json::Node();
        nameNodep->initNamed("fields", fieldsNodep);
        recordNodep->appendChild(nameNodep);

        nameNodep = new Json::Node();
        nameNodep->initNamed("value", uploadRecordValueNodep);
        dataObjNodep = new Json::Node();
        dataObjNodep->initStruct();
        dataObjNodep->appendChild(nameNodep);
        nameNodep = new Json::Node();
        nameNodep->initNamed("dataObj", dataObjNodep);
        fieldsNodep->appendChild(nameNodep);

        /* now turn the operation in opNodep into an array and name it 'operations' */
        leafNodep = new Json::Node();
        leafNodep->initArray();
        leafNodep->appendChild(opNodep);
        nameNodep = new Json::Node();
        nameNodep->initNamed("operations", leafNodep);

        /* and finally take the operations named element and put it into a struct */
        uploadNodep = new Json::Node();
        uploadNodep->initStruct();
        uploadNodep->appendChild(nameNodep);

        /* fill fields with a single field representing an uploadable object */
        uploadNodep->unparse(&jsonData);
        printf("unparsed record create request to:\n%s\nEOF\n", jsonData.c_str());

        reqp = new XApi::ClientReq();
        urlp = const_cast<char *>(_rstReqp->getRcvUrl()->c_str());
        printf("url=%s\n", urlp);
        reqp->setSendContentLength(jsonData.length());
        sprintf(tbuffer, "/database/1/iCloud.com.Cazamar.Web/development/private/records/modify?ckWebAuthToken=%s&ckAPIToken=ef0651510f74629ed41bf76d81df7c0be2f3e5bcd532291f8b8f3671e5d9310b",
                main_webAuthToken.c_str());
        printf("Doing put call (create)with '%s'\n", tbuffer);
        reqp->startCall( connp,
                         tbuffer,
                         /* isPost */ XApi::reqPost);

        outPipep = reqp->getOutgoingPipe();

        outPipep->write(jsonData.c_str(), jsonData.length());
        outPipep->eof();

        code = reqp->waitForHeadersDone();
        printf("waitforheaders (objtest) code is %d\n", code);
        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        printf("Received (objtest2) %d bytes of data:'%s'\n", code, tbuffer);
        
        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        printf("json (record create) parse code=%d\n", code);
        delete reqp;
        reqp = NULL;
    }

    outPipep = getOutgoingPipe();

    dict.add("redir", tp);
    code = getConn()->interpretFile((char *) "sapi-apple.html", &dict, &response);
    if (code != 0) {
        sprintf(tbuffer, "Oops, interpretFile code is %d\n", code);
        obufferp = tbuffer;
    }
    else {
        obufferp = const_cast<char *>(response.c_str());
    }
    
    setSendContentLength(strlen(obufferp));
    
    /* reverse the pipe -- must know length, or have set content length to -1 */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    printf("server (objtest): sent %d/%d bytes\n", code, (int) strlen(obufferp));
    outPipep->eof();
    
    requestDone();
    printf("server (objtest): all done\n");
}

void
SApiTestReq::ReadTestMethod()
{
    char tbuffer[4096];
    char *obufferp;
    int32_t code;
    // Rst::Hdr *hdrp;
    std::string response;
    SApi::Dict dict;
    const char *tp;
    Json::Node *jnodep = NULL;
    Json::Node *downloadNodep;
    std::string downloadUrl;
    std::string tstring;
    Json json;

    CThreadPipe *inPipep = getIncomingPipe();
    CThreadPipe *outPipep = NULL;
    printf("server: in objtest startMethod\n");
#if 0
    for(hdrp = getRecvHeaders(); hdrp; hdrp=hdrp->_dqNextp) {
        printf("Header %s:%s\n", hdrp->_key.c_str(), hdrp->_value.c_str());
    }
#endif
    printf("reading (objtest) data...\n");
    while(1) {
        code = inPipep->read(tbuffer, sizeof(tbuffer)-1);
        if (code <= 0)
            break;
        if (code >= sizeof(tbuffer))
            printf("server: bad count from pipe read\n");
        tbuffer[code] = 0;
        printf("%s", tbuffer);
    }
    printf("\nReads (objtest) done\n");
    
    XApi *xapip;
    XApi::ClientConn *connp;
    BufGen *bufGenp;

    xapip = new XApi();
    bufGenp = new BufTls("");
    bufGenp->init(const_cast<char *>("api.apple-cloudkit.com"), 443);
    connp = xapip->addClientConn(bufGenp);

    /* read record using 5 element key */
    {
        XApi::ClientReq *reqp;
        CThreadPipe *inPipep;
        char tbuffer[16384];
        char *urlp;
        Json::Node *nameNodep;
        Json::Node *leafNodep;
        Json::Node *recordNodep;
        Json::Node *lookupNodep;
        Json::Node *arrayNodep;
        std::string jsonData;
        Json::Node *readNodep = NULL;

        readNodep = new Json::Node();
        readNodep->initStruct();

        leafNodep = new Json::Node();
        leafNodep->initString("true", 1);
        nameNodep = new Json::Node();
        nameNodep->initNamed("numbersAsStrings", leafNodep);
        readNodep->appendChild(nameNodep);

        lookupNodep = new Json::Node();
        lookupNodep->initStruct();
        recordNodep = new Json::Node();
        recordNodep->initString("100", 1);
        nameNodep = new Json::Node();
        nameNodep->initNamed("recordName", recordNodep);
        lookupNodep->appendChild(nameNodep);

        /* this is the records array */
        arrayNodep = new Json::Node();
        arrayNodep->initArray();
        arrayNodep->appendChild(lookupNodep);

        nameNodep = new Json::Node();
        nameNodep->initNamed("records", arrayNodep);
        readNodep->appendChild(nameNodep);

        /* fill fields with a single field representing an uploadable object */
        readNodep->unparse(&jsonData);
        printf("unparsed record create request to:\n%s\nEOF\n", jsonData.c_str());

        reqp = new XApi::ClientReq();
        urlp = const_cast<char *>(_rstReqp->getRcvUrl()->c_str());
        printf("url=%s\n", urlp);
        reqp->setSendContentLength(jsonData.length());
        sprintf(tbuffer, "/database/1/iCloud.com.Cazamar.Web/development/private/records/lookup?ckWebAuthToken=%s&ckAPIToken=ef0651510f74629ed41bf76d81df7c0be2f3e5bcd532291f8b8f3671e5d9310b",
                main_webAuthToken.c_str());
        printf("Doing put call (create)with '%s'\n", tbuffer);
        reqp->startCall( connp,
                         tbuffer,
                         /* isPost */ XApi::reqPost);

        outPipep = reqp->getOutgoingPipe();

        outPipep->write(jsonData.c_str(), jsonData.length());
        outPipep->eof();

        code = reqp->waitForHeadersDone();
        printf("waitforheaders (objtest) code is %d\n", code);
        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        printf("Received (objtest2) %d bytes of data:'%s'\n", code, tbuffer);
        
        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        printf("json (record create) parse code=%d\n", code);

        /* find 'downloadURL' in returned structure */
        downloadNodep = jnodep->searchForChild(std::string("downloadURL"), 0);
        if (downloadNodep) {
            downloadUrl = downloadNodep->_children.head()->_name;
        }
        else
            downloadUrl = "";

        delete reqp;
        reqp = NULL;
    }

    {
        XApi::ClientReq *reqp;
        CThreadPipe *inPipep;
        char tbuffer[16384];
        std::string jsonData;
        std::string urlHost;
        std::string urlPath;
        XApi::ClientConn *downloadConnp;
        BufGen *downloadSocketp;
        uint16_t port;

        reqp = new XApi::ClientReq();
        printf("**sapitest req %p allocated\n", reqp);

        /* we should really be getting the default port from splitUrl */
        reqp->setSendContentLength(0);
        Rst::splitUrl(downloadUrl, &urlHost, &urlPath, &port);
        downloadSocketp = new BufTls("");
        downloadSocketp->init(const_cast<char *>(urlHost.c_str()), port);
        downloadConnp = xapip->addClientConn(downloadSocketp);
        
        strcpy(tbuffer, urlPath.c_str());

        printf("Doing put call (download)with '%s'\n", tbuffer);
        printf("**sapitest req startcall %p\n", reqp);
        reqp->startCall( downloadConnp,
                         tbuffer,
                         /* !isPost */ XApi::reqGet);

        outPipep = reqp->getOutgoingPipe();
        outPipep->eof();

        printf("**data written to outgoing pip req=%p pipe=%p count=%d atEof=%d\n",
               reqp, outPipep, outPipep->count(), outPipep->atEof());

        code = reqp->waitForHeadersDone();

        printf("waitforheaders (objtest download data) code is %d\n", code);
        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        printf("Received (objtest2) %d bytes of data:'%s'\n", code, tbuffer);
        
        tp = tbuffer;

        delete reqp;
        reqp = NULL;
    }

    outPipep = getOutgoingPipe();

    dict.add("redir", tp);
    code = getConn()->interpretFile((char *) "sapi-apple.html", &dict, &response);
    if (code != 0) {
        sprintf(tbuffer, "Oops, interpretFile code is %d\n", code);
        obufferp = tbuffer;
    }
    else {
        obufferp = const_cast<char *>(response.c_str());
    }
    
    setSendContentLength(strlen(obufferp));
    
    /* reverse the pipe -- must know length, or have set content length to -1 */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    printf("server (objtest): sent %d/%d bytes\n", code, (int) strlen(obufferp));
    outPipep->eof();
    
    requestDone();
    printf("server (objtest): all done\n");
}

void
SApiTestReq::DeleteTestMethod()
{
    char tbuffer[4096];
    char *obufferp;
    int32_t code;
    // Rst::Hdr *hdrp;
    std::string response;
    SApi::Dict dict;
    const char *tp;
    Json::Node *jnodep = NULL;
    std::string uploadUrl;
    std::string tstring;
    Json json;

    CThreadPipe *inPipep = getIncomingPipe();
    CThreadPipe *outPipep = NULL;
    printf("server: in objtest startMethod\n");
#if 0
    for(hdrp = getRecvHeaders(); hdrp; hdrp=hdrp->_dqNextp) {
        printf("Header %s:%s\n", hdrp->_key.c_str(), hdrp->_value.c_str());
    }
#endif
    printf("reading (objtest) data...\n");
    while(1) {
        code = inPipep->read(tbuffer, sizeof(tbuffer)-1);
        if (code <= 0)
            break;
        if (code >= sizeof(tbuffer))
            printf("server: bad count from pipe read\n");
        tbuffer[code] = 0;
        printf("%s", tbuffer);
    }
    printf("\nReads (objtest) done\n");
    
    XApi *xapip;
    XApi::ClientConn *connp;
    BufGen *bufGenp;

    xapip = new XApi();
    bufGenp = new BufTls("");
    bufGenp->init(const_cast<char *>("api.apple-cloudkit.com"), 443);
    connp = xapip->addClientConn(bufGenp);

    /* create record using 5 element key */
    {
        XApi::ClientReq *reqp;
        CThreadPipe *inPipep;
        char tbuffer[16384];
        char *urlp;
        Json::Node *opNodep;
        Json::Node *nameNodep;
        Json::Node *leafNodep;
        Json::Node *recordNodep;
        std::string jsonData;
        Json::Node *uploadNodep = NULL;

        opNodep = new Json::Node();
        opNodep->initStruct();

        /* create an operations record, starting with 'operationType' */
        leafNodep = new Json::Node();
        leafNodep->initString("forceDelete", 1);
        nameNodep = new Json::Node();
        nameNodep->initNamed("operationType", leafNodep);
        opNodep->appendChild(nameNodep);

        /* record is a peer of operationType */
        recordNodep = new Json::Node();
        recordNodep->initStruct();
        nameNodep = new Json::Node();
        nameNodep->initNamed("record", recordNodep);
        opNodep->appendChild(nameNodep);

#if 0
        /* add in desiredKeys */
        leafNodep = new Json::Node();
        leafNodep->initArray();
        nameNodep = new Json::Node();
        nameNodep->initNamed("desiredKeys", leafNodep);
        recordNodep->appendChild(nameNodep);
#endif

        /* add in recordName */
        leafNodep = new Json::Node();
        leafNodep->initString("100", 1);
        nameNodep = new Json::Node();
        nameNodep->initNamed("recordName", leafNodep);
        recordNodep->appendChild(nameNodep);

#if 0
        leafNodep = new Json::Node();
        leafNodep->initString("bucketType", 1);
        nameNodep = new Json::Node();
        nameNodep->initNamed("recordType", leafNodep);
        recordNodep->appendChild(nameNodep);

        /* add in fields struct, containing a named dataObj field that has
         * a value of the uploaded file description.  Then add 'fields' to
         * the operation we're constructing.
         */
        fieldsNodep = new Json::Node();
        fieldsNodep->initStruct();
        nameNodep = new Json::Node();
        nameNodep->initNamed("fields", fieldsNodep);
        recordNodep->appendChild(nameNodep);

        nameNodep = new Json::Node();
        nameNodep->initNamed("value", uploadRecordValueNodep);
        dataObjNodep = new Json::Node();
        dataObjNodep->initStruct();
        dataObjNodep->appendChild(nameNodep);
        nameNodep = new Json::Node();
        nameNodep->initNamed("dataObj", dataObjNodep);
        fieldsNodep->appendChild(nameNodep);
#endif

        /* now turn the operation in opNodep into an array and name it 'operations' */
        leafNodep = new Json::Node();
        leafNodep->initArray();
        leafNodep->appendChild(opNodep);
        nameNodep = new Json::Node();
        nameNodep->initNamed("operations", leafNodep);

        /* and finally take the operations named element and put it into a struct */
        uploadNodep = new Json::Node();
        uploadNodep->initStruct();
        uploadNodep->appendChild(nameNodep);

        /* fill fields with a single field representing an uploadable object */
        uploadNodep->unparse(&jsonData);
        printf("unparsed record create request to:\n%s\nEOF\n", jsonData.c_str());

        reqp = new XApi::ClientReq();
        urlp = const_cast<char *>(_rstReqp->getRcvUrl()->c_str());
        printf("url=%s\n", urlp);
        reqp->setSendContentLength(jsonData.length());
        sprintf(tbuffer, "/database/1/iCloud.com.Cazamar.Web/development/private/records/modify?ckWebAuthToken=%s&ckAPIToken=ef0651510f74629ed41bf76d81df7c0be2f3e5bcd532291f8b8f3671e5d9310b",
                main_webAuthToken.c_str());
        printf("Doing put call (create)with '%s'\n", tbuffer);
        reqp->startCall( connp,
                         tbuffer,
                         /* isPost */ XApi::reqPost);

        outPipep = reqp->getOutgoingPipe();

        outPipep->write(jsonData.c_str(), jsonData.length());
        outPipep->eof();

        code = reqp->waitForHeadersDone();
        printf("waitforheaders (objtest) code is %d\n", code);
        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        printf("Received (objtest2) %d bytes of data:'%s'\n", code, tbuffer);
        
        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        printf("json (record create) parse code=%d\n", code);
        delete reqp;
        reqp = NULL;
    }

    outPipep = getOutgoingPipe();

    dict.add("redir", tp);
    code = getConn()->interpretFile((char *) "sapi-apple.html", &dict, &response);
    if (code != 0) {
        sprintf(tbuffer, "Oops, interpretFile code is %d\n", code);
        obufferp = tbuffer;
    }
    else {
        obufferp = const_cast<char *>(response.c_str());
    }
    
    setSendContentLength(strlen(obufferp));
    
    /* reverse the pipe -- must know length, or have set content length to -1 */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    printf("server (objtest): sent %d/%d bytes\n", code, (int) strlen(obufferp));
    outPipep->eof();
    
    requestDone();
    printf("server (objtest): all done\n");
}

void
server(int argc, char **argv, int port)
{
    SApi *sapip;

    sapip = new SApi();
    /* need to cast since factory returns a subclass, and C++ doesn't accept that as
     * matching in type.
     */
    sapip->registerUrl( "/", 
                        (SApi::RequestFactory *) &SApiTestReq::factory,
                        (SApi::StartMethod) &SApiTestReq::HomeScreenMethod);
    sapip->registerUrl("/appleLogin", 
                       (SApi::RequestFactory *) &SApiTestReq::factory,
                       (SApi::StartMethod) &SApiTestReq::AppleLoginMethod);
    sapip->registerUrl("/keyData", 
                       (SApi::RequestFactory *) &SApiTestReq::factory,
                       (SApi::StartMethod) &SApiTestReq::AppleLoginKeyDataMethod);
    sapip->registerUrl("/writeTest", 
                       (SApi::RequestFactory *) &SApiTestReq::factory,
                       (SApi::StartMethod) &SApiTestReq::WriteTestMethod);
    sapip->registerUrl("/deleteTest",
                       (SApi::RequestFactory *) &SApiTestReq::factory,
                       (SApi::StartMethod) &SApiTestReq::DeleteTestMethod);
    sapip->registerUrl("/readTest",
                       (SApi::RequestFactory *) &SApiTestReq::factory,
                       (SApi::StartMethod) &SApiTestReq::ReadTestMethod);
    sapip->initWithPort(port);

    while(1) {
        sleep(1);
    }
}
