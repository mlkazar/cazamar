#include <unistd.h>
#include <stdlib.h>

#include "radioscan.h"

int 
main(int argc, char **argv)
{
    CDisp *cdisp;
    RadioScan *scanp;
    RadioScanQuery *queryp;

    printf("hi\n");
    cdisp = new CDisp();
    cdisp->init(4);

    scanp = new RadioScan();
    scanp->init(cdisp);
    printf("back from init\n");

    scanp->searchStation(std::string("WYEP"), &queryp);

    printf("query is %p\n", queryp);
    
    while(1) {
        sleep(1);
    }

    return 0;
}
