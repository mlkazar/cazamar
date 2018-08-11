#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#import <Cocoa/Cocoa.h>

#include "cthread.h"
#include "upobj.h"
#include "upload.h"

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

    NSBundle *myBundle = [NSBundle mainBundle];
    NSString *path= [myBundle pathForResource:@"status" ofType:@"png"]; /* file must exist */
    /* path will end /status.png, but we only want through the trailing / */
    path = [path substringToIndex: ([path length] - 10)];
    uploadp->_pathPrefix = std::string([path cStringUsingEncoding: NSUTF8StringEncoding]);

    uploadp->_sapip = sapip = new SApi();
    sapip->setPathPrefix(uploadp->_pathPrefix);
    sapip->initWithPort(7701);

    uploadApp = new UploadApp(uploadp->_pathPrefix);
    uploadApp->init(sapip);

    return NULL;
}
