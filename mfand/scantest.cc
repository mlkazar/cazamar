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
    BufSocketFactory socketFactory;
    QueryMonitor *monitorp;
    CThreadHandle *chp;

    if (argc < 2) {
        printf("usage: scantest name <name>\n  or scantest browse\n");
        return -1;
    }

    scanp = new RadioScan();
    scanp->init(&socketFactory, "");
    printf("back from init\n");

    monitorp = new QueryMonitor();
    chp = new CThreadHandle();

    if (strcmp(argv[1], "lookup") == 0) {
        queryp = new RadioScanQuery();

        // monitor this query
        chp->init((CThread::StartMethod) &QueryMonitor::start, monitorp, queryp);

        scanp->searchStation(std::string(argv[2]), &queryp);

        printf("\nAll done with query=%p for string='%s'\n", queryp, queryp->_query.c_str());

        displayStations(queryp);

        delete queryp;
    } else if (strcmp(argv[1], "browse") == 0) {
        printf("Starting browse test\n");

        /* create a new browse query, and pass it to the monitor, and then get rid of
         * the old query.
         */
        queryp = new RadioScanQuery();

        // monitor this query
        chp->init((CThread::StartMethod) &QueryMonitor::start, monitorp, queryp);

        const char *countryp = "US";
        const char *genrep = "";
        if (argc > 2) {
            if (strcasecmp(argv[2], "all") == 0)
                countryp = "";
            else
                countryp = argv[2];
        }
        if (argc > 3) {
            if (strcasecmp(argv[3], "all") == 0)
                genrep = "";
            else
                genrep = argv[3];
        }

        // ignore state and city for now
        queryp->initBrowse(scanp, 5, countryp, "", "", genrep);

        monitorp->updateQuery(queryp);

        scanp->browseStations(queryp);

        printf("Back from browsing\n");

        monitorp->stop();

        displayStations(queryp);

        delete queryp;
        queryp = NULL;
    } else {
        printf("unrecognized test operation '%s'\n", argv[1]);
        return -1;
    }

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
