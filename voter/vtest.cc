#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "rpc.h"
#include "osp.h"
#include "sdr.h"
#include "voter.h"

int
main(int argc, char **argv) {
    static const uint32_t maxPorts = 32;
    uint32_t portCount;
    uint32_t port;
    Voter *votersp[maxPorts];
    Voter *tvp;
    VoterAddr myAddr;
    VoterAddr voterAddrs[maxPorts];

    if (argc < 3) {
        printf("usage: vtest first-port1 count ...\n");
        return -1;
    }

    portCount = atoi(argv[2]);
    if (portCount > maxPorts) {
        printf("vtest: max port count is %d\n", maxPorts);
        return -1;
    }
    port = atoi(argv[1]);
    
    myAddr._ipAddr = Voter::getMyAddr();        // port will be filled in below.

    for(uint32_t i=0; i<portCount; i++) {
        if (i >= maxPorts)
            break;
        voterAddrs[i]._ipAddr = myAddr._ipAddr;
        voterAddrs[i]._port = port++;
    }
    printf("Starting %d voters\n", portCount);

    for(uint32_t i=0;i<portCount;i++) {
        votersp[i] = tvp = new Voter();
        myAddr._port = voterAddrs[i]._port;
        tvp->setPeers(voterAddrs, portCount, myAddr);
        // TODO: don't need to provide both port and my address, which
        // contains the port.
        tvp->init(myAddr._port);
    }

    while(true) {
        sleep(10);
    }

    return 0;
}
