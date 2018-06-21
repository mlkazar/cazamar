#include <unistd.h>
#import <Cocoa/Cocoa.h>

#include "upload.h"

void
Upload::init(notifyProc *notifyProcp, void *contextp)
{
    _notifyProcp = notifyProcp;
    _notifyContextp = contextp;

#if 0
    code = pthread_create(&pthreadId, NULL, &Upload::server, this);
    if (code != 0)
        printf("Upload: failed to create pthread\n");
#else
    server(this);
#endif
}

/* static */ void *
Upload::server(void *contextp)
{
    /* TBD: do we really need a separate thread for this, given that all of the
     * code here sets up async processes anyway?
     */
    Upload *uploadp = (Upload *)contextp;
    SApi *sapip;

    uploadp->_sapip = sapip = new SApi();
    sapip->setContext(uploadp);
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
    NSBundle *myBundle = [NSBundle mainBundle];
    NSString *path= [myBundle pathForResource:@"status" ofType:@"png"];
    std::string homePath;
        
    up = (Upload *) _sapip->getContext();
    printf("Using global context %p\n", up);

    /* path will end /a.b, but we only want through the trailing / */
    path = [path substringToIndex: ([path length] - 10)];

    contextp = SApiLogin::createLoginCookie(this);
    contextp->setPathPrefix([path cStringUsingEncoding: NSUTF8StringEncoding]);

    if (!contextp || contextp->_webAuthToken.length() == 0) {
        loginHtml = "<a href=\"/appleLoginScreen\">Apple Login</a><p><a href=\"/msLoginScreen\">        MS Login</a>";
    }
    else {
        loginHtml = "Logged in<p><a href=\"/logoutScreen\">Logout</a>";
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
