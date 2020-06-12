#include "rbufstr.h"

/* simple stream implementation of an rbuf.  It's possible that we'd have a more
 * efficient implementation if we use a set of contiguous buffers, and this
 * interface allows that, but right now, we're just doing something easy.
 */

/* append data to the end of the rbuf, allocating more storage
 * if required.  There is no need for the data to be allocated
 * contiguously.
 */
void 
RbufStr::append(char *datap, uint32_t len)
{
    _data.append(const_cast<char *>(datap), len);
}

void 
RbufStr::prepend(char *datap, uint32_t len)
{
    _data.insert(0, const_cast<char *>(datap), len);
    _readPos += len;
}

/* throw away all data in the rbuf */
void
RbufStr::erase()
{
    _data.clear();
}

/* reset the current read position in the rbuf */
uint32_t
RbufStr::setReadPosition(uint32_t pos)
{
    if (pos > _data.length())
        pos = _data.length();
    _readPos = pos;
    return pos;
}

uint32_t
RbufStr::getReadPosition()
{
    return _readPos;
}

/* remove bytes and perhaps buffers from the start of the rbuf; adjust the
 * read position to stay at the same point in the rbuf.
 */
void
RbufStr::pop(uint32_t nbytes)
{
    _data.erase(0, nbytes);
    if (_readPos < nbytes)
        _readPos = 0;
    else
        _readPos -= nbytes;
}


/* copy out up to nbytes from the buffer from its current position */
uint32_t
RbufStr::read(char *bufferp, uint32_t nbytes)
{
    uint32_t endPos = nbytes + _readPos;
    uint32_t tlen;
    
    if (endPos > _data.length()) {
        endPos = _data.length();
    }

    std::string tstr;

    tstr = _data.substr(_readPos, endPos);
    tlen = endPos - _readPos;
    memcpy(bufferp, _data.c_str(), tlen);
    _readPos += tlen;
    return tlen;
}
    

/* like read, and proceeding from the read point, but just returns
 * pointers into the rbuf and lengths, and increments the rbuf read
 * pointer.
 *
 * So, you can use repeated calls to scan to iterate over all the data in an
 * rbuf, say to call write on each segment.  Normally returns the # of bytes
 * in the next contiguous segment, but returns 0 at EOF, and negative values
 * if an error occurs.
 */
uint32_t
RbufStr::scan(char **datapp)
{
    uint32_t tlen;
    uint32_t returnLen;
    const char *tp;

    tlen = _data.length();
    if (_readPos >= tlen) {
        *datapp = NULL;
        return 0;
    }

    /* otherwise, return what we have */
    tp = _data.data();
    *datapp = const_cast<char *>(tp)+_readPos;
    returnLen = tlen - _readPos;
    _readPos = tlen;

    return returnLen;
}
