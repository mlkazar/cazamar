#ifndef __CDISP_H_ENV__
#define __CDISP_H_ENV__ 1

#include "cthread.h"
#include "dqueue.h"

class CDisp;
class CDispHelper;
class CDispTask;
class CDispGroup;

/* One of these for each task.  Must be subclassed, since requires a
 * real start function to do anything useful.  When scheduled after
 * being queued to a group, its start function is invoked.
 */
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
    static const uint8_t _queueGroupPaused = 4;

    CDisp *_disp;
    CDispGroup *_group;
    CDispHelper *_helperp;              /* valid once active */

    /* in active, pending or paused queue */
    CDispTask *_dqNextp;
    CDispTask *_dqPrevp;
    uint8_t _inQueue;

    CDisp *getDisp() {
        return _disp;
    }

    CDispGroup *getGroup() {
        return _group;
    }

    CDispTask() {
        _group = NULL;
        _helperp = NULL;
        _inQueue = _queueNone;
        _disp = NULL;
    }

    virtual int32_t start() = 0;

    virtual ~CDispTask();
};

/* One of these per helper task.  These are the tasks that perform the
 * task execution.
 */
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

/* for retrieving statistics */
class CDispStats {
 public:
    uint32_t _activeHelpers;
    uint32_t _availableHelpers;
    uint32_t _activeTasks;
    uint32_t _pendingTasks;

    CDispStats() {
        memset(this, 0, sizeof(CDispStats));
    }
};

/* Usually one of these per process.  It contains a set of
 * CDiskGroups, each of which is where tasks are queued.
 */
class CDisp {
 public:
    friend class CDispGroup;
    friend class CDispHelper;
    friend class CDispTask;

 private:
    uint8_t _nhelpers;    /* # of helpers */

    /* count of number of runnable tasks, including actually running, but not including
     * any tasks that have been moved to the pausedTasks queue.
     */
    uint32_t _activeCount;

    /* helper tasks actually running tasks */
    dqueue<CDispHelper> _activeHelpers;

    /* helpers waiting for tasks to perform */
    dqueue<CDispHelper> _availableHelpers;

    /* tasks currently running on a helper */
    dqueue<CDispTask> _activeTasks;

    /* tasks waiting for an available helper */
    dqueue<CDispTask> _pendingTasks;

    /* all groups */
    dqueue<CDispGroup> _allGroups;

    /* note that some tasks may be on a group paused list as well, if
     * a specific group is paused.  If the entire dispatcher is
     * paused, the tasks are just sitting in pending and we just wait
     * until the helpers get started again.
     */

    /* is this still used??? */
    // uint8_t _waitingForActive;

 public:
    uint8_t _waitingForActive;
    CThreadMutex _lock;
    CThreadCV _activeCv;

 CDisp() : _activeCv(&_lock) {
        _waitingForActive = 0;
        _activeCount = 0;
    }

 private:
    int32_t queueTask(CDispTask *taskp, int head=0);

    int isAllIdleNL() {
        return (_activeTasks.count() == 0 &&
                _pendingTasks.count() == 0 &&
                _activeHelpers.count() == 0);
    }

    int isAllDoneNL();

    void tryDispatches();

 public:
    void getStats( CDispStats *statsp);

    int32_t init(uint32_t ntasks);

    int32_t pause( int noWait=0);

    int32_t stop( int noWait=0);

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

    int isAllIdle() {
        int rcode;
        _lock.take();
        rcode = isAllIdleNL();
        _lock.release();

        return rcode;
    }

    void unthreadGroup(CDispGroup *group);
};

class CDispGroup {
    friend class CDisp;
    friend class CDispHelper;
    friend class CDispTask;

    typedef void CompletionProc(CDisp *cdisp, void *contextp);

    typedef enum {
        _STOPPED = 1,
        _PAUSED = 2,
        _RUNNING = 3} Mode;

    /* count of number of runnable tasks, including actually running, but not including
     * any tasks that have been moved to the pausedTasks queue.
     */
    uint32_t _activeCount;

    CDispGroup::CompletionProc *_completionProcp;
    void *_completionContextp;
    CDisp *_cdisp;
    CThreadCV _activeCV;     /* associated with cdisp's _lock mutex */
    uint8_t _activeWaiting;
    dqueue<CDispTask> _pausedTasks;     /* where group paused tasks go */
    Mode _runMode;

 public:
    /* for the list of all groups in a dispatcher */
    CDispGroup *_dqNextp;
    CDispGroup *_dqPrevp;
    
 public:
    CDispGroup() : _activeCV(NULL) {
        _completionProcp = NULL;
        _completionContextp = NULL;
        _runMode = _RUNNING;
    }

    ~CDispGroup() {
        CDisp *disp = _cdisp;
        disp->unthreadGroup(this);
    }

    void init(CDisp *disp) {
        _cdisp = disp;
        _activeCount = 0;
        _activeWaiting = 0;
        _activeCV.setMutex(&disp->_lock);
        disp->_allGroups.append(this);

        /* dispatcher may have stopped, so when creating a new dispatcher group,
         * make sure it is running again.
         */
        disp->resume();
    }

    void setCompletionProc(CDispGroup::CompletionProc *procp, void *contextp) {
        _completionProcp = procp;
        _completionContextp = contextp;
    }


    int32_t queueTask(CDispTask *taskp, int head=0);

    int isAllDoneNL() {
        return (_activeCount == 0 && _pausedTasks.count() == 0);
    }

    int isAllIdleNL() {
        return (_activeCount == 0);
    }

    void stop(int noWait=0);

    void resume();

    void pause(int noWait = 0);

    int isAllDone() {
        int rcode;

        _cdisp->_lock.take();
        rcode = isAllDoneNL();
        _cdisp->_lock.release();

        return rcode;
    }

    int isAllIdle() {
        int rcode;

        _cdisp->_lock.take();
        rcode = isAllIdleNL();
        _cdisp->_lock.release();

        return rcode;
    }

    void checkCompletionNL(CDisp *disp);
};

#endif /* __CDISP_H_ENV__ */
