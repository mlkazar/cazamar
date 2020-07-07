#include "disp.h"
#include "task.h"
#include "osp.h"
#include "socklocal.h"
#include "ezcall.h"
#include "rbufstr.h"

#include <stdlib.h>

class MainServer : public Task {
    class MainServerTask : public EzCall::ServerTask {
        std::shared_ptr<RbufStr> newBufp;
    public:
        void init(std::shared_ptr<Rbuf> rbufp, EzCall::ServerReq *reqp, void *contextp) {
            newBufp = std::make_shared<RbufStr>();
            newBufp->append((char *) "Biafra", 7);
            reqp->sendResponse(newBufp);
        }
    };

    static EzCall::ServerTask *factoryProc() {
        MainServerTask *serverTaskp;
        serverTaskp = new MainServerTask();
        return serverTaskp;
    }

    EzCall *ezCallp;
    SockLocalSys *sysp;

public:
    void init(SockLocalNet *netp, const char *namep) {
        SockLocalNode *nodep;
        nodep = new SockNode(std::string(namep));
        sysp = new SockLocalSys(NULL, netp);
        sysp->ifconfig(nodep);
        ezCallp = new EzCall();
        ezCallp->init(sysp);
        ezCallp->setFactoryProc( &MainServer::factoryProc);
    }
};

class MainClient : public Task {
    SockLocalSys *_sysp;
    SockLocalNode *_nodep;
    EzCall::ClientReq *_reqp;
    uint32_t _counter;
    uint32_t _maxCounter;
    EzCall *_ezCallp;

public:
    void init(SockLocalNet *netp, const char *serverNamep, uint32_t maxCounter) {
        _counter = 0;
        _maxCounter = maxCounter;

        _nodep = new SockLocalNode(serverNamep);

        _sysp = new SockLocalSys(NULL, netp);
        _ezCallp = new EzCall();
        _ezCallp->init(_sysp);
        doCall();
    }
        
    void doCall() {
        std::shared_ptr<RbufStr> rbufp;

        if (_counter >= _maxCounter) {
            printf("Done with %d calls\n", _maxCounter);
            exit(0);
        }

        setRunMethod(&MainClient::afterCall);
        rbufp = std::make_shared<RbufStr>();
        rbufp->append((char *) "Jello", 5);
        _ezCallp->call(_nodep, rbufp, &_reqp, this);
    }
    
    void afterCall() {
        if (_counter == 0)
            printf("back after call\n");
        _counter++;
        doCall();
    }

};

int
main(int argc, char **argv)
{
    int i;
    SockLocalNet *netp;
    MainClient client;
    MainServer server;
    uint32_t maxCounter;

    if (argc < 2) {
        printf("usage: eztest <max counter>\n");
        return -1;
    }

    maxCounter = atoi(argv[1]);

    for(i=0;i<2;i++)
        new Disp();

    netp = new SockLocalNet();
    
    server.init(netp, "jello");
    client.init(netp, "jello", maxCounter);

    while(1) {
        sleep(1);
    }
}
