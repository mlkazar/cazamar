#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>

#include <new>

#include "osp.h"

/* stupid rabbit */
pthread_mutex_t AllocCommonHeader::_mutex;
dqueue<AllocCommonHeader> AllocCommonHeader::_hash[AllocCommonHeader::_hashSize];

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

/* static */ void *
AllocCommonHeader::commonNew(uint32_t size, void *retAddrp)
{
    char *datap;
    AllocCommonHeader *headerp;
    uint32_t ix;
    static int didOnce = 0;

    if (!didOnce) {
        pthread_mutex_init(&_mutex, NULL);
        didOnce = 1;
    }

    datap = (char *) malloc(size+sizeof(AllocCommonHeader));
    headerp = (AllocCommonHeader *)datap;
    osp_assert(headerp->_magic != _magicAlloc); /* TBD: remove */
    datap += sizeof(AllocCommonHeader);
    headerp->_magic = _magicAlloc;
    headerp->_size = size;
    headerp->_retAddrp = retAddrp;
    memset(datap, 0, size);
    
    pthread_mutex_lock(&_mutex);
    ix = hashIx(retAddrp);
    _hash[ix].append(headerp);
    pthread_mutex_unlock(&_mutex);

    osp_assert((intptr_t) (headerp->_dqNextp) != 2);

    return datap;
}

/* static */ void
AllocCommonHeader::commonDelete(void *p, void *retAddrp)
{
    char *datap;
    AllocCommonHeader *headerp;
    uint32_t ix;
    
    datap = (char *) p;

    datap -= sizeof(AllocCommonHeader);
    headerp = (AllocCommonHeader *)datap;

#if 0
    printf("FREE block at %p with size 0x%x allocRet=%p deleteRet=%p\n",
           p, headerp->_size, headerp->_retAddrp, retAddrp);
#endif
    osp_assert(headerp->_magic = _magicAlloc);
    headerp->_magic = _magicFree;
    
    memset(p, 0xab, headerp->_size);

    pthread_mutex_lock(&_mutex);
    ix = hashIx(headerp->_retAddrp);
    osp_assert(_hash[ix].count() > 0);
    _hash[ix].remove(headerp);
    // headerp->_delRetAddrp = retAddrp;
    pthread_mutex_unlock(&_mutex);

    free(datap);
}

// #define MLK_NEW
#undef MLK_NEW

#ifdef MLK_NEW
void *
operator new(size_t size, const std::nothrow_t) throw()
{
    return AllocCommonHeader::commonNew(size, __builtin_return_address(0));
}

void *
operator new(size_t size) throw(std::bad_alloc)
{
    return AllocCommonHeader::commonNew(size, __builtin_return_address(0));
}

void *
operator new[](size_t size, const std::nothrow_t) throw()
{
    return AllocCommonHeader::commonNew(size, __builtin_return_address(0));
}

void *
operator new[](size_t size) throw(std::bad_alloc)
{
    return AllocCommonHeader::commonNew(size, __builtin_return_address(0));
}

void
operator delete(void *p) throw()
{
    AllocCommonHeader::commonDelete(p, __builtin_return_address(0));
}

void
operator delete[](void *p) throw()
{
    AllocCommonHeader::commonDelete(p, __builtin_return_address(0));
}
#endif /* MLK_NEW */
