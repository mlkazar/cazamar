#include "disp.h"
#include "task.h"
#include "osp.h"
#include "socklocal.h"
#include "ezcall.h"
#include "rbufstr.h"

class MainServer : public Task {
    class MainServerTask : public EzCall::ServerTask {
        std::shared_ptr<RbufStr> newBufp;
    public:
        void init(std::shared_ptr<Rbuf> rbufp, EzCall::ServerReq *reqp, void *contextp) {
            printf("processing server request\n");
            newBufp = std::make_shared<RbufStr>();
            newBufp->append((char *) "Biafra", 7);
            reqp->sendResponse(newBufp);
        }
    };

    static EzCall::ServerTask *factoryProc() {
        MainServerTask *serverTaskp;
        printf("Received call\n");
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

public:
    void init(SockLocalNet *netp, const char *serverNamep) {
        EzCall *ezCallp;
        std::shared_ptr<RbufStr> rbufp;

        _nodep = new SockLocalNode(serverNamep);

        _sysp = new SockLocalSys(NULL, netp);
        ezCallp = new EzCall();
        ezCallp->init(_sysp);
        
        setRunMethod(&MainClient::afterCall);
        rbufp = std::make_shared<RbufStr>();
        rbufp->append((char *) "Jello", 5);
        ezCallp->call(_nodep, rbufp, &_reqp, this);
    }
    
    void afterCall() {
        printf("back after call\n");
    }

};

int
main(int argc, char **argv)
{
    int i;
    SockLocalNet *netp;
    MainClient client;
    MainServer server;

    for(i=0;i<2;i++)
        new Disp();

    netp = new SockLocalNet();
    
    server.init(netp, "jello");
    client.init(netp, "jello");

    while(1) {
        sleep(1);
    }
}
