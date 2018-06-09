#include <unistd.h>
#include <stdio.h>

#include "rpc.h"
#include "mfclient.h"
#include "dqueue.h"

int
main(int argc, char **argv)
{
    static const uint32_t sleepTime = 1;
    MfClient *clientp;
    Rpc *rpcp;
    dqueue<MfEntry> elist;
    MfEntry *ep;
    int32_t code;

    rpcp = new Rpc();
    rpcp->init();

    clientp = new MfClient(rpcp);

    clientp->init();

    clientp->announceSong((char *)"kazar", (char *) "Moby", (char *)"Why do things suck?", 1, 1);
    sleep(sleepTime);
    clientp->announceSong((char *)"kazar", (char *) "Yes", (char *)"Close To The Edge", 1, 1);
    clientp->announceSong((char *)"kazar", (char *) "Pink Floyd", (char *)"Echoes", 1, 1);
    clientp->announceSong( (char *)"kazar", (char *) "Pink Floyd",
                           (char *)"Set the Controls for the heart of the Sun", 1, 1);
    clientp->announceSong((char *)"kazar", (char *) "Mapei", (char *)"Don't Wait", 1, 1);

    code = clientp->getAllPlaying(3, &elist);

    printf("get code is %d\n", code);
    for(ep = elist.head(); ep; ep=ep->_dqNextp) {
        printf("Retrieve %s %s/%s\n", ep->_whop, ep->_songp, ep->_artistp);
        delete ep;
    }
    elist.init();
}
