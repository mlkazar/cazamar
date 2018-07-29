#include <stdio.h>
#include <string>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "sapi.h"
#include "sapilogin.h"
#include "xapi.h"
#include "bufsocket.h"
#include "buftls.h"
#include "json.h"
#include "cfsms.h"
#include "walkdisp.h"

void server(int argc, char **argv, int port);

class DataSourceFile : public CDataSource {
    int _fd;
    CThreadMutex _lock;

 public:
    int32_t open(const char *fileNamep);

    int32_t getAttr(CAttr *attrp);

    int32_t read( uint64_t offset, uint32_t count, char *bufferp);

    int32_t close() {
        if (_fd >= 0) {
            printf("DataSourceFile closing fd=%d\n", _fd);
            ::close(_fd);
            _fd = -1;
        }
        return 0;
    }

    static void statToAttr(struct stat *tstatp, CAttr *attrp) {
#ifdef __linux__
        attrp->_mtime = tstatp->st_mtim.tv_sec *1000000 + tstatp->st_mtim.tv_nsec;
        attrp->_ctime = tstatp->st_ctim.tv_sec *1000000 + tstatp->st_ctim.tv_nsec;
#else
        attrp->_mtime = tstatp->st_mtimespec.tv_sec *1000000 + tstatp->st_mtimespec.tv_nsec;
        attrp->_ctime = tstatp->st_ctimespec.tv_sec *1000000 + tstatp->st_ctimespec.tv_nsec;
#endif
        attrp->_length = tstatp->st_size;
    }

    DataSourceFile() {
        _fd = -1;
    }

    ~DataSourceFile() {
        close();
    }
};

int32_t
DataSourceFile::open(const char *fileNamep)
{
    _fd = ::open(fileNamep, O_RDONLY);
    if (_fd < 0)
	return -1;
    else
	return 0;
}

int32_t
DataSourceFile::getAttr(CAttr *attrp)
{
    struct stat tstat;
    int code;

    if (_fd < 0)
	return -1;

    code = fstat(_fd, &tstat);
    if (code < 0)
	return -1;

    statToAttr(&tstat, attrp);

    return 0;
}

int32_t
DataSourceFile::read( uint64_t offset, uint32_t count, char *bufferp)
{
    int32_t code;
    int64_t retOffset;

    if (_fd < 0)
	return -1;

    /* make seek + read for this file atomic */
    _lock.take();

    retOffset = lseek(_fd, offset, SEEK_SET);
    if (retOffset < 0) {
	_lock.release();
	return -1;
    }

    code = (int32_t) ::read(_fd, bufferp, count);

    _lock.release();

    return code;
}

class Uploader {
public:
    typedef enum { STOPPED = 1,
                   PAUSED = 2,
                   RUNNING = 3} Status;
    CfsMs *_cfsp;
    std::string _fsRoot;      /* not counting terminal '/' */
    uint32_t _fsRootLen;
    SApiLoginMS *_loginMSp;
    CDisp *_disp;
    CThreadMutex _lock;
    WalkTask *_walkTaskp;
    Status _status;
    std::string _cloudRoot;

    Uploader() {
        _cfsp = NULL;
        _fsRootLen = 0;
        _status = STOPPED;
        _loginMSp = NULL;
        return;
    }

    static int32_t mainCallback(void *contextp, std::string *pathp, struct stat *statp);

    void init(std::string cloudRoot, std::string fsRoot, SApiLoginMS *loginMSp) {
        _cloudRoot = cloudRoot;
        _fsRoot = fsRoot;
        _fsRootLen = _fsRoot.length();
        _loginMSp = loginMSp;
    }

    void pause();

    void stop();

    void start();

    void resume();

    Status getStatus() {
        return _status;
    }

    void getFullStatus( Status *statep,
                        uint64_t *filesCopiedp,
                        uint64_t *bytesCopiedp,
                        uint64_t *totalFilesp,
                        uint64_t *totalBytesp);
};

void
Uploader::start()
{
    CnodeMs *rootp;
    CnodeMs *testDirp;
    CAttr attrs;
    CAttr dirAttrs;
    int32_t code;
    std::string uploadUrl;
    WalkTask *taskp;

    printf("cfstest: tests start login=%p\n", _loginMSp);
    _cfsp = new CfsMs(_loginMSp);
    _cfsp->root((Cnode **) &rootp, NULL);
    rootp->getAttr(&attrs, NULL);

    code = _cfsp->namei(_cloudRoot, (Cnode **) &testDirp, NULL);
    if (code != 0) {
        code = _cfsp->mkdir(_cloudRoot, (Cnode **) &testDirp, NULL);
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

    /* copy the pictures directory to a subdir of testdir */
    _disp = new CDisp();
    printf("Created new cdisp at %p\n", _disp);
    _disp->init(8);      /* TBD: crank this up to 8 */

    printf("Starting copy\n");
    taskp = new WalkTask();
    taskp->initWithPath(_fsRoot);
    taskp->setCallback(&Uploader::mainCallback, this);
    _disp->queueTask(taskp);

    printf("uploader: started\n");
}

void
Uploader::stop()
{
    /* TBD: call the dispatcher to stop */
    return;
}

void
Uploader::pause()
{
    /* TBD: call the dispatcher to pause */
    return;
}

void
Uploader::resume()
{
    /* call the dispatcher to resume execution of tasks */
    return;
}

/* these fields should all be indexed by a cookie dictionary hanging
 * off of the SApi structure.
 */
class UploadApp {
public:
    std::string _cloudRoot;
    std::string _fsRoot;
    Uploader *_uploaderp;
    SApiLoginCookie *_loginCookiep;

    UploadApp() {
        _uploaderp = NULL;
        _loginCookiep = NULL;

        /* TBD: get this from some configuration mechanism */
#ifdef __linux__
        _fsRoot = "/home/pi/UpTest";
#else
        _fsRoot = "/Users/kazar/bin";
#endif
        _cloudRoot = "/TestDir";
    }

    void runTests(SApiLoginCookie *loginCookiep);
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
class UploadHomeScreen : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    UploadHomeScreen(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

class UploadStartScreen : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    UploadStartScreen(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

class UploadStopScreen : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    UploadStopScreen(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

/* static */ int32_t
Uploader::mainCallback(void *contextp, std::string *pathp, struct stat *statp)
{
    std::string cloudName;
    DataSourceFile dataFile;
    int32_t code;
    Uploader *up = (Uploader *) contextp;
    CfsMs *cfsp = up->_cfsp;
    Cnode *cnodep;
    std::string relativeName;
    CAttr cloudAttr;
    CAttr fsAttr;

    /* e.g. remove /usr/home from /usr/home/foo/bar, leaving /foo/bar */
    relativeName = pathp->substr(up->_fsRootLen);

    printf("In walkcallback %s\n", pathp->c_str());
    cloudName = up->_cloudRoot + relativeName;

    /* before doing upload, stat the object to see if we've already done the copy; don't
     * do this for dirs.
     */
    if ((statp->st_mode & S_IFMT) != S_IFDIR) {
        code = cfsp->stat(cloudName, &cloudAttr, NULL);
        if (code == 0) {
            DataSourceFile::statToAttr(statp, &fsAttr);

            if (fsAttr._length == cloudAttr._length &&
                cloudAttr._mtime - fsAttr._mtime > 300*1000000000ULL) {
                /* file size is same, and cloud timestamp is more than
                 * 300 seconds later than file's timestamp, then we figure
                 * we've already copied this file.
                 */
                printf("callback: skipping already copied %s\n", pathp->c_str());
                return 0;
            }
        }
    }

    if ((statp->st_mode & S_IFMT) == S_IFDIR) {
        /* do a mkdir */
        code = cfsp->mkdir(cloudName, &cnodep, NULL);
        if (code == 0)
            cnodep->release();
        printf("mkdir of %p done, code=%d\n", cloudName.c_str(), code);
    }
    else if ((statp->st_mode & S_IFMT) == S_IFREG) {
        code = dataFile.open(pathp->c_str());
        if (code != 0) {
            printf("Failed to open file %s\n", pathp->c_str());
            return code;
        }
        code = cfsp->sendFile(cloudName, &dataFile, NULL);
        printf("sendfile path=%s test done, code=%d\n", cloudName.c_str(), code);

        /* dataFile destructor closes file */
    }
    else {
        printf("Uptest: skipping file with weird type %s\n", pathp->c_str());
        code = -1;
    }
    return code;
}

void
UploadHomeScreen::startMethod()
{
    char tbuffer[16384];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    std::string loginHtml;
    UploadApp *uploadApp;
    SApiLoginCookie *contextp;
    std::string authToken;
    int loggedIn = 0;
        
    if ((uploadApp = (UploadApp *) getCookieKey("main")) == NULL) {
        uploadApp = new UploadApp();
        setCookieKey("main", uploadApp);
        printf("Setting Cookie to %p\n", uploadApp);
    }
    else {
        printf("Cookie already set in homepage %p\n", uploadApp);
    }

    /* this does a get if it already exists */
    contextp = SApiLogin::createLoginCookie(this);
    contextp->enableSaveRestore();

    if (contextp && contextp->getActive())
        authToken = contextp->getActive()->getAuthToken();

    if (!contextp || authToken.length() == 0) {
        loginHtml = "<a href=\"/appleLoginScreen\">Apple Login</a><p><a href=\"/msLoginScreen\">        MS Login</a>";
    }
    else {
        loginHtml = "Logged in<p><a href=\"/logoutScreen\">Logout</a>";
        loggedIn = 1;
    }
    loginHtml += "<p><a href=\"/startBackups\">Start backup</a>";
    loginHtml += "<p><a href=\"/stopBackups\">Stop backup</a>";
    
    dict.add("loginText", loginHtml);
    code = getConn()->interpretFile((char *) "upload-home.html", &dict, &response);
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

void
UploadApp::runTests( SApiLoginCookie *loginCookiep)
{
    if (_uploaderp && _uploaderp->getStatus() == Uploader::RUNNING)
        return;

    /* remember this */
    _loginCookiep = loginCookiep;

    if (!_uploaderp) {
        _uploaderp = new Uploader();
        _uploaderp->init( _cloudRoot,
                          _fsRoot,
                          loginCookiep->_loginMSp);
    }

    if (_uploaderp->getStatus() == Uploader::STOPPED) {
        _uploaderp->start();
    }
    else if (_uploaderp->getStatus() == Uploader::PAUSED) {
        _uploaderp->resume();
    }
}

void
UploadStartScreen::startMethod()
{
    char tbuffer[16384];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    std::string loginHtml;
    UploadApp *uploadApp;
    SApiLoginCookie *loginCookiep;
    std::string authToken;
    int loggedIn = 0;
        
    if ((uploadApp = (UploadApp *) getCookieKey("main")) == NULL) {
        strcpy(tbuffer, "No app; visit home page first<p><a href=\"/\">Home screen</a>");
        obufferp = tbuffer;
    }
    else {
        /* this does a get if it already exists */
        loginCookiep = SApiLogin::createLoginCookie(this);
        loginCookiep->enableSaveRestore();

        if (loginCookiep && loginCookiep->getActive())
            authToken = loginCookiep->getActive()->getAuthToken();

        if (!loginCookiep || authToken.length() == 0) {
            strcpy(tbuffer, "Must login before doing backups<p><a href=\"/\"Home screen</a>");
            obufferp = tbuffer;
            code = 0;
        }
        else {
            code = getConn()->interpretFile((char *) "upload-start.html", &dict, &response);
        }

        if (code != 0) {
            sprintf(tbuffer, "Oops, interpretFile code is %d\n", code);
            obufferp = tbuffer;
        }
        else {
            obufferp = const_cast<char *>(response.c_str());
            loggedIn = 1;
        }
    }

    setSendContentLength(strlen(obufferp));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    outPipep->eof();
    
    requestDone();

    if (loggedIn) {
        uploadApp->runTests(loginCookiep);
    }
}

void
UploadStopScreen::startMethod()
{
    char tbuffer[16384];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    std::string loginHtml;
    UploadApp *uploadApp;
    std::string authToken;
        
    if ((uploadApp = (UploadApp *) getCookieKey("main")) == NULL) {
        strcpy(tbuffer, "No app running to stop; visit home page first<p>"
               "<a href=\"/\">Home screen</a>");
        obufferp = tbuffer;
    }
    else {
        code = getConn()->interpretFile((char *) "upload-stop.html", &dict, &response);

        if (code != 0) {
            sprintf(tbuffer, "Oops, interpretFile code is %d\n", code);
            obufferp = tbuffer;
        }
        else {
            obufferp = const_cast<char *>(response.c_str());
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
UploadHomeScreen::factory(std::string *opcodep, SApi *sapip)
{
    UploadHomeScreen *reqp;
    reqp = new UploadHomeScreen(sapip);
    return reqp;
}

SApi::ServerReq *
UploadStartScreen::factory(std::string *opcodep, SApi *sapip)
{
    UploadStartScreen *reqp;
    reqp = new UploadStartScreen(sapip);
    return reqp;
}

SApi::ServerReq *
UploadStopScreen::factory(std::string *opcodep, SApi *sapip)
{
    UploadStopScreen *reqp;
    reqp = new UploadStopScreen(sapip);
    return reqp;
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
    sapip->registerUrl("/", &UploadHomeScreen::factory);
    sapip->registerUrl("/startBackups", &UploadStartScreen::factory);
    sapip->registerUrl("/stopBackups", &UploadStopScreen::factory);

    while(1) {
        sleep(1);
    }
}
