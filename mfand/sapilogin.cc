/*

Copyright 2016-2020 Cazamar Systems

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#include <stdio.h>
#include <string>
#include <stdlib.h>
#include <sys/stat.h>

#include "sapi.h"
#include "sapilogin.h"
#include "xapi.h"
#include "bufsocket.h"
#include "buftls.h"
#include "json.h"
#include "jwt.h"

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

SApiLoginCookie *SApiLogin::_globalCookiep = NULL;

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
SApiLoginReq::AppleLoginKeyDataMethod()
{
    char tbuffer[4096];
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
    const char *urlp;
    SApiLoginCookie *cookiep = SApiLogin::getLoginCookie(this);

    printf("SApiLoginCookie=%p\n", cookiep);

    if (!cookiep) {
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

    SApiLoginGeneric *genLoginp = cookiep->getActive();

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
        std::string authToken;
        
        printf("apptest: retrieving key='%s'\n", genLoginp->getAuthId().c_str());
        callbackString = "/keyData?id=" + genLoginp->getAuthId();

        xapip = new XApi();
        bufGenp = new BufTls(cookiep->getPathPrefix());
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

        delete reqp;
        delete jnodep;
        reqp = NULL;

        authToken = genLoginp->getAuthToken();

        /* this converts the once only token into a real authentication token */
        genLoginp->refineAuthToken(&authToken, cookiep);

        /* save the updated authentication state, if enabled */
        cookiep->save();
    }

    requestDone();

    genLoginp->printAuthState();
}

/* function to initialize an SApiLoginApple class; pass in the SApi object
 * associated with the web server responding for the web site.
 */
void
SApiLoginApple::init(SApi *sapip, SApiLoginCookie *cookiep, std::string finalUrl)
{
    _sapip = sapip;
    cookiep->setActive(this);
    _finalUrl = finalUrl;
    sapip->registerUrl( "/sapiKeyData", 
                        &SApiLoginReq::factory,
                        (SApi::StartMethod) &SApiLoginReq::AppleLoginKeyDataMethod);
}

void
SApiLoginApple::initFromFile(SApiLoginCookie *cookiep, std::string authToken)
{
    _authToken = authToken;
    _sapip = NULL;
    cookiep->setActive(this);
}

/* return a string representing the contents of a login web page for
 * this application.
 */
int32_t
SApiLoginApple::getLoginPage(std::string *outStringp, SApiLoginCookie *cookiep)
{
    int32_t code;
    SApi::Dict dict;
    const char *tp;
    Json::Node *jnodep;
    Json::Node *redirNodep;
    Json json;
    std::string loginPath;

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
        bufGenp = new BufTls(cookiep->getPathPrefix());
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

        delete reqp;
        reqp = NULL;
        
        if (cookiep) {
            loginPath = cookiep->getPathPrefix();
        }
        loginPath +=  "login-apple.html";

        dict.add("redir", tp);
        dict.add("final", _finalUrl);
        code = SApi::ServerConn::interpretFile((char *) loginPath.c_str(), &dict, outStringp);
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
SApiLoginMS::init(SApi *sapip, SApiLoginCookie *cookiep, std::string finalUrl)
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
    cookiep->setActive(this);
    sapip->registerUrl("/sapiKeyData", 
                       &SApiLoginReq::factory,
                       (SApi::StartMethod) &SApiLoginReq::AppleLoginKeyDataMethod);
}

void
SApiLoginMS::initFromFile( SApiLoginCookie *cookiep,
                           std::string authToken,
                           std::string refreshToken)
{
    _sapip = NULL;
    setAuthToken(authToken);
    _refreshToken = refreshToken;
    cookiep->setActive(this);
    getUserInfo(cookiep);
}

/* return a string representing the contents of a login web page for
 * this application.
 */
int32_t
SApiLoginMS::getLoginPage(std::string *outStringp, SApiLoginCookie *cookiep)
{
    int32_t code;
    SApi::Dict dict;
    Json json;
    std::string loginPath;

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
        redirectUrl += "&prompt=login";
        redirectUrl += "&state=" + _authId;
        dict.add("redir", redirectUrl);
        dict.add("final", _finalUrl);
        
        if (cookiep) {
            loginPath = cookiep->getPathPrefix();
            loginPath += "login-ms.html";
        }

        code = SApi::ServerConn::interpretFile((char *) loginPath.c_str(), &dict, outStringp);
        if (code != 0) {
            sprintf(tbuffer, "Oops, interpretFile code is %d\n", code);
            *outStringp = std::string(tbuffer);
        }
    }
    return 0;
}

int32_t
SApiLoginMS::getUserInfo(SApiLoginCookie *cookiep)
    /* now use the API to get info about the user */
{
    char tbuffer[0x4000];
    XApi *xapip;
    XApi::ClientConn *connp;
    BufGen *bufGenp;
    XApi::ClientReq *reqp;
    CThreadPipe *inPipep;
    const char *tp;
    Json json;
    Json::Node *userNodep;
    Json::Node *createdNodep;
    Json::Node *jnodep;
    std::string callbackString;
    std::string tokenString;
    int32_t code;
    CThreadPipe *outPipep;

    /* JWT tokens already self-identify, and do so better than this hack, which 
     * returns the display name of the root dirs creator.  We can't use the graph
     * 'me' identification, since for an enterprise user, we don't have the rights
     * to read the user's info.
     */
    if (getIsJwt())
        return 0;

    callbackString = "/v1.0/me/drive/root";

    xapip = new XApi();
    bufGenp = new BufTls(cookiep->getPathPrefix());
    bufGenp->init(const_cast<char *>("graph.microsoft.com"), 443);
    connp = xapip->addClientConn(bufGenp);
    reqp = new XApi::ClientReq();
    tokenString = "Bearer " + getAuthToken();
    reqp->addHeader("Authorization", tokenString.c_str());
    reqp->addHeader("Content-Type", "application/json");
    reqp->startCall( connp,
                     callbackString.c_str(),
                     /* isPost */ XApi::reqGet);

    outPipep = reqp->getOutgoingPipe();
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
        createdNodep = jnodep->searchForChild("user", 0);
        if (createdNodep) {
            userNodep = createdNodep->searchForChild("displayName", 0);
            if (userNodep) {
                printf("user '%s'\n", userNodep->_children.head()->_name.c_str());
                setAuthTokenName("Personal: " + userNodep->_children.head()->_name);
            }
        }
    }

    delete reqp;
    delete jnodep;
    reqp = NULL;

    return 0;
}


int32_t
SApiLoginMS::refineAuthToken(std::string *atokenp, SApiLoginCookie *cookiep)
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
        bufGenp = new BufTls(cookiep->getPathPrefix());
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
        printf("Refine code %d\n", code);
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

        delete reqp;
        delete jnodep;
        reqp = NULL;
    }
    
    /* make a call to Graph API to get principal name, and set it in the context */
    getUserInfo(cookiep);

    return 0;
}

int32_t
SApiLoginMS::refresh()
{
    /* make a call to get access token and refresh tokens from MS, using code */
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
    postData += "&refresh_token=" + _refreshToken;
    postData += "&redirect_uri=https%3a%2f%2fdjtogoapp.duckdns.org:7700%2flogin4ms";
    postData += "&grant_type=refresh_token";
    postData += "&client_secret=" + Rst::urlEncode(&_clientSecret);
    
    xapip = new XApi();
    bufGenp = new BufTls(_cookiep->getPathPrefix());
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
    
    printf("**refresh token code=%d\n", code);
    
    if (code != 0) {
        delete reqp;
        reqp = NULL;
        return code;
    }
    
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
    
    delete reqp;
    delete jnodep;
    reqp = NULL;
    
    if (code == 0) {
        /* save the updated tokens after a refresh */
        if (_cookiep != NULL) {
            _cookiep->save();
        }
    }
    
    return code;
}

/* static */ SApiLoginCookie *
SApiLogin::createGlobalCookie(std::string pathPrefix, std::string libPath)
{
    SApiLoginCookie *cookiep;

    cookiep = new SApiLoginCookie();
    cookiep->setPathPrefix(pathPrefix);
    cookiep->setLibPath(libPath);
    _globalCookiep = cookiep;
    return cookiep;
}

/* static */ SApiLoginCookie *
SApiLogin::getLoginCookie(SApi::ServerReq *reqp) {
    if (_globalCookiep)
        return _globalCookiep;

    return (SApiLoginCookie *) reqp->getCookieKey("sapiLogin");
}

/* static */ SApiLoginCookie *
SApiLogin::createLoginCookie(SApi::ServerReq *reqp) {
    SApiLoginCookie *cookiep;

    if (_globalCookiep)
        return _globalCookiep;

    if ((cookiep = (SApiLoginCookie *) reqp->getCookieKey("sapiLogin")) == NULL) {
        cookiep = new SApiLoginCookie();
        reqp->setCookieKey("sapiLogin", cookiep);
        cookiep->setPathPrefix(reqp->_sapip->getPathPrefix());
        cookiep->setLibPath(reqp->_sapip->getPathPrefix());     /* add libPath to SAPI if needed */
    }

    return cookiep;
}

void
SApiLoginReq::AppleLoginScreenMethod()
{
    char tbuffer[16384];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    SApiLoginCookie *cookiep;
    std::string authToken;
        
    cookiep = SApiLogin::getLoginCookie(this);
    if (cookiep == NULL) {
        cookiep = new SApiLoginCookie();
        setCookieKey("sapiLogin", cookiep);
    }
    else {
        if (cookiep->getActive())
            authToken = cookiep->getActive()->getAuthToken();
        if (authToken.length() == 0) {
            cookiep->_loginApplep = new SApiLoginApple();
            cookiep->_loginApplep->setAppParams("/database/1/iCloud.com.Cazamar.Login1/development/public/users/caller?ckAPIToken=4e2811fdef054c7cb02aca853299b50151f5b7c40e5cdbd9a7762c135af3e99a");
            cookiep->_loginApplep->init(_sapip, cookiep, "/");
            code = cookiep->_loginApplep->getLoginPage(&response, cookiep);
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

/* MS */
void
SApiLoginReq::MSLoginScreenMethod()
{
    char tbuffer[16384];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    SApiLoginCookie *cookiep = SApiLogin::getLoginCookie(this);
    std::string authToken;

    printf("In MSLoginScreenMethod cookiep=%p\n", cookiep);
    if (cookiep == NULL) {
        cookiep = new SApiLoginCookie();
        setCookieKey("sapiLogin", cookiep);
    }
    else {
        if (cookiep->getActive())
            authToken = cookiep->getActive()->getAuthToken();
        if (authToken.length() == 0) {
            cookiep->_loginMSp = new SApiLoginMS();
            cookiep->_loginMSp->setAppParams( "cd97cce7-f4a3-40c2-acc7-66a7924c6341",
                                              "lznisJHEK|msZGD85941{@)");

            cookiep->_loginMSp->init(_sapip, cookiep, "/");
            code = cookiep->_loginMSp->getLoginPage(&response, cookiep);
            obufferp = const_cast<char *>(response.c_str());
            printf("Using authId %s\n", cookiep->_loginMSp->_authId.c_str());
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

void
SApiLoginReq::LogoutScreenMethod()
{
    char tbuffer[16384];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    SApiLoginCookie *cookiep = SApiLogin::getLoginCookie(this);
    std::string logoutPath;

    if (cookiep == NULL) {
        strcpy(tbuffer, "Error -- no cookie in logout");
        setSendContentLength(strlen(tbuffer));
        inputReceived();
        outPipep->write(tbuffer, strlen(tbuffer));
        outPipep->eof();
        requestDone();
        return;
    }

    cookiep->logout();

    logoutPath = cookiep->getPathPrefix() + "login-logout.html";
    code = getConn()->interpretFile((char *) logoutPath.c_str(), &dict, &response);
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

int32_t
SApiLoginCookie::save()
{
    FILE *filep;
    int code;
    Json::Node *dataNodep;
    Json::Node *tnodep;
    Json::Node *nnodep;
    std::string result;

    std::string stateFile;
    if (!_saveRestoreEnabled || _loginActivep == NULL)
        return 0;

    stateFile = _libPath + "auth.js";
    filep = fopen(stateFile.c_str(), "w");
    if (!filep)
        return 0;

    /* create a json structure with keys authToken, refreshToken and authid, each being
     * a string.  Unmarshall it and write the data out.
     */
    dataNodep = new Json::Node();
    dataNodep->initStruct();

    tnodep = new Json::Node();
    tnodep->initString(_loginActivep->getAuthToken().c_str(), 1);
    nnodep = new Json::Node();
    nnodep->initNamed("authToken", tnodep);
    dataNodep->appendChild(nnodep);
    
    tnodep = new Json::Node();
    tnodep->initString(_loginActivep->getRefreshToken().c_str(), 1);
    nnodep = new Json::Node();
    nnodep->initNamed("refreshToken", tnodep);
    dataNodep->appendChild(nnodep);
    
    tnodep = new Json::Node();
    tnodep->initString(_loginActivep->getAuthId().c_str(), 1);
    nnodep = new Json::Node();
    nnodep->initNamed("authId", tnodep);
    dataNodep->appendChild(nnodep);

    tnodep = new Json::Node();
    if (_loginMSp)
        tnodep->initString("ms", 1);
    else if (_loginApplep)
        tnodep->initString("apple", 1);
    else
        tnodep->initString("none", 1);
    nnodep = new Json::Node();
    nnodep->initNamed("authType", tnodep);
    dataNodep->appendChild(nnodep);

    dataNodep->unparse(&result);

    code = fwrite(result.c_str(), result.length(), 1, filep);
    if (code <= 0) {
        /* failed to write data */
        code = -1;
    }
    else
        code = 0;

    fclose(filep);

    return code;
}

int32_t
SApiLoginCookie::restore()
{
    int code;
    FILE *filep;
    size_t fileLength;
    char *fileDatap;
    char *origFileDatap;
    std::string stateFile;
    std::string authToken;
    std::string authType;
    std::string refreshToken;
    Json::Node *rootNodep;
    Json json;
    Json::Node *tnodep;
    struct stat tstat;

    if (!_saveRestoreEnabled)
        return 0;

    stateFile = _libPath + "auth.js";
    filep = fopen(stateFile.c_str(), "r");
    if (!filep)
        return 0;

    code = fstat(fileno(filep), &tstat);
    if (code < 0) {
        fclose(filep);
        return code;
    }

    fileLength = tstat.st_size;
    if (fileLength > 1000000) {  /* sanity check */
        fclose(filep);
        return -1;
    }
    origFileDatap = fileDatap = new char[fileLength];

    code = fread(fileDatap, fileLength, 1, filep);
    if (code <= 0){
        fclose(filep);
        delete [] origFileDatap;
        return -2;
    }

    /* done with the file */
    fclose(filep);
    filep = NULL;

    code = json.parseJsonChars(&fileDatap, &rootNodep);
    if (code != 0) {
        printf("sapilogin: failed to parse auth file code=%d\n", code);
    }
    else {
        /* search children for stuff */
        tnodep = rootNodep->searchForChild("authToken", 0);
        if (tnodep)
            authToken = tnodep->_children.head()->_name;
        tnodep = rootNodep->searchForChild("refreshToken", 0);
        if (tnodep)
            refreshToken = tnodep->_children.head()->_name;
        tnodep = rootNodep->searchForChild("authType", 0);
        if (tnodep)
            authType = tnodep->_children.head()->_name;

        /* now create the SApiLoginGeneric objects */
        if (authType == "ms") {
            if (!_loginMSp) {
                _loginMSp = new SApiLoginMS();
            }
            _loginMSp->setAppParams( "cd97cce7-f4a3-40c2-acc7-66a7924c6341",
                                     "lznisJHEK|msZGD85941{@)");
            _loginMSp->initFromFile(this, authToken, refreshToken);
        }
        else if (authType == "apple") {
            if (!_loginApplep) {
                _loginApplep = new SApiLoginApple();
            }
            _loginApplep->setAppParams("/database/1/iCloud.com.Cazamar.Login1/development/public/users/caller?ckAPIToken=4e2811fdef054c7cb02aca853299b50151f5b7c40e5cdbd9a7762c135af3e99a");
            _loginApplep->initFromFile(this, authToken);
        }
    }

    delete [] origFileDatap;
    return code;
}

void
SApiLoginGeneric::setAuthToken(std::string newStr) {
    const char *tp;
    std::string jsonData;
    char *jsonDatap;
    Json::Node *jnodep;
    Json::Node *childNodep;
    std::string failString;
    int32_t code;

    _authToken = newStr;
    _changeCounter++;

    /* try decoding JWT, if this *is* a JWT */
    setAuthTokenName(std::string("Personal account"));
    tp = newStr.c_str();
    setIsJwt(0);        /* default */
    if (strchr(tp, '.') != NULL) {
        code = Jwt::decode(newStr, &jsonData);
        if (code == 0) {
            Json jsys;
            jsonDatap = (char *) jsonData.c_str();
            code = jsys.parseJsonChars(&jsonDatap, &jnodep);
            if (code == 0) {
                setIsJwt(1);
                childNodep = jnodep->searchForChild("unique_name", 0);
                if (childNodep)
                    setAuthTokenName(childNodep->_children.head()->_name);
                delete jnodep;
            }
        }
    }
}
