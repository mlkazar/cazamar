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

    virtual void setAuthToken(std::string newStr) {
        _authToken = newStr;
    }

    virtual void setRefreshToken(std::string newStr) {
        _refreshToken = newStr;
    }

    /* by default, login mechanisms don't support refresh tokens */
    virtual int32_t refresh() {
        return -1;
    }

    std::string _authToken;     /* auth token for authenticated calls */
    std::string _refreshToken;  /* refresh token if auth token has limited lifetime */
    std::string _finalUrl;      /* URL to switch to when authentication done */
    std::string _authId;        /* ID used as key in key server */
    SApiLoginCookie *_cookiep;

#ifdef __linux__
    random_data _randomBuf;
    char _randomState[64];
#endif

    SApiLoginGeneric() {
        _cookiep = NULL;
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

public: 
    virtual ~SApiLoginMS() {
        _sapip = NULL;
    }

    SApiLoginMS() {
        _sapip = NULL;
    }

    void init(SApi *sapip, SApiLoginCookie *cookiep, std::string finalUrl);
    void initFromFile(SApiLoginCookie *cookiep, std::string authToken, std::string refreshToken);
    int32_t getLoginPage(std::string *outStringp, SApiLoginCookie *cookiep);

    void setAppParams(std::string clientId, std::string clientSecret) {
        _clientId = clientId;
        _clientSecret = clientSecret;
    }

    int32_t refineAuthToken(std::string *tokenp, SApiLoginCookie *cookiep);

    int32_t refresh();
};

class AppleLoginKeyData : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcodep, SApi *sapip);

    AppleLoginKeyData(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

class AppleLoginScreen : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    AppleLoginScreen(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

/* MS */
class MSLoginScreen : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    MSLoginScreen(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

class LogoutScreen : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    LogoutScreen(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

class SApiLogin {
 public:
    
    static void initSApi (SApi *sapip) {
        sapip->registerUrl("/appleLoginScreen", &AppleLoginScreen::factory);
        sapip->registerUrl("/msLoginScreen", &MSLoginScreen::factory);
        sapip->registerUrl("/logoutScreen", &LogoutScreen::factory);
    }

    static SApiLoginCookie *getLoginCookie(SApi::ServerReq *reqp) {
        return (SApiLoginCookie *) reqp->getCookieKey("sapiLogin");
    }

    static SApiLoginCookie *createLoginCookie(SApi::ServerReq *reqp);

    virtual ~SApiLogin() {
        return;
    };
};

/* state associated with SApiLogin and our web session cookie */
class SApiLoginCookie {
public:
    SApiLoginApple *_loginApplep;
    SApiLoginMS *_loginMSp;
    SApiLoginGeneric *_loginActivep;    /* one currently being logged in for this cookie */
    std::string _pathPrefix;
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

        /* this points to one of the above login structures, so we don't have to log
         * this guy out.  But we do get rid of the pointer.
         */
        if (_loginActivep)
            _loginActivep = NULL;
    }

    void enableSaveRestore() {
        _saveRestoreEnabled = 1;
        restore();
    }

    int32_t save();

    int32_t restore();

    std::string getPathPrefix() {
        return _pathPrefix;
    }

    void setPathPrefix(std::string pathPrefix) {
        _pathPrefix = pathPrefix;
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
