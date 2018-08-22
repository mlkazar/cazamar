#include <stdio.h>
#include <string>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "upload.h"

void server(int argc, char **argv, int port, std::string pathPrefix);

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
    server(argc-2, argv+2, port, std::string(""));

    return 0;
}

void
server(int argc, char **argv, int port, std::string pathPrefix)
{
    SApi *sapip;
    UploadApp *uploadApp;
    SApiLoginCookie *loginCookiep;

    sapip = new SApi();
    sapip->setPathPrefix(pathPrefix); /* this is where everyone gets the path prefix */
    sapip->initWithPort(port);

    loginCookiep = SApiLogin::createGlobalCookie(pathPrefix);
    loginCookiep->enableSaveRestore();

    /* initLoop doesn't return */
    uploadApp = new UploadApp(pathPrefix);
    uploadApp->setGlobalLoginCookie(loginCookiep);
    uploadApp->initLoop(sapip);
}
