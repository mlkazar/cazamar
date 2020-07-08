#include "disp.h"
#include "task.h"
#include "osp.h"
#include "socklocal.h"
#include "ezcall.h"
#include "rbufstr.h"

#include <stdlib.h>

class MainServer : public Task {
    class MainServerTask : public EzCall::ServerTask {
        std::shared_ptr<RbufStr> _newBufp;
        static int32_t _counter;
    public:
        /* reqp is live from init to sendResponse */
        void init(std::shared_ptr<Rbuf> rbufp, EzCall::ServerReq *reqp, void *contextp) {
            if (_counter == 0)
                printf("Server task received '%s' -- should be 'Jello'\n",
                       rbufp->getStr().c_str());
            _newBufp = std::make_shared<RbufStr>();
            _newBufp->append((char *) "Biafra", 7);
            reqp->sendResponse(_newBufp);
            _counter++;
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
        sysp = new SockLocalSys(netp);
        sysp->ifconfig(nodep);
        ezCallp = new EzCall();
        ezCallp->init(sysp, "royal");
        ezCallp->setFactoryProc( &MainServer::factoryProc);
    }
};

int32_t MainServer::MainServerTask::_counter = 0;

class MainClient : public Task {
    SockLocalSys *_sysp;
    SockLocalNode *_serverNodep;
    SockLocalNode *_myNodep;
    EzCall::ClientReq *_reqp;
    uint32_t _counter;
    uint32_t _maxCounter;
    EzCall *_ezCallp;

public:
    void init( SockLocalNet *netp, 
               const char *myNamep,
               const char *serverNamep, 
               uint32_t maxCounter) {
        _counter = 0;
        _maxCounter = maxCounter;

        _serverNodep = new SockLocalNode(serverNamep);
        _myNodep = new SockLocalNode(myNamep);

        _sysp = new SockLocalSys(netp);
        _sysp->ifconfig(_myNodep);
        _ezCallp = new EzCall();
        _ezCallp->init(_sysp, "");
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
        _ezCallp->call(_serverNodep, "royal", rbufp, &_reqp, this);
    }
    
    void afterCall() {
        std::shared_ptr<Rbuf> rbufp;
        if (_counter == 0) {
            rbufp = _reqp->getResponseBuffer();
            printf("back after call with '%s' -- should be 'Biafra'\n",
                   rbufp->getStr().c_str());
        }
        _counter++;
        _reqp->release();
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
    
    server.init(netp, "biafra");
    client.init(netp, "jello", "biafra", maxCounter);

    while(1) {
        sleep(1);
    }
}
