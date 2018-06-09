#include "cthread.h"

void
CThreadHandle::init(CThread::StartMethod startMethod, CThread *threadp, void *contextp)
{
    int code;

    _startMethod = startMethod;
    _threadp = threadp;
    _contextp = contextp;

    code = pthread_create(&_pthreadId, NULL, &CThreadHandle::startWrapper, this);
}

/* static */ void *
CThreadHandle::startWrapper(void *ahandlep)
{
    CThreadHandle *handlep = (CThreadHandle *) ahandlep;

    ((handlep->_threadp)->*(handlep->_startMethod))(handlep->_contextp);

    pthread_exit(NULL);
}

int32_t
CThreadPipe::write(const char *bufferp, int32_t count)
{
    int32_t endPos;
    int32_t tcount;
    int32_t bytesThisTime;
    int32_t bytesCopied;

    bytesCopied = 0;
    _lock.take();
    osp_assert(!_eof);
    endPos = _pos + _count;
    while(count > 0) {
        if (endPos >= (signed) _maxBytes)
            endPos -= _maxBytes;

        /* don't copy more than requested */
        bytesThisTime = count;

        /* don't copy more than fit in the buffer */
        tcount = _maxBytes - _count;
        if (bytesThisTime > tcount)
            bytesThisTime = tcount;

        /* don't copy beyond the wrap */
        tcount = _maxBytes - _pos;
        if (bytesThisTime > tcount)
            bytesThisTime = tcount;

        if (bytesThisTime <= 0) {
            _cv.wait();
            continue;
        }

        memcpy(_data+endPos, bufferp, bytesThisTime);
        bufferp += bytesThisTime;
        bytesCopied += bytesThisTime;
        count -= bytesThisTime;
        endPos += bytesThisTime;
        _count += bytesThisTime;

        /* wakeup any readers, since we've added some data */
        _cv.broadcast();

    }
    _lock.release();

    return bytesCopied;
}

int32_t
CThreadPipe::read(char *bufferp, int32_t count)
{
    int32_t tcount;
    int32_t bytesThisTime;
    int32_t bytesCopied;

    bytesCopied = 0;

    _lock.take();
    while( 1) {
        if (_count == 0 || count == 0) {
            /* no more data left, or no more room left in receiver */
            if (bytesCopied == 0) {
                if (_eof)
                    break;
                _cv.wait();
                continue;
            }
            else {
                /* no more bytes, but we have returned some already, so we're
                 * done.
                 */
                break;
            }
        }

        if (_pos >= _maxBytes)
            _pos -= _maxBytes;

        /* don't read more than requested */
        bytesThisTime = count;

        /* don't read more bytes than present in the buffer */
        if (bytesThisTime > (signed) _count)
            bytesThisTime = _count;

        /* don't go past buffer wrap in one memcpy */
        tcount = _maxBytes - _pos;
        if (tcount < bytesThisTime)
            bytesThisTime = tcount;

        memcpy(bufferp, _data + _pos, bytesThisTime);
        _pos += bytesThisTime;
        if (_pos >= _maxBytes) {
            /* we try not to wrap, so at most this may be equal to the end */
            osp_assert(_pos == _maxBytes);
            _pos -= _maxBytes;
        }
        _count -= bytesThisTime;
        count -= bytesThisTime;
        bufferp += bytesThisTime;
        bytesCopied += bytesThisTime;

        /* if someone may have been waiting for space into which to write,
         * let them know there's space available now.
         */
        _cv.broadcast();
    }
    _lock.release();

    return bytesCopied;
}

/* discard available data until we see the EOF flag go on */
void
CThreadPipe::waitForEof()
{
    printf("**waitForEof pipe=%p\n", this);
    _lock.take();
    while(1) {
        if (_eof)
            break;
        if (_count > 0) {
            /* discard this data */
            _pos += _count;
            if (_pos >= _maxBytes) {
                _pos -= _maxBytes;
            }
            _count = 0;
            _cv.broadcast();    /* let people know there's more room */
        }
        _cv.wait();
    }
    _lock.release();
}

void
CThreadPipe::eof()
{
    _lock.take();
    printf("**setting eof for pipe=%p\n", this);
    _eof = 1;
    _lock.release();

    _cv.broadcast();
}
