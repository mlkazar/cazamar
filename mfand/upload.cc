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

    _status = RUNNING;

    printf("uploader: started\n");
}

void
Uploader::stop()
{
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
                up->_filesSkipped++;
                return 0;
            }
        }
    }

    up->_filesCopied++;
    up->_bytesCopied += statp->st_size;

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
            up->_fileCopiesFailed++;
            printf("Failed to open file %s\n", pathp->c_str());
            return code;
        }
        code = cfsp->sendFile(cloudName, &dataFile, NULL);
        printf("sendfile path=%s test done, code=%d\n", cloudName.c_str(), code);
        if (code)
            up->_fileCopiesFailed++;

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
    std::string pathPrefix;
        
    if ((uploadApp = (UploadApp *) getCookieKey("main")) == NULL) {
        pathPrefix = getSApi()->getPathPrefix();
        uploadApp = new UploadApp(pathPrefix);
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
    loginHtml += "<p><a href=\"/startBackups\">Start/resume backup</a>";
    loginHtml += "<p><a href=\"/pauseBackups\">Pause backup</a>";
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

    fileName = pathPrefix + "config.js";
    filep = fopen(fileName.c_str(), "r");
    if (!filep)
        return;

    /* parseJsonFile closes filep on both success and error returns */
    code = json.parseJsonFile(filep, &rootNodep);
    if (code != 0) {
        return;
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

            /* now add the entry */
            addConfigEntry(cloudRoot, fsRoot, lastFinishedTime);
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

int32_t
UploadApp::addConfigEntry(std::string cloudRoot, std::string fsRoot, uint64_t lastFinishedTime)
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
        
    if ((uploadApp = (UploadApp *) getCookieKey("main")) == NULL) {
        strcpy(tbuffer, "<html>No app; visit home page first<p><a href=\"/\">Home screen</a></html>");
        obufferp = tbuffer;
    }
    else {
       /* this does a get if it already exists */
        loginCookiep = SApiLogin::createLoginCookie(this);
        loginCookiep->enableSaveRestore();
        uploadApp->_loginCookiep = loginCookiep;

        if (loginCookiep->getActive())
            authToken = loginCookiep->getActive()->getAuthToken();

        if (authToken.length() == 0) {
            strcpy(tbuffer, "<html>Must login before doing backups<p><a href=\"/\">Home screen</a></html>");
            obufferp = tbuffer;
            code = -1;
        }
        else {
            code = getConn()->interpretFile((char *) "upload-start.html", &dict, &response);
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
        
    if ((uploadApp = (UploadApp *) getCookieKey("main")) == NULL) {
        strcpy(tbuffer, "<html>No app running to stop; visit home page first<p>"
               "<a href=\"/\">Home screen</a></html>");
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
        
    if ((uploadApp = (UploadApp *) getCookieKey("main")) == NULL) {
        strcpy(tbuffer, "No app running to pause; visit home page first (1)<p>"
               "<a href=\"/\">Home screen</a>");
        obufferp = tbuffer;
    }
    else {
        code = getConn()->interpretFile((char *) "upload-pause.html", &dict, &response);

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
    Uploader *uploaderp;
    UploadEntry *ep;
    uint32_t i;
        
    if ((uploadApp = (UploadApp *) getCookieKey("main")) == NULL) {
        strcpy(tbuffer, "[Initializing]");
        obufferp = tbuffer;
    }
    else {
        response = "<table style=\"width:80%\">";
        response += "<tr><th>Local dir</th><th>Cloud dir</th><th>Files copied</th><th>"
            "Bytes copied</th><th>"
            "Files skipped</th><th>"
            "Failures</th><th>"
            "State</th><th>"
            "Edit</th></tr>\n";
        for(i=0;i<UploadApp::_maxUploaders;i++) {
            ep = uploadApp->_uploadEntryp[i];
            if (!ep)
                continue;
            uploaderp = ep->_uploaderp;
            sprintf(tbuffer, "%ld files", (long) (uploaderp? uploaderp->_filesCopied : 0));
            filesString = std::string(tbuffer);
            sprintf(tbuffer, "%ld bytes", (long) (uploaderp? uploaderp->_bytesCopied: 0));
            bytesString = std::string(tbuffer);
            sprintf(tbuffer, "%ld skipped", (long) (uploaderp? uploaderp->_filesSkipped : 0));
            skippedString = std::string(tbuffer);
            sprintf(tbuffer, "%ld failures", (long) (uploaderp? uploaderp->_fileCopiesFailed : 0));
            errorsString = std::string(tbuffer);
            sprintf(tbuffer, "<a href=\"/\" onclick=\"delConfirm(%d); return false\">Delete</a>", i);
            editString = std::string(tbuffer);
            response += ("<tr><td>"+ep->_fsRoot+"</td><td>" +
                         ep->_cloudRoot+ "</td><td>" +
                         filesString + "</td><td>" +
                         bytesString + "</td><td>" +
                         skippedString + "</td><td>" +
                         errorsString + "</td><td>" +
                         (uploaderp? uploaderp->getStatusString() : "Idle") + "</td><td>" +
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
        
    if ((uploadApp = (UploadApp *) getCookieKey("main")) == NULL) {
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
    int noCreate = 0;
        
    if ((uploadApp = (UploadApp *) getCookieKey("main")) == NULL) {
        strcpy(tbuffer, "<html>No app running to load config; visit home page first<p>"
               "<a href=\"/\">Home screen</a></html>");
        obufferp = tbuffer;
        noCreate = 1;
    }
    else {
        code = getConn()->interpretFile((char *) "upload-add.html", &dict, &response);

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
        uploadApp->addConfigEntry(cloudPath, filePath, 0);
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
        
    if ((uploadApp = (UploadApp *) getCookieKey("main")) == NULL) {
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
UploadCreateConfig::factory(std::string *opcodep, SApi *sapip)
{
    UploadCreateConfig *reqp;
    reqp = new UploadCreateConfig(sapip);
    return reqp;
}

int32_t
UploadApp::initLoop(SApi *sapip)
{
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
    sapip->registerUrl("/createEntry", &UploadCreateConfig::factory);

    while(1) {
        sleep(1);
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