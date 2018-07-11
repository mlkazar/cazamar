#include "cthread.h"
#include "cdisp.h"

void
CDispHelper::start(void *contextp)
{
    CDisp *disp = (CDisp *) contextp;
    CDispTask *taskp;
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

        taskp->start();

        disp->_lock.take();
        /* remove from queue in case destructor doesn't */
        osp_assert(taskp->_inQueue == CDispTask::_queueActive);
        disp->_activeTasks.remove(taskp);
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
    }
}

/* must be called with lock held */
void
CDisp::tryDispatches()
{
    CDispHelper *helperp;
    CDispTask *taskp;

    while(_pendingTasks.count() && _availableHelpers.count()) {
        /* assign the pending work item to the available helper */
        taskp = _pendingTasks.pop();
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

int32_t
CDisp::queueTask(CDispTask *taskp)
{
    _lock.take();
    _pendingTasks.append(taskp);
    taskp->_inQueue = CDispTask::_queuePending;
    tryDispatches();
    _lock.release();

    return 0;
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
