#include <string>
#include "dqueue.h"

#include "xapi.h"

#include "bufgen.h"
#include "cthread.h"

/* Usage: this module is responsible for taking a query string, like 'WYEP' and finding
 * the radio station info for that station, and providing the info, and the stream URL,
 * to the caller.  We'd also like to upcall station status information, specifically,
 * whether the streaming URL appear to be working.
 *
 * A query (RadioScanQuery) can match multiple stations (RadioScanStation).  Each
 * station may have multiple associated streams.
 */

class RadioScan;
class RadioScanQuery;
class RadioScanStation;
class RadioScanLoadTask;

class RadioScanQuery {
public:
    std::string _query;
    RadioScan *_scanp;
    dqueue<RadioScanStation> _stations;
    uint32_t _refCount;
    std::string _baseStatus;
    std::string _detailedStatus;
    int _aborted;

    RadioScanQuery() {
        _refCount = 0;
        _aborted = 0;
    }

    void init(RadioScan *scanp, std::string query) {
        _query = query;
        _scanp = scanp;
    }

    int32_t searchStreamTheWorld();

    int32_t searchDar();

    int32_t searchFile();

    int32_t searchShoutcast();

    void abort() {
        _aborted = 1;
        _baseStatus = std::string("Aborting");
    }

    int isAborted() {
        return _aborted;
    }
};

class RadioScanStation {
 public:

    class Entry {
    public:
        Entry *_dqNextp;
        Entry *_dqPrevp;
        uint32_t _streamRateKb;    /* station stream rate in kbits/second */
        std::string _streamUrl;
        std::string _streamType;
        int8_t _alive;  /* -1 means unknown, 0 is no, 1 is yes */
    };

    typedef int32_t (streamUrlProc)(void *urlContextp, const char *urlStringp);
    uint32_t _refCount;
    uint8_t _deleted;
    RadioScan *_scanp;
    RadioScanQuery *_queryp;
    dqueue<Entry> _entries;
    std::string _stationName;
    std::string _stationShortDescr;
    RadioScanStation *_dqNextp;
    RadioScanStation *_dqPrevp;
    std::string _stationSource;
    uint32_t _streamRateKb;    /* station stream rate in kbits/second */
    std::string _streamType;

    void init(RadioScanQuery *queryp) {
        _queryp = queryp;
        _scanp = queryp->_scanp;
        _queryp->_stations.append(this);
    }

    ~RadioScanStation() {
        _queryp->_stations.remove(this);
    }

    void addEntry(const char *streamUrlp, const char *typep, uint32_t streamRateKb);

    int32_t streamApply(std::string url, streamUrlProc *urlProcp, void *urlContextp);

    void updatedEntries();

    void updatedState();

    Entry *getFirstEntry();

    Entry *getNextEntry(Entry *ep);

    void hold();

    void release();

    void del(); /* delete is a reserved word */

    int parsePls(const char *resultp, streamUrlProc *urlProcp, void *urlContextp);

    int parseUrl(const char *resultp, streamUrlProc *urlProcp, void *urlContextp);

    static int queryMatch(const char *a, const char *b);

    static int hasISubstr(const char *keyp, std::string target);

    static int32_t stwCallback(void *contextp, const char *urlp);

    static int skipPastEol(const char **datap, int32_t *lenp);

    static int32_t splitLine(const char *bufferp, uint32_t count, char **targetsp);
};

/* instantiate a radioscan object once, and then perform multiple search operations.
 * It creates its own cdisp object now, but if we ever have multiple users of a 
 * thread pool, we can pass in a cdisp instead.
 */
class RadioScan {
    dqueue<RadioScanQuery> _activeQueries;
    static CThreadMutex _lock;

 public:
    XApi *_xapip;
    BufGen *_dirBufp;
    XApi::ClientConn *_dirConnp;
    BufGen *_stwBufp;
    XApi::ClientConn *_stwConnp;
    BufGenFactory *_factoryp;

    void init(BufGenFactory *factoryp);

    void searchStation(std::string query, RadioScanQuery **respp);

    static void takeLock() {
        _lock.take();
    }

    static void releaseLock() {
        _lock.release();
    }

    int32_t retrieveContents(std::string url , std::string *strp);
};

class RadioScanLoadTask : public CThread {
    RadioScan *_scanp;
public:
    void start(void *argp);

    void init(RadioScan *scanp) {
        _scanp = scanp;
    }
};
