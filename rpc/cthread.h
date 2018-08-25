#ifndef __CTHREAD_H_ENV__
#define __CTHREAD_H_ENV__ 1

#include <pthread.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

#include "osp.h"

class CThread {
 public:
    typedef void (CThread::*StartMethod)(void *contextp);

    CThread() {};

    static uint64_t self() {
#ifdef __linux__
        return syscall(SYS_gettid);
#else
        /* MacOS */
        return (uint64_t) pthread_mach_thread_np(pthread_self());
#endif
    }
};

class CThreadMutex {
    friend class CThreadCV;
 private:
    pthread_mutex_t _pthreadMutex;
    uint64_t _ownerId;

 public:
    CThreadMutex() {
        int32_t code;
        _ownerId = 0;
        code = pthread_mutex_init(&_pthreadMutex, NULL);
        osp_assert(code == 0);
    }

    /* returns zero if we got the lock, non-zero otherwise */
    int tryTake() {
        uint64_t me;
        int code;
        me = CThread::self();
        osp_assert(me != _ownerId);
        code = pthread_mutex_trylock(&_pthreadMutex);
        if (code == 0) {
            _ownerId = me;
        }
        return code;
    }

    void take() {
        uint64_t me;
        me = CThread::self();
        osp_assert(me != _ownerId);
        pthread_mutex_lock(&_pthreadMutex);
        _ownerId = me;
    }

    void release() {
        uint64_t me;
        me = CThread::self();

        osp_assert(_ownerId == me);
        _ownerId = 0;
        pthread_mutex_unlock(&_pthreadMutex);
    }

    pthread_mutex_t *getPthreadMutex() {
        return &_pthreadMutex;
    }

    ~CThreadMutex() {
        pthread_mutex_destroy(&_pthreadMutex);
    }
};

class CThreadCV {
 private:
    pthread_cond_t _pthreadCV;
    CThreadMutex *_mutexp;

 public:
    /* if you don't have a convenient pointer to the mutex at construction time,
     * use NULL, and then call setMutex before using the CV.
     */
    CThreadCV(CThreadMutex *mutexp) {
        int32_t code;

        _mutexp = mutexp;
        code = pthread_cond_init(&_pthreadCV, NULL);
    }

    void setMutex(CThreadMutex *mutexp) {
        _mutexp = mutexp;
    }

    void wait() {
        int code;
        uint64_t me = CThread::self();

        osp_assert(_mutexp != NULL);
        
        osp_assert(_mutexp->_ownerId == me);
        _mutexp->_ownerId = 0;
        code = pthread_cond_wait(&_pthreadCV, _mutexp->getPthreadMutex());
        _mutexp->_ownerId = me;
        if (code)
            printf("cond wait code=%d\n", code);
    }

    void broadcast() {
        int32_t code;
        code = pthread_cond_broadcast(&_pthreadCV);
        if (code) {
            printf("broadcast failed code=%d\n", code);
        }
    }

    void signalOne() {
        pthread_cond_signal(&_pthreadCV);
    }

    ~CThreadCV() {
        pthread_cond_destroy(&_pthreadCV);
    }
};

/* a unidirectional pipe */
class CThreadPipe {
 private:
    static const uint32_t _maxBytes = 4096;
    uint32_t _count;
    uint32_t _pos;
    uint8_t _eof;
    char _data[_maxBytes];

    /* lock protecting shared variables.  People wait for CV when
     * buffer is full on write, or buffer is empty on read.  Both
     * don't happen at the same time, so we don't bother having two
     * CVs.
     */
    CThreadMutex _lock;
    CThreadCV _cv;

 public:
    CThreadPipe() : _cv(&_lock) {
        reset();
    }

    void reset() {
        _lock.take();
        _count = 0;
        _pos = 0;
        _eof = 0;
        _lock.release();
    }

    /* write data into the pipe; don't return until all data written */
    int32_t write(const char *bufferp, int32_t count);

    /* read data from the pipe, return any non-zero available data; return
     * 0 bytes if EOF was called on the other side.
     */
    int32_t read(char *bufferp, int32_t count);

    /* called by writer when no more data will be sent */
    void eof();

    /* return true if at EOF */
    int atEof() {
        return _eof;
    }

    void waitForEof();

    int32_t count() {
        int32_t tcount;
        _lock.take();
        tcount = _count;
        _lock.release();
        return tcount;
    }
};

class CThreadHandle {
 private:
    pthread_t _pthreadId;
    CThread::StartMethod _startMethod;
    CThread *_threadp;
    void *_contextp;
    uint64_t _selfId;

 public:
    void init(CThread::StartMethod startMethod, CThread *threadp, void *contextp);

    static void *startWrapper(void *contextp);
};

#endif /* __CTHREAD_H_ENV__ */
