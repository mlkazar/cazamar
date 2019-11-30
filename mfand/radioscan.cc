#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "osp.h"
#include "radioscan.h"
#include "buftls.h"

/* stupid statics */
CThreadMutex RadioScan::_lock;

void
RadioScan::init(CDisp *disp)
{
    _cdisp = disp;
    _cdispGroup = new CDispGroup();
    _cdispGroup->init(_cdisp);

    _xapip = new XApi();

    _bufTlsp = new BufTls("");
    _bufTlsp->init(const_cast<char *>("djtogoapp.duckdns.org"), 7700);

    _connp = _xapip->addClientConn(_bufTlsp);

    RadioScanLoadTask *loadTaskp;
    loadTaskp = new RadioScanLoadTask();
    loadTaskp->init(this);
    _cdispGroup->queueTask(loadTaskp);
}

void
RadioScan::searchStation(std::string query, RadioScanQuery **respp)
{
    RadioScanQuery *resp;

    resp = new RadioScanQuery();
    *respp = resp;

    takeLock();
    
    releaseLock();

    *respp = resp;
}

int32_t
RadioScanLoadTask::start()
{
    FILE *newFilep;
    char tbuffer[4096];
    XApi::ClientReq *reqp;
    CThreadPipe *inPipep;
    int32_t code;
    int32_t nbytes;
    int32_t totalBytes;
    
    printf("Starting download\n");
    newFilep = fopen("stations.new", "w");
    reqp = new XApi::ClientReq();
    reqp->startCall(_scanp->_connp, "/get?id=stations.rsd", /* isGet */ XApi::reqGet);
    
    code = reqp->waitForHeadersDone();

    inPipep = reqp->getIncomingPipe();

    totalBytes = 0;
    if (code) {
        delete reqp;
    }
    else {
        while(1) {
            nbytes = inPipep->read(tbuffer, sizeof(tbuffer));
            if (nbytes > 0) {
                code = fwrite(tbuffer, 1, nbytes, newFilep);
                totalBytes += code;
                if (code <= 0)
                    break;
            }
            else
                break;
        } /* loop over all */
    }

    delete inPipep;
    delete reqp;
    fclose(newFilep);
    newFilep = NULL;
    rename("stations.new", "stations.rsd");
    printf("Received %d bytes\n", totalBytes);

    return 0;
}

RadioScanQuery::Entry *
RadioScanQuery::getFirstEntry()
{
    RadioScanQuery::Entry *ep;

    RadioScan::takeLock();
    ep = _entries.head();
    RadioScan::releaseLock();

    return ep;
}

RadioScanQuery::Entry *
RadioScanQuery::getNextEntry(RadioScanQuery::Entry *aep)
{
    RadioScanQuery::Entry *ep;

    RadioScan::takeLock();
    ep = aep->_dqNextp;
    RadioScan::releaseLock();

    return ep;
}


void
RadioScanQuery:: hold()
{
    RadioScan::takeLock();
    ++_refCount;
    RadioScan::releaseLock();
}


void 
RadioScanQuery::release()
{
    RadioScan::takeLock();
    osp_assert(_refCount > 0);
    ++_refCount;
    RadioScan::releaseLock();
}

void
RadioScanQuery::del()
{
    RadioScan::takeLock();
    _deleted = 1;
    RadioScan::releaseLock();
}

