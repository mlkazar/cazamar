#include <unistd.h>
#include <stdlib.h>

#include "bufsocket.h"
#include "radioscan.h"
#include "cthread.h"

void
displayStations(RadioScanQuery *queryp)
{
    RadioScanStation *stationp;
    RadioScanStation::Entry *ep;
    uint32_t nextScannedVersion;
    uint32_t prevScannedVersion;

    while(!queryp->_verifying) {
        printf("Searching, %s\n", queryp->getStatus().c_str());
        sleep(1);
    }

    uint32_t count=0;
    for( stationp = queryp->_goodStations.head();
         stationp != nullptr;
         stationp = stationp->_dqNextp) {
        count++;
    }
    printf("Initial station list has %d entries\n", count);

    // now watch for verification updatesx
    prevScannedVersion = 0;
    while(true) {
        bool stopAfter = queryp->_allVerified;
        nextScannedVersion = queryp->_nextVersion;

        for( stationp = queryp->_goodStations.head();
             stationp;
             stationp = stationp->_dqNextp) {
            if (stationp->_updateVersion >= prevScannedVersion) {
                if (stationp->_verified) {
                    printf("Update for station %s: verifiedWorking=%d\n",
                           stationp->_stationName.c_str(), stationp->_verifiedWorking);
                    printf("Station name is '%s'\n", stationp->_stationName.c_str());
                    printf("Description:\n%s\n", stationp->_stationShortDescr.c_str());
                    if (stationp->_iconUrl.length() > 0)
                        printf("Icon %s\n", stationp->_iconUrl.c_str());
                    printf("Source was %s\n", stationp->_stationSource.c_str());
                    printf("Streams:\n");
                    for(ep = stationp->_entries.head(); ep; ep=ep->_dqNextp) {
                        printf("%s with type=%s rate=%d kbits/sec(icy-br=%d)\n",
                               ep->_streamUrl.c_str(), ep->_streamType.c_str(), ep->_streamRateKb,
                               ep->_sawIcyBr);
                    }
                    printf("End of stream list\n\n");
                } else {
                    printf("Station %s still not verified??\n",
                           stationp->_stationName.c_str());
                }
            }
        } // for loop

        // safe to stop if allVerified was before a full scan
        if (stopAfter)
            break;

        prevScannedVersion = nextScannedVersion;
        sleep(1);
    }
    printf("All done!\n");
}

class QueryMonitor : public CThread {
    int _stopped;
    RadioScanQuery *_queryp;
public:
    QueryMonitor() {
        _stopped = 0;
    }

    void start(void *cxp) {
        _queryp = (RadioScanQuery *)cxp;
        displayStations(_queryp);
    }

    void stop() {
        _stopped = 1;
    }

    void updateQuery(RadioScanQuery *queryp) {
        _queryp = queryp;
    }
};

int 
main(int argc, char **argv)
{
    RadioScan *scanp;
    RadioScanQuery *queryp;
    BufSocketFactory socketFactory;
    RadioScan::ScanType scanType;
    QueryMonitor *monitorp;
    CThreadHandle *chp;

    if (argc < 2) {
        printf("usage: scantest name|tag <name>\n");
        return -1;
    }

    scanp = new RadioScan();
    scanp->init(&socketFactory, "");
    scanp->setStrictLicense();
    printf("back from init\n");

    monitorp = new QueryMonitor();
    chp = new CThreadHandle();

    if (strcmp(argv[1], "name") == 0 || strcmp(argv[1], "tag") == 0) {
        queryp = new RadioScanQuery();
        queryp->initSmart(scanp, std::string(argv[2]));

        // monitor this query
        chp->init((CThread::StartMethod) &QueryMonitor::start, monitorp, queryp);

        // start the actual search
        if (strcmp(argv[1], "name") == 0)
            scanType = RadioScan::useName;
        else
            scanType = RadioScan::useTag;
        scanp->searchStation(queryp, scanType);

        printf("\nAll done with query=%p for string='%s'\n", queryp, argv[2]);
        delete queryp;
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
