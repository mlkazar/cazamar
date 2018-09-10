#include "cthread.h"
#include "cdisp.h"

void
CDispHelper::start(void *contextp)
{
    CDisp *disp = (CDisp *) contextp;
    CDispTask *taskp;
    CDispGroup *group;

    disp->_lock.take();
    _inQueue = _queueAvailable;
    disp->_availableHelpers.append(this);
    if (disp->_pendingTasks.count()) {
        /* don't need to keep lock since we'll always call tryDispatches again
         * after every append to _available.
         */
        disp->tryDispatches();
    }

    while(1) {
        if (_taskp == NULL) {
            _cv.wait();
            continue;
        }

        /* we still have the cdisp lock, pull the task out; it was
         * already put in the active queue by tryDispatches.
         */
        taskp = _taskp;
        _taskp = NULL;
        taskp->_disp = disp;
        disp->_lock.release();

        group = taskp->_group;

        taskp->start();

        disp->_lock.take();

        /* remove from queue in case destructor doesn't */
        osp_assert(taskp->_inQueue == CDispTask::_queueActive);
        disp->_activeTasks.remove(taskp);
        if ( disp->_waitingForActive) {
            disp->_waitingForActive = 0;
            disp->_activeCv.broadcast();
        }
        taskp->_inQueue = CDispTask::_queueNone;

        osp_assert(_inQueue == CDispHelper::_queueActive);
        disp->_activeHelpers.remove(this);
        /* no need to change queue specifier, since we're changing it again below */

        /* and put ourselves back in the available queue; once we're here, we can get
         * a task queued to _taskp.
         */
        disp->_availableHelpers.append(this);
        _inQueue = _queueAvailable;

        /* now assign new tasks to helpers, since we've freed a helper.  Note that
         * the helper assigned may be us, in which case we'll find the assigned
         * task above, instead of going to sleep.
         */
        disp->tryDispatches();

        disp->_lock.release();

        delete taskp;

        disp->_lock.take();
        osp_assert(group->_activeCount > 0);
        group->_activeCount--;

        group->checkCompletionNL(disp);
    }
}

void
CDispGroup::checkCompletionNL(CDisp *disp)
{
    /* check if we should call completion proc; make sure it is called exactly once; do this
     * after doing one dispatch, so that we don't trigger the completion callback before
     * the first task is submitted.
     *
     * Note that after it triggers, you must call setCompletionProc again before it can
     * trigger again.
     */
    if (isAllDoneNL() && _completionProcp != NULL) {
        CDisp::CompletionProc *procp = _completionProcp;
        void *contextp = _completionContextp;
        _completionProcp = NULL;
        _completionContextp = NULL;

        disp->_lock.release();
        procp(disp, contextp);
        disp->_lock.take();
    }
}

/* must be called with lock held */
void
CDisp::tryDispatches()
{
    CDispHelper *helperp;
    CDispTask *taskp;
    CDispGroup *group;

    while(_pendingTasks.count() && _availableHelpers.count()) {
        if (_runMode == _PAUSED)
            break;
        /* assign the pending work item to the available helper */
        taskp = _pendingTasks.pop();

        /* if stopped, take the pending tasks and just delete them */
        if (_runMode == _STOPPED) {
            taskp->_inQueue = CDispTask::_queueNone;
            group = taskp->_group;
            osp_assert(group->_activeCount > 0);
            group->_activeCount--;
            _lock.release();
            delete taskp;
            _lock.take();
            continue;
        }

        helperp = _availableHelpers.pop();
        _activeTasks.append(taskp);
        taskp->_inQueue = CDispTask::_queueActive;
        _activeHelpers.append(helperp);
        helperp->_inQueue = CDispHelper::_queueActive;

        /* assign helper */
        osp_assert(helperp->_taskp == NULL);
        helperp->_taskp = taskp;
        helperp->_cv.broadcast();
    }
}

/* called with disp lock held */
int32_t
CDisp::queueTask(CDispTask *taskp, int head)
{
    taskp->_disp = this;

    if (_runMode == _STOPPED) {
        /* drop lock before deleting task, since CDispTask destructor
         * grabs locks.
         */
         _lock.release();
        delete taskp;
        _lock.take();

        return -1;
    }

    if (head)
        _pendingTasks.prepend(taskp);
    else
        _pendingTasks.append(taskp);

    taskp->_inQueue = CDispTask::_queuePending;
    tryDispatches();

    return 0;
}

int32_t
CDispGroup::queueTask(CDispTask *taskp, int head)
{
    int32_t rcode;
    
    _cdisp->_lock.take();
    taskp->_group = this;
    _activeCount++;
    rcode = _cdisp->queueTask(taskp, head);
    _cdisp->_lock.release();

    return rcode;
}

int32_t
CDisp::init(uint32_t ntasks)
{
    uint32_t i;
    CDispHelper *helperp;
    CThreadHandle *hp;

    _ntasks = ntasks;
    for(i=0;i<ntasks;i++) {
        helperp = new CDispHelper(&_lock);
        hp = new CThreadHandle();
        hp->init((CThread::StartMethod) &CDispHelper::start, helperp, this);
    }

    return 0;
}

int32_t
CDisp::stop()
{
    CDispTask *taskp;
    CDispGroup *group;
    _lock.take();
    _runMode = _STOPPED;

    while(1) {
        while((taskp = _pendingTasks.pop()) != NULL) {
            taskp->_inQueue = CDispTask::_queueNone;
            group = taskp->_group;
            osp_assert(group->_activeCount > 0);
            group->_activeCount--;
            _lock.release();
            delete taskp;
            _lock.take();
        }
        if (_activeTasks.count() != 0) {
            _waitingForActive = 1;
            _activeCv.wait();
            continue;
        }
        break;
    }
    _lock.release();

    return 0;
}

int32_t
CDisp::pause()
{
    _lock.take();
    _runMode = _PAUSED;
    while(1) {
        if (_activeTasks.count() != 0) {
            _waitingForActive = 1;
            _activeCv.wait();
            continue;
        }
        else
            break;
    }
    /* may leave some tasks in pending queue until resume is called */
    _lock.release();
    return 0;
}

int32_t
CDisp::resume()
{
    _lock.take();
    if (_runMode == _STOPPED) {
        /* start the dispatcher up again */
        _runMode = _RUNNING;
        tryDispatches();
    }
    else if (_runMode == _PAUSED) {
        _runMode = _RUNNING;
        tryDispatches();
    }
    _lock.release();
    return 0;
}

CDispTask::~CDispTask() {
    CDisp *disp = _disp;


    /* if _disp isn't even set yet, we've never been queued */
    if (disp) {
        disp->_lock.take();

        /* remove from queue */
        if (_inQueue == _queueActive) {
            disp->_activeTasks.remove(this);
        }
        else if (_inQueue == _queuePending) {
            disp->_pendingTasks.remove(this);
        }
        _inQueue = _queueNone;

        disp->_lock.release();
    }
}

void
CDisp::getStats( CDispStats *statsp) {
    _lock.take();
    statsp->_activeHelpers = _activeHelpers.count();
    statsp->_availableHelpers = _availableHelpers.count();
    statsp->_activeTasks = _activeTasks.count();
    statsp->_pendingTasks = _pendingTasks.count();
    statsp->_runMode = _runMode;
    _lock.release();
}
