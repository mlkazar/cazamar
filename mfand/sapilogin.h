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

#ifndef _SAPILOGIN_H_ENV_
#define _SAPILOGIN_H_ENV_ 1 

#include <stdio.h>
#include <string>
#include <stdlib.h>
#include "sapi.h"

/* This file contains the basics for a toy test application, and the
 * login authentication framework used by any application that
 * performs user logins.  It assumes that there's a key server
 * (keyserv) running at djtogoapp.duckdns.org:7700 as well, and that
 * the key server is registered as the application server for the
 * oauth2 application.  It will be sent the web auth token by the
 * login web service you're using, whether Apple, Google or Microsoft.
 * Fuck Amazon, and their closed API.  And slightly screw Apple for
 * not documenting their wire API, instead only having an SDK.
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
class SApiLoginCookie;
class SApiLoginReq;

/* this class defines the generic interface to SApiLogin* */
class SApiLoginGeneric {
public:
    virtual int32_t getLoginPage( std::string *outStringp, SApiLoginCookie *cookiep) = 0;
    virtual int32_t refineAuthToken(std::string *tokenp, SApiLoginCookie *cookiep) = 0;

    virtual std::string getAuthToken() {
        return _authToken;
    }

    virtual std::string getRefreshToken() {
        return _refreshToken;
    }

    virtual std::string getAuthId() {
        return _authId;
    }

    virtual void setAuthToken(std::string newStr);

    virtual std::string getAuthTokenName() {
        return _authTokenName;
    }

    void setAuthTokenName(std::string newStr) {
        _authTokenName = newStr;
    }

    virtual void setRefreshToken(std::string newStr) {
        _refreshToken = newStr;
        _changeCounter++;
    }

    /* by default, login mechanisms don't support refresh tokens */
    virtual int32_t refresh() {
        return -1;
    }

    void setIsJwt(uint8_t is) {
        _isJwt = is;
    }

    uint8_t getIsJwt() {
        return _isJwt;
    }

    std::string _authToken;     /* auth token for authenticated calls */
    std::string _authTokenName; /* name logged in */
    std::string _refreshToken;  /* refresh token if auth token has limited lifetime */
    std::string _finalUrl;      /* URL to switch to when authentication done */
    std::string _authId;        /* ID used as key in key server */
    std::string _code;          // We receive this code from logging in
    SApiLoginCookie *_cookiep;
    uint32_t _changeCounter;
    uint8_t _isJwt;

#ifdef __linux__
    random_data _randomBuf;
    char _randomState[64];
#endif

    SApiLoginGeneric() {
        _cookiep = NULL;
        _changeCounter = 0;
        _isJwt = 0;
#ifdef __linux__
        _randomBuf.state = NULL;
        initstate_r(time(0) + getpid(),
                    _randomState,
                    sizeof(_randomState),
                    &_randomBuf);
#else
        srandomdev();
#endif
    }

    SApiLoginCookie *getCookie();

    void testDamaged(int doRefresh) {
        _authToken = std::string("Jello Biafra");
        if (doRefresh) {
            _refreshToken = std::string("Gang of Four");
        }
    }

    uint32_t getChangeCounter() {
        return _changeCounter;
    }

    void logout() {
        _authToken.erase();
        _refreshToken.erase();
        _authId.erase();
    }

    void printAuthState() {
        printf("Auth token='%s'\nRefresh token='%s'\n",
               _authToken.c_str(), _refreshToken.c_str());
    }
};

/* this class is used to generate a login page containing a button
 * that triggers the creation of a child window connected to Apple's
 * authentication servers.
 *
 * The init function is first called with a SApi structure
 * representing the application's web server.  After that, the
 * application can call getLoginPage to get the contents of a web page
 * with the login button for Apple's authentication servers.  The
 * function sets _authId to the key used to tag this particular
 * authentication request, which will be used later to retrieve the
 * authentication tokens from the key server.
 */
class SApiLoginApple : public SApiLoginGeneric {
    SApi *_sapip;
    // CThreadPipe *outPipe;
    std::string _apiUrl;

public: 
    SApiLoginApple() {
        _sapip = NULL;
    }

    void init(SApi *sapip, SApiLoginCookie *cookiep, std::string finalUrl);

    void initFromFile(SApiLoginCookie *cookiep, std::string authToken);

    int32_t getLoginPage(std::string *outStringp, SApiLoginCookie *cookiep);

    void setAppParams(std::string apiUrl) {
        _apiUrl = apiUrl;
    }

    int32_t refineAuthToken(std::string *tokenp, SApiLoginCookie *cookiep) {
        return 0;
    }

    virtual ~SApiLoginApple() {
        _sapip = NULL;
    }
};

class SApiLoginMS : public SApiLoginGeneric {
    SApi *_sapip;
    // CThreadPipe *outPipe;
    std::string _clientId;
    std::string _clientSecret;
    bool _didNetworkInit;

public: 
    virtual ~SApiLoginMS() {
        _sapip = NULL;
    }

    SApiLoginMS() {
        _sapip = NULL;
        _didNetworkInit = false;
    }

    void init(SApi *sapip, SApiLoginCookie *cookiep, std::string finalUrl);

    void initFromFile(SApiLoginCookie *cookiep, std::string authToken, std::string refreshToken);

    int32_t getLoginPage(std::string *outStringp, SApiLoginCookie *cookiep);

    void setAppParams(std::string clientId, std::string clientSecret) {
        _clientId = clientId;
        _clientSecret = clientSecret;
    }

    void keyServer(int port);

    int32_t refineAuthToken(std::string *tokenp, SApiLoginCookie *cookiep);

    int32_t getUserInfo(SApiLoginCookie *cookiep);

    int32_t refresh();
};

class SApiLoginReq : public SApi::ServerReq {
 public:
    SApiLoginReq(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    static SApi::ServerReq *factory(SApi *sapip) {
        return new SApiLoginReq(sapip);
    }

    void AppleLoginKeyDataMethod();

    void AppleLoginScreenMethod();

    void MSLoginScreenMethod();

    void LogoutScreenMethod();
};

class SApiLogin {
 public:
    
    static SApiLoginCookie *_globalCookiep;

    static void initSApi (SApi *sapip) {
        sapip->registerUrl( "/msLoginScreen", 
                            &SApiLoginReq::factory,
                            (SApi::StartMethod) &SApiLoginReq::MSLoginScreenMethod);
        sapip->registerUrl( "/logoutScreen", 
                            &SApiLoginReq::factory,
                            (SApi::StartMethod) &SApiLoginReq::LogoutScreenMethod);
    }

    static SApiLoginCookie *getLoginCookie(SApi::ServerReq *reqp);

    static SApiLoginCookie *createLoginCookie(SApi::ServerReq *reqp);

    static SApiLoginCookie *createGlobalCookie(std::string pathPrefix, std::string libPath);

    virtual ~SApiLogin() {
        return;
    };
};

class SApiLoginKeyReq : public SApi::ServerReq {
public:
    static const uint32_t _bigStr = 4096;

    static SApiLoginKeyReq *keyFactory(SApi *sapip) {
        printf("in keyfactory\n");
        return new SApiLoginKeyReq(sapip);
    }

    SApiLoginKeyReq(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void keyLoginMethod();
};

/* state associated with SApiLogin and our web session cookie */
class SApiLoginCookie {
public:
    SApiLoginApple *_loginApplep;
    SApiLoginMS *_loginMSp;
    SApiLoginGeneric *_loginActivep;    /* one currently being logged in for this cookie */
    std::string _pathPrefix;
    std::string _libPath;
    uint8_t _saveRestoreEnabled;
#ifdef __linux__
    random_data _randomBuf;
    char _randomState[64];
#endif
    
    void logout() {
        if (_loginApplep) {
            _loginApplep->logout();
            /* TBD: should delete _loginApplep as soon as we add refcount to ensure not in use */
        }
        if (_loginMSp) {
            _loginMSp->logout();
        }

        /* save the state, if we're doing that */
        save();
    }

    void enableSaveRestore() {
        if (!_saveRestoreEnabled) {
            _saveRestoreEnabled = 1;
            restore();
        }
    }

    std::string getAuthToken() {
        if (_loginActivep)
            return _loginActivep->getAuthToken();
        else
            return std::string("");
    }

    int32_t refresh() {
        if (_loginActivep)
            return _loginActivep->refresh();
        else
            return -2;
    }

    std::string getRefreshToken() {
        if (_loginActivep)
            return _loginActivep->getRefreshToken();
        else
            return std::string("");
    }

    int32_t save();

    int32_t restore();

    std::string getPathPrefix() {
        return _pathPrefix;
    }

    void setPathPrefix(std::string pathPrefix) {
        _pathPrefix = pathPrefix;
    }

    std::string getLibPath() {
        return _libPath;
    }

    void setLibPath(std::string libPath) {
        _libPath = libPath;
    }

    SApiLoginGeneric *getActive() {
        return _loginActivep;
    }

    void setActive(SApiLoginGeneric *activep) {
        _loginActivep = activep;
        activep->_cookiep = this;
    }

    SApiLoginCookie() {
        _loginApplep = NULL;
        _loginMSp = NULL;
        _loginActivep = NULL;
        _saveRestoreEnabled = 0;
#ifdef __linux__
        _randomBuf.state = NULL;
        initstate_r(time(0) + getpid(),
                    _randomState,
                    sizeof(_randomState),
                    &_randomBuf);
#else
        srandomdev();
#endif
    }
};

#endif /* _SAPILOGIN_H_ENV_*/
