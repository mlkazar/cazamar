#include <unistd.h>
#include <stdlib.h>

#include "radioscan.h"

int 
main(int argc, char **argv)
{
    CDisp *cdisp;
    RadioScan *scanp;
    RadioScanQuery *queryp;
    RadioScanStation *stationp;
    RadioScanStation::Entry *ep;

    cdisp = new CDisp();
    cdisp->init(4);

    scanp = new RadioScan();
    scanp->init(cdisp);
    printf("back from init\n");

    scanp->searchStation(std::string(argv[1]), &queryp);

    printf("All done -- query is %p for '%s'\n", queryp, queryp->_query.c_str());

    for(stationp = queryp->_stations.head(); stationp; stationp = stationp->_dqNextp) {
        printf("Station name is '%s'\n", stationp->_stationName.c_str());
        printf("Description:\n%s\n", stationp->_stationShortDescr.c_str());
        printf("Source was %s\n", stationp->_stationSource.c_str());

        printf("Streams:\n");
        for(ep = stationp->_entries.head(); ep; ep=ep->_dqNextp) {
            printf("%s with type=%s rate=%d kbits/sec\n",
                   ep->_streamUrl.c_str(), ep->_streamType.c_str(), ep->_streamRateKb);
        }
        printf("End of stream list\n\n");
    }

    delete queryp;

    printf("Sleeping now\n");

    while(1) {
        sleep(1);
    }

    return 0;
}
