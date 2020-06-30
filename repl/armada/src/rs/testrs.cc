/* This must come first to ensure that it is picked up before
 * any other Avere headers. Doing so ensures the correct interaction
 * between DoSysInit and the static constructors for the modules
 * it initializes both directly and indirectly.
 */
#include <init/init.h> /* DoSysInit */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <cmdctl/stat/StatCollection.h>
#include <osp/ospnet.h>
#include <rs/rs.h>
#include <platform/platform.h>
#include <zerocopy/zcinit.h>

/* Needed for SysInit -- although these are not referenced directly,
 * their presence is necessary for the DoSysInit() call below.
 */
#include <init/collection_Core.h> /* DoSysInit */

OspNetVLAN *test_vlanp;
Rs *test_rsp;

static const uint32_t RsListenerPort = 8000;
int test_noExit = 0;
uint32_t test_clientCalls;
uint32_t test_maxClientCalls = 8;
int32_t test_clientThreads=4;
int32_t test_clientTTL = 0;
RsConn *test_connp;

void
clientResponse ( void (*dispatchProcp)(),
		 void *contextp,
		 OspNetMBuf *respDatap,
		 int32_t rcode)
{
    OspNetMBuf *bufp;
    char *datap;
    int32_t code;
    char tbuffer[4];

    if (rcode) {
	printf("response code = %d\n", rcode);
	osp_assert(respDatap == NULL);
    }
    else {
	datap = respDatap->pullUp(4, tbuffer);
	osp_assert(respDatap->lenm() == 4 &&
		   strcmp(datap, "bye") == 0);

	respDatap->mfreem();
    }

    if (++test_clientCalls >= test_maxClientCalls) {
	test_clientThreads--;
	printf("Thread all done, %d left\n", test_clientThreads);
	if (test_clientThreads == 0) {
            test_connp->release();      /* can also use ::destroy */
	    printf("Fini\n");
	    if (!test_noExit) {
                _exit(0);
            }
            else {
                printf("test_connp=%p\n", test_connp);
            }
            test_connp = NULL;
	}
	return;
    }

    bufp = test_vlanp->get(0);
    datap = bufp->append(3, MB_RS_TAG);
    strcpy(datap, "h1");
    static bool switchServices;
    code = test_connp->call( bufp, switchServices ? 17 : 18, 1, clientResponse, NULL);
    switchServices = !switchServices;

    return;
}

int
client(int argc, char **argv)
{
    OspNetVLAN *vlanp;
    Rs *rsp;
    int32_t code;
    RsConn *connp;
    OspNetSockAddr taddr;
    OspNetMBuf *bufp;
    char *datap;
    int32_t i;

    for(i=2;i<argc;i++) {
	if (strcmp(argv[i],"-c") == 0) {
	    test_maxClientCalls = atoi(argv[i+1]);
	    i++;
	}
	else if (strcmp(argv[i], "-p") == 0) {
	    test_clientThreads = atoi(argv[i+1]);
	    i++;
	}
        else if (strcmp(argv[i], "-ttl") == 0) {
            test_clientTTL = atoi(argv[i+1]);
            i++;
        }
        else if (strcmp(argv[i], "-noexit") == 0) {
            test_noExit = 1;
        }
    }
    
    test_vlanp = vlanp = OspNetVLAN::getInstance();
    test_rsp = rsp = Rs::getInstanceClient();

    taddr._addr = 0x7f000001;
    taddr._port = RsListenerPort;
    connp = rsp->getClientConn(&taddr);
    connp = rsp->getClientConn(&taddr);
    connp = rsp->getClientConn(&taddr);
    test_connp = connp;
    if (test_clientTTL) {
        connp->setHardTimeout(test_clientTTL);
    }

    for(i=0;i<test_clientThreads;i++) {
	bufp = vlanp->get(0);
	datap = bufp->append(3, MB_RS_TAG);
	strcpy(datap, "h1");
	code = connp->call( bufp, 17, 1, clientResponse, NULL);
        osp_assert(code == 0);
    }

    while(1) {
	sleep(1000);
    }

    return 0;
}

/* static */ void
serverRequest( void *contextp, RsServerCall *scallp, OspNetMBuf *callDatap)
{
    OspNetMBuf *responsep;
    char *datap;
    uint32_t len;

    datap = callDatap->data();
    len = callDatap->lenm();
    
    osp_assert(len == 3 && datap[0] == 'h');
    callDatap->mfreem();

    responsep = test_vlanp->get(50);
    datap = responsep->append(4, MB_RS_TAG);
    strcpy(datap, "bye");
    scallp->sendResponse(responsep, 0);
}

int
server(int argc, char **argv)
{
    OspNetVLAN *vlanp;
    Rs *rsp;    
    RsService *rservicep;
    RsService *rservicep2;
        
    test_vlanp = vlanp = OspNetVLAN::getInstance();
    test_rsp = rsp = Rs::createServer(RsListenerPort, 
                                      true /* noSharePreferredAllocator */);
    rservicep = new RsService(rsp, 17, serverRequest, NULL);
    rservicep2 = new RsService(rsp, 18, serverRequest, NULL);
    
    while(1) {
	sleep(1000);
    }

    return 0;
}

int
main(int argc, char **argv)
{
    if (argc <= 1) {
	printf("usage: testrs c [-c <count>] | s\n");
	printf("run one cmd 'testrs s' and another cmd 'testrs c -c 1000'\n");
	exit(1);
    }

    DoSysInit(true,Core);

    if (strcmp(argv[1], "c") == 0) {
	return client(argc, argv);
    }
    else if (strcmp(argv[1], "s") == 0) {
	return server(argc, argv);
    }
    return 0;
}
