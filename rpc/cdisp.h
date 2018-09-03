#ifndef __CDISP_H_ENV__
#define __CDISP_H_ENV__ 1

#include "cthread.h"
#include "dqueue.h"

class CDisp;
class CDispHelper;
class CDispTask;
class CDispGroup;

class CDispTask {
    friend class CDisp;
 public:
    /* which queue the task is in (defines use of dqNextp/dqPrevp;
     * active means that task has been assigned to an active helper
     * thread; pending means that task is waiting for an available
     * helper thread.
     */
    static const uint8_t _queueNone = 1;
    static const uint8_t _queueActive = 2;
    static const uint8_t _queuePending = 3;

    CDisp *_disp;
    CDispGroup *_group;
    CDispHelper *_helperp;              /* valid once active */

    /* in children queue, active, or pending */
    CDispTask *_dqNextp;
    CDispTask *_dqPrevp;
    uint8_t _inQueue;

    CDisp *getDisp() {
        return _disp;
    }

    CDispGroup *getGroup() {
        return _group;
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

class CDispStats {
 public:
    uint32_t _activeHelpers;
    uint32_t _availableHelpers;
    uint32_t _activeTasks;
    uint32_t _pendingTasks;
    uint32_t _runMode;

    CDispStats() {
        memset(this, 0, sizeof(CDispStats));
    }
};

class CDisp {
 public:
    friend class CDispGroup;

    typedef void CompletionProc(CDisp *cdisp, void *contextp);

    typedef enum {
        _STOPPED = 1,
        _PAUSED = 2,
        _RUNNING = 3} Mode;
    friend class CDispHelper;
    friend class CDispTask;

 private:
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

 private:
    int32_t queueTask(CDispTask *taskp, int head=0);

    int isAllDoneNL() {
        return (_activeTasks.count() == 0 &&
                _pendingTasks.count() == 0 &&
                _activeHelpers.count() == 0);
    }

    void tryDispatches();

 public:
    void getStats( CDispStats *statsp);

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
        rcode = isAllDoneNL();
        _lock.release();

        return rcode;
    }
};

class CDispGroup {
    friend class CDisp;
    friend class CDispHelper;

    uint32_t _activeCount;
    CDisp::CompletionProc *_completionProcp;
    void *_completionContextp;
    CDisp *_cdisp;
    CThreadCV _activeCV;     /* associated with cdisp's _lock mutex */
    uint8_t _activeWaiting;
    
 public:
    CDispGroup() : _activeCV(NULL) {
        _completionProcp = NULL;
        _completionContextp = NULL;
    }

    void init(CDisp *disp) {
        _cdisp = disp;
        _activeCount = 0;
        _activeWaiting = 0;
        _activeCV.setMutex(&disp->_lock);

        /* dispatcher may have stopped, so when creating a new dispatcher group,
         * make sure it is running again.
         */
        disp->resume();
    }

    void setCompletionProc(CDisp::CompletionProc *procp, void *contextp) {
        _completionProcp = procp;
        _completionContextp = contextp;
    }


    int32_t queueTask(CDispTask *taskp, int head=0);

    int isAllDoneNL()
    {
        int rcode;
        
        if (_activeCount == 0)
            rcode = 1;
        else
            rcode = 0;
        return rcode;
    }

    void stop() {
        _cdisp->stop();
    }

    void resume() {
        _cdisp->resume();
    }

    void pause() {
        _cdisp->pause();
    }

    int isAllDone() {
        int rcode;

        _cdisp->_lock.take();
        rcode = isAllDoneNL();
        _cdisp->_lock.release();

        return rcode;
    }

    void checkCompletionNL(CDisp *disp);
};

#endif /* __CDISP_H_ENV__ */
