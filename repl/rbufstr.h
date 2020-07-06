#ifndef __RBUFSTR_H_ENV__
#define __RBUFSTR_H_ENV__ 1

#include "rbuf.h"
#include <string>

/* interface for buffers */
class RbufStr : public Rbuf {
    uint32_t _readPos;
    std::string _data;
    
public:
    /* use shared pointers */
    RbufStr() {
        _readPos = 0;
    };

    /* append data to the end of the rbuf, allocating more storage
     * if required.  There is no need for the data to be allocated
     * contiguously.
     */
    void append(char *datap, uint32_t len);

    /* put these characters at the start of the buffer */
    void prepend(char *datap, uint32_t len);

    /* throw away all data in the rbuf */
    void erase();

    /* reset the current read position in the rbuf to value specified.  If out of bounds,
     * set to the end.
     */
    uint32_t setReadPosition(uint32_t pos);

    /* get the current read position */
    uint32_t getReadPosition();

    /* remove bytes and perhaps buffers from the start of the rbuf */
    void pop(uint32_t nbytes);

    /* copy out up to nbytes from the buffer from its current position */
    uint32_t read(char *bufferp, uint32_t nbytes);

    /* like read, and proceeding from the read point, but just returns
     * pointers into the rbuf and lengths, and increments the rbuf read
     * pointer.
     *
     * So, you can use repeated calls to scan to iterate over all the data in an
     * rbuf, say to call write on each segment.  Normally returns the # of bytes
     * in the next contiguous segment, but returns 0 at EOF, and negative values
     * if an error occurs.
     */
    uint32_t scan(char **datapp);
};

#endif /* __RBUFSTR_H_ENV__ */
