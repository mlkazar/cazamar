#ifndef __UPLOAD_H_ENV__
#define __UPLOAD_H_ENV__ 1

#include <stdio.h>
#include <string>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "sapi.h"
#include "sapilogin.h"
#include "xapi.h"
#include "bufsocket.h"
// #include "buftls.h"
#include "json.h"
#include "cfsms.h"
#include "walkdisp.h"

class Uploader;
class UploadApp;
class UploadEntry;

class DataSourceString : public CDataSource {
    const char *_datap;
    std::string _dataStr;

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

    void setString( std::string data) {
        _dataStr = data;
        _datap = _dataStr.c_str();
    }

    DataSourceString ( std::string data) {
        _dataStr = data;
        _datap = _dataStr.c_str();
    }

    ~DataSourceString() {
	return;
    }
};

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

class UploadEntry {
public:
    Uploader *_uploaderp;
    UploadApp *_app;
    std::string _fsRoot;
    std::string _cloudRoot;
    uint64_t _lastFinishedTime;

    UploadEntry() {
        _uploaderp = NULL;
        _app = NULL;
        _lastFinishedTime = 0;
    }

    ~UploadEntry();

    void stop();
};

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

    /* some stats */
    uint64_t _filesCopied;
    uint64_t _bytesCopied;
    uint64_t _filesSkipped;
    uint64_t _fileCopiesFailed;

    Uploader() {
        _cfsp = NULL;
        _fsRootLen = 0;
        _status = STOPPED;
        _loginMSp = NULL;

        _filesCopied = 0;
        _bytesCopied = 0;
        _filesSkipped = 0;
        _fileCopiesFailed = 0;

        return;
    }

    static int32_t mainCallback(void *contextp, std::string *pathp, struct stat *statp);

    void init(std::string cloudRoot, std::string fsRoot, SApiLoginMS *loginMSp) {
        _cloudRoot = cloudRoot;
        _fsRoot = fsRoot;
        _fsRootLen = (uint32_t) _fsRoot.length();
        _loginMSp = loginMSp;
    }

    void pause();

    void stop();

    void start();

    void resume();

    Status getStatus() {
        /* see if we finished the tree walk, since we don't get callbacks when done */
        if (_status != STOPPED) {
            if (_disp->isAllDone())
                _status = STOPPED;
        }
        return _status;
    }

    int isIdle() {
        if (_status == STOPPED && _disp->isAllDone())
            return 1;
        else
            return 0;
    }

    std::string getStatusString() {
        Status status = getStatus();
        if (status == STOPPED)
            return "Stopped";
        else if (status == PAUSED)
            return "Paused";
        else
            return "Running";
    }

    void getFullStatus( Status *statep,
                        uint64_t *filesCopiedp,
                        uint64_t *bytesCopiedp,
                        uint64_t *totalFilesp,
                        uint64_t *totalBytesp);
};

/* these fields should all be indexed by a cookie dictionary hanging
 * off of the SApi structure.
 */
class UploadApp {
public:
    static const uint32_t _maxUploaders = 128;
    UploadEntry *_uploadEntryp[_maxUploaders]; /* array of pointers to UploaderEntries */
    SApiLoginCookie *_loginCookiep;
    std::string _pathPrefix;
    std::string fsRoot;
    std::string cloudRoot;

    UploadApp(std::string pathPrefix) {
        uint32_t i;
        for(i=0;i<_maxUploaders;i++) {
            _uploadEntryp[i] = NULL;
        }
        _pathPrefix = pathPrefix;
        _loginCookiep = NULL;

        readConfig(pathPrefix);

        /* TBD: get this from some configuration mechanism */
#ifndef __linux__
        if (_uploadEntryp[0] == NULL) {
            fsRoot = std::string(getenv("HOME")) + "/Pictures";
            cloudRoot = "/" + std::string(getenv("USER")) + "_backups";
            addConfigEntry(cloudRoot, fsRoot, 0);
            writeConfig(pathPrefix);
        }
#endif
    }

    int32_t initLoop(SApi *sapip);

    int32_t init(SApi *sapip);

    int32_t addConfigEntry(std::string cloudRoot, std::string fsRoot, uint64_t lastFinishedTime);

    int32_t deleteConfigEntry(int32_t ix);

    void stop() {
        uint32_t i;
        UploadEntry *ep;
        Uploader *uploaderp;
        for(i=0; i<_maxUploaders; i++) {
            ep = _uploadEntryp[i];
            if (!ep)
                continue;
            uploaderp = ep->_uploaderp;
            if (!uploaderp)
                continue;
            uploaderp->stop();
        }
    }

    void pause() {
        uint32_t i;
        UploadEntry *ep;
        Uploader *uploaderp;
        for(i=0; i<_maxUploaders; i++) {
            ep = _uploadEntryp[i];
            if (!ep)
                continue;
            uploaderp = ep->_uploaderp;
            if (!uploaderp)
                continue;
            uploaderp->pause();
        }
    }

    void start() {
        uint32_t i;
        UploadEntry *ep;
        Uploader *uploaderp;
        Uploader::Status upStatus;

        for(i=0; i<_maxUploaders; i++) {
            ep = _uploadEntryp[i];
            if (!ep)
                continue;
            uploaderp = ep->_uploaderp;
            if (!uploaderp) {
                /* create the uploader */
                ep->_uploaderp = uploaderp = new Uploader();
                uploaderp->init(ep->_cloudRoot, ep->_fsRoot, _loginCookiep->_loginMSp);
            }
            upStatus = uploaderp->getStatus();
            if (upStatus == Uploader::PAUSED)
                uploaderp->resume();
            else if (upStatus == Uploader::STOPPED)
                uploaderp->start();
            /* otherwise already running */
        }
    }

    void readConfig(std::string pathPrefix);

    int32_t writeConfig(std::string pathPrefix);
};

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

class UploadPauseScreen : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    UploadPauseScreen(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

class UploadStatusData : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    UploadStatusData(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

class UploadLoadConfig : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    UploadLoadConfig(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

class UploadDeleteConfig : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    UploadDeleteConfig(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

class UploadCreateConfig : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    UploadCreateConfig(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

#endif /* __UPLOAD_H_ENV__ */
