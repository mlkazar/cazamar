#include <stdio.h>
#include <string>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

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
class KeyServ : public CThread {
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
    CThreadMutex _lock;

    void prune(void *contextp);
};

void
KeyServ::prune( void *cxp)
{
    Entry *ep;
    Entry *nep;
    uint32_t now;

    while(1) {
        sleep(60);
        _lock.take();
        now = time(0);
        for(ep = _allKeys.head(); ep; ep=nep) {
            nep = ep->_dqNextp;

            if (now > ep->_timeSet + 120) {
                printf("deleting token with key=%s\n", ep->_oauthId.c_str());
                _allKeys.remove(ep);
                delete ep;
            }
        }
        _lock.release();
    }
}

class AppleLoginReq : public SApi::ServerReq {
public:
    static AppleLoginReq *factory(SApi *sapip) {
        return new AppleLoginReq(sapip);
    }

    AppleLoginReq(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void AppleLoginKeyDataMethod();

    void AppleLoginMethod();

    void RetrievalMethod();

    void sendResponse(uint32_t error, const char *errorp, const char *responsep);
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

void
AppleLoginReq::RetrievalMethod()
{
    // Rst::Hdr *hdrp;
    std::string response;
    SApi::Dict dict;
    Rst::Hdr *hdrp;
    dqueue<Rst::Hdr> *urlPairsp;
    std::string id;
    FILE *filep;
    char tbuffer[4096];
    struct stat tstat;
    uint32_t nbytes;
    int32_t code;
    CThreadPipe *pipep;
    
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
    
    filep = fopen(id.c_str(), "r");
    if (!filep) {
        sendResponse(500, "Ugh -- file not found", "");
        return;
    }

    code = fstat(fileno(filep), &tstat);
    if (code < 0) {
        sendResponse(500, "Ugh -- file not found", "");
        return;
    }

    nbytes = tstat.st_size;
    setSendContentLength(nbytes);

    inputReceived();

    pipep = getOutgoingPipe();
    filep = fopen(id.c_str(), "r");
    while(nbytes > 0) {
        code = fread(tbuffer, 1, sizeof(tbuffer), filep);
        if (code > 0)
            pipep->write(tbuffer, code);

        if (code != sizeof(tbuffer))
            break;
    }
    pipep->eof();
    requestDone();
    fclose(filep);

}

void
AppleLoginReq::AppleLoginKeyDataMethod()
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

    keyServp->_lock.take();
    for(ksep = keyServp->_allKeys.head(); ksep; ksep=ksep->_dqNextp) {
        if (ksep->_oauthId == id)
            break;
    }

    if (!ksep) {
        keyServp->_lock.release();
        sendResponse(101, "id not found", "");
        return;
    }

    printf("ret>> key='%s' value='%s'\n", ksep->_oauthId.c_str(), ksep->_oauthKey.c_str());

    sendResponse(0, "", ksep->_oauthKey.c_str());
    keyServp->_lock.release();
}

void
AppleLoginReq::sendResponse(uint32_t error, const char *errorp, const char *responsep)
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

/* parse the incoming request's headers to determine the key and the
 * value for our token lookup table.
 */
void
AppleLoginReq::AppleLoginMethod()
{
    char tbuffer[BIGSTR];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    std::string tableKey;
    std::string tableValue;
    size_t ampPos;
    size_t tokenPos;
    size_t endPos;
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
        entryp->_timeSet = time(0);

        keyServp->_lock.take();
        keyServp->_allKeys.prepend(entryp);
        keyServp->_lock.release();
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
    CThreadHandle *cp;

    keyServp = new KeyServ();
    sapip = new SApi();
    sapip->setContext(keyServp);
    sapip->useTls();
    
    /* called from Apple authentication servers with our key */
    sapip->registerUrl("/login",
                       (SApi::RequestFactory *) &AppleLoginReq::factory,
                       (SApi::StartMethod) &AppleLoginReq::AppleLoginMethod);
    sapip->registerUrl("/login4ms",
                       (SApi::RequestFactory *) &AppleLoginReq::factory,
                       (SApi::StartMethod) &AppleLoginReq::AppleLoginMethod);
    
    /* called by our application to retrieve a stored key */
    sapip->registerUrl("/keyData", 
                       (SApi::RequestFactory *) &AppleLoginReq::factory,
                       (SApi::StartMethod) &AppleLoginReq::AppleLoginKeyDataMethod);

    /* simple file retrieval scheme */
    sapip->registerUrl("/get",
                       (SApi::RequestFactory *) &AppleLoginReq::factory,
                       (SApi::StartMethod) &AppleLoginReq::RetrievalMethod);

    sapip->initWithPort(port);

    /* handle non-SSL get requests as well, on port N+1 (7701) */
    sapip = new SApi();
    sapip->setContext(keyServp);
    /* simple file retrieval scheme */
    sapip->registerUrl("/get",
                       (SApi::RequestFactory *) &AppleLoginReq::factory,
                       (SApi::StartMethod) &AppleLoginReq::RetrievalMethod);

    sapip->initWithPort(port+1);

    cp = new CThreadHandle();
    cp->init((CThread::StartMethod) &KeyServ::prune, keyServp, NULL);

    while(1) {
        sleep(1);
    }
}
