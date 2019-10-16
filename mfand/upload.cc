#include <stdio.h>
#include <string>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>

#include "sapi.h"
#include "sapilogin.h"
#include "xapi.h"
#include "bufsocket.h"
#include "buftls.h"
#include "json.h"
#include "cfsms.h"
#include "walkdisp.h"
#include "upload.h"

/* stupid rabbit */
UploadApp *UploadApp::_globalApp;

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

/* static */ int32_t
UploadApp::parseInterval(std::string istring)
{
    const char *tp = istring.c_str();
    int tc;
    int32_t interval;
    
    interval = atoi(tp);
    while((tc = *tp++) != 0) {
        if ((tc >= '0' && tc <= '9') || tc == ' ' || tc == '\t')
            continue;
        if (tc == 'd' || tc == 'D') {
            interval *= 86400;
            break;
        }
        else if (tc == 'h' || tc == 'H') {
            interval *= 3600;
        }
        else if (tc == 'm' || tc == 'M') {
            interval *= 60;
            break;
        }
    }
    return interval;
}

/* static */ std::string
UploadApp::showInterval(int32_t interval)
{
    std::string istring;
    const char *unitsp;
    uint32_t divisor;
    char tbuffer[128];

    if (interval < 60) {
        unitsp = " secs";
        divisor = 1;
    }
    else if (interval < 3600) {
        unitsp = " mins";
        divisor = 60;
    }
    else if (interval < 86400) {
        unitsp = " hours";
        divisor = 3600;
    }
    else {
        unitsp = " days";
        divisor = 86400;
    }
    
    sprintf(tbuffer, "%d %s", interval/divisor, unitsp);
    return std::string(tbuffer);
}

/* static */ std::string
UploadApp::getDate(time_t secs)
{
    char tbuffer[100];

    if (secs == 0)
        return std::string("Never");
    else {
        ctime_r(&secs, tbuffer);
        return std::string(tbuffer, 4, 15);
    }
}

/* static */ void
Uploader::done(CDisp *disp, void *contextp)
{
    Uploader *up = (Uploader *) contextp;
    printf("Uploader done for FS path=%s\n", up->_fsRoot.c_str());
    if (up->_stateProcp) {
        up->_stateProcp(up->_stateContextp);
    }
}

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

    printf("Uploader %p starts with cfsp=%p\n", this, _cfsp);

    /* we're in STARTING state until we start the tree walk */
    _status = STARTING;
    _stopReason = REASON_DONE;

    code = _cfsp->root((Cnode **) &rootp, NULL);
    if (checkAbort(code)) {
        printf("root creation failed, probabliy auth issue\n");
        return;
    }

    code = rootp->getAttr(&attrs, NULL);
    if (code != 0) {
        _status = STOPPED;
        checkAbort(code);
        printf("root getattr failed code=%d\n", code);
        return;
    }

    code = _cfsp->namei(_cloudRoot, /* force*/ 1, (Cnode **) &testDirp, NULL);
    if (code != 0) {
        code = _cfsp->mkpath(_cloudRoot, (Cnode **) &testDirp, NULL);
        if (code != 0) {
            _status = STOPPED;
            checkAbort(code);
            printf("mkpath failed code=%d\n", code);
            return;
        }
    }

    /* in case the tree was changed at the server, we clear out our
     * name and attribute caches.
     */
    testDirp->invalidateTree();

    /* lookup succeeded */
    code = testDirp->getAttr(&dirAttrs, NULL);
    if (code != 0) {
        _status = STOPPED;
        printf("dir getattr failed code=%d\n", code);
        checkAbort(code);
        return;
    }
    testDirp->release();
    testDirp = NULL;

    /* reset stats */
    _filesCopied = 0;
    _bytesCopied = 0;
    _filesSkipped = 0;
    _fileCopiesFailed = 0;

    /* copy the pictures directory to a subdir of testdir */
    _group = new CDispGroup();
    _group->init(_cdisp);
    printf("Created new CDispGroup at %p\n", _group);
    _group->setCompletionProc(&Uploader::done, this);

    printf("Starting copy\n");
    taskp = new WalkTask();
    taskp->initWithPath(_fsRoot);
    taskp->setCallback(&Uploader::mainCallback, this);
    _group->queueTask(taskp);

    _status = RUNNING;

    printf("uploader: started\n");
}

void
Uploader::stop()
{
    /* null this out so it doesn't look like a successful completion */
    if (_group)
        _group->stop();
    _status = STOPPED;
    return;
}

void
Uploader::pause()
{
    if (_group) 
        _group->pause();
    _status = PAUSED;
    return;
}

void
Uploader::resume()
{
    /* call the dispatcher to resume execution of tasks */
    if (_group)
        _group->resume();
    _status = RUNNING;
    _stopReason = REASON_DONE;
    return;
}

/* static */ int32_t
Uploader::mainCallback(void *contextp, std::string *pathp, struct stat *statp)
{
    std::string cloudName;
    DataSourceFile dataFile;
    int32_t code;
    Uploader *up = (Uploader *) contextp;
    Cfs *cfsp = up->_cfsp;
    Cnode *cnodep;
    std::string relativeName;
    CAttr cloudAttr;
    CAttr fsAttr;

    /* e.g. remove /usr/home from /usr/home/foo/bar, leaving /foo/bar */
    relativeName = pathp->substr(up->_fsRootLen);

    if (up->_verbose)
        printf("In walkcallback %s\n", pathp->c_str());
    cloudName = cfsp->legalizeIt(up->_cloudRoot + relativeName);

    /* before doing upload, stat the object to see if we've already done the copy; don't
     * do this for dirs.
     */
    if ((statp->st_mode & S_IFMT) == S_IFREG) {
        code = cfsp->stat(cloudName, &cloudAttr, NULL);
        if (code == 0) {
            DataSourceFile::statToAttr(statp, &fsAttr);

            if (fsAttr._length == cloudAttr._length &&
                cloudAttr._mtime - fsAttr._mtime > 300*1000000000ULL) {
                /* file size is same, and cloud timestamp is more than
                 * 300 seconds later than file's timestamp, then we figure
                 * we've already copied this file.
                 */
                if (up->_verbose)
                    printf("callback: skipping already copied %s\n", pathp->c_str());
                up->_filesSkipped++;
                up->_bytesCopied += fsAttr._length;
                return 0;
            }
        }
        else if (up->checkAbort(code)) {
            return 0;
        }
    }
    else if ((statp->st_mode & S_IFMT) == S_IFLNK) {
        code = cfsp->stat(cloudName, &cloudAttr, NULL);
        if (code == 0) {
            DataSourceFile::statToAttr(statp, &fsAttr);
            if (cloudAttr._mtime - fsAttr._mtime > 100*1000000000ULL) {
                if (up->_verbose)
                    printf("callback: skipping already copied symlink %s\n", pathp->c_str());
                up->_filesSkipped++;
                up->_bytesCopied += fsAttr._length;
                return 0;
            }
        }
        else if (up->checkAbort(code)) {
            return 0;
        }
    }

    if ((statp->st_mode & S_IFMT) == S_IFDIR) {
        /* do a mkdir */
        code = cfsp->mkdir(cloudName, &cnodep, NULL);
        if (code == 0) {
            cnodep->release();
            up->_filesCopied++;         /* dir created */
        }
        else {
            if (code != 17) {
                printf("Upload: mkdir failed with code=%d path=%s\n",
                       (int) code, pathp->c_str());
                logError(code, "mkdir failed", *pathp);
            }
            up->_filesSkipped++;        /* dir exists */
            if (up->checkAbort(code))
                return 0;
        }
        if (up->_verbose)
            printf("mkdir of %p done, code=%d\n", cloudName.c_str(), code);
    }
    else if ((statp->st_mode & S_IFMT) == S_IFREG) {
        code = dataFile.open(pathp->c_str());
        if (code != 0) {
            up->_fileCopiesFailed++;
            logError(code, "failed to open regular file", *pathp);
            printf("Upload: failed to open file %s\n", pathp->c_str());
            up->checkAbort(code);
            return code;
        }

        /* send the file, updating bytesCopied on the fly */
        code = cfsp->sendFile(cloudName, &dataFile, &up->_bytesCopied, NULL);

        if (up->_verbose)
            printf("sendfile path=%s test done, code=%d\n", cloudName.c_str(), code);
        if (code) {
            printf("Upload: sendfile path=%s failed code=%d\n", pathp->c_str(), code);
            up->_fileCopiesFailed++;
            up->checkAbort(code);
        }
        else {
            up->_filesCopied++;
        }

        /* dataFile destructor closes file */
    }
    else if ((statp->st_mode & S_IFMT) == S_IFLNK) {
        DataSourceString dataString("Symbolic link\n");
        code = cfsp->sendFile(cloudName, &dataString, NULL, NULL);
        if (up->_verbose)
            printf("sendfile path=%s link done code=%d\n", cloudName.c_str(), code);
        if (code) {
            printf("Upload: sendfile symlink path=%s link failed code=%d\n", pathp->c_str(), code);
            up->_fileCopiesFailed++;
            up->checkAbort(code);
        }
        else {
            up->_filesCopied++;
        }
    }
    else {
        printf("Upload: skipping file with weird type %s\n", pathp->c_str());
        code = -1;
    }
    return code;
}

/* return true if we've encountered a fatal error */
int
Uploader::checkAbort(int32_t code)
{
    if (code == CFS_ERR_ACCESS) {
        if (_group != NULL) {
            _group->stop(/* nowait */ 1);
        }
        _status = STOPPED;
        _stopReason = REASON_AUTH;
        return 1;
    }

    return 0;
}

void
Uploader::logError(int32_t code, std::string errorString, std::string longErrorString) {
    UploadApp *app = UploadApp::getGlobalApp();
    if (app) {
        app->_log.logError(CfsLog::opPosix, code, errorString, longErrorString);
    }
}

void
UploadReq::UploadHomeScreenMethod()
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
    std::string pathPrefix;
    std::string fileName;
    DIR *picDirp;
    const char *picPathp;
    int noPic;
        
    uploadApp = UploadApp::getGlobalApp();

    uploadApp->resetAuthProblems();

    /* this does a get if it already exists */
    contextp = SApiLogin::createLoginCookie(this);
    contextp->enableSaveRestore();

    /* see if we can read the pictures directory; if not, the user should add
     * us to the privacy application list.
     */
    noPic = 0;
    picPathp = uploadApp->_picPath.c_str();
    if (strlen(picPathp) > 0) {
        picDirp = opendir(picPathp);
        if (picDirp == NULL)
            noPic = 1;
        else
            closedir(picDirp);
    }
    

    if (contextp && contextp->getActive())
        authToken = contextp->getActive()->getAuthToken();

    if (noPic) {
        loginHtml += "<a href=\"/helpPics\"><font color=\"red\">WARNING: Kite needs permission to read Pictures; click to fix</font></a><p>";
    }
    if (!contextp || authToken.length() == 0) {
        loginHtml += "<a href=\"/msLoginScreen\">MS Login</a>";
    }
    else {
        loginHtml += "Logged in<p><a href=\"/logoutScreen\">Logout</a>";
    }
    sprintf(tbuffer, "<p><a href=\"/\" onclick=\"getBackupInt(); return false\">Set backup interval (%s)</a>", UploadApp::showInterval(uploadApp->_backupInterval).c_str());
    loginHtml += tbuffer;
    loginHtml += "<p><a href=\"/info\">Look inside</a>";
    fileName = uploadApp->_pathPrefix + "upload-home.html";
    dict.add("loginText", loginHtml);
    code = getConn()->interpretFile(fileName.c_str(), &dict, &response);
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
UploadApp::readConfig(std::string pathPrefix)
{
    Json json;
    Json::Node *rootNodep;
    FILE *filep;
    std::string fileName;
    Json::Node *tnodep;
    Json::Node *nnodep;
    std::string cloudRoot;
    std::string fsRoot;
    uint64_t lastFinishedTime;
    int32_t code;
    uint8_t enabled;
    uint32_t backupInt;

    _backupInterval = 24 * 3600;

    fileName = pathPrefix + "config.js";
    filep = fopen(fileName.c_str(), "r");
    if (!filep)
        return;

    /* parseJsonFile closes filep on both success and error returns */
    code = json.parseJsonFile(filep, &rootNodep);
    if (code != 0) {
        return;
    }

    tnodep = rootNodep->searchForChild("backupInt");
    if (tnodep) {
        backupInt = atoi(tnodep->_children.head()->_name.c_str());
        if (backupInt != 0)
            _backupInterval = backupInt;
    }

    /* read the json from the file -- it's a structure with an array
     * named backupEntries, each of which contains tags cloudRoot,
     * fsRoot, and lastFinishedTime.
     */
    tnodep = rootNodep->searchForChild("backupEntries"); /* find name node */
    if (tnodep) {
        tnodep=tnodep->_children.head(); /* first entry is array node */
        tnodep = tnodep->_children.head(); /* first entry *in* array */
        for(; tnodep; tnodep=tnodep->_dqNextp) {
            /* tnode is a structure */
            nnodep = tnodep->searchForChild("cloudRoot");
            if (nnodep) {
                cloudRoot = nnodep->_children.head()->_name;
            }
            nnodep = tnodep->searchForChild("fsRoot");
            if (nnodep) {
                fsRoot = nnodep->_children.head()->_name;
            }
            nnodep = tnodep->searchForChild("lastFinishedTime");
            if (nnodep) {
                lastFinishedTime = atoi(nnodep->_children.head()->_name.c_str());
            }
            else
                lastFinishedTime = 0;
            nnodep = tnodep->searchForChild("enabled");
            if (nnodep) {
                enabled = atoi(nnodep->_children.head()->_name.c_str());
            }
            else
                enabled = 1;

            /* now add the entry */
            addConfigEntry(cloudRoot, fsRoot, lastFinishedTime, enabled);
        }
    }
}

int32_t
UploadApp::writeConfig(std::string pathPrefix)
{
    Json json;
    Json::Node *rootNodep;
    Json::Node *nnodep;
    Json::Node *tnodep;
    Json::Node *snodep;
    Json::Node *arrayNodep;
    FILE *filep;
    uint32_t i;
    UploadEntry *ep;
    std::string fileName;
    std::string result;
    int32_t code;
    int32_t tcode;

    /* construct the json tree */
    rootNodep = new Json::Node();
    rootNodep->initStruct();

    /* add backup interval to the json data */
    tnodep = new Json::Node();
    tnodep->initInt(_backupInterval);
    nnodep = new Json::Node();
    nnodep->initNamed("backupInt", tnodep);
    rootNodep->appendChild(nnodep);

    /* create array */
    arrayNodep = new Json::Node();
    arrayNodep->initArray();

    /* wrap name around array, and plug it into the root node */
    nnodep = new Json::Node();
    nnodep->initNamed("backupEntries", arrayNodep);
    rootNodep->appendChild(nnodep);

    _entryLock.take();
    for(i=0;i<_maxUploaders;i++) {
        if ((ep = _uploadEntryp[i]) != NULL) {
            snodep = new Json::Node();
            snodep->initStruct();

            tnodep = new Json::Node();
            tnodep->initString(ep->_cloudRoot.c_str(), 1);
            nnodep = new Json::Node();
            nnodep->initNamed("cloudRoot", tnodep);
            snodep->appendChild(nnodep);

            tnodep = new Json::Node();
            tnodep->initString(ep->_fsRoot.c_str(), 1);
            nnodep = new Json::Node();
            nnodep->initNamed("fsRoot", tnodep);
            snodep->appendChild(nnodep);

            tnodep = new Json::Node();
            tnodep->initInt(ep->_lastFinishedTime);
            nnodep = new Json::Node();
            nnodep->initNamed("lastFinishedTime", tnodep);
            snodep->appendChild(nnodep);

            tnodep = new Json::Node();
            tnodep->initInt(ep->_enabled);
            nnodep = new Json::Node();
            nnodep->initNamed("enabled", tnodep);
            snodep->appendChild(nnodep);

            arrayNodep->appendChild(snodep);
        } /* entry exists */
    } /* for each uploader */
    _entryLock.release();

    /* at this point, we unmarshal the tree into the file */
    fileName = pathPrefix + "config.js";
    filep = fopen(fileName.c_str(), "w");
    if (!filep) {
        printf("can't open config.js\n");
        delete rootNodep;
        return -1;
    }

    /* create the string; we always have to close the file, but we only 
     * care about the code from close if the write succeded.
     */
    rootNodep->unparse(&result);
    code = fwrite(result.c_str(), result.length(), 1, filep);
    if (code == 1)
        code = 0;
    else
        code = -1;

    tcode = fclose(filep);
    if (code == 0)
        code = tcode;

    delete rootNodep;
    return code;
}

/* must be called with entry lock */
int32_t
UploadApp::deleteConfigEntry(int32_t ix)
{
    UploadEntry *ep;

    if (ix < 0 || ix >= _maxUploaders)
        return -1;

    if ((ep = _uploadEntryp[ix]) == NULL) {
        return -2;
    }

    ep->stop();
    _uploadEntryp[ix] = NULL;
    delete ep;
    return 0;
}

/* toggle the enabled flag */
int32_t
UploadApp::setEnabledConfig(int32_t ix)
{
    UploadEntry *ep;

    if (ix < 0 || ix >= _maxUploaders)
        return -1;

    _entryLock.take();
    if ((ep = _uploadEntryp[ix]) == NULL) {
        _entryLock.release();
        return -2;
    }

    ep->_enabled = !ep->_enabled;
    _entryLock.release();

    return 0;
}

int32_t
UploadApp::addConfigEntry( std::string cloudRoot,
                           std::string fsRoot,
                           uint32_t lastFinishedTime,
                           int enabled)
{
    UploadEntry *ep;
    uint32_t i;
    int32_t bestFreeIx;

    bestFreeIx = -1;

    _entryLock.take();
    for(i=0;i<_maxUploaders;i++) {
        ep = _uploadEntryp[i];
        if (ep == NULL) {
            if (bestFreeIx == -1)
                bestFreeIx = i;
            continue;
        }
        if (ep->_fsRoot == fsRoot) {
            /* update in place */
            ep->_cloudRoot = cloudRoot;
            ep->_lastFinishedTime = lastFinishedTime;
            _entryLock.release();
            return 0;
        }
    }

    /* here, no such entry */
    if (bestFreeIx == -1) {
        _entryLock.release();
        return -1;      /* no room for another backup entry */
    }
    ep = new UploadEntry();
    ep->_fsRoot = fsRoot;
    ep->_app = this;
    ep->_cloudRoot = cloudRoot;
    ep->_lastFinishedTime = lastFinishedTime;
    ep->_enabled = enabled;
    _uploadEntryp[bestFreeIx] = ep;

    _entryLock.release();
    return 0;
}

void
UploadReq::UploadStartAllScreenMethod()
{
    char tbuffer[16384];
    int32_t code=0;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    std::string loginHtml;
    UploadApp *uploadApp;
    SApiLoginCookie *loginCookiep;
    std::string authToken;
    std::string fileName;
    int startCode;
        
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app; visit home page first<p><a href=\"/\">"
               "Home screen</a></html>");
    }
    else {
       /* this does a get if it already exists */
        loginCookiep = SApiLogin::createLoginCookie(this);
        /* don't care about this, since we're using a global login cookie */
        // loginCookiep->enableSaveRestore();
        // uploadApp->setGlobalLoginCookie(loginCookiep);

        if (loginCookiep->getActive())
            authToken = loginCookiep->getActive()->getAuthToken();

        if (authToken.length() == 0) {
            strcpy(tbuffer, "Must login before doing backups");
        }
        else {
            startCode = uploadApp->startAll();
            if (startCode)
                strcpy(tbuffer, "Started backups");
            else
                strcpy(tbuffer, "Create some backup entries to start");
        }
    }

    setSendContentLength(strlen(tbuffer));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(tbuffer, strlen(tbuffer));
    outPipep->eof();
    
    requestDone();

}

void
UploadReq::UploadStartSelScreenMethod()
{
    char tbuffer[16384];
    int32_t code=0;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    std::string loginHtml;
    UploadApp *uploadApp;
    SApiLoginCookie *loginCookiep;
    std::string authToken;
    std::string fileName;
    int startCode;
        
    tbuffer[0] = 0;
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app; visit home page first<p><a href=\"/\">"
               "Home screen</a></html>");
    }
    else {
       /* this does a get if it already exists */
        loginCookiep = SApiLogin::createLoginCookie(this);
        /* don't care about this, since we're using a global login cookie */
        // loginCookiep->enableSaveRestore();
        // uploadApp->setGlobalLoginCookie(loginCookiep);

        if (loginCookiep->getActive())
            authToken = loginCookiep->getActive()->getAuthToken();

        if (authToken.length() == 0) {
            strcpy(tbuffer, "Must login before doing backups");
        }
        else {
            startCode = uploadApp->startSel();
            if (startCode)
                strcpy(tbuffer, "Started backups");
            else
                strcpy(tbuffer, "Select backup entries to start");
        }
    }

    setSendContentLength(strlen(tbuffer));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(tbuffer, strlen(tbuffer));
    outPipep->eof();
    
    requestDone();
}

void
UploadReq::UploadDeleteSelScreenMethod()
{
    char tbuffer[16384];
    int32_t code=0;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    std::string loginHtml;
    UploadApp *uploadApp;
    SApiLoginCookie *loginCookiep;
    std::string authToken;
    std::string fileName;
    int startCode;
        
    tbuffer[0] = 0;
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app; visit home page first<p><a href=\"/\">"
               "Home screen</a></html>");
    }
    else {
       /* this does a get if it already exists */
        loginCookiep = SApiLogin::createLoginCookie(this);
        /* don't care about this, since we're using a global login cookie */
        // loginCookiep->enableSaveRestore();
        // uploadApp->setGlobalLoginCookie(loginCookiep);

        if (loginCookiep->getActive())
            authToken = loginCookiep->getActive()->getAuthToken();

        if (authToken.length() == 0) {
            strcpy(tbuffer, "Must login before doing backups");
        }
        else {
            startCode = uploadApp->deleteSel();
            if (startCode)
                strcpy(tbuffer, "Deleted entries");
            else
                strcpy(tbuffer, "Select backup entries to delete");
        }
    }

    setSendContentLength(strlen(tbuffer));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(tbuffer, strlen(tbuffer));
    outPipep->eof();
    
    requestDone();
}

void
UploadReq::UploadPauseAllScreenMethod()
{
    char tbuffer[16384];
    int32_t code=0;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    std::string loginHtml;
    UploadApp *uploadApp;
    SApiLoginCookie *loginCookiep;
    std::string authToken;
    std::string fileName;
    int startCode;
        
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app; visit home page first<p><a href=\"/\">"
               "Home screen</a></html>");
    }
    else {
       /* this does a get if it already exists */
        loginCookiep = SApiLogin::createLoginCookie(this);
        /* don't care about this, since we're using a global login cookie */
        // loginCookiep->enableSaveRestore();
        // uploadApp->setGlobalLoginCookie(loginCookiep);

        if (loginCookiep->getActive())
            authToken = loginCookiep->getActive()->getAuthToken();

        if (authToken.length() == 0) {
            strcpy(tbuffer, "Must login before doing backups");
        }
        else {
            startCode = uploadApp->pauseAll();
            if (startCode)
                strcpy(tbuffer, "Paused backups");
            else
                strcpy(tbuffer, "No entries selected to pause");
        }
    }

    setSendContentLength(strlen(tbuffer));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(tbuffer, strlen(tbuffer));
    outPipep->eof();
    
    requestDone();

}

void
UploadReq::UploadPauseSelScreenMethod()
{
    char tbuffer[16384];
    int32_t code=0;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    std::string loginHtml;
    UploadApp *uploadApp;
    SApiLoginCookie *loginCookiep;
    std::string authToken;
    std::string fileName;
    int startCode;
        
    tbuffer[0] = 0;
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app; visit home page first<p><a href=\"/\">"
               "Home screen</a></html>");
    }
    else {
       /* this does a get if it already exists */
        loginCookiep = SApiLogin::createLoginCookie(this);
        /* don't care about this, since we're using a global login cookie */
        // loginCookiep->enableSaveRestore();
        // uploadApp->setGlobalLoginCookie(loginCookiep);

        if (loginCookiep->getActive())
            authToken = loginCookiep->getActive()->getAuthToken();

        if (authToken.length() == 0) {
            strcpy(tbuffer, "Must login before doing backups");
        }
        else {
            startCode = uploadApp->pauseSel();
            if (startCode)
                strcpy(tbuffer, "Paused selected backups");
            else
                strcpy(tbuffer, "Select backup entries to pause");
        }
    }

    setSendContentLength(strlen(tbuffer));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(tbuffer, strlen(tbuffer));
    outPipep->eof();
    
    requestDone();
}

void
UploadReq::UploadZapAuthTokenMethod()
{
    char tbuffer[16384];
    int32_t code=0;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    std::string loginHtml;
    UploadApp *uploadApp;
    SApiLoginCookie *loginCookiep;
    SApiLoginGeneric *tokenInfop;
    std::string authToken;
    std::string fileName;
        
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app; visit home page first<p><a href=\"/\">"
               "Home screen</a></html>");
    }
    else {
       /* this does a get if it already exists */
        loginCookiep = SApiLogin::createLoginCookie(this);
        /* don't care about this, since we're using a global login cookie */
        // loginCookiep->enableSaveRestore();
        // uploadApp->setGlobalLoginCookie(loginCookiep);

        tokenInfop = loginCookiep->getActive();
        if (tokenInfop) {
            tokenInfop->testDamaged(/* damage both */ 1);
            strcpy(tbuffer, "Tokens zapped");
        }
        else {
            strcpy(tbuffer, "No token holder found");
        }
    }

    setSendContentLength(strlen(tbuffer));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(tbuffer, strlen(tbuffer));
    outPipep->eof();
    
    requestDone();
}

void
UploadReq::UploadZapBothTokensMethod()
{
    char tbuffer[16384];
    int32_t code=0;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    std::string loginHtml;
    UploadApp *uploadApp;
    SApiLoginCookie *loginCookiep;
    SApiLoginGeneric *tokenInfop;
    std::string authToken;
    std::string fileName;
        
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app; visit home page first<p><a href=\"/\">"
               "Home screen</a></html>");
    }
    else {
       /* this does a get if it already exists */
        loginCookiep = SApiLogin::createLoginCookie(this);
        /* don't care about this, since we're using a global login cookie */
        // loginCookiep->enableSaveRestore();
        // uploadApp->setGlobalLoginCookie(loginCookiep);

        tokenInfop = loginCookiep->getActive();
        if (tokenInfop) {
            tokenInfop->testDamaged(/* damage both */ 1);
            strcpy(tbuffer, "Tokens zapped");
        }
        else {
            strcpy(tbuffer, "No token holder found");
        }
    }

    setSendContentLength(strlen(tbuffer));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(tbuffer, strlen(tbuffer));
    outPipep->eof();
    
    requestDone();
}

void
UploadReq::UploadStopAllScreenMethod()
{
    char tbuffer[16384];
    int32_t code=0;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    std::string loginHtml;
    UploadApp *uploadApp;
    SApiLoginCookie *loginCookiep;
    std::string authToken;
    std::string fileName;
    int startCode;
        
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app; visit home page first<p><a href=\"/\">"
               "Home screen</a></html>");
    }
    else {
       /* this does a get if it already exists */
        loginCookiep = SApiLogin::createLoginCookie(this);
        /* don't care about this, since we're using a global login cookie */
        // loginCookiep->enableSaveRestore();
        // uploadApp->setGlobalLoginCookie(loginCookiep);

        if (loginCookiep->getActive())
            authToken = loginCookiep->getActive()->getAuthToken();

        if (authToken.length() == 0) {
            strcpy(tbuffer, "Must login before doing backups");
        }
        else {
            startCode = uploadApp->stopAll();
            if (startCode)
                strcpy(tbuffer, "Stopped backups");
            else
                strcpy(tbuffer, "Create some backup entries to stop");
        }
    }

    setSendContentLength(strlen(tbuffer));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(tbuffer, strlen(tbuffer));
    outPipep->eof();
    
    requestDone();

}

void
UploadReq::UploadStopSelScreenMethod()
{
    char tbuffer[16384];
    int32_t code=0;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    std::string loginHtml;
    UploadApp *uploadApp;
    SApiLoginCookie *loginCookiep;
    std::string authToken;
    std::string fileName;
    int startCode;
        
    tbuffer[0] = 0;
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app; visit home page first<p><a href=\"/\">"
               "Home screen</a></html>");
    }
    else {
       /* this does a get if it already exists */
        loginCookiep = SApiLogin::createLoginCookie(this);
        /* don't care about this, since we're using a global login cookie */
        // loginCookiep->enableSaveRestore();
        // uploadApp->setGlobalLoginCookie(loginCookiep);

        if (loginCookiep->getActive())
            authToken = loginCookiep->getActive()->getAuthToken();

        if (authToken.length() == 0) {
            strcpy(tbuffer, "Must login before doing backups");
        }
        else {
            startCode = uploadApp->stopSel();
            if (startCode)
                strcpy(tbuffer, "Stopped backups");
            else
                strcpy(tbuffer, "Select backup entries to stop");
        }
    }

    setSendContentLength(strlen(tbuffer));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(tbuffer, strlen(tbuffer));
    outPipep->eof();
    
    requestDone();
}

void
UploadReq::UploadInfoScreenMethod()
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
    std::string fileName;

    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app running to stop; visit home page first<p>"
               "<a href=\"/\">Home screen</a></html>");
        obufferp = tbuffer;
    }
    else {
        fileName = uploadApp->_pathPrefix + "upload-info.html";
        code = getConn()->interpretFile(fileName.c_str(), &dict, &response);

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

void
UploadReq::UploadHelpPicsMethod()
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
    std::string fileName;

    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app running to stop; visit home page first<p>"
               "<a href=\"/\">Home screen</a></html>");
        obufferp = tbuffer;
    }
    else {
        fileName = uploadApp->_pathPrefix + "upload-pics.html";
        code = getConn()->interpretFile(fileName.c_str(), &dict, &response);

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

    if (uploadApp)
        uploadApp->stopAll();
}

void
UploadReq::UploadStatusDataMethod()
{
    char tbuffer[16384];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    UploadApp *uploadApp;
    std::string loginHtml;
    std::string filesString;
    std::string bytesString;
    std::string errorsString;
    std::string skippedString;
    std::string selectString;
    std::string enabledString;
    std::string finishedString;
    Uploader *uploaderp;
    UploadEntry *ep;
    uint32_t i;
        
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "[Initializing]");
        obufferp = tbuffer;
    }
    else {
        if (uploadApp->authProblems()) {
            response += "<a href=\"/helpPics\"><font color=\"red\">WARNING: saved login info appears to have expired; logout and login again; click for more info</font></a><p>";
        }
        response += "<table style=\"width:80%\">";
        response += "<tr><th>Select</th><th>Local dir</th><th>Cloud dir</th><th>"
            "Files copied</th><th>"
            "Files skipped</th><th>"
            "Bytes @ dest</th><th>"
            "Failures</th><th>"
            "State</th><th>"
            "Finished at</th><th>"
            "Enabled</th></tr>";
        for(i=0;i<UploadApp::_maxUploaders;i++) {
            ep = uploadApp->_uploadEntryp[i];
            if (!ep)
                continue;
            uploaderp = ep->_uploaderp;
            sprintf(tbuffer, "%ld files", (long) (uploaderp? uploaderp->_filesCopied : 0));
            filesString = std::string(tbuffer);
            sprintf(tbuffer, "%ld skipped", (long) (uploaderp? uploaderp->_filesSkipped : 0));
            skippedString = std::string(tbuffer);
            sprintf(tbuffer, "%ld MB", (long) (uploaderp? uploaderp->_bytesCopied/1000000: 0));
            bytesString = std::string(tbuffer);
            sprintf(tbuffer, "%ld failures", (long) (uploaderp? uploaderp->_fileCopiesFailed : 0));
            errorsString = std::string(tbuffer);
            sprintf(tbuffer, "<a href=\"/\" onclick=\"setEnabled(%d); return false\">%s</a>",
                    i, (ep->_enabled? "Enabled" : "Disabled"));
            enabledString = std::string(tbuffer);
            finishedString = UploadApp::getDate(ep->_lastFinishedTime);
            sprintf(tbuffer, "<a href=\"/\" onclick=\"setSelected(%d); return false\">%s</a>",
                    i, (ep->_selected? "<i class=\"material-icons\">check_box</i>" : "<i class=\"material-icons\">check_box_outline_blank</i>"));
            selectString = std::string(tbuffer);
            response += ("<tr><td>"+selectString+"</td><td>"+
                         ep->_fsRoot+"</td><td>" +
                         ep->_cloudRoot+ "</td><td>" +
                         filesString + "</td><td>" +
                         skippedString + "</td><td>" +
                         bytesString + "</td><td>" +
                         errorsString + "</td><td>" +
                         (uploaderp? uploaderp->getStatusString() : "Idle") + "</td><td>" +
                         finishedString + "</td><td>" +
                         enabledString + "</td></tr>");
        }
        response += "</table>\n";
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
UploadReq::UploadInfoDataMethod()
{
    char tbuffer[16384];
    char *obufferp;
    int32_t code;
    std::string response;
    SApi::Dict dict;
    Json json;
    CThreadPipe *outPipep = getOutgoingPipe();
    UploadApp *uploadApp;
    std::string loginHtml;
    std::string filesString;
    std::string bytesString;
    std::string errorsString;
    std::string skippedString;
    std::string editString;
    std::string enabledString;
    std::string finishedString;
    UploadErrorEntry *errorp;
    CfsStats *sp;
    CDispStats ds;
    XApiPoolStats poolStats;
    XApiPool *xapiPoolp;
        
    if ( (uploadApp = UploadApp::getGlobalApp()) == NULL ||
         uploadApp->_cfsp == NULL) {
        strcpy(tbuffer, "[Initializing]");
        obufferp = tbuffer;
    }
    else {
        sp = uploadApp->_cfsp->getStats();

        /* put out one line for each logged error */
        response = "<p><center>Error log</center><p>";
        response += "<table style=\"width:80%\">";
        response += "<tr><th>Operation</th><th>HTTP code</th><th>Short Error</th><th>Long Error</th></tr>";

        /* put in a loop over all bad errors */
        uploadApp->_lock.take();
        for( errorp = uploadApp->_errorEntries.head();
             errorp;
             errorp=errorp->_dqNextp) {
            sprintf( tbuffer, "<tr><td>%s</td><td>%d</td><td>%s</td><td>%s</td></tr>\n",
                     errorp->_op.c_str(),
                     errorp->_httpError,
                     errorp->_shortError.c_str(),
                     errorp->_longError.c_str());
            response += tbuffer;
        }

        uploadApp->_lock.release();

        response += "</table>\n";

        /* print out detailed stats */
        response += "<p><center>Call totals</center><p>";
        response += "<table style=\"width:50%\">\n";
        response += "<tr><th>Stat</th><th>Value</th></tr>\n";

        sprintf(tbuffer, "<tr><td>Total REST calls</td><td>%llu</td></tr>\n",
                (long long) sp->_totalCalls);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>FillAttr calls</td><td>%llu</td></tr>\n",
                (long long) sp->_fillAttrCalls);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>GetAttr calls</td><td>%llu</td></tr>\n",
                (long long) sp->_getAttrCalls);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>GetPath calls</td><td>%llu</td></tr>\n",
                (long long) sp->_getPathCalls);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>Lookup calls</td><td>%llu</td></tr>\n",
                (long long) sp->_lookupCalls);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>SendSmallFiles</td><td>%llu</td></tr>\n",
                (long long) sp->_sendSmallFilesCalls);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>SendLargeFiles</td><td>%llu</td></tr>\n",
                (long long) sp->_sendLargeFilesCalls);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>SendDataCalls</td><td>%llu</td></tr>\n",
                (long long) sp->_sendDataCalls);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>Mkdir calls</td><td>%llu</td></tr>\n",
                (long long) sp->_mkdirCalls);
        response += tbuffer;

        response += "</table>\n";

        response += "<p><center>Error stats</center><p>";
        response += "<table style=\"width:50%\">\n";
        response += "<tr><th>Stat</th><th>Value</th></tr>\n";

        sprintf(tbuffer, "<tr><td>Auth required</td><td>%llu</td></tr>\n",
                (long long) sp->_authRequired);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>Overloaded 5XX</td><td>%llu</td></tr>\n",
                (long long) sp->_overloaded5xx);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>Busy 429</td><td>%llu</td></tr>\n",
                (long long) sp->_busy429);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>Stalled 409</td><td>%llu</td></tr>\n",
                (long long) sp->_busy409);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>Mkdir random 400</td><td>%llu</td></tr>\n",
                (long long) sp->_bad400);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>Unknown HTTP fail</td><td>%llu</td></tr>\n",
                (long long) sp->_mysteryErrors);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>XApi fail</td><td>%llu</td></tr>\n",
                (long long) sp->_xapiErrors);
        response += tbuffer;

        response += "</table>\n";

        if (uploadApp->_cdisp)
            uploadApp->_cdisp->getStats(&ds);
        xapiPoolp = uploadApp->_cfsp->getPool();
        if (xapiPoolp)
            xapiPoolp->getStats(&poolStats);

        response += "<p><center>Dispatcher stats</center><p>";
        response += "<table style=\"width:50%\">\n";
        response += "<tr><th>Stat</th><th>Value</th></tr>\n";

        sprintf(tbuffer, "<tr><td>Active Helpers</td><td>%llu</td></tr>\n",
                (long long) ds._activeHelpers);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>Available Helpers</td><td>%llu</td></tr>\n",
                (long long) ds._availableHelpers);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>Active Tasks</td><td>%llu</td></tr>\n",
                (long long) ds._activeTasks);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>Pending Tasks</td><td>%llu</td></tr>\n",
                (long long) ds._pendingTasks);
        response += tbuffer;
        sprintf(tbuffer, "<tr><td>Avg/Max Busy time / # Healthy</td><td>%llu ms / %llu ms / %llu healthy / %llu total active</td></tr>\n",
                (long long) poolStats._averageMs,
                (long long) poolStats._longestMs,
                (long long) poolStats._healthyCount,
                (long long) poolStats._activeCount);
        response += tbuffer;

        response += "</table>\n";

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
UploadReq::UploadLoadConfigMethod()
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
        
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app running to load config; visit home page first<p>"
               "<a href=\"/\">Home screen</a></html>");
        obufferp = tbuffer;
    }
    else {
        strcpy(tbuffer, "Data From Config");
        obufferp = tbuffer;
    }

    setSendContentLength(strlen(obufferp));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    outPipep->eof();
    
    requestDone();
}

void
UploadReq::UploadCreateConfigMethod()
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
    dqueue<Rst::Hdr> *urlPairsp;
    Rst::Hdr *hdrp;
    std::string filePath;
    std::string cloudPath;
    std::string fileName;
    int noCreate = 0;
        
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app running to load config; visit home page first<p>"
               "<a href=\"/\">Home screen</a></html>");
        obufferp = tbuffer;
        noCreate = 1;
    }
    else {
        fileName = uploadApp->_pathPrefix + "upload-add.html";
        code = getConn()->interpretFile(fileName.c_str(), &dict, &response);

        if (code != 0) {
            sprintf(tbuffer, "Oops, interpretFile code is %d\n", code);
            obufferp = tbuffer;
        }
        else {
            obufferp = const_cast<char *>(response.c_str());
        }
        
    }

    urlPairsp = getRstReq()->getUrlPairs();
    for(hdrp = urlPairsp->head(); hdrp; hdrp=hdrp->_dqNextp) {
        if (hdrp->_key == "fspath")
            filePath = Rst::urlDecode(&hdrp->_value);
        else if (hdrp->_key == "cloudpath")
            cloudPath = Rst::urlDecode(&hdrp->_value);
    }

    if (filePath.length() == 0 || cloudPath.length() == 0) {
        strcpy(tbuffer, "One of file or cloud path is empty");
        noCreate = 1;
    }

    if (!noCreate) {
        uploadApp->addConfigEntry(cloudPath, filePath, 0, 1);
        uploadApp->writeConfig(uploadApp->_libPath);
    }

    setSendContentLength(strlen(obufferp));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    outPipep->eof();
    
    requestDone();
}

void
UploadReq::UploadDeleteConfigMethod()
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
    dqueue<Rst::Hdr> *urlPairsp;
    Rst::Hdr *hdrp;
    int ix;
        
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app running to load config; visit home page first<p>"
               "<a href=\"/\">Home screen</a></html>");
    }
    else {
        strcpy(tbuffer, "DONE");
    }
    obufferp = tbuffer;

    urlPairsp = getRstReq()->getUrlPairs();
    ix = -1;
    for(hdrp = urlPairsp->head(); hdrp; hdrp=hdrp->_dqNextp) {
        if (hdrp->_key == "ix") {
            ix = atoi(hdrp->_value.c_str());
        }
    }

    if (ix >= 0) {
        uploadApp->deleteConfigEntry(ix);
        uploadApp->writeConfig(uploadApp->_libPath);
    }

    setSendContentLength(strlen(obufferp));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    outPipep->eof();
    
    requestDone();
}

void
UploadReq::UploadBackupIntervalMethod()
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
    std::string fileName;
    dqueue<Rst::Hdr> *urlPairsp;
    Rst::Hdr *hdrp;
    int32_t secs;
        
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "No app running to pause; visit home page first (1)<p>"
               "<a href=\"/\">Home screen</a>");
        obufferp = tbuffer;
    }
    else {
        fileName = uploadApp->_pathPrefix + "upload-backup-int.html";
        code = getConn()->interpretFile(fileName.c_str(), &dict, &response);

        if (code != 0) {
            sprintf(tbuffer, "Oops, interpretFile code is %d\n", code);
            obufferp = tbuffer;
        }
        else {
            obufferp = const_cast<char *>(response.c_str());
        }
    }

    urlPairsp = getRstReq()->getUrlPairs();
    secs = -1;
    for(hdrp = urlPairsp->head(); hdrp; hdrp=hdrp->_dqNextp) {
        if (hdrp->_key == "interval") {
            secs = UploadApp::parseInterval(hdrp->_value);
        }
    }

    if (secs > 0) {
        uploadApp->_backupInterval = secs;
    }

    setSendContentLength(strlen(obufferp));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    outPipep->eof();
    
    requestDone();

    uploadApp->writeConfig(uploadApp->_libPath);
}

void
UploadReq::UploadSetEnabledConfigMethod()
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
    dqueue<Rst::Hdr> *urlPairsp;
    Rst::Hdr *hdrp;
    int ix;
        
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app running to load config; visit home page first<p>"
               "<a href=\"/\">Home screen</a></html>");
    }
    else {
        strcpy(tbuffer, "DONE");
    }
    obufferp = tbuffer;

    urlPairsp = getRstReq()->getUrlPairs();
    ix = -1;
    for(hdrp = urlPairsp->head(); hdrp; hdrp=hdrp->_dqNextp) {
        if (hdrp->_key == "ix") {
            ix = atoi(hdrp->_value.c_str());
        }
    }

    if (ix >= 0) {
        uploadApp->setEnabledConfig(ix);
        uploadApp->writeConfig(uploadApp->_libPath);
    }

    setSendContentLength(strlen(obufferp));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    outPipep->eof();
    
    requestDone();
}

void
UploadReq::UploadSetSelectedConfigMethod()
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
    dqueue<Rst::Hdr> *urlPairsp;
    Rst::Hdr *hdrp;
    int ix;
    UploadEntry *ep;
        
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app running to load config; visit home page first<p>"
               "<a href=\"/\">Home screen</a></html>");
    }
    else {
        strcpy(tbuffer, "DONE");
    }
    obufferp = tbuffer;

    urlPairsp = getRstReq()->getUrlPairs();
    ix = -1;
    for(hdrp = urlPairsp->head(); hdrp; hdrp=hdrp->_dqNextp) {
        if (hdrp->_key == "ix") {
            ix = atoi(hdrp->_value.c_str());
        }
    }

    if (ix >= 0 && ix < UploadApp::_maxUploaders) {
        ep = uploadApp->_uploadEntryp[ix];
        if (ep)
            ep->_selected = !ep->_selected;
    }

    setSendContentLength(strlen(obufferp));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    outPipep->eof();
    
    requestDone();
}

int32_t
UploadApp::init(SApi *sapip, int single)
{
    CThreadHandle *hp;

    /* setup login URL listeners */
    SApiLogin::initSApi(sapip);

    /* register the home screen as well */
    sapip->registerUrl("/", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadHomeScreenMethod);

    sapip->registerUrl("/startAll", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadStartAllScreenMethod);
    sapip->registerUrl("/startSel", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadStartSelScreenMethod);

    sapip->registerUrl("/pauseAll", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadPauseAllScreenMethod);
    sapip->registerUrl("/pauseSel", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadPauseSelScreenMethod);

    sapip->registerUrl("/stopAll", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadStopAllScreenMethod);
    sapip->registerUrl("/stopSel", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadStopSelScreenMethod);

#if 0
    sapip->registerUrl("/stopBackups", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadStopScreenMethod);
    sapip->registerUrl("/pauseBackups", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadPauseScreenMethod);
#endif
    sapip->registerUrl("/statusData", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadStatusDataMethod);
    sapip->registerUrl("/infoData", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadInfoDataMethod);
    sapip->registerUrl("/loadConfig", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadLoadConfigMethod);
    sapip->registerUrl("/deleteItem", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadDeleteConfigMethod);
    sapip->registerUrl("/setEnabled", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadSetEnabledConfigMethod);
    sapip->registerUrl("/setSelected", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadSetSelectedConfigMethod);
    sapip->registerUrl("/createEntry", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadCreateConfigMethod);
    sapip->registerUrl("/backupInterval", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadBackupIntervalMethod);
    sapip->registerUrl("/info", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadInfoScreenMethod);
    sapip->registerUrl("/helpPics", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadHelpPicsMethod);
    sapip->registerUrl("/zapAuthToken", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadZapAuthTokenMethod);
    sapip->registerUrl("/zapBothTokens", 
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadZapBothTokensMethod);
    sapip->registerUrl("/deleteSel",
                       (SApi::RequestFactory *) &UploadReq::factory,
                       (SApi::StartMethod) &UploadReq::UploadDeleteSelScreenMethod);

    _cdisp = new CDisp();
    _cdisp->init(single? 1 : 24);   /*24*/

    hp = new CThreadHandle();
    hp->init((CThread::StartMethod) &UploadApp::schedule, this, NULL);

    return 0;
}

void
UploadApp::schedule(void *cxp)
{
    uint32_t i;
    UploadEntry *ep;

    while(1) {
        sleep(60);
        printf("SCANNING\n");
        _entryLock.take();
        for(i=0;i<_maxUploaders;i++) {
            ep = _uploadEntryp[i];

            if (!ep || !ep->_enabled || ep->_manual)
                continue;

            /* here we have a valid entry in STOPPED state; if it has been
             * long enough since it last successfully ran, start it.
             */
            if (ep->_lastFinishedTime + _backupInterval < osp_time_sec()) {
                startEntry(ep);
            }
        }
        _entryLock.release();
        printf("SLEEPING\n");
    }
}

int32_t
UploadApp::initLoop(SApi *sapip, int single)
{
    init(sapip, single);

    while(1) {
        sleep(1);
    }
}

/* static */ void
UploadApp::stateChanged(void *contextp)
{
    UploadEntry *ep = (UploadEntry *) contextp;
    UploadApp *app = ep->_app;

    ep->_lastFinishedTime = osp_time_sec();
    app->writeConfig(app->_libPath);
}

/* called with _entryLock held */
void
UploadApp::startEntry(UploadEntry *ep) {
    Uploader *uploaderp;
    Uploader::Status upStatus;

    if (!ep || !_loginCookiep || !_loginCookiep->_loginMSp)
        return;
    uploaderp = ep->_uploaderp;

    if (uploaderp) {
        upStatus = uploaderp->getStatus();
        if (upStatus == Uploader::PAUSED) {
            uploaderp->resume();
            return;
        }
        else if (upStatus == Uploader::RUNNING) {
            return;
        }
        else if (upStatus == Uploader::STOPPED) {
            delete uploaderp;
            ep->_uploaderp = NULL;
            uploaderp = NULL;
        }
    }
            
    if (!uploaderp) {
        if (!_cfsp) {
            _cfsp = new CfsMs(_loginCookiep, _pathPrefix);
            _cfsp->setLog(&_log);
        }

        /* create the uploader */
        ep->_uploaderp = uploaderp = new Uploader();
        uploaderp->init(ep->_cloudRoot,
                        ep->_fsRoot,
                        _cdisp,
                        _cfsp,
                        &UploadApp::stateChanged,
                        ep);
        uploaderp->start();
    }
}

UploadEntry::~UploadEntry() {
    osp_assert(!_uploaderp || _uploaderp->isIdle());
    delete _uploaderp;
}

/* called with entryLock held */
void
UploadEntry::stop() {
    if (!_uploaderp)
        return;
    _uploaderp->stop();
    while(!_uploaderp->isIdle()) {
        sleep(1);
    }
}

void
UploadApp::resetAuthProblems()
{
    uint32_t i;
    Uploader *up;

    for(i=0;i<_maxUploaders;i++) {
        up = (_uploadEntryp[i] ? _uploadEntryp[i]->_uploaderp : NULL);
        if (up && up->_status == Uploader::STOPPED && up->_stopReason == Uploader::REASON_AUTH) {
            up->_stopReason = Uploader::REASON_DONE;
        }
    }
}

int
UploadApp::authProblems()
{
    uint32_t i;
    int rcode = 0;
    Uploader *up;

    for(i=0;i<_maxUploaders;i++) {
        up = (_uploadEntryp[i] ? _uploadEntryp[i]->_uploaderp : NULL);
        if (up && up->_status == Uploader::STOPPED && up->_stopReason == Uploader::REASON_AUTH) {
            rcode = 1;
            break;
        }
    }

    return rcode;
}

void
UploadApp::Log::logError( CfsLog::OpType type,
                          int32_t httpError,
                          std::string errorString,
                          std::string longErrorString) {
    UploadApp *up = _uploadApp;
    UploadErrorEntry *ep;
    UploadErrorEntry *lastEp;

    printf("Operation %d httpCode=%d '%s'\n",
           type, httpError, errorString.c_str());
 
    up->_lock.take();

    lastEp = up->_errorEntries.tail();
    if (lastEp && (lastEp->_httpError == httpError &&
                   lastEp->_shortError == errorString &&
                   lastEp->_longError == longErrorString)) {
        /* just a duplicate */
        up->_lock.release();
        return;
    }

    ep = new UploadErrorEntry();
    ep->_op = CfsLog::opToString(type);
    ep->_httpError = httpError;
    ep->_shortError = errorString;
    ep->_longError = longErrorString;

    up->_errorEntries.append(ep);

    /* and don't let us queue too many; just keep the most recent maxErrorEntries */
    while(up->_errorEntries.count() > UploadApp::_maxErrorEntries) {
        ep = up->_errorEntries.pop();
        delete ep;
    }

    up->_lock.release();
}
