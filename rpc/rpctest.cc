#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "rpc.h"
#include "osp.h"
#include "sdr.h"

class TestServer : public RpcServer {

 public:
    bool _testTimeout;
    uint32_t _counter;

    class TestServerContext : public RpcServerContext {
        bool _testTimeout;
        uint32_t *_counterp;

        int32_t serverMethod(RpcServer *serverp, Sdr *inDatap, Sdr *outDatap) {
            uint32_t value;

            inDatap->copyLong(&value, 0);

            getConn()->reverseConn();

            if (_testTimeout) {
                if ((((*_counterp)++) & 3) == 2) {
                    printf("Call stalling to test timeouts, count=%d\n", *_counterp);
                    sleep(4);
                }
            }

            value++;
            outDatap->copyLong(&value, 1);

            return 0;
        }

    public:
        TestServerContext(bool testTimeout, uint32_t *counterp) {
            _testTimeout = testTimeout;
            _counterp = counterp;
        }
    };

    RpcServerContext *getContext(uint32_t opcode) {
        TestServerContext *sp;

        if (opcode != 3) {
            printf("RpcTest: bad opcode received, op=%d\n", opcode);
            return NULL;
        }
        sp = new TestServerContext(_testTimeout, &_counter);
        return sp;
    }

public:
    TestServer(Rpc *rpcp, bool testTimeout) : RpcServer(rpcp) {
        _testTimeout = testTimeout;
        _counter = 0;
    }
};

class TestClientContext : public RpcClientContext {
    RpcConn *_connp;
    char *_tagp;
    CThreadHandle *_threadp;
 public:
    void init() {
        _threadp = new CThreadHandle();
        _threadp->init((CThread::StartMethod) &TestClientContext::init2, this, NULL);
    }

    void init2(void *cxp) {
        int32_t code;
        RpcSdr *sendSdrp;
        RpcSdr *recvSdrp;
        uint32_t oldValue;
        uint32_t newValue;
        uint32_t count=0;

        while(1) {
            /* make the call (makeCall / getResponse / finishCall) */
            code = makeCall(_connp, /* opcode */ 3, &sendSdrp, &recvSdrp);
            if (code) {
                printf("RpcTest: makecall fail %d\n", code);
                sleep(1);
                continue;
            }

            oldValue = (random() & 0xFF);
            sendSdrp->copyLong(&oldValue, /* doMarshal */ 1);

            code = getResponse();
            if (code) {
                printf("RpcTest: call response=%d\n", code);
                sleep(1);
                continue;
            }

            code = recvSdrp->copyLong(&newValue, /* !doMarshal */ 0);

            finishCall();

            if (oldValue + 1 != newValue)
                printf("RpcTest: call bad value code=%d oldValue=%d newValue=%d\n\n",
                       code, oldValue, newValue);
            if ( (++count % 10000) == 0)
                printf("RpcTest: '%s' count=%d\n", _tagp, count);
        }
    }

    TestClientContext(Rpc *rpcp, RpcConn *connp, char *debugTagp) : RpcClientContext(rpcp) {
        _connp = connp;
        _tagp = debugTagp;
        return;
    }
};

int
main(int argc, char **argv)
{
    TestServer *testServerp;
    Rpc *rpcp;
    RpcListener *listenerp;
    uuid_t serviceId;
    bool testTimeout = false;

    rpcp = new Rpc();
    rpcp->init();

    if (argc < 2) {
        printf("RpcTest: usage: rpctest <c|s> [switches]\n");
        printf("-t -- test timeout by having half the calls wait 5 seconds\n");
        return -1;
    }

    for(uint32_t i=2; i<argc; i++) {
        if (strcmp(argv[i], "-t") == 0)
            testTimeout = 1;
    }

    if (strcmp(argv[1], "s") == 0) {
        /* create a service */
        Rpc::uuidFromLongId(&serviceId, 7);
        testServerp = new TestServer(rpcp, testTimeout);
        rpcp->addServer(testServerp, &serviceId);

        /* create an endpoint for the server */
        listenerp = new RpcListener();
        listenerp->init(rpcp, testServerp, 7711);

        while(1) {
            sleep(1);
        }
    }
    else {
        TestClientContext *cp;
        RpcServer *serverp;
        struct sockaddr_in destAddr;
        int32_t code;
        RpcConn *connp;

        /* create a server */
        Rpc::uuidFromLongId(&serviceId, 7);
        serverp = rpcp->addServer(NULL, &serviceId);

        /* open a conn to the target */
        destAddr.sin_family = AF_INET;
        destAddr.sin_addr.s_addr = htonl(0x7f000001);
        destAddr.sin_port = htons(7711);
        code = rpcp->addClientConn(&destAddr, &connp);
        printf("RpcTest: addclientconn code=%d\n", code);
        if (code)
            return code;

        /* bind conn to the server */
        connp->setServer(serverp);

        // set hard timeout on calls to 2 seconds.
        connp->setHardTimeout(2000);

        /* make a client call */
        cp = new TestClientContext(rpcp, connp, (char *) "a");
        cp->init();
        printf("RpcTest: Back from client call\n");

        cp = new TestClientContext(rpcp, connp, (char *) "b");
        cp->init();
        printf("RpcTest: Back from client call\n");

        while(1) {
            sleep(1);
        }
    }

    return 0;
}
