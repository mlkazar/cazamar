#ifndef __CDISP_H_ENV__
#define __CDISP_H_ENV__ 1

#include "cthread.h"
#include "dqueue.h"

class CDisp;
class CDispHelper;
class CDispTask;

class CDispTask {
    friend class CDisp;
 public:
    /* which queue the task is in (defines use of dqNextp/dqPrevp;
     * active means that task has been assigned to an active helper
     * thread; pending means that task is waiting for an available
     * helper thread.
     */
    static const uint8_t _queueNone = 0;
    static const uint8_t _queueActive = 1;
    static const uint8_t _queuePending = 2;

    CDisp *_disp;
    CDispHelper *_helperp;              /* valid once active */

    /* in children queue, active, or pending */
    CDispTask *_dqNextp;
    CDispTask *_dqPrevp;
    uint8_t _inQueue;

    CDisp *getDisp() {
        return _disp;
    }

    virtual int32_t start() = 0;

    virtual ~CDispTask();
};

class CDispHelper : public CThread {
    friend class CDisp;

 public:
    static const uint8_t _queueActive = 1;
    static const uint8_t _queueAvailable = 2;

    CThreadHandle *_cthreadp;
    CDispTask *_taskp;

    /* in active or available */
    uint8_t _inQueue;
    CDispHelper *_dqNextp;
    CDispHelper *_dqPrevp;
    CThreadCV _cv;
    CDisp *_disp;

    CDispHelper(CThreadMutex *lockp): _cv(lockp) {
        _cthreadp = NULL;
        _taskp = NULL;
        _dqNextp = _dqPrevp = NULL;
        _inQueue = 0;
        _disp = NULL;   /* filled in by user */
    }

    void start(void *contextp);
};

class CDisp {
    typedef enum {
        _STOPPED = 1,
        _PAUSED = 2,
        _RUNNING = 3} Mode;
    friend class CDispHelper;
    friend class CDispTask;

    uint8_t _ntasks;

    dqueue<CDispHelper> _activeHelpers;
    dqueue<CDispHelper> _availableHelpers;

    dqueue<CDispTask> _activeTasks;
    dqueue<CDispTask> _pendingTasks;

    Mode _runMode;
    uint8_t _waitingForActive;

 public:
    CThreadMutex _lock;
    CThreadCV _activeCv;

 CDisp() : _activeCv(&_lock) {
        _runMode = _RUNNING;
        _waitingForActive = 0;
    }

    int32_t queueTask(CDispTask *taskp);

    int32_t init(uint32_t ntasks);

    int32_t pause();

    int32_t stop();

    int32_t resume();

    /* return true if still executing tasks */
    int isActive() {
        int rcode;

        _lock.take();
        rcode = (_activeTasks.count() != 0);
        _lock.release();
        return rcode;
    }

    /* return true if all done, even if paused */
    int isAllDone() {
        int rcode;

        _lock.take();
        rcode = (_activeTasks.count() == 0 && _pendingTasks.count() == 0);
        _lock.release();

        return rcode;
    }

    void tryDispatches();
};

#endif /* __CDISP_H_ENV__ */
