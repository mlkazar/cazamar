#include <stdio.h>
#include <sys/time.h>

#include "osp.h"

void
osp_panic(char *why, char *filep, int line)
{
    char *datap = (char *) 1;

    printf("osp_panic: %s at %s:%d\n", why, filep, line);
    *datap = 0xEE;
}

/* static */ OspMBuf *
OspMBuf::alloc(uint32_t asize)
{
    OspMBuf *mbp;

    if (asize < _defaultSize)
        asize = _defaultSize;
    mbp = new OspMBuf();
    mbp->_allocDatap = new char[asize];
    mbp->_allocBytes = asize;
    mbp->_datap = mbp->_allocDatap;
    mbp->_dataBytes = 0;
    mbp->_dqNextp = mbp->_dqPrevp = NULL;

    return mbp;
}

uint32_t
osp_time_ms()
{
    struct timeval tv;
    static int initDone = 0;
    static uint32_t baseSecs;

    gettimeofday(&tv, NULL);

    if (!initDone) {
        baseSecs = (uint32_t) tv.tv_sec;
        initDone = 1;
    }

    return (uint32_t) (tv.tv_sec - baseSecs) * 1000 + tv.tv_usec/1000;
}

uint32_t
osp_time_sec()
{
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return (uint32_t) tv.tv_sec;
}
