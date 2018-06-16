#include <stdio.h>
#include <string>
#include <stdlib.h>
#include "sapi.h"
#include "sapilogin.h"
#include "xapi.h"
#include "bufsocket.h"
#include "buftls.h"
#include "json.h"

/* This file contains the basics for a toy test application, and the
 * login authentication framework used by any application that
 * performs user logins.  It assumes that there's a key server
 * (keyserv) running at djtogoapp.duckdns.org:7700 as well, and that
 * the key server is registered as the application server for the
 * oauth2 application.  It will be sent the web auth token by the
 * login web service you're using, whether apple, google or microsoft.
 * Fuck Amazon, and their closed API.
 *
 * The basic control flow is pretty tricky.  An application begins by
 * using SApiLoginApple::getLoginPage to get the HTTP response
 * contents to start a login process, assuming we need to get tokens.
 *
 * The getLoginPage function will actually contact Apple's servers to
 * get a redirect URL which can be contacted to start the login
 * process with the Apple authentication servers.  It will
 * parameterize the login-apple.html file with that string, so that
 * the "Login to Apple iCloud" button will actually contact create a
 * new child window filled with the redirect URL (TBD: call this
 * function automatically).  Before creating this child window, the
 * application window's Javascript code establishes a listener for a
 * 'kazar1' message sent to the application; it will be send at the
 * end of the login popup window.
 *
 * The child popup window first visits the Apple login URL.  On
 * success, the Apple login servers redirect the child window to the
 * key server, at the URL registered with the application.  The URL
 * includes the authentication token, which is now delivered to the
 * key server.  The key server's response to the visit then fills the
 * login window.  The response has a "Save tokens" button, which when
 * pressed sends a 'kazar1' nmessage to its parent window, which is
 * the application's window.  This event tells the application window
 * that it is now safe to retrieve the web authentication tokens from
 * the key server.
 *
 * Before creating and filling the Apple login window, the
 * application's login window sets up a Javascript message listener so
 * that when a message called 'kazar1' is sent to this application
 * login window, another Javascript function, haveToken, will be
 * invoked.  This function uses XMLHTttpRequest to post a dummy
 * message to the application's /keyData URL, telling it that the key
 * server now has the web authentication token required by the
 * application.  A listener for this /keyData URL was established by
 * the SApiLoginApple class.  At this point, our C++ code is now running,
 * and can contact the key server to obtain and store the web authentication
 * keys.
 */

class SApiLoginApple;
class SApiLoginMS;

/* This is an internal class used by SApiAppleLogin to handle the callback
 * from Javascript once the login process has completed.  Basically, the
 * key server returns the contents of login-apple-done.html, which has a 
 * 'save tokens' button that triggers the reference to /keyData in the main
 * application, which invokes this startMethod.
 *
 * When this is invoked, it contacts the key server, passing in the
 * token ID, and obtaining the saved web auth token that was saved
 * there.
 *
 * Once done, the SApiAppleLogin class has the auth token stored internally.
 */
void
AppleLoginKeyData::startMethod()
{
    char tbuffer[4096];
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
    const char *urlp;
    SApiLoginGeneric *genLoginp = (SApiLoginGeneric *) _sapip->getContext();

    SApiLoginCookie *contextp = (SApiLoginCookie *) getCookieKey("sapiLogin");
    printf("SApiLoginCookie=%p\n", contextp);

    if (!contextp) {
        CThreadPipe *outPipep = getOutgoingPipe();
        printf("no cookie in AppleLoginKeyData\n");
        strcpy(tbuffer, "Error -- no cookie in login screen");
        setSendContentLength(strlen(tbuffer));
        inputReceived();
        outPipep->write(tbuffer, strlen(tbuffer));
        outPipep->eof();
        requestDone();
        return;
    }

    CThreadPipe *inPipep = getIncomingPipe();
    
    {
        code = inPipep->read(tbuffer, sizeof(tbuffer)-1);
        if (code >= 0) {
            if (code >= sizeof(tbuffer))
                printf("apptest/keyData: bad count from pipe read\n");
            else
                tbuffer[code] = 0;
        }
    }

    response = std::string(tbuffer);
    
    urlp = const_cast<char *>(_rstReqp->getRcvUrl()->c_str());
    
    inputReceived();

    /* now that we've been notified that the authorization keys have been deposited with
     * the key server, pick them up using the request ID.
     */
    {
        XApi *xapip;
        XApi::ClientConn *connp;
        BufGen *bufGenp;
        XApi::ClientReq *reqp;
        CThreadPipe *inPipep;
        char tbuffer[16384];
        char *urlp;
        const char *tp;
        Json::Node *authTokenNodep;
        Json::Node *jnodep;
        std::string callbackString;
        
        printf("apptest: retrieving key='%s'\n", genLoginp->getAuthId().c_str());
        callbackString = "/keyData?id=" + genLoginp->getAuthId();

        xapip = new XApi();
        bufGenp = new BufTls();
        bufGenp->init(const_cast<char *>("djtogoapp.duckdns.org"), 7700);
        connp = xapip->addClientConn(bufGenp);
        reqp = new XApi::ClientReq();
        urlp = const_cast<char *>(_rstReqp->getRcvUrl()->c_str());
        reqp->setSendContentLength(0);
        reqp->startCall( connp,
                         callbackString.c_str(),
                         /* !isPost */ XApi::reqGet);
        code = reqp->waitForHeadersDone();
        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < (signed) sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        
        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        tp = (char *) "Default junk";
        if (code == 0) {
            authTokenNodep = jnodep->searchForChild("webAuthToken", 0);
            if (authTokenNodep) {
                genLoginp->setAuthToken(authTokenNodep->_children.head()->_name);
            }
        }

        inPipep->waitForEof();
        delete reqp;
        delete jnodep;
        reqp = NULL;

        contextp->_webAuthToken = genLoginp->getAuthToken();

        genLoginp->refineAuthToken(&contextp->_webAuthToken);
    }

    requestDone();

    genLoginp->printAuthState();
}

SApi::ServerReq *
AppleLoginKeyData::factory(std::string *opcodep, SApi *sapip)
{
    AppleLoginKeyData *reqp;

    reqp = new AppleLoginKeyData(sapip);
    return reqp;
}

/* function to initialize an SApiLoginApple class; pass in the SApi object
 * associated with the web server responding for the web site.
 */
void
SApiLoginApple::init(SApi *sapip, std::string finalUrl)
{
    _sapip = sapip;
    sapip->setContext(this);
    _finalUrl = finalUrl;
    sapip->registerUrl("/sapiKeyData", &AppleLoginKeyData::factory);
}

/* return a string representing the contents of a login web page for
 * this application.
 */
int32_t
SApiLoginApple::getLoginPage(std::string *outStringp)
{
    int32_t code;
    SApi::Dict dict;
    const char *tp;
    Json::Node *jnodep;
    Json::Node *redirNodep;
    Json json;

    /* make call to trigger retrieval of correct redirect URL from Apple */
    {
        XApi *xapip;
        XApi::ClientConn *connp;
        BufGen *bufGenp;
        XApi::ClientReq *reqp;
        CThreadPipe *inPipep;
        char tbuffer[16384];
        std::string redirectUrl;
        size_t tokenPos;
        size_t endPos;
        std::string tableKey;
        
        xapip = new XApi();
        bufGenp = new BufTls();
        bufGenp->init(const_cast<char *>("api.apple-cloudkit.com"), 443);
        connp = xapip->addClientConn(bufGenp);
        reqp = new XApi::ClientReq();
        reqp->setSendContentLength(0);
        reqp->startCall( connp,
                         _apiUrl.c_str(),
                         /* !isPost */ XApi::reqGet);
        code = reqp->waitForHeadersDone();
        inPipep = reqp->getIncomingPipe();
        code = inPipep->read(tbuffer, sizeof(tbuffer));
        if (code >= 0 && code < (signed) sizeof(tbuffer)-1) {
            tbuffer[code] = 0;
        }
        
        tp = tbuffer;
        code = json.parseJsonChars((char **) &tp, &jnodep);
        tp = (char *) "Default junk";
        if (code == 0) {
            redirNodep = jnodep->searchForChild("redirectURL", 0);
            if (redirNodep) {
                redirectUrl = redirNodep->_children.head()->_name.c_str();
                tp = redirectUrl.c_str();
            }
        }

        /* extract the id from the redirect URL; it's labeled oauth_token */
        tokenPos = redirectUrl.find("oauth_token=");
        if (tokenPos != std::string::npos) {
            tokenPos += 12;     /* skip past '=' */
            endPos = redirectUrl.find('&', tokenPos);
            if (endPos == std::string::npos) {
                /* use rest of token */
                tableKey = redirectUrl.substr(tokenPos);
            }
            else {
                /* go up to but not including the terminating '&' */
                tableKey = redirectUrl.substr(tokenPos, endPos-tokenPos);
            }
        }
        _authId = tableKey;

        inPipep->waitForEof();
        delete reqp;
        reqp = NULL;
        
        dict.add("redir", tp);
        dict.add("final", _finalUrl);
        code = SApi::ServerConn::interpretFile((char *) "login-apple.html", &dict, outStringp);
        if (code != 0) {
            sprintf(tbuffer, "Oops, interpretFile code is %d\n", code);
            *outStringp = std::string(tbuffer);
        }
    }
    return 0;
}

/* function to initialize an SApiLoginApple class; pass in the SApi object
 * associated with the web server responding for the web site.
 */
void
SApiLoginMS::init(SApi *sapip, std::string finalUrl)
{
    char tbuffer[128];
    int32_t tval;
    
    _sapip = sapip;

#ifdef __linux__
    random_r(&_randomBuf, &tval);
#else
    tval = random();
#endif

    tval = tval & 0x7FFFFFFF;
    sprintf(tbuffer, "%u", tval);
    _authId = std::string(tbuffer);

    _finalUrl = finalUrl;
    sapip->setContext(this);
    sapip->registerUrl("/sapiKeyData", &AppleLoginKeyData::factory);
}

/* return a string representing the contents of a login web page for
 * this application.
 */
int32_t
SApiLoginMS::getLoginPage(std::string *outStringp)
{
    int32_t code;
    SApi::Dict dict;
    Json json;

    /* make call to trigger retrieval of correct redirect URL from Apple */
    {
        char tbuffer[4096];
        std::string redirectUrl;
        std::string tableKey;
        
        redirectUrl = "https://login.microsoftonline.com/common/oauth2/v2.0/authorize?";
        redirectUrl += "client_id=" + _clientId;
        redirectUrl += "&response_type=code";
        redirectUrl += "&redirect_uri=https%3a%2f%2fdjtogoapp.duckdns.org:7700%2flogin4ms";
        redirectUrl += "&response_mode=query";
        redirectUrl += "&scope=offline_access%20files.readwrite";
        redirectUrl += "&state=" + _authId;
        dict.add("redir", redirectUrl);
        dict.add("final", _finalUrl);
        code = SApi::ServerConn::interpretFile((char *) "login-ms.html", &dict, outStringp);
        if (code != 0) {
            sprintf(tbuffer, "Oops, interpretFile code is %d\n", code);
            *outStringp = std::string(tbuffer);
        }
    }
    return 0;
}

int32_t
SApiLoginMS::refineAuthToken(std::string *atokenp)
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
        Json::Node *authTokenNodep;
        Json::Node *jnodep;
        std::string callbackString;
        int32_t code;
        
        callbackString = "/common/oauth2/v2.0/token";

        postData = "client_id=" + _clientId;
        postData += "&scope=files.readwrite";
        postData += "&code=" + (*atokenp);
        postData += "&redirect_uri=https%3a%2f%2fdjtogoapp.duckdns.org:7700%2flogin4ms";
        postData += "&grant_type=authorization_code";
        postData += "&client_secret=" + Rst::urlEncode(&_clientSecret);

        xapip = new XApi();
        bufGenp = new BufTls();
        bufGenp->init(const_cast<char *>("login.microsoftonline.com"), 443);
        connp = xapip->addClientConn(bufGenp);
        reqp = new XApi::ClientReq();
        reqp->setSendContentLength(postData.length());
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
        tp = (char *) "Default junk";
        if (code == 0) {
            authTokenNodep = jnodep->searchForChild("access_token", 0);
            if (authTokenNodep) {
                setAuthToken(authTokenNodep->_children.head()->_name);
            }
            authTokenNodep = jnodep->searchForChild("refresh_token", 0);
            if (authTokenNodep) {
                setRefreshToken(authTokenNodep->_children.head()->_name);
            }
        }

        inPipep->waitForEof();
        delete reqp;
        delete jnodep;
        reqp = NULL;
    }
    
    return 0;
}

void
AppleLoginScreen::startMethod()
{
    char tbuffer[16384];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    SApiLoginCookie *contextp;
        
    contextp = (SApiLoginCookie *) getCookieKey("sapiLogin");
    if (contextp == NULL) {
        contextp = new SApiLoginCookie();
        setCookieKey("sapiLogin", contextp);
    }
    else {
        if (contextp->_webAuthToken.length() == 0) {
            contextp->_loginApplep = new SApiLoginApple();
            contextp->_loginApplep->setAppParams("/database/1/iCloud.com.Cazamar.Login1/development/public/users/caller?ckAPIToken=4e2811fdef054c7cb02aca853299b50151f5b7c40e5cdbd9a7762c135af3e99a");
            contextp->_loginApplep->init(_sapip, "/");
            code = contextp->_loginApplep->getLoginPage(&response);
            obufferp = const_cast<char *>(response.c_str());
        }
        else {
            obufferp = tbuffer;
            strcpy(tbuffer, "Error -- in login screen with a token");
        }
    }
    
    setSendContentLength(strlen(obufferp));
    
    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    outPipep->eof();
    
    requestDone();
}

SApi::ServerReq *
AppleLoginScreen::factory(std::string *opcodep, SApi *sapip)
{
    AppleLoginScreen *reqp;
    reqp = new AppleLoginScreen(sapip);
    return reqp;
}

/* MS */
void
MSLoginScreen::startMethod()
{
    char tbuffer[16384];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    SApiLoginCookie *contextp = (SApiLoginCookie *) getCookieKey("sapiLogin");
    
    if (contextp == NULL) {
        contextp = new SApiLoginCookie();
        setCookieKey("sapiLogin", contextp);
    }
    else {
        if (contextp->_webAuthToken.length() == 0) {
            contextp->_loginMSp = new SApiLoginMS();
            contextp->_loginMSp->setAppParams( "60a94129-6c64-493e-b91f-1bd6d0c09cd1",
                                               "awqdimBY081)~-nXYMPD19:");

            contextp->_loginMSp->init(_sapip, "/");
            code = contextp->_loginMSp->getLoginPage(&response);
            obufferp = const_cast<char *>(response.c_str());
        }
        else {
            obufferp = tbuffer;
            strcpy(tbuffer, "Error -- in login screen with a token");
        }
    }
    
    setSendContentLength(strlen(obufferp));
    
    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    outPipep->eof();
    
    requestDone();
}

SApi::ServerReq *
MSLoginScreen::factory(std::string *opcodep, SApi *sapip)
{
    MSLoginScreen *reqp;
    reqp = new MSLoginScreen(sapip);
    return reqp;
}

void
LogoutScreen::startMethod()
{
    char tbuffer[16384];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    SApiLoginCookie *contextp = (SApiLoginCookie *) getCookieKey("sapiLogin");
        
    if (contextp == NULL) {
        strcpy(tbuffer, "Error -- no cookie in logout");
        setSendContentLength(strlen(tbuffer));
        inputReceived();
        outPipep->write(tbuffer, strlen(tbuffer));
        outPipep->eof();
        requestDone();
        return;
    }

    contextp->_webAuthToken.erase();

    code = getConn()->interpretFile((char *) "login-logout.html", &dict, &response);
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
}

SApi::ServerReq *
LogoutScreen::factory(std::string *opcodep, SApi *sapip)
{
    LogoutScreen *reqp;
    reqp = new LogoutScreen(sapip);
    return reqp;
}
