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
        if (_fd >= 0)
            ::close(_fd);
        return 0;
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

#ifdef __linux__
    attrp->_mtime = tstat.st_mtime.tv_sec *1000000 + tstat.st_mtime.tv_nsec;
    attrp->_ctime = tstat.st_ctime.tv_sec *1000000 + tstat.st_ctime.tv_nsec;
#else
    attrp->_mtime = tstat.st_mtimespec.tv_sec *1000000 + tstat.st_mtimespec.tv_nsec;
    attrp->_ctime = tstat.st_ctimespec.tv_sec *1000000 + tstat.st_ctimespec.tv_nsec;
#endif
    attrp->_length = tstat.st_size;

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
    CfsMs *_cfsp;
    std::string _basePath;      /* not counting terminal '/' */
    uint32_t _basePathLen;
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    HomeScreen(SApi *sapip) : SApi::ServerReq(sapip) {
        _cfsp = NULL;
        return;
    }

    void startMethod();

    void runTests(SApiLoginMS *loginMSp);

    static int32_t mainCallback(void *contextp, std::string *pathp, struct stat *statp);
};

int32_t
HomeScreen::mainCallback(void *contextp, std::string *pathp, struct stat *statp)
{
    std::string cloudName;
    DataSourceFile dataFile;
    int32_t code;
    HomeScreen *homeScreenp = (HomeScreen *) contextp;
    CfsMs *cfsp = homeScreenp->_cfsp;
    Cnode *cnodep;
    std::string relativeName;

    /* e.g. remove /usr/home from /usr/home/foo/bar, leaving /foo/bar */
    relativeName = pathp->substr(homeScreenp->_basePathLen);

    printf("In walkcallback %s\n", pathp->c_str());
    cloudName = "/TestDir" + relativeName;
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
HomeScreen::runTests(SApiLoginMS *loginMSp)
{
    CnodeMs *rootp;
    CnodeMs *testDirp;
    CAttr attrs;
    CAttr dirAttrs;
    int32_t code;
    std::string uploadUrl;
    CDisp *disp;
    WalkTask *taskp;
    const char *pathp = "/Users/kazar/bin";

    printf("cfstest: tests start login=%p\n", loginMSp);
    _cfsp = new CfsMs(loginMSp);
    _cfsp->root((Cnode **) &rootp, NULL);
    rootp->getAttr(&attrs, NULL);

    code = rootp->lookup("TestDir", (Cnode **)&testDirp, NULL);
    if (code != 0) {
        code = rootp->mkdir("TestDir", (Cnode **) &testDirp, NULL);
        if (code != 0) {
            printf("makedir failed code=%d\n", code);
            return;
        }
    }

    /* setup base path so we know what part of the file system path
     * to splice out.
     */
    _basePath = std::string(pathp);
    _basePathLen = _basePath.length();

    /* lookup succeeded */
    code = testDirp->getAttr(&dirAttrs, NULL);
    if (code != 0) {
        printf("dir getattr failed code=%d\n", code);
    }

    /* copy the pictures directory to a subdir of testdir */
    disp = new CDisp();
    printf("Created new cdisp at %p\n", disp);
    disp->init(1);      /* TBD: crank this up */

    printf("Starting tests\n");
    taskp = new WalkTask();
    taskp->initWithPath(pathp);
    taskp->setCallback(&HomeScreen::mainCallback, this);
    disp->queueTask(taskp);

    while(disp->isActive())
        sleep(1);

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
        runTests(contextp->_loginMSp);
    }
}

SApi::ServerReq *
HomeScreen::factory(std::string *opcodep, SApi *sapip)
{
    HomeScreen *reqp;
    reqp = new HomeScreen(sapip);
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
    sapip->registerUrl("/", &HomeScreen::factory);

    while(1) {
        sleep(1);
    }
}
