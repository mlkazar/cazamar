#include <stdio.h>
#include <unistd.h>

#include "rbufstr.h"
#include "socklocal.h"

class ServerTask : public Task {
public:
    class Client : public SockClient {
    public:
        ServerTask *_serverTaskp;
        void indicatePacket(std::shared_ptr<SockConn> connp, std::shared_ptr<Rbuf> bufp) {
            std::shared_ptr<Rbuf> newBufp;
            if (_serverTaskp->_firstTime) {
                printf("Server received '%s'\n", bufp->getStr().c_str());
                _serverTaskp->_firstTime = 0;
            }
            newBufp = std::make_shared<RbufStr>();
            newBufp->append((char * )"Biafra", 6);
            connp->send(newBufp);
        }
        
        Client(ServerTask *stp) : _serverTaskp(stp) {};
    };

    Client _me;
    int _firstTime;
    SockLocalSys *_sysp;
    
    ServerTask() : _me(this) {
        return;
    }

    void init(SockLocalNet *netp) {
        SockNode node("server-1");
        _firstTime = 1;
        _sysp = new SockLocalSys(&_me, netp);
        _sysp->ifconfig(&node);
        _sysp->listen("");
    }

} mainServerTask;

class ClientTask : public Task {
    class Client : public SockClient {
    public:
        ClientTask *_clientTaskp;

        Client(ClientTask *ctp) : _clientTaskp(ctp) {};

        void indicatePacket(std::shared_ptr<SockConn> connp, std::shared_ptr<Rbuf> bufp) {
            if (_clientTaskp->_firstTime) {
                printf("Client received '%s'\n", bufp->getStr().c_str());
                _clientTaskp->_firstTime = 0;
            }
            _clientTaskp->_counter++;
            _clientTaskp->doSends();
        };
    } _me;

    /* class variables */
    SockLocalSys *_sysp;
    int32_t _counter;
    int _firstTime;

public:
    void doSends() {
        SockNode node("server-1");
        std::shared_ptr<SockConn> connp;
        std::shared_ptr<Rbuf> bufp;

        if (_counter > 1000000) {
            printf("All done\n");
            exit(0);
        }

        connp = _sysp->getConnection(&node);
        if (connp.get() == NULL) {
            printf("getConnection failed\n");
            return;
        }

        bufp = std::make_shared<RbufStr>();
        bufp->append((char *) "Jello", 5);
        connp->send( bufp);
    }

    void init(SockLocalNet *netp) {
        SockNode node("client-1");
        _firstTime = 1;
        _counter = 0;
        _sysp = new SockLocalSys(&_me, netp);
        _sysp->ifconfig(&node);
        doSends();
    }

    ClientTask() : _me(this) {};
} mainClientTask;

int
main(int argc, char **argv)
{
    int i;
    SockLocalNet *netp;

    printf("sltest: write this\n");
    
    printf("Starting\n");
    
    for(i=0;i<2;i++)
        new Disp();

    /* create a network for server and client to ifconfig into */
    netp = new SockLocalNet();

    mainServerTask.init(netp);
    mainClientTask.init(netp);

    while(1) {
        sleep(1);
    }

    return 0;
}
