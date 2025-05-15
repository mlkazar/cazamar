#include <stdio.h>
#include <string>
#include <stdlib.h>
#include "sapi.h"
#include "sapilogin.h"
#include "xapi.h"
#include "bufsocket.h"
#include "buftls.h"
#include "json.h"

void server(int argc, char **argv, int port);

/* these fields should all be indexed by a cookie dictionary hanging
 * off of the SApi structure.
 */
class AppTestContext {
public:
    AppTestContext() {
        return;
    }
};

/* This is the main program for the test application; it just runs the
 * server application function after peeling off the port and app name
 * from the command line arguments.
 */
int
main(int argc, char **argv)
{
    int port;

    if (argc <= 1) {
        printf("usage: apptest <port>\n");
        return 1;
    }

    port = atoi(argv[1]);

    /* peel off command name and port */
    server(argc-2, argv+2, port);

    return 0;
}

#if 0
void
AppleLoginKeyData::runTests(AppTestContext *contextp, SApiLoginGeneric *genLoginp)
{
    /* make a call to get access token and refresh tokens from MS, using code */
    {
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
        
        callbackString = "/v1.0/me/drive/root/children";

        postData = "{\n";
        postData += "\"name\": \"TestDir\",\n";
        postData += "\"folder\": {},\n";
        postData += "\"@microsoft.graph.conflictBehavior\": \"rename\"\n";
        postData += "}\n";

        xapip = new XApi();
        bufGenp = new BufTls("");
        bufGenp->init(const_cast<char *>("graph.microsoft.com"), 443);
        connp = xapip->addClientConn(bufGenp);
        reqp = new XApi::ClientReq();
        reqp->setSendContentLength(postData.length());
        authHeader = "Bearer " + genLoginp->getAuthToken();
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

        delete reqp;
        delete jnodep;
        reqp = NULL;
    }

    {
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
        
        callbackString = "/v1.0/me/drive/root:/testfile:/content";

        postData = "This is a test for test.txt\n";

        xapip = new XApi();
        bufGenp = new BufTls("");
        bufGenp->init(const_cast<char *>("graph.microsoft.com"), 443);
        connp = xapip->addClientConn(bufGenp);
        reqp = new XApi::ClientReq();
        reqp->setSendContentLength(postData.length());
        authHeader = "Bearer " + genLoginp->getAuthToken();
        reqp->addHeader("Authorization", authHeader.c_str());
        reqp->addHeader("Content-Type", "text/plain");
        reqp->startCall( connp,
                         callbackString.c_str(),
                         /* isPost */ XApi::reqPut);

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

        delete reqp;
        delete jnodep;
        reqp = NULL;
    }
}
#endif

/* This class presents the basic application test screen.  It gets you
 * to the login screen using SApiLoginApple if you aren't logged in
 * (if there's no saved token).  Otherwise, it presents the
 * application screen from login-home.html.
 *
 * The class runs the startMethod when the registered URL is reached.
 * That's '/' for this application.
 */
class HomeScreen : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(SApi *sapip) {
        HomeScreen *reqp;
        reqp = new HomeScreen(sapip);
        return reqp;
    }

    HomeScreen(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

void
HomeScreen::startMethod()
{
    char tbuffer[16384];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    std::string loginHtml;
    AppTestContext *appContextp;
    SApiLoginCookie *contextp;
    std::string authToken;
        
    if ((appContextp = (AppTestContext *) getCookieKey("main")) == NULL) {
        appContextp = new AppTestContext();
        setCookieKey("main", appContextp);
        printf("Setting Cookie to %p\n", appContextp);
    }
    else {
        printf("Cookie already set in homepage %p\n", appContextp);
    }

    /* this does a get if it already exists */
    contextp = SApiLogin::createLoginCookie(this);

    if (contextp && contextp->getActive())
        authToken = contextp->getActive()->getAuthToken();

    if (!contextp || authToken.length() == 0) {
        loginHtml = "<a href=\"/appleLoginScreen\">Apple Login</a><p><a href=\"/msLoginScreen\">        MS Login</a>";
    }
    else {
        loginHtml = "Logged in<p><a href=\"/logoutScreen\">Logout</a>";
    }
    
    dict.add("loginText", loginHtml);
    code = getConn()->interpretFile((char *) "login-home.html", &dict, &response);
    if (code != 0) {
        snprintf(tbuffer, sizeof(tbuffer),"Oops, interpretFile code is %d\n", code);
        obufferp = tbuffer;
    }
    else {
        obufferp = const_cast<char *>(response.c_str());
    }

    setSendContentLength(strlen(obufferp));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    outPipep->eof();
    
    requestDone();
}

void
server(int argc, char **argv, int port)
{
    SApi *sapip;

    sapip = new SApi();
    sapip->initWithPort(port);

    /* setup login URL listeners */
    SApiLogin::initSApi(sapip);

    /* register the home screen as well */
    sapip->registerUrl("/", &HomeScreen::factory, (SApi::StartMethod) &HomeScreen::startMethod);

    while(1) {
        sleep(1);
    }
}
