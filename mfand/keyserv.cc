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

#define BIGSTR  4096

/* context for sapi */
class KeyServ {
public:
    class Entry {
    public:
        std::string _oauthId;   /* the ID from caller */
        std::string _oauthKey;  /* the security key */
        Entry *_dqNextp;
        Entry *_dqPrevp;
        uint32_t _timeSet;

        Entry() {
            _timeSet = 0;
        }
    };

    dqueue<Entry> _allKeys;
};

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

/* request to retrieve a key, given the corresponding ID; request has id=<url encoded id>
 * after the name.
 */
class AppleLoginKeyData : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcodep, SApi *sapip);

    AppleLoginKeyData(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();

    void sendResponse(uint32_t error, const char *errorp, const char *responsep);
};

void
AppleLoginKeyData::startMethod()
{
    // Rst::Hdr *hdrp;
    std::string response;
    SApi::Dict dict;
    Rst::Hdr *hdrp;
    dqueue<Rst::Hdr> *urlPairsp;
    std::string id;
    KeyServ *keyServp = static_cast<KeyServ *>(_sapip->getContext());
    KeyServ::Entry *ksep;
    
#if 0
    for(hdrp = getRecvHeaders(); hdrp; hdrp=hdrp->_dqNextp) {
        printf("Header %s:%s\n", hdrp->_key.c_str(), hdrp->_value.c_str());
    }
#endif
    
    urlPairsp = _rstReqp->getUrlPairs();
    for(hdrp = urlPairsp->head(); hdrp; hdrp=hdrp->_dqNextp) {
        if (strcasecmp(hdrp->_key.c_str(), "id") == 0) {
            id = hdrp->_value;
        }
    }

    if (id.length() == 0) {
        sendResponse(100, "no 'id' string", "");
        return;
    }

    for(ksep = keyServp->_allKeys.head(); ksep; ksep=ksep->_dqNextp) {
        if (ksep->_oauthId == id)
            break;
    }

    if (!ksep) {
        sendResponse(101, "id not found", "");
        return;
    }

    printf("ret>> key='%s' value='%s'\n", ksep->_oauthId.c_str(), ksep->_oauthKey.c_str());

    sendResponse(0, "", ksep->_oauthKey.c_str());
}

void
AppleLoginKeyData::sendResponse(uint32_t error, const char *errorp, const char *responsep)
{
    Json json;
    std::string jsonData;
    CThreadPipe *opipep;
    Json::Node *responseNodep;
    Json::Node *tnodep;
    Json::Node *nnodep;

    responseNodep = new Json::Node();
    responseNodep->initStruct();

    tnodep = new Json::Node();
    tnodep->initInt(error);
    nnodep = new Json::Node();
    nnodep->initNamed("error", tnodep);
    responseNodep->appendChild(nnodep);

    if (error != 0) {
        tnodep = new Json::Node();
        tnodep->initString(errorp, 1);
        nnodep = new Json::Node();
        nnodep->initNamed("errorString", tnodep);
        responseNodep->appendChild(nnodep);
    }
    else {
        tnodep = new Json::Node();
        tnodep->initString(responsep, 1);
        nnodep = new Json::Node();
        nnodep->initNamed("webAuthToken", tnodep);
        responseNodep->appendChild(nnodep);
    }

    responseNodep->unparse(&jsonData);
    setSendContentLength(jsonData.length());
    delete responseNodep;       /* frees all components, too */

    /* must set response length before this call, since this call starts sending
     * the response headers.
     */
    inputReceived();

    opipep = getOutgoingPipe();
    opipep->write( jsonData.c_str(), jsonData.length());
    opipep->eof();
    
    requestDone();
}

SApi::ServerReq *
AppleLoginKeyData::factory(std::string *opcodep, SApi *sapip)
{
    AppleLoginKeyData *reqp;

    reqp = new AppleLoginKeyData(sapip);
    return reqp;
}

/* The AppleLogin class is invoked when the Apple authentication
 * servers want to deliver a web auth token to the application, which
 * they do by sending an HTTP request to our URL /login.  The key for
 * our database lookup is provided by a "Referer:" URL, which contains
 * an oauth_token=XXX entry, which is our key (not the actual token,
 * but just the ID the key server will receive from the real
 * application when it wants to retrieve the actual web authentication
 * token).
 *
 * A separate 'ckWebAuthToken: XXX' header provides the actual web
 * authentication token that the key server will store until the
 * application requests it.
 */
class AppleLogin : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    AppleLogin(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

SApi::ServerReq *
AppleLogin::factory(std::string *opcodep, SApi *sapip)
{
    AppleLogin *serverReqp;

    serverReqp = new AppleLogin(sapip);
    return serverReqp;
}

/* parse the incoming request's headers to determine the key and the
 * value for our token lookup table.
 */
void
AppleLogin::startMethod()
{
    char tbuffer[BIGSTR];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    std::string tableKey;
    std::string tableValue;
    uint32_t ampPos;
    uint32_t tokenPos;
    uint32_t endPos;
    KeyServ *keyServp = static_cast<KeyServ *>(_sapip->getContext());
    int isMS = 0;
    int isApple = 0;
    Rst::Request *rstReqp;
    std::string *baseUrlp;
    Rst::Hdr *hdrp;
    dqueue<Rst::Hdr> *urlPairsp;

    rstReqp = getRstReq();
    CThreadPipe *inPipep = getIncomingPipe();
    CThreadPipe *outPipep = getOutgoingPipe();

    baseUrlp = rstReqp->getBaseUrl();
    
    if (strcmp(baseUrlp->c_str(), "/login4ms") == 0)
        isMS = 1;
    else if (strcmp(baseUrlp->c_str(), "/login") == 0)
        isApple = 1;

    if (isMS) {
        /* search for state and code strings for table entries */
        urlPairsp = _rstReqp->getUrlPairs();
        for(hdrp = urlPairsp->head(); hdrp; hdrp=hdrp->_dqNextp) {
            /* could also look for ckSession */
            if (strcasecmp("state", hdrp->_key.c_str()) == 0) {
                tableKey = hdrp->_value;
            }
            else if (strcasecmp("code", hdrp->_key.c_str()) == 0) {
                tableValue = hdrp->_value;
            }
        }
    }
    else {
        for(hdrp = getRecvHeaders(); hdrp; hdrp=hdrp->_dqNextp) {
            if (strcasecmp(hdrp->_key.c_str(), "referer") == 0) {
                ampPos = hdrp->_value.find('?');
                if (ampPos == std::string::npos) {
                    /* couldn't find parameter delimeter, so we'll fail */
                    printf("KeyServ: couldn't find '?' delimeter on incoming request\n");
                    break;
                }
                tokenPos = hdrp->_value.find("oauth_token=", ampPos+1);
                if (tokenPos == std::string::npos)
                    break;
                tokenPos += 12;     /* skip past '=' */
                endPos = hdrp->_value.find('&', tokenPos);
                if (endPos == std::string::npos) {
                    /* use rest of token */
                    tableKey = hdrp->_value.substr(tokenPos);
                }
                else {
                    /* go up to but not including the terminating '&' */
                    tableKey = hdrp->_value.substr(tokenPos, endPos-tokenPos);
                }
            }
#if 0
            printf("Header %s:%s\n", hdrp->_key.c_str(), hdrp->_value.c_str());
#endif
        }
    }
    while(1) {
        code = inPipep->read(tbuffer, sizeof(tbuffer)-1);
        if (code <= 0)
            break;
        if (code >= (signed) sizeof(tbuffer))
            printf("server: bad count from pipe read\n");
        tbuffer[code] = 0;
        printf("%s", tbuffer);
    }

    {
        /* print out some info for debugging */
        urlPairsp = _rstReqp->getUrlPairs();
        for(hdrp = urlPairsp->head(); hdrp; hdrp=hdrp->_dqNextp) {
            /* could also look for ckSession */
            if (strcasecmp("ckWebAuthToken", hdrp->_key.c_str()) == 0) {
                tableValue = hdrp->_value;
            }
        }
    }

    if (tableKey.length() > 0 && tableValue.length() > 0) {
        printf("<<Set key='%s'/value='%s'\n", tableKey.c_str(), tableValue.c_str());
        KeyServ::Entry *entryp = new KeyServ::Entry();
        entryp->_oauthId = tableKey;
        entryp->_oauthKey = tableValue;
        keyServp->_allKeys.prepend(entryp);
    }

    printf("\nReads (generic login) done\n");
    
    {
        if (isMS) {
            code = getConn()->interpretFile( (char *) "login-ms-done.html", 
                                             &dict, 
                                             &response);
        }
        else {
            code = getConn()->interpretFile( (char *) "login-apple-done.html", 
                                             &dict, 
                                             &response);
        }

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
    outPipep->eof();
    
    requestDone();
}

/* main program for the key server.  URL=/login is used for delivering the tokens
 * from the apple servers, and URL=/keyData is used by the application to retrieve
 * the delivered tokens.
 */
void
server(int argc, char **argv, int port)
{
    SApi *sapip;
    KeyServ *keyServp;

    keyServp = new KeyServ();
    sapip = new SApi();
    sapip->setContext(keyServp);
    sapip->useTls();
    
    /* called from Apple authentication servers with our key */
    sapip->registerUrl("/login", &AppleLogin::factory);
    sapip->registerUrl("/login4ms", &AppleLogin::factory);
    
    /* called by our application to retrieve a stored key */
    sapip->registerUrl("/keyData", &AppleLoginKeyData::factory);
    sapip->initWithPort(port);

    while(1) {
        sleep(1);
    }
}
