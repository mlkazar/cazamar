#include <stdio.h>
#include <string>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "upload.h"

void server(int port, std::string pathPrefix, int single);

/* This is the main program for the test application; it just runs the
 * server application function after peeling off the port and app name
 * from the command line arguments.
 */
int
main(int argc, char **argv)
{
    int port;
    int i;
    int single = 0;

    if (argc <= 1) {
        printf("usage: apptest <port> [-single]\n");
        return 1;
    }

    port = atoi(argv[1]);

    argc -= 2;
    argv += 2;

    for(i=0; i<argc;i++) {
        if (!strcmp(argv[i], "-single"))
            single = 1;
    }

    /* peel off command name and port */
    server(port, std::string(""), single);

    return 0;
}

void
server(int port, std::string pathPrefix, int single)
{
    SApi *sapip;
    UploadApp *uploadApp;
    SApiLoginCookie *loginCookiep;

    sapip = new SApi();
    sapip->setPathPrefix(pathPrefix); /* this is where everyone gets the path prefix */
    sapip->initWithPort(port);

    loginCookiep = SApiLogin::createGlobalCookie(pathPrefix, pathPrefix);
    loginCookiep->enableSaveRestore();

    /* initLoop doesn't return */
    uploadApp = new UploadApp(pathPrefix, pathPrefix);
    uploadApp->setGlobalLoginCookie(loginCookiep);
    uploadApp->initLoop(sapip, single);
}
