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
 * performs user logins.  It no longer uses a key server; instead the
 * browser is directed to deliver the tokens to localhost:7700.
 *
 * A program put in a link to trigger a login, linking to
 * /msLoginScreen.  When a user clicks on that button in the app's UI,
 * the MSLoginScreenMethod will be invoked.
 *
 * The MSLoginScreenMethod calls SApiLoginMS::getLoginPage to get a
 * page that pops up the canned login window created by Microsoft's
 * authentication server to start the token creation process.  The
 * page that drives this is in login-ms.html, and says not to pay
 * attention to it since it will disappear when the login process is
 * finished.
 *
 * The app is registered with MSFT as Backup4Cloud in
 * michael.kazar@verizon.net's account; all it does is provide a
 * client ID and secret for our app.  The MSFT auth server will send
 * the login code (the first token in a series of tokens required to
 * be really authenticated) back to https://localhost:7700/login4ms by
 * directing the browser to that page.  This eventually runs
 * keyLoginMethod in this file.  We're passed the authentication code
 * as an "&" parameter in the URL, which we save in the SApiLoginMs
 * object.  The HTML that we return is displayed to the user, so we
 * return the contents of login-ms-done.html; this basically tells the
 * user that the login process is done.  The login-ms-done HTML also
 * posts a message to the window that opened this one (login-ms.html)
 * with the contents 'kazar1'.  This tells the login-ms window (the
 * one that says not to pay attention to the man behind the curtain)
 * to finish login processing.  The last thing the login-ms-done
 * window does is close itself.
 *
 * To finish login processing, login-ms.html posts a message to
 * /sapiKeyData.  This runs AppleLoginKeyDataMethod, which once upon a
 * time, contacted a separate process that received the tokens.  Now
 * that we're using a saner flow, this code just takes the
 * authentication code returned above, and contacts the MSFT token
 * server to get an authentication tokens from it and saves it in
 * auth.js.  Once this is complete, the Javascript in the
 * login-ms.html window redirects itself to the application's top page
 * ("/"), and we're done.
 */

class SApiLoginMS;

SApiLoginCookie *SApiLogin::_globalCookiep = NULL;

/* This is an internal class used by SApiAppleLogin to handle the
 * callback from Javascript once the login process has completed.
 *
 * When this is invoked, we should have the login code in the
 * SApiLoginMs structure.  We ask MSFT to exchange the login code for
 * an auth token, store it internally and save it to a file
 * (saveCookieState).
 *
 * Once done, the SApiAppleLogin class has the auth token stored
 * internally, and the code in haveToken in login-ms.html redirects
 * the window to the app's initial screen once we send a response to
 * the HTTP request that triggered us.
 */
void
SApiLoginReq::AppleLoginKeyDataMethod()
{
    char tbuffer[4096];
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
    SApiLoginCookie *cookiep = SApiLogin::getLoginCookie(this);

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
    
    code = inPipep->read(tbuffer, sizeof(tbuffer)-1);
    if (code >= 0) {
        if (code >= sizeof(tbuffer))
            printf("apptest/keyData: bad count from pipe read\n");
        else
            tbuffer[code] = 0;
    }

    response = std::string(tbuffer);
    
    inputReceived();

    /* now that we've been notified that the authorization keys have been deposited with
     * the key server, pick them up using the request ID.
     */
    {
        std::string callbackString;
        std::string authToken;
        
        /* this converts the once only token into a real authentication token */
        genLoginp->refineAuthToken(&genLoginp->_code, cookiep);

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
SApiLoginMS::init(SApi *sapip, SApiLoginCookie *cookiep, std::string finalUrl)
{
    // initialize the server responding to delivery of keys.
    keyServer(7700);

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
        redirectUrl += "&redirect_uri=https%3a%2f%2flocalhost:7700%2flogin4ms";
        redirectUrl += "&response_mode=query";
        redirectUrl += "&scope=offline_access%20files.readwrite";
        redirectUrl += "&prompt=login"; // still need?
        redirectUrl += "&code_challenge=bosonbosonbosonbosonbosonbosonbosonboson1234";
        redirectUrl += "&code_challenge_type=plain";
        redirectUrl += "&state=" + _authId;
        dict.add("redir", redirectUrl);
        dict.add("final", _finalUrl);
        
        if (cookiep) {
            loginPath = cookiep->getPathPrefix();
            loginPath += "login-ms.html";
        }

        code = SApi::ServerConn::interpretFile((char *) loginPath.c_str(), &dict, outStringp);
        if (code != 0) {
            snprintf(tbuffer, sizeof(tbuffer), "Oops, interpretFile code is %d\n", code);
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
        postData += "&redirect_uri=https%3a%2f%2flocalhost:7700%2flogin4ms";
        postData += "&grant_type=authorization_code";
        postData += "&code_verifier=bosonbosonbosonbosonbosonbosonbosonboson1234";
#if 0
        // don't use when using localhost
        postData += "&client_secret=" + Rst::urlEncode(&_clientSecret);
#endif

        xapip = new XApi();
        bufGenp = new BufTls(cookiep->getPathPrefix());
        bufGenp->init(const_cast<char *>("login.microsoftonline.com"), 443);
        connp = xapip->addClientConn(bufGenp);
        reqp = new XApi::ClientReq();
        reqp->addHeader("Origin", "https://localhost");
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
    postData += "&redirect_uri=https%3a%2f%2flocalhost:7700%2flogin4ms";
    postData += "&grant_type=refresh_token";
//    postData += "&client_secret=" + Rst::urlEncode(&_clientSecret);
    
    xapip = new XApi();
    bufGenp = new BufTls(_cookiep->getPathPrefix());
    bufGenp->init(const_cast<char *>("login.microsoftonline.com"), 443);
    connp = xapip->addClientConn(bufGenp);
    reqp = new XApi::ClientReq();
    reqp->addHeader("Origin", "https://localhost");
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

void
SApiLoginKeyReq::keyLoginMethod()
{
    char tbuffer[_bigStr];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    std::string tableKey;
    SApiLoginMS *sapiLoginp = static_cast<SApiLoginMS *>(_sapip->getContext());
    Rst::Hdr *hdrp;
    dqueue<Rst::Hdr> *urlPairsp;

    CThreadPipe *outPipep = getOutgoingPipe();

    /* search for state and code strings for table entries */
    urlPairsp = _rstReqp->getUrlPairs();
    for(hdrp = urlPairsp->head(); hdrp; hdrp=hdrp->_dqNextp) {
        /* could also look for ckSession */
        if (strcasecmp("code", hdrp->_key.c_str()) == 0) {
            sapiLoginp->_code = hdrp->_value;
        }
    }

    printf("\nReads (generic login) done\n");
    
    code = getConn()->interpretFile( (char *) "login-ms-done.html", 
                                     &dict, 
                                     &response);

    if (code != 0) {
        snprintf(tbuffer, sizeof(tbuffer),"Oops, interpretFile code is %d\n", code);
        obufferp = tbuffer;
    }
    else {
        obufferp = const_cast<char *>(response.c_str());
    }
    
    setSendContentLength(strlen(obufferp));
    
    /* reverse the pipe -- must know length, or have set content length to -1 */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    outPipep->eof();
    
    requestDone();
}

void
SApiLoginMS::keyServer(int port)
{
    SApi *sapip;

    sapip = new SApi();
    sapip->setContext(this);
    sapip->useTls();
    
    sapip->registerUrl("/login4ms",
                       (SApi::RequestFactory *) &SApiLoginKeyReq::keyFactory,
                       (SApi::StartMethod) &SApiLoginKeyReq::keyLoginMethod);
    
    sapip->initWithPort(port);
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

// MS.  This is the first function invoked when the user clicks on the
// "MS Login" button.  It's context is the SApiLogin structure.  It
// creates a SApiLoginMS structure that will drive the login process.
void
SApiLoginReq::MSLoginScreenMethod()
{
    char tbuffer[16384];
    char *obufferp;
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
            cookiep->_loginMSp->setAppParams( "adafdab2-9b78-4c4c-833f-826b0e9be124",
                                              "~2q8Q~_vm1eR5oEb_QWFz1pmo6MVxihLClPn2bdH");

            cookiep->_loginMSp->init(_sapip, cookiep, "/");
            cookiep->_loginMSp->getLoginPage(&response, cookiep);
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
    
    outPipep->write(obufferp, strlen(obufferp));
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
        snprintf(tbuffer, sizeof(tbuffer), "Oops, interpretFile code is %d\n", code);
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
            _loginMSp->setAppParams( "adafdab2-9b78-4c4c-833f-826b0e9be124",
                                     "~2q8Q~_vm1eR5oEb_QWFz1pmo6MVxihLClPn2bdH");
            _loginMSp->initFromFile(this, authToken, refreshToken);
        }
        else if (authType == "apple") {
            osp_assert(0);
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
