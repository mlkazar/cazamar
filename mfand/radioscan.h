#include <string>
#include <cdisp.h>
#include <dqueue.h>

#include "xapi.h"
#include "buftls.h"

/* Usage: this module is responsible for taking a query string, like 'WYEP' and finding
 * the radio station info for that station, and providing the info, and the stream URL,
 * to the caller.  We'd also like to upcall station status information, specifically,
 * whether the streaming URL appear to be working.
 */

class RadioScan;
class RadioScanLoadTask;

class RadioScanQuery {
    class Entry {
    public:
        Entry *_dqNextp;
        Entry *_dqPrevp;
        std::string _shortName;
        std::string _description;
        std::string _streamUrl;
        uint8_t _alive;
    };

 public:
    uint32_t _refCount;
    uint8_t _deleted;
    std::string _query;
    RadioScan *_scanp;
    dqueue<Entry> _entries;

    void updatedEntries();

    void updatedState();

    Entry *getFirstEntry();

    Entry *getNextEntry(Entry *ep);

    void hold();

    void release();

    void del(); /* delete is a reserved word */
};

/* instantiate a radioscan object once, and then perform multiple search operations.
 * It creates its own cdisp object now, but if we ever have multiple users of a 
 * thread pool, we can pass in a cdisp instead.
 */
class RadioScan {
    CDisp *_cdisp;
    CDispGroup *_cdispGroup;
    dqueue<RadioScanQuery> _activeQueries;
    XApi *_xapip;
    BufTls *_bufTlsp;
    static CThreadMutex _lock;

 public:
    XApi::ClientConn *_connp;

    void init(CDisp *cdisp);

    void searchStation(std::string query, RadioScanQuery **respp);

    static void takeLock() {
        _lock.take();
    }

    static void releaseLock() {
        _lock.release();
    }
};

class RadioScanLoadTask : public CDispTask {
    RadioScan *_scanp;
public:
    int32_t start();

    void init(RadioScan *scanp) {
        _scanp = scanp;
    }
};
