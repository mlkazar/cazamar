#ifndef __OSP_TIMER_H_ENV__
#define __OSP_TIMER_H_ENV__ 1

#include "osp.h"
#include "dqueue.h"
#include "cthread.h"

#include <pthread.h>
#include <sys/time.h>

/* The rules for managing the timers are pretty complex.  When a timer hasn't been
 * canceled, it still has the original ref count of 1 from its creation.  The 
 * refCount goes up by 1 if it is in a callback.
 *
 * The initial refCount disappears iff the _canceled flag is off.
 */

class OspTimer {
 public:
    typedef void Callback(OspTimer *timeoutp, void *contextp);

    static pthread_once_t _once;
    static pthread_mutex_t  _mutex;
    static dqueue<OspTimer> _allTimers;
    static CThreadHandle *_timerHandlep;
    static int _sleeping;
    static CThread _dummyCThread;
    static pthread_cond_t _cv;

    uint8_t _canceled;
    uint8_t _inQueue;
    struct timespec _expires;
    Callback *_procp;
    void *_contextp;
    int32_t _refCount;

 public:
    OspTimer *_dqNextp;
    OspTimer *_dqPrevp;

    OspTimer() {
        return;
    }

    static void initSys();

    void init(uint32_t timerMs, Callback *procp, void *contextp);

    void helper(void *contextp);

    void holdNL() {
        _refCount++;
    }

    void releaseNL();

    void cancel();

    ~OspTimer();

    int canceled() {
        return _canceled;
    }
};


#endif /*  __OSP_TIMER_H_ENV__ */
