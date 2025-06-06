#include "cthread.h"

CThreadMutex CThread::_libcMutex;

void
CThreadHandle::init(CThread::StartMethod startMethod, CThread *threadp, void *contextp)
{
    _startMethod = startMethod;
    _threadp = threadp;
    _contextp = contextp;

    (void) pthread_create(&_pthreadId, NULL, &CThreadHandle::startWrapper, this);
}

/* static */ void *
CThreadHandle::startWrapper(void *ahandlep)
{
    CThreadHandle *handlep = (CThreadHandle *) ahandlep;
    handlep->_selfId = CThread::self();

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

    endPos = _pos + _count;
    while(count > 0) {
        /* if other side indicated EOF, treat writes as if they're unable to do anything */
        if (_eof) {
            _lock.release();
            printf("cthreadpipe::write at EOF\n");
            return -1;
        }

        if (endPos >= (signed) _maxBytes)
            endPos -= _maxBytes;

        /* don't copy more than requested */
        bytesThisTime = count;

        /* don't copy more than fit in the buffer */
        tcount = _maxBytes - _count;
        if (bytesThisTime > tcount)
            bytesThisTime = tcount;

        /* don't copy beyond the wrap */
        tcount = _maxBytes - endPos;
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
            if (_eof || count == 0) {
                /* we've run out of data and EOF is set, or we've run out of room;
                 * note that we don't have to check _count == 0 with eof case, since
                 * we already know that count==0 or _count == 0.
                 */
                break;
            }
            /* here, we've run out of data, but EOF isn't set yet, and we do have
             * more room in the incoming buffer.  Wait for more data.
             */
            _cv.wait();
            continue;
        }

        /* if _pos was pointing to the end, move it back to the start
         * of the buffer.
         */
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
    _eof = 1;
    _lock.release();

    _cv.broadcast();
}

void
 CThreadCV::broadcast() {
    int32_t code;
    code = pthread_cond_broadcast(&_pthreadCV);
    if (code) {
        printf("broadcast failed code=%d\n", code);
    }
}
