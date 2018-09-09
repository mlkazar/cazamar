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
            // printf("DataSourceFile closing fd=%d\n", _fd);
            ::close(_fd);
            _fd = -1;
        }
        return 0;
    }

    static void statToAttr(struct stat *tstatp, CAttr *attrp) {
#ifdef __linux__
        attrp->_mtime = ((uint64_t) tstatp->st_mtim.tv_sec *1000000000ULL + 
                         tstatp->st_mtim.tv_nsec);
        attrp->_ctime = ((uint64_t) tstatp->st_ctim.tv_sec *1000000000ULL + 
                         tstatp->st_ctim.tv_nsec);
#else
        attrp->_mtime = ((uint64_t) tstatp->st_mtimespec.tv_sec *1000000000ULL + 
                         tstatp->st_mtimespec.tv_nsec);
        attrp->_ctime = ((uint64_t) tstatp->st_ctimespec.tv_sec *1000000000ULL + 
                         tstatp->st_ctimespec.tv_nsec);
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

class UploadErrorEntry {
 public:
    std::string _op;
    int32_t _httpError;
    std::string _shortError;
    std::string _longError;
    UploadErrorEntry *_dqNextp;
    UploadErrorEntry *_dqPrevp;
};

class UploadEntry {
public:
    Uploader *_uploaderp;
    UploadApp *_app;
    std::string _fsRoot;
    std::string _cloudRoot;
    uint32_t _lastFinishedTime;
    uint8_t _enabled;

    UploadEntry() {
        _uploaderp = NULL;
        _app = NULL;
        _lastFinishedTime = 0;
        _enabled = 1;
    }

    ~UploadEntry();

    void stop();
};

class Uploader {
public:
    typedef enum { STOPPED = 1,
                   PAUSED = 2,
                   RUNNING = 3,
                   STALLED = 4,
                   STARTING = 5,
    } Status;

    typedef void StateProc(void *contextp);

    CDisp *_cdisp;
    CDispGroup *_group;
    Cfs *_cfsp;
    std::string _fsRoot;      /* not counting terminal '/' */
    uint32_t _fsRootLen;
    CThreadMutex _lock;
    WalkTask *_walkTaskp;
    Status _status;
    std::string _cloudRoot;
    uint8_t _verbose;
    StateProc *_stateProcp;
    void *_stateContextp;

    /* some stats */
    uint64_t _filesCopied;
    uint64_t _bytesCopied;
    uint64_t _filesSkipped;
    uint64_t _fileCopiesFailed;

    Uploader() {
        _cdisp = NULL;
        _group = NULL;
        _cfsp = NULL;
        _fsRootLen = 0;
        _status = STOPPED;
        _verbose = 0;
        _stateProcp = NULL;
        _stateContextp = NULL;

        _filesCopied = 0;
        _bytesCopied = 0;
        _filesSkipped = 0;
        _fileCopiesFailed = 0;

        return;
    }

    ~Uploader() {
        if (_group) {
            delete _group;
            _group = NULL;
        }
        /* don't delete walkTaskp, since it auto deletes */
    }

    static int32_t mainCallback(void *contextp, std::string *pathp, struct stat *statp);

    void init(std::string cloudRoot,
              std::string fsRoot,
              CDisp *cdisp,
              Cfs *cfsp,
              StateProc *procp,
              void *contextp) {
        _cloudRoot = cloudRoot;
        _cdisp = cdisp;
        _cfsp = cfsp;
        _fsRoot = fsRoot;
        _fsRootLen = (uint32_t) _fsRoot.length();
        _stateProcp = procp;
        _stateContextp = contextp;
    }

    void setStateProc(StateProc *procp, void *contextp) {
        _stateProcp = procp;
        _stateContextp = contextp;
    }

    void setVerbose() {
        _verbose = 1;
    }

    void pause();

    void stop();

    void start();

    void resume();

    static void done(CDisp *disp, void *contextp);

    Status getStatus() {
        Status status;

        /* see if we finished the tree walk, since we don't get callbacks when done;
         * if in STARTING state, we havne't fired up the cdisp group yet.
         */
        if (_status != STOPPED && _status != STARTING) {
            if (_group && _group->isAllDone())
                _status = STOPPED;
        }

        status = _status;

        if (_cfsp->getStalling() && (status == RUNNING || status == STARTING))
            status = STALLED;
            
        return status;
    }

    int isIdle() {
        if (_status == STOPPED && _group->isAllDone())
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
        else if (status == STALLED)
            return "Server2Busy";
        else if (status == STARTING)
            return "Starting";
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
class UploadApp : public CThread {
public:
    static const uint32_t _maxUploaders = 128;
    static const uint32_t _maxErrorEntries = 64;
    static UploadApp *_globalApp;
    UploadEntry *_uploadEntryp[_maxUploaders]; /* array of pointers to UploaderEntries */
    SApiLoginCookie *_loginCookiep;
    std::string _pathPrefix;
    std::string _libPath;
    std::string fsRoot;
    std::string cloudRoot;
    uint32_t _backupInterval;
    Cfs *_cfsp;
    CDisp *_cdisp;
    CThreadMutex _lock; /* protect error entries */
    dqueue<UploadErrorEntry> _errorEntries;
    CThreadMutex _entryLock;    /* protect upload entries */

    class Log : public CfsLog {
        UploadApp *_uploadApp;
    public:
        void logError( CfsLog::OpType type,
                       int32_t httpError,
                       std::string errorString,
                       std::string longErrorString);

        Log(UploadApp *uploadApp) {
            _uploadApp = uploadApp;
        }
    } _log;

    UploadApp(std::string pathPrefix, std::string libPath) : _log(this) {
        uint32_t i;
        for(i=0;i<_maxUploaders;i++) {
            _uploadEntryp[i] = NULL;
        }
        _pathPrefix = pathPrefix;
        _libPath = libPath;
        _loginCookiep = NULL;
        _cfsp = NULL;
        _cdisp = NULL;

        readConfig(libPath);

        /* TBD: get this from some configuration mechanism */
#ifndef __linux__
        if (_uploadEntryp[0] == NULL) {
            fsRoot = std::string(getenv("HOME")) + "/Pictures";
            cloudRoot = "/" + std::string(getenv("USER")) + "_pictures";
            addConfigEntry(cloudRoot, fsRoot, 0, 1);
            writeConfig(libPath);
        }
#endif
        _globalApp = this;
    }

    void setGlobalLoginCookie(SApiLoginCookie *loginCookiep) {
        _loginCookiep = loginCookiep;
    }

    static std::string showInterval(int32_t interval);
    
    static int32_t parseInterval(std::string istring);

    static UploadApp *getGlobalApp() {
        return _globalApp;
    }

    void schedule(void *cxp);

    int32_t initLoop(SApi *sapip);

    int32_t init(SApi *sapip);

    static std::string getDate(time_t secs);

    int32_t addConfigEntry( std::string cloudRoot,
                            std::string fsRoot,
                            uint32_t lastFinishedTime,
                            int enabled);

    int32_t deleteConfigEntry(int32_t ix);

    int32_t setEnabledConfig(int32_t ix);

    void stop() {
        uint32_t i;
        UploadEntry *ep;
        Uploader *uploaderp;

        _entryLock.take();
        for(i=0; i<_maxUploaders; i++) {
            ep = _uploadEntryp[i];
            if (!ep)
                continue;
            uploaderp = ep->_uploaderp;
            if (!uploaderp)
                continue;
            uploaderp->stop();
        }
        _entryLock.release();
    }

    void pause() {
        uint32_t i;
        UploadEntry *ep;
        Uploader *uploaderp;

        _entryLock.take();
        for(i=0; i<_maxUploaders; i++) {
            ep = _uploadEntryp[i];
            if (!ep)
                continue;
            uploaderp = ep->_uploaderp;
            if (!uploaderp)
                continue;
            uploaderp->pause();
        }
        _entryLock.release();
    }

    void startEntry(UploadEntry *ep);

    void start() {
        UploadEntry *ep;
        uint32_t i;

        _entryLock.take();
        for(i=0; i<_maxUploaders; i++) {
            ep = _uploadEntryp[i];
            if (ep)
                startEntry(ep);
        }
        _entryLock.release();
    }

    static void stateChanged(void *contextp);

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

class UploadInfoScreen : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    UploadInfoScreen(SApi *sapip) : SApi::ServerReq(sapip) {
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

class UploadInfoData : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    UploadInfoData(SApi *sapip) : SApi::ServerReq(sapip) {
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

class UploadSetEnabledConfig : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    UploadSetEnabledConfig(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

class UploadBackupInterval : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    UploadBackupInterval(SApi *sapip) : SApi::ServerReq(sapip) {
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
