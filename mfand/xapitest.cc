#include <stdio.h>
#include <string>

#include "xapi.h"
#include "bufsocket.h"
#include "buftls.h"

void client(int argc, char **argv, int port);
void server(int argc, char **argv, int port);

int
main(int argc, char **argv)
{
    int port;

    if (argc <= 1) {
        printf("usage: xapitest c|s <port> [<count>] [s|n]\n");
        printf("usage: count only present for client ('c')\n");
        return 1;
    }

    port = atoi(argv[2]);

    if (strcmp(argv[1], "c") == 0) {
        client(argc-3, argv+3, port);
    }
    else if (strcmp(argv[1], "s") == 0) {
        server(argc-3, argv+3, port);
    }

    return 0;
}

void
client(int argc, char **argv, int port)
{
    XApi *xapip;
    XApi::ClientConn *connp;
    BufGen *socketp;
    XApi::ClientReq *reqp;
    char tbuffer[1024];
    CThreadPipe *inPipep;
    CThreadPipe *outPipep;
    uint32_t ix;
    int32_t code;
    int32_t maxCount = 1;
    int32_t useSecure = 0;
    Rst::Hdr *hdrp;

    xapip = new XApi();

    if (argc > 0) {
        maxCount = atoi(argv[0]);
    }

    if (argc > 1) {
        if (*argv[1] == 's') {
            useSecure = 1;
        }
    }

    if (!useSecure) {
        socketp = new BufSocket();
        socketp->init(const_cast<char *>("localhost"), port);
    }
    else {
        socketp = new BufTls();
        socketp->init(const_cast<char *>("localhost"), port);
    }

    connp = xapip->addClientConn(socketp);

    for(ix=0;ix<maxCount;ix++) {
        /* now prepare a call */
        strcpy(tbuffer, "Client call data\n");
        reqp = new XApi::ClientReq();
        reqp->setSendContentLength(strlen(tbuffer));
        reqp->addHeader("X-XAPITEST", "xapitest-value");
        reqp->startCall(connp, "/service", /* isPost */ XApi::reqPost);

        inPipep = reqp->getIncomingPipe();
        outPipep = reqp->getOutgoingPipe();

        outPipep->write(tbuffer, strlen(tbuffer));
        outPipep->eof();

        code = reqp->waitForHeadersDone();

        printf("client: headers back, code=%d\n", code);
        if (code == 0) {
            for(hdrp = reqp->getRecvHeaders(); hdrp; hdrp=hdrp->_dqNextp) {
                printf("Header %s:%s\n", hdrp->_key.c_str(), hdrp->_value.c_str());
            }
            printf("client: reading response data:\n");
            while(1) {
                code = inPipep->read(tbuffer, sizeof(tbuffer)-1);
                if (code <= 0) {
                    break;
                }
                if (code >= sizeof(tbuffer)) {
                    printf("client: large read %d\n", code);
                    break;
                }
                tbuffer[code] = 0;
                printf("%s", tbuffer);
            }
            printf("\nclient: done reading response data\n");
        }
        printf("client: call done\n");

        delete reqp;
        reqp = NULL;
    }

    printf("Multi-call test is done\n");
}

class Service : public XApi::ServerReq {
    static uint32_t _counter;
public:
    void init() {
        return;
    }

    void startMethod() {
        char tbuffer[100];
        int32_t code;
        Rst::Hdr *hdrp;

        CThreadPipe *inPipep = getIncomingPipe();
        CThreadPipe *outPipep = getOutgoingPipe();
        printf("server: in startMethod\n");
        for(hdrp = getRecvHeaders(); hdrp; hdrp=hdrp->_dqNextp) {
            printf("Header %s:%s\n", hdrp->_key.c_str(), hdrp->_value.c_str());
        }
        printf("reading data...\n");
        while(1) {
            code = inPipep->read(tbuffer, sizeof(tbuffer)-1);
            if (code <= 0)
                break;
            if (code >= sizeof(tbuffer))
                printf("server: bad count from pipe read\n");
            tbuffer[code] = 0;
            printf("%s", tbuffer);
        }
        printf("\nReads done\n");

        _counter++;

        sprintf(tbuffer, "counter=%d\n", _counter);

        setSendContentLength(strlen(tbuffer));

        /* time to send the response headers */
        addHeader("X-CAZAMAR-CSUM", "333");
        inputReceived();

        code = outPipep->write(tbuffer, strlen(tbuffer));
        printf("server: sent %d/%d bytes\n", code, (int) strlen(tbuffer));
        outPipep->eof();

        requestDone();
        printf("server: all done\n");
    }
};

uint32_t Service::_counter = 1001;

XApi::ServerReq *
serverRestFactory(std::string *opcodep)
{
    Service *reqp;

    reqp = new Service();
    reqp->init();
    return reqp;
}


void
server(int argc, char **argv, int port)
{
    XApi *xapip;
    int useSecure=0;
    BufGen *lsocketp = NULL;

    if (argc > 0) {
        if (*argv[0] == 's')
            useSecure = 1;
    }

    xapip = new XApi();
    xapip->registerFactory(&serverRestFactory);
    if (useSecure) {
        lsocketp = new BufTls();
        lsocketp->init((char *) NULL, port);
        xapip->initWithBufGen(lsocketp);
    }
    else
        xapip->initWithPort(port);
    while(1) {
        sleep(1);
    }
}
