#include <unistd.h>
#include <stdlib.h>

#include "bufsocket.h"
#include "radioscan.h"
#include "cthread.h"

class QueryMonitor : public CThread {
    int _stopped;
    RadioScanQuery *_queryp;
public:
    QueryMonitor() {
        _stopped = 0;
    }

    void start(void *cxp) {
        _queryp = (RadioScanQuery *)cxp;
        while(1) {
            sleep(2);
            if (_stopped)
                break;

            /* there's a race condition between changing the query
             * object and this code, but it should hit very rarely,
             * and this is just a test program.
             */
            if (_queryp)
                printf("Status: %s\n", _queryp->getStatus().c_str());
        }
    }

    void stop() {
        _stopped = 1;
    }

    void updateQuery(RadioScanQuery *queryp) {
        _queryp = queryp;
    }

};

void
displayStations(RadioScanQuery *queryp)
{
    RadioScanStation *stationp;
    RadioScanStation::Entry *ep;

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
}

int 
main(int argc, char **argv)
{
    RadioScan *scanp;
    RadioScanQuery *queryp;
    RadioScanQuery *oldQueryp;
    BufSocketFactory socketFactory;
    QueryMonitor *monitorp;
    CThreadHandle *chp;

    scanp = new RadioScan();
    scanp->init(&socketFactory, "");
    printf("back from init\n");

    queryp = new RadioScanQuery();
    monitorp = new QueryMonitor();
    chp = new CThreadHandle();
    chp->init((CThread::StartMethod) &QueryMonitor::start, monitorp, queryp);
    scanp->searchStation(std::string(argv[1]), &queryp);

    printf("All done with query=%p for string='%s'\n", queryp, queryp->_query.c_str());

    displayStations(queryp);

    printf("Starting browse test\n");

    /* create a new browse query, and pass it to the monitor, and then get rid of
     * the old query.
     */
    oldQueryp = queryp;
    queryp = new RadioScanQuery();
    queryp->initBrowse(scanp, 10, "France", "", "");
    monitorp->updateQuery(queryp);
    delete oldQueryp;

    scanp->browseStations(queryp);

    printf("Back from browsing\n");

    monitorp->stop();

    displayStations(queryp);

    delete queryp;
    queryp = NULL;

#if 0
    printf("Sleeping now\n");

    while(1) {
        sleep(1);
    }
#else
    printf("ScanTest done\n");
#endif
    return 0;
}
