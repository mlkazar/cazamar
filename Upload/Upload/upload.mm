#include <unistd.h>
#import <Cocoa/Cocoa.h>

#include "upload.h"

void
Upload::init(notifyProc *notifyProcp, void *contextp)
{
    _notifyProcp = notifyProcp;
    _notifyContextp = contextp;

    server(this);
}

void
Upload::runTests()
{
    NSAlert *alert;
    CnodeMs *rootp;
    CnodeMs *childDirp;
    Cattr attr;
    Cattr childAttr;
    int32_t code;
    std::string uploadUrl;

    if (!_loginMSp) {
	alert = [[NSAlert alloc] init];
	alert.messageText = @"Not logged in";
	alert.informativeText = @"Login started but no tokens available";
	[alert runModal];
	return;
    }

    _cfsp = new CfsMs(_loginMSp);
    _cfsp->root((Cnode **) &rootp, NULL);
    rootp->getAttr(&attr, NULL);
    code = rootp->mkdir("TestDir", (Cnode **) &childDirp, NULL);
    if (code == 0)
	childDirp->getAttr(&childAttr, NULL);
    else
	printf("makedir failed code=%d\n", code);

    code = rootp->startSession(std::string("testfile"),
			       &uploadUrl);
    printf("upload URL=%s\n", uploadUrl.c_str());

    alert = [[NSAlert alloc] init];
    alert.messageText = @"Tests done";
    alert.informativeText = @"All tests complete";
    [alert runModal];
    
}

/* static */ void *
Upload::server(void *contextp)
{
    Upload *uploadp = (Upload *)contextp;
    SApi *sapip;

    NSBundle *myBundle = [NSBundle mainBundle];
    NSString *path= [myBundle pathForResource:@"status" ofType:@"png"]; /* file must exist */
    /* path will end /status.png, but we only want through the trailing / */
    path = [path substringToIndex: ([path length] - 10)];
    uploadp->_pathPrefix = std::string([path cStringUsingEncoding: NSUTF8StringEncoding]);

    uploadp->_sapip = sapip = new SApi();
    sapip->setContext(uploadp);
    sapip->setPathPrefix(uploadp->_pathPrefix);
    sapip->initWithPort(7701);

    /* setup login URL listeners */
    SApiLogin::initSApi(sapip);

    /* register the home screen as well */
    sapip->registerUrl("/", &HomeScreen::factory);

    {
        char tbuffer[1000];
        getcwd(tbuffer, sizeof(tbuffer));
        printf("wd is %s\n", tbuffer);
    }

    return NULL;
}

void
HomeScreen::startMethod()
{
    char tbuffer[16384];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    CThreadPipe *outPipep = getOutgoingPipe();
    std::string loginHtml;
    SApiLoginCookie *contextp;
    Upload *up;
    std::string homePath;
    std::string authToken;
        
    up = (Upload *) _sapip->getContext();
    printf("Using global context %p\n", up);

    contextp = SApiLogin::createLoginCookie(this);
    contextp->setPathPrefix(up->_pathPrefix);

    if (contextp && contextp->getActive())
	authToken = contextp->getActive()->getAuthToken();
    if (authToken.length() == 0) {
        loginHtml = "<a href=\"/appleLoginScreen\">Apple Login</a><p><a href=\"/msLoginScreen\">        MS Login</a>";
    }
    else {
        loginHtml = "Logged in<p><a href=\"/logoutScreen\">Logout</a>";
	up->_loginMSp = contextp->_loginMSp;
    }
    
    homePath = contextp->getPathPrefix() + "login-home.html";
    dict.add("loginText", loginHtml);
    code = getConn()->interpretFile((char *) homePath.c_str(), &dict, &response);
    if (code != 0) {
        sprintf(tbuffer, "Oops, interpretFile code is %d\n", code);
        obufferp = tbuffer;
    }
    else {
        obufferp = const_cast<char *>(response.c_str());
    }

    setSendContentLength((int32_t) strlen(obufferp));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(obufferp, (int32_t) strlen(obufferp));
    outPipep->eof();
    
    requestDone();
}

SApi::ServerReq *
HomeScreen::factory(std::string *opcodep, SApi *sapip)
{
    HomeScreen *reqp;
    reqp = new HomeScreen(sapip);
    return reqp;
}
