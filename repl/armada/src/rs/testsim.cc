#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <rs/rs.h>
#include <stdlib.h>
#include <osp/ospnet.h>
#include <platform/platform.h>

#include "testsim.xdr.h"

static const uint32_t RsListenerPort = 8000;
RsConn *test_connp;
Rs *test_rsp;
OspNetVLAN *test_vlanp;
int32_t test_maxClientCalls = 1000;

void
streamIODone( /*IN */ void *contextp,
	    /* OUT */ int32_t rcode,
	    /* OUT */ uint32_t *bp,
	    /* OUT */ SdrPipe *opipep)
{
    char *tp;
    OspNetMBuf *mbufp;
    
    mbufp = opipep->getChain();
    tp = mbufp->data();

    printf("*stream call done b=%d opipe-data='%s'\n", *bp, tp);
}

void 
fetchDone( /*IN */ void *contextp,
	   /*OUT */int32_t rcode,
	   /*OUT*/ testrpcPie * piep,
	   /*OUT*/ testDinner *dinnerp,
	   /*OUT*/ testLunch *lcp,
	   /*OUT*/ char **barpp)
{
    testBreakfast bf;
    testSauce sauce[3];
    int32_t code;
    RsConn *connp = test_connp;

    osp_assert(lcp->testLunch_len == 2 &&
	       lcp->testLunch_val[0]._a == 1 &&
	       lcp->testLunch_val[1]._a == 3 &&
	       strcmp(lcp->testLunch_val[0]._b, "one") == 0 &&
	       strcmp(lcp->testLunch_val[1]._b, "three") == 0);

    osp_assert( strcmp(*barpp, "bar") == 0 &&
		piep->_c == 15 &&
		piep->_d == 16);

    if (--test_maxClientCalls <= 0) {
	printf("Fini\n");
	exit(1);
    }

    /* start a new call */
    bf.testBreakfast_len = 3;
    bf.testBreakfast_val = sauce;
    sauce[0]._a = 0;
    sauce[1]._a = 2;
    sauce[2]._a = 4;

    code = (TESTRPCfetchData
	    (connp, NULL, fetchDone, NULL, 3, (testChar *) "entry", const_cast<char *>("foo"), &bf));

}

int32_t
appendData(void *contextp, SDR *sdrp, SdrPipe *pipe)
{
    OspNetMBuf *mbufp;
    char *tp;

    printf(" in append data\n");
    mbufp = sdrp->getChain();
    tp = mbufp->append(3, MB_RS_TAG);
    strcpy(tp, "hi");
    return 1;
}

int32_t
sappendData(void *contextp, SDR *sdrp, SdrPipe *pipe)
{
    OspNetMBuf *mbufp;
    char *tp;

    printf(" in server append data\n");
    mbufp = sdrp->getChain();
    tp = mbufp->append(4, MB_RS_TAG);
    strcpy(tp, "bye");

    return 1;
}

int
client(int argc, char **argv)
{
    int32_t code;
    int32_t i;
    OspNetVLAN *vlanp;
    Rs *rsp;
    OspNetSockAddr taddr;
    RsConn *connp;
    testBreakfast bf;
    testSauce sauce[3];
    SdrPipe apipe;

    for(i=2;i<argc;i++) {
	if (strcmp(argv[i],"-c") == 0) {
	    test_maxClientCalls = atoi(argv[i+1]);
	    i++;
	}
    }
    test_vlanp = vlanp = OspNetVLAN::getInstance();

    test_rsp = rsp = Rs::getInstanceClient();

    taddr._addr = 0x7f000001;
    taddr._port = RsListenerPort;
    connp = rsp->getClientConn(&taddr);
    test_connp = connp;

    bf.testBreakfast_len = 3;
    bf.testBreakfast_val = sauce;
    sauce[0]._a = 0;
    sauce[1]._a = 2;
    sauce[2]._a = 4;

    code = (TESTRPCfetchData
	    (connp, NULL, fetchDone, NULL, 3, (testChar *) "entry", const_cast<char *>("foo"), &bf));
    printf(" client code=%d\n", code);

    apipe._marshallProcp = appendData;
    code = (TESTRPCstreamIO
	    (connp, NULL, streamIODone, NULL, 4, &apipe));
    printf(" client code=%d\n", code);

    while(1) {
	sleep (1000);
    }
}

/* static */ void
serverRequest( void *contextp, RsServerCall *scallp, OspNetMBuf *callDatap)
{
    int32_t code;

    code = TESTRPCExecuteRequest( /* ctx */ NULL, scallp, callDatap);
    if (code) {
	printf("server callout failed code=%d\n", code);
    }
}

int32_t
STESTRPCfetchData( void *contextp,
		   RsServerCall *callp,
		   int ix,
		   testChar *entryp,
		   char *inStringp,
		   testBreakfast *bfp,
		   testrpcPie *piep,
		   testDinner *dinnerp,
		   testLunch *lcp,
		   char **barpp)
{
    char *barp;

    osp_assert(strcmp(inStringp, "foo") == 0 && ix == 3);
    piep->_c = 0xc+ix;
    piep->_d = 0xd+ix;

    lcp->testLunch_len = 2;
    lcp->testLunch_val = (testSauce *) osp_alloc(2*sizeof(testSauce));
    lcp->testLunch_val[0]._a = 1;
    strcpy(lcp->testLunch_val[0]._b, "one");
    lcp->testLunch_val[1]._a = 3;
    strcpy(lcp->testLunch_val[1]._b, "three");
    dinnerp->testDinner_len = 2;
    dinnerp->testDinner_val = (char *) osp_alloc(2);

    barp = (char *) osp_alloc(4);
    strcpy(barp, "bar");
    *barpp = barp;

    osp_assert( strcmp((char *) entryp, "entry") == 0 &&
		bfp->testBreakfast_len == 3 &&
		bfp->testBreakfast_val[0]._a == 0 &&
		bfp->testBreakfast_val[1]._a == 2 &&
		bfp->testBreakfast_val[2]._a == 4);

    return 0;
}

int32_t
STESTRPCstreamIO( void *contextp,
		  RsServerCall *callp,
		  uint32_t a,
		  SdrPipe *inPipep,
		  uint32_t *bp,
		  SdrPipe *outPipep)
{
    OspNetMBuf *mbufp;

    printf( "streamIO a=%d\n", a);
    *bp = a+1;

    mbufp = inPipep->getChain();
    printf(" server side incoming stream data '%s'\n", mbufp->data());

    outPipep->_marshallProcp = sappendData;
    outPipep->_marshallContextp = NULL;
    return 0;
}

int
server(int argc, char **argv)
{
    OspNetVLAN *vlanp;
    Rs *rsp;
    RsService *rservicep;
    
    test_vlanp = vlanp = OspNetVLAN::getInstance();
    test_rsp = rsp = Rs::createServer(RsListenerPort,
                                      true /* noSharePreferredAllocator */);
    rservicep = new RsService(rsp, 17, serverRequest, NULL);

    while(1) {
	sleep(1000);
    }

    return 0;
}

int
main(int argc, char **argv)
{
    if (argc <= 1) {
	printf("usage: testrs c|s\n");
	printf("run one cmd 'testsim s' and another 'testsim c'\n");
	exit(1);
    }

    Dispatcher::initialize();

    if (strcmp(argv[1], "c") == 0) {
	return client(argc, argv);
    }
    else if (strcmp(argv[1], "s") == 0) {
	return server(argc, argv);
    }
    return 0;
    
}

int32_t
STESTRPCJBCall(void*a, RsServerCall *b , unsigned int c, Leak *d)
{
    return 0;
}
