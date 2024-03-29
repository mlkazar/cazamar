#ifndef _OSP_H_ENV__
#define _OSP_H_ENV__ 1

#include <sys/types.h>
#include "osptypes.h"
#include <pthread.h>
#include "dqueue.h"

class AllocCommonHeader {
 public:
    static const uint16_t _magicAlloc=0xA00A;
    static const uint16_t _magicFree=0xF00F;

    static const uint32_t _hashSize = 97;
    static pthread_mutex_t _mutex;
    static dqueue<AllocCommonHeader> _hash[_hashSize];

    uint16_t _magic;
    uint16_t _padding;
    uint32_t _size;
    void *_retAddrp;
    // void *_delRetAddrp;
    AllocCommonHeader *_dqNextp;
    AllocCommonHeader *_dqPrevp;

    static uint32_t hashIx(void *x) {
        uint64_t realX = (uint64_t) x;
        return (realX & 0x7FFFFFFF) % _hashSize;
    }

    static void *commonNew(uint32_t size, void *retAddrp);

    static void commonDelete(void *p, void *retAddrp);
};

#define osp_assert(x) do { \
    if (!(x)) {                            \
        osp_panic((char *) #x, (char *) __FILE__, __LINE__);     \
    }                                      \
 } while(0)

extern void osp_panic(char *why, char *filep, int line);

extern uint32_t osp_time_ms();

extern uint32_t osp_time_sec(); /* time since 1970 */

#include "ospmbuf.h"

#endif /* _OSP_H_ENV__ */
