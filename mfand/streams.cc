#include "streams.h"

/* XXX Note that error recovery is not implemented, beyond aborting on the first failed
 * create call.
 */

FileInStream::FileInStream(FILE *filep)
{
    _inFilep = filep;

    fillBuffer();
}

void
FileInStream::fillBuffer()
{
    int tc;

    tc = fgetc(_inFilep);
    if (tc < 0)
        _buffer = 0;
    else
        _buffer = tc;
}

int
FileInStream::top()
{
    return _buffer;
}

void
FileInStream::next()
{
    fillBuffer();
}

