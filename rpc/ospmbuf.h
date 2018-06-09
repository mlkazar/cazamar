#ifndef __OSP_MBUF_H_ENV__
#define __OSP_MBUF_H_ENV__ 1

#include <sys/types.h>
#include <string.h>

#include "osptypes.h"

class OspMBuf {
 private:
    /* for the buffer */
    uint32_t _allocBytes;
    char *_allocDatap;

 public:
    /* actual data segment within the buffer */
    char *_datap;
    uint32_t _dataBytes;

    /* links */
    OspMBuf *_dqNextp;
    OspMBuf *_dqPrevp;

 public:
    static const uint32_t _defaultSize = 1024;

    static OspMBuf *alloc(uint32_t count);

    /* return how many bytes remain at the end of the buffer */
    uint32_t bytesAtEnd() {
        uint32_t headBytes;

        headBytes = (uint32_t) (_datap - _allocDatap);
        osp_assert(_allocBytes >= headBytes + _dataBytes);
        return _allocBytes - headBytes - _dataBytes;
    }

    /* return how many bytes of data are present in the buffer */
    uint32_t dataBytes() {
        return _dataBytes;
    }

    char *data() {
        return _datap;
    }

    static void freeList(OspMBuf *abufp) {
        OspMBuf *nbufp;
        for(; abufp; abufp = nbufp) {
            nbufp = abufp->_dqNextp;
            delete abufp;
            abufp = nbufp;
        }
    }

    /* remove count bytes from the start of the buffer, returning a pointer to
     * those bytes.
     */
    char *popNBytes(uint32_t count) {
        char *datap;

        if (count > _dataBytes)
            return NULL;

        datap = _datap;
        _datap += count;
        _dataBytes -= count;
        return datap;
    }

    void reset() {
        _datap = _allocDatap;
        _dataBytes = 0;
    }

    void pushNBytes(char *datap, uint32_t count) {
        osp_assert(count <= bytesAtEnd());
        memcpy(_datap + _dataBytes, datap, count);
        _dataBytes += count;
    }

    char *pushNBytesNoCopy(uint32_t count) {
        char *datap;

        osp_assert(count <= bytesAtEnd());
        datap = _datap + _dataBytes;
        _dataBytes += count;
        return datap;
    }

    ~OspMBuf() {
        delete [] _allocDatap;
    }
};

#endif /* __OSP_MBUF_H_ENV__ */
