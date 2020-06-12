#ifndef _DISP_H_ENV__
#define _DISP_H_ENV__ 1

#include <pthread.h>
#include "lock.h"
#include "dqueue.h"

class Task;

class Disp {
 public:
    static const uint32_t _maxDisps = 128;
    static uint32_t _dispIx;       /* unlocked references */
    static std::atomic<uint32_t> _dispAddIx;
    static Disp *_dispArray[_maxDisps];

    class QueueElt {
    public:
        QueueElt *_dqNextp;
        QueueElt *_dqPrevp;
        Task *_taskp;
    };

    class Queue {
    public:
        dqueue<QueueElt> _queue;
        SpinLock _lock;

        void append(Task *taskp);

        /* unlocked */
        uint32_t count() {
            return _queue.count();
        }

        void append(QueueElt *eltp) {
            _lock.take();
            _queue.append(eltp);
            _lock.release();
        }

        QueueElt *pop() {
            QueueElt *eltp;

            _lock.take();
            eltp = _queue.pop();
            _lock.release();

            return eltp;
        }
    };

    Queue _queue;

    static void *dispStartRoutine(void *adisp) {
        Disp *disp = (Disp *) adisp;
        disp->dispatchTasks();
        return NULL;
    }

    void dispatchTasks();

    void append(Task *taskp);

    static void queue(Task *taskp);

    Disp() {
        uint32_t ix;
        pthread_t junkId;
        pthread_create(&junkId, NULL, &dispStartRoutine, this);
        ix = _dispAddIx++;      /* atomic by type dcl */
        _dispArray[ix] = this;
    }

    void pause() {
        return;
    }
};

#endif /*  _DISP_H_ENV__ */
