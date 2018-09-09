#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#import <Cocoa/Cocoa.h>

#include "cthread.h"
#include "upobj.h"

void
Upload::init(notifyProc *notifyProcp, void *contextp)
{
    _notifyProcp = notifyProcp;
    _notifyContextp = contextp;

    server(this);
}

/* static */ void *
Upload::server(void *contextp)
{
    Upload *uploadp = (Upload *)contextp;
    SApi *sapip;
    UploadApp *uploadApp;
    SApiLoginCookie *loginCookiep;
    NSArray *libArray;
    NSString *libPathStr;
    std::string libPath;

    NSBundle *myBundle = [NSBundle mainBundle];
    NSString *path= [myBundle pathForResource:@"status" ofType:@"png"]; /* file must exist */

    libArray = NSSearchPathForDirectoriesInDomains( NSLibraryDirectory, NSUserDomainMask, YES );
    libPathStr = [libArray[0] stringByAppendingString: @"/Cazamar"];
    libPath = std::string([libPathStr cStringUsingEncoding: NSUTF8StringEncoding]);
    ::mkdir(libPath.c_str(), 0777);
    libPath += "/";	/* after mkdir, add trailing '/' */

    /* path will end /status.png, but we only want through the trailing / */
    path = [path substringToIndex: ([path length] - 10)];
    uploadp->_pathPrefix = std::string([path cStringUsingEncoding: NSUTF8StringEncoding]);

    sapip = new SApi();
    sapip->setPathPrefix(uploadp->_pathPrefix);
    sapip->initWithPort(7701);

    loginCookiep = SApiLogin::createGlobalCookie(uploadp->_pathPrefix, libPath);
    loginCookiep->enableSaveRestore();

    uploadApp = new UploadApp(uploadp->_pathPrefix, libPath);
    uploadApp->setGlobalLoginCookie(loginCookiep);
    uploadApp->init(sapip);

    uploadp->_uploadApp = uploadApp;
    uploadp->_sapip = sapip;

    return NULL;
}

void
Upload::backup()
{
    if (_uploadApp) {
	_uploadApp->start();
    }
}
