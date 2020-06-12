#ifndef __RBUF_H_ENV__
#define __RBUF_H_ENV__ 1

#include "osptypes.h"
#include "dqueue.h"

/* interface for buffers; note that read position is used and modified by read and scan,
 * but pop, prepend, append all affect the buffer indepdent of the read position, although
 * they modify the read position to keep its location fixed at the same place in the
 * previously existing string, or as close to it as possible.
 */
class Rbuf {
 public:
    /* for convenience, if someone wants to make a queue of these things */
    Rbuf *_dqNextp;
    Rbuf *_dqPrevp;

    /* append data to the end of the rbuf, allocating more storage
     * if required.  There is no need for the data to be allocated
     * contiguously.
     */
    virtual void append(char *datap, uint32_t len) = 0;

    virtual void prepend(char *datap, uint32_t len) = 0;

    /* throw away all data in the rbuf */
    virtual void erase()= 0;

    /* set the read position to the specified value */
    virtual uint32_t setReadPosition(uint32_t pos) = 0;

    /* get the current read position */
    virtual uint32_t getReadPosition() = 0;

    /* remove bytes and perhaps buffers from the start of the rbuf */
    virtual void pop(uint32_t nbytes) = 0;

    /* copy out up to nbytes from the buffer from its current position; update
     * current position until after the data we read.
     */
    virtual uint32_t read(char *bufferp, uint32_t nbytes) = 0;

    /* like read, and proceeding from the read point, but just returns
     * pointers into the rbuf and lengths, and increments the rbuf read
     * pointer.
     *
     * So, you can use repeated calls to scan to iterate over all the data in an
     * rbuf, say to call write on each segment.  Normally returns the # of bytes
     * in the next contiguous segment, but returns 0 at EOF, and negative values
     * if an error occurs.
     */
    virtual uint32_t scan(char **datapp) = 0;
};

#endif /* __RBUF_H_ENV__ */
