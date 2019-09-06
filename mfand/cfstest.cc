#include <stdio.h>
#include <string>
#include <stdlib.h>
#include "sapi.h"
#include "sapilogin.h"
#include "xapi.h"
#include "bufsocket.h"
#include "buftls.h"
#include "json.h"
#include "cfsms.h"

void server(int argc, char **argv, int port);

class TestDataSource : public CDataSource {
    const char *_datap;

 public:
    int32_t getAttr(CAttr *attrp) {
	attrp->_mtime = 1000000000ULL;
	attrp->_ctime = 1000000000ULL;
	attrp->_length = strlen(_datap)+1;
	return 0;
    }

    int32_t read( uint64_t offset, uint32_t count, char *bufferp) {
	int32_t tcount = count;
	int32_t length = (int32_t) strlen(_datap)+1;

	if (offset >= length)
	    return 0;

	if (tcount > length - offset)
	    tcount = (int32_t) (length-offset);

	memcpy(bufferp, _datap+offset, tcount);
	return tcount;
    }

    int32_t close() {
	return 0;
    }

    TestDataSource (const char *datap) {
        _datap = datap;
	return;
    }

    ~TestDataSource() {
	return;
    }
};

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
    static SApi::ServerReq *factory( SApi *sapip) {
        HomeScreen *reqp;
        reqp = new HomeScreen(sapip);
        return reqp;
    }

    HomeScreen(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();

    void runTests(SApiLoginCookie *loginCookiep);
};

void
HomeScreen::runTests(SApiLoginCookie *loginCookiep)
{
    CfsMs *cfsp;
    CnodeMs *rootp;
    CnodeMs *testDirp;
    CAttr attrs;
    CAttr dirAttrs;
    int32_t code;
    std::string uploadUrl;

    printf("cfstest: tests start loginCookie=%p\n", loginCookiep);
    cfsp = new CfsMs(loginCookiep, "");
    cfsp->root((Cnode **) &rootp, NULL);
    rootp->getAttr(&attrs, NULL);

    code = rootp->lookup("TestDir", 0, (Cnode **)&testDirp, NULL);
    if (code != 0) {
        code = rootp->mkdir("TestDir", (Cnode **) &testDirp, NULL);
        if (code != 0) {
            printf("makedir failed code=%d\n", code);
            return;
        }
    }

    /* lookup succeeded */
    code = testDirp->getAttr(&dirAttrs, NULL);
    if (code != 0) {
        printf("dir getattr failed code=%d\n", code);
    }

    code = testDirp->startSession(std::string("testfile"), &uploadUrl);
    printf("upload URL=%s\n", uploadUrl.c_str());

    if (code == 0) {
	printf("test sending data\n");
	TestDataSource testSource("This is some test data\n");
        CAttr attrs;
        uint32_t sentBytes;

        testSource.getAttr(&attrs);

	code = rootp->sendData( &uploadUrl,
				&testSource,
				attrs._length,
				0,
				attrs._length,
                                &sentBytes);
	printf("test send code=%d\n", code);

#if 0
	dataFile.open((char *) "/Users/kazar/Desktop/kernel");
	dataFile.getAttr(&childAttr);
	printf("Send size should be %ld\n", (long) childAttr._length);

	/* test file copy */
	code = rootp->sendFile( std::string("testfile2"),
				&dataFile,
				childAttr._length,
				NULL);
	printf("sendfile status=%d\n", code);
#endif
    }

    TestDataSource test2Source("This is test2 data\n");

    code = cfsp->sendFile("/TestDir/test2file", &test2Source, NULL, NULL);
    printf("path based sendfile test done, code=%d\n", code);

    printf("cfstest: tests done\n");
}

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
    int loggedIn = 0;
        
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
        loggedIn = 1;
    }
    
    dict.add("loginText", loginHtml);
    code = getConn()->interpretFile((char *) "login-home.html", &dict, &response);
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
    outPipep->eof();
    
    requestDone();

    if (loggedIn) {
        runTests(contextp);
    }
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
    sapip->registerUrl( "/",
                        &HomeScreen::factory,
                        (SApi::StartMethod) &HomeScreen::startMethod);

    while(1) {
        sleep(1);
    }
}
