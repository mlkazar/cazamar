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

    printf("cfstest: tests start login=%p\n", _loginMSp);
    _cfsp = new CfsMs(_loginMSp);
    _cfsp->root((Cnode **) &rootp, NULL);
    rootp->getAttr(&attrs, NULL);

    code = _cfsp->namei(_cloudRoot, (Cnode **) &testDirp, NULL);
    if (code != 0) {
        code = _cfsp->mkpath(_cloudRoot, (Cnode **) &testDirp, NULL);
        if (code != 0) {
            printf("mkpath failed code=%d\n", code);
            return;
        }
    }

    /* lookup succeeded */
    code = testDirp->getAttr(&dirAttrs, NULL);
    if (code != 0) {
        printf("dir getattr failed code=%d\n", code);
    }

    /* reset stats */
    _filesCopied = 0;
    _bytesCopied = 0;
    _filesSkipped = 0;
    _fileCopiesFailed = 0;

    /* copy the pictures directory to a subdir of testdir */
    _disp = new CDisp();
    printf("Created new cdisp at %p\n", _disp);
    _disp->setCompletionProc(&Uploader::done, this);
    _disp->init(24);

    printf("Starting copy\n");
    taskp = new WalkTask();
    taskp->initWithPath(_fsRoot);
    taskp->setCallback(&Uploader::mainCallback, this);
    _disp->queueTask(taskp);

    _status = RUNNING;

    printf("uploader: started\n");
}

void
Uploader::stop()
{
    /* null this out so it doesn't look like a successful completion */
    _disp->stop();
    _status = STOPPED;
    return;
}

void
Uploader::pause()
{
    _disp->pause();
    _status = PAUSED;
    return;
}

void
Uploader::resume()
{
    /* call the dispatcher to resume execution of tasks */
    _disp->resume();
    _status = RUNNING;
    return;
}

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

    if (up->_verbose)
        printf("In walkcallback %s\n", pathp->c_str());
    cloudName = up->_cloudRoot + relativeName;

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
    }

    if ((statp->st_mode & S_IFMT) == S_IFDIR) {
        /* do a mkdir */
        code = cfsp->mkdir(cloudName, &cnodep, NULL);
        if (code == 0) {
            cnodep->release();
            up->_filesCopied++;         /* dir created */
        }
        else {
            if (code != 17)
                printf("Upload: mkdir failed with code=%d path=%s\n",
                       (int) code, pathp->c_str());
            up->_filesSkipped++;        /* dir exists */
        }
        if (up->_verbose)
            printf("mkdir of %p done, code=%d\n", cloudName.c_str(), code);
    }
    else if ((statp->st_mode & S_IFMT) == S_IFREG) {
        code = dataFile.open(pathp->c_str());
        if (code != 0) {
            up->_fileCopiesFailed++;
            printf("Upload: failed to open file %s\n", pathp->c_str());
            return code;
        }

        /* send the file, updating bytesCopied on the fly */
        code = cfsp->sendFile(cloudName, &dataFile, &up->_bytesCopied, NULL);

        if (up->_verbose)
            printf("sendfile path=%s test done, code=%d\n", cloudName.c_str(), code);
        if (code) {
            printf("Upload: sendfile path=%s failed code=%d\n", pathp->c_str(), code);
            up->_fileCopiesFailed++;
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
    std::string pathPrefix;
    std::string fileName;
        
    uploadApp = UploadApp::getGlobalApp();

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
    loginHtml += "<p><a href=\"/startBackups\">Start/resume backup</a>";
    loginHtml += "<p><a href=\"/pauseBackups\">Pause backup</a>";
    loginHtml += "<p><a href=\"/stopBackups\">Stop backup</a>";
    sprintf(tbuffer, "<p><a href=\"/\" onclick=\"getBackupInt(); return false\">Set backup interval (%s)</a>", UploadApp::showInterval(uploadApp->_backupInterval).c_str());
    loginHtml += tbuffer;
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

    fileName = pathPrefix + "config.js";
    filep = fopen(fileName.c_str(), "r");
    if (!filep)
        return;

    /* parseJsonFile closes filep on both success and error returns */
    code = json.parseJsonFile(filep, &rootNodep);
    if (code != 0) {
        return;
    }

    _backupInterval = 3600;

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

int32_t
UploadApp::deleteConfigEntry(int32_t ix)
{
    UploadEntry *ep;

    if (ix < 0 || ix >= _maxUploaders)
        return -1;

    if ((ep = _uploadEntryp[ix]) == NULL)
        return -2;

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

    if ((ep = _uploadEntryp[ix]) == NULL)
        return -2;

    ep->_enabled = !ep->_enabled;

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
            return 0;
        }
    }

    /* here, no such entry */
    if (bestFreeIx == -1)
        return -1;      /* no room for another backup entry */
    ep = new UploadEntry();
    ep->_fsRoot = fsRoot;
    ep->_app = this;
    ep->_cloudRoot = cloudRoot;
    ep->_lastFinishedTime = lastFinishedTime;
    ep->_enabled = enabled;
    _uploadEntryp[bestFreeIx] = ep;
    return 0;
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
    std::string fileName;
        
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app; visit home page first<p><a href=\"/\">Home screen</a></html>");
        obufferp = tbuffer;
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
            strcpy(tbuffer, "<html>Must login before doing backups<p><a href=\"/\">Home screen</a></html>");
            obufferp = tbuffer;
            code = -1;
        }
        else {
            fileName = uploadApp->_pathPrefix + "upload-start.html";
            code = getConn()->interpretFile(fileName.c_str(), &dict, &response);
            if (code != 0) {
                sprintf(tbuffer, "Oops, interpretFile code is %d\n", code);
                obufferp = tbuffer;
            }
            else {
                obufferp = const_cast<char *>(response.c_str());
                loggedIn = 1;
            }
        }
    }

    setSendContentLength(strlen(obufferp));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    outPipep->eof();
    
    requestDone();

    if (loggedIn) {
        uploadApp->start();
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
    std::string fileName;

    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "<html>No app running to stop; visit home page first<p>"
               "<a href=\"/\">Home screen</a></html>");
        obufferp = tbuffer;
    }
    else {
        fileName = uploadApp->_pathPrefix + "upload-stop.html";
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
        uploadApp->stop();
}

void
UploadPauseScreen::startMethod()
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
        strcpy(tbuffer, "No app running to pause; visit home page first (1)<p>"
               "<a href=\"/\">Home screen</a>");
        obufferp = tbuffer;
    }
    else {
        fileName = uploadApp->_pathPrefix + "upload-pause.html";
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
        uploadApp->pause();
}

void
UploadStatusData::startMethod()
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
    Uploader *uploaderp;
    UploadEntry *ep;
    uint32_t i;
        
    if ((uploadApp = UploadApp::getGlobalApp()) == NULL) {
        strcpy(tbuffer, "[Initializing]");
        obufferp = tbuffer;
    }
    else {
        response = "<table style=\"width:80%\">";
        response += "<tr><th>Local dir</th><th>Cloud dir</th><th>Files copied</th><th>"
            "Files skipped</th><th>"
            "Bytes @ dest</th><th>"
            "Failures</th><th>"
            "State</th><th>"
            "Finished at</th><th>"
            "Enabled</th><th>"
            "Edit</th></tr>\n";
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
            sprintf(tbuffer, "<a href=\"/\" onclick=\"delConfirm(%d); return false\">Delete</a>", i);
            editString = std::string(tbuffer);
            response += ("<tr><td>"+ep->_fsRoot+"</td><td>" +
                         ep->_cloudRoot+ "</td><td>" +
                         filesString + "</td><td>" +
                         skippedString + "</td><td>" +
                         bytesString + "</td><td>" +
                         errorsString + "</td><td>" +
                         (uploaderp? uploaderp->getStatusString() : "Idle") + "</td><td>" +
                         finishedString + "</td><td>" +
                         enabledString + "</td><td>" +
                         editString + "</td></tr>\n");
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
UploadLoadConfig::startMethod()
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
UploadCreateConfig::startMethod()
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
        uploadApp->writeConfig(uploadApp->_pathPrefix);
    }

    setSendContentLength(strlen(obufferp));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    outPipep->eof();
    
    requestDone();
}

void
UploadDeleteConfig::startMethod()
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
        uploadApp->writeConfig(uploadApp->_pathPrefix);
    }

    setSendContentLength(strlen(obufferp));

    /* reverse the pipe -- must know length, or have set content length to -1 by now */
    inputReceived();
    
    code = outPipep->write(obufferp, strlen(obufferp));
    outPipep->eof();
    
    requestDone();
}

void
UploadBackupInterval::startMethod()
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

    uploadApp->writeConfig(uploadApp->_pathPrefix);
}

void
UploadSetEnabledConfig::startMethod()
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
        uploadApp->writeConfig(uploadApp->_pathPrefix);
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

SApi::ServerReq *
UploadPauseScreen::factory(std::string *opcodep, SApi *sapip)
{
    UploadPauseScreen *reqp;
    reqp = new UploadPauseScreen(sapip);
    return reqp;
}

SApi::ServerReq *
UploadStatusData::factory(std::string *opcodep, SApi *sapip)
{
    UploadStatusData *reqp;
    reqp = new UploadStatusData(sapip);
    return reqp;
}

SApi::ServerReq *
UploadLoadConfig::factory(std::string *opcodep, SApi *sapip)
{
    UploadLoadConfig *reqp;
    reqp = new UploadLoadConfig(sapip);
    return reqp;
}

SApi::ServerReq *
UploadDeleteConfig::factory(std::string *opcodep, SApi *sapip)
{
    UploadDeleteConfig *reqp;
    reqp = new UploadDeleteConfig(sapip);
    return reqp;
}

SApi::ServerReq *
UploadSetEnabledConfig::factory(std::string *opcodep, SApi *sapip)
{
    UploadSetEnabledConfig *reqp;
    reqp = new UploadSetEnabledConfig(sapip);
    return reqp;
}

SApi::ServerReq *
UploadBackupInterval::factory(std::string *opcodep, SApi *sapip)
{
    UploadBackupInterval *reqp;
    reqp = new UploadBackupInterval(sapip);
    return reqp;
}

SApi::ServerReq *
UploadCreateConfig::factory(std::string *opcodep, SApi *sapip)
{
    UploadCreateConfig *reqp;
    reqp = new UploadCreateConfig(sapip);
    return reqp;
}

int32_t
UploadApp::init(SApi *sapip)
{
    CThreadHandle *hp;

    /* setup login URL listeners */
    SApiLogin::initSApi(sapip);

    /* register the home screen as well */
    sapip->registerUrl("/", &UploadHomeScreen::factory);
    sapip->registerUrl("/startBackups", &UploadStartScreen::factory);
    sapip->registerUrl("/stopBackups", &UploadStopScreen::factory);
    sapip->registerUrl("/pauseBackups", &UploadPauseScreen::factory);
    sapip->registerUrl("/statusData", &UploadStatusData::factory);
    sapip->registerUrl("/loadConfig", &UploadLoadConfig::factory);// do we need this?
    sapip->registerUrl("/deleteItem", &UploadDeleteConfig::factory);
    sapip->registerUrl("/setEnabled", &UploadSetEnabledConfig::factory);
    sapip->registerUrl("/createEntry", &UploadCreateConfig::factory);
    sapip->registerUrl("/backupInterval", &UploadBackupInterval::factory);

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
        for(i=0;i<_maxUploaders;i++) {
            ep = _uploadEntryp[i];

            if (!ep || !ep->_enabled)
                continue;

            /* here we have a valid entry in STOPPED state; if it has been
             * long enough since it last successfully ran, start it.
             */
            if (ep->_lastFinishedTime + _backupInterval < osp_time_sec()) {
                startEntry(ep);
            }
        }
        printf("SLEEPING\n");
    }
}

int32_t
UploadApp::initLoop(SApi *sapip)
{
    init(sapip);

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
    app->writeConfig(app->_pathPrefix);
}

void
UploadApp::startEntry(UploadEntry *ep) {
    Uploader *uploaderp;
    Uploader::Status upStatus;

    if (!ep || !ep->_enabled || !_loginCookiep)
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
        /* create the uploader */
        ep->_uploaderp = uploaderp = new Uploader();
        uploaderp->init(ep->_cloudRoot,
                        ep->_fsRoot,
                        _loginCookiep->_loginMSp,
                        &UploadApp::stateChanged,
                        ep);
        uploaderp->start();
    }
}

UploadEntry::~UploadEntry() {
    osp_assert(!_uploaderp || _uploaderp->isIdle());
    delete _uploaderp;
}

void
UploadEntry::stop() {
    if (!_uploaderp)
        return;
    _uploaderp->stop();
    while(!_uploaderp->isIdle()) {
        sleep(1);
    }
}
