#include "testrpc.h"

Rs *test_rsp;

int
client(int argc, char **argv)
{
    int32_t code;
    OspNetSockAddr taddr;
    RsConn *connp;

    test_rsp = rsp = new Rs;
    code = rsp->init(vlanp, 5001);
    printf("rs init code=%d\n", code);

    taddr._addr = 0x7f000001;
    taddr._port = 5000;
    connp = rsp->getClientConn(rsp, vlanp, &taddr);
    printf("getclientconn doned\n");
    test_connp = connp;

    
}

int
server(int argc, char **argv)
{
}

int
main(int argc, char **argv)
{
    if (strcmp(argv[1], "c") == 0) {
	return client(argc, argv);
    }
    else if (strcmp(argv[1], "s") == 0) {
	return server(argc, argv);
    }
}
