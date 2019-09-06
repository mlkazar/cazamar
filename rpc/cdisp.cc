#include "cthread.h"
#include "cdisp.h"

/* The dispatcher module works as follows.  You create a CDisp
 * structure, representing a pool of worker threads and a set of tasks
 * for the workers to perform.
 *
 * Then each application sharing those CDisp workers creates a
 * CDispGroup, and then queues a CDispTask for that group.
 *
 * The CDispTask gets subclassed with a start function that gets
 * called without any parameters.  The superclass provides a getDisp
 * and getGroup method that return the appropriate pointers to the
 * structures.
 *
 * You can also call setCompletionProc(proc, context) on the group,
 * and the proc will be called when all the tasks queued at the group
 * have all terminated.  It's the caller's responsibility to ensure
 * that there are no race conditions between a completion proc and
 * starting up new tasks.  The completion proc's signature is called
 * with the CDisp and the context.
 *
 * You can call the following methods on the group and/or the
 * dispatcher.
 *
 * stop(int noWait) -- stops all tasks in the group/disp, freeing all
 * the tasks that haven't run yet, and then waiting for any currently
 * executing tasks finish before the groups' completion proc is called
 * for each of the groups with active tasks.  If noWait is false, the
 * call will wait until all tasks have finished execution.
 *
 * pause(int noWait) -- pauses execution of all tasks associated with
 * the dispatcher or the group.  If noWait is false, the call will
 * also wait until any executing tasks finish.
 */

/* Internal: called when a worker is first created, to start
 * dispatching requests.
 */
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
        taskp->_inQueue = CDispTask::_queueNone;
        disp->_activeTasks.remove(taskp);
        osp_assert(group->_activeCount > 0);
        group->_activeCount--;
        osp_assert(disp->_activeCount > 0);
        disp->_activeCount--;

        /* we're about to free a worker, so wakeup anyone waiting for a worker */
        if ( disp->_waitingForActive) {
            disp->_waitingForActive = 0;
            disp->_activeCv.broadcast();
        }
        osp_assert(_inQueue == CDispHelper::_queueActive);
        disp->_activeHelpers.remove(this);

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

        /* check if this group's tasks have completed, and if so, run
         * the dispatcher.
         */
        group->checkCompletionNL(disp);

        disp->_lock.release();

        delete taskp;

        disp->_lock.take();
    }
}

/* dispatch the group's callback */
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
        CDispGroup::CompletionProc *procp = _completionProcp;
        void *contextp = _completionContextp;
        _completionProcp = NULL;
        _completionContextp = NULL;

        disp->_lock.release();
        procp(disp, contextp);
        disp->_lock.take();
    }
}

/* Internal: must be called with lock held.  Match up helper tasks
 * with pending tasks.
 */
void
CDisp::tryDispatches()
{
    CDispHelper *helperp;
    CDispTask *taskp;
    CDispGroup *group;

    while(_pendingTasks.count() && _availableHelpers.count()) {
        /* assign the pending work item to the available helper */
        taskp = _pendingTasks.pop();

        /* if stopped, take the pending tasks and just delete them */
        group = taskp->_group;
        if (group->_runMode == CDispGroup::_STOPPED) {
            taskp->_inQueue = CDispTask::_queueNone;
            osp_assert(group->_activeCount > 0);
            group->_activeCount--;
            osp_assert(_activeCount > 0);
            _activeCount--;
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

/* Internal: called with disp lock held */
int32_t
CDisp::queueTask(CDispTask *taskp, int head)
{
    taskp->_disp = this;

    if (head)
        _pendingTasks.prepend(taskp);
    else
        _pendingTasks.append(taskp);
    _activeCount++;
    /* caller (CDispGroup::queueTask) bumped group's _activeCount field */

    taskp->_inQueue = CDispTask::_queuePending;
    tryDispatches();

    return 0;
}

/* External: called with a task to queue for this group, and a boolean
 * indicating if the task should be queued at the head of the run
 * queue or the tail.
 */
int32_t
CDispGroup::queueTask(CDispTask *taskp, int head)
{
    int32_t rcode;
    
    _cdisp->_lock.take();

    taskp->_group = this;

    /* group has been stopped, so discard the task immediately */
    if (_runMode == _STOPPED) {
        _cdisp->_lock.release();
        delete taskp;
        return -1;
    }

    if (_runMode == _PAUSED) {
        _pausedTasks.append(taskp);
        _cdisp->_lock.release();
        return 0;
    }

    _activeCount++;
    /* group->_activeCount gets bumped by queueTask */
    rcode = _cdisp->queueTask(taskp, head);
    _cdisp->_lock.release();

    return rcode;
}

/* External: called to stop all the tasks associated with a group.  If
 * noWait is set, the call doesn't return until all executing tasks
 * associated with the group have completed.
 */
void
CDispGroup::stop(int noWait) {
    CDisp *cdisp = _cdisp;
    CDispTask *taskp;
    CDispTask *ntaskp;
    CDispGroup *group;
    dqueue<CDispTask> stoppedTasks;

    cdisp->_lock.take();

    _runMode = _STOPPED;

    while(1) {
        /* get rid of all queued tasks for this group */
        for(taskp = cdisp->_pendingTasks.head(); taskp; taskp = ntaskp) {
            /* save next pointer before removing */
            ntaskp = taskp->_dqNextp;

            group = taskp->_group;
            if (group == this) {
                /* remove from pending queue and save it in a local list */
                osp_assert(taskp->_inQueue == CDispTask::_queuePending);
                cdisp->_pendingTasks.remove(taskp);
                taskp->_inQueue = CDispTask::_queueNone;
                stoppedTasks.append(taskp);

                /* decrement active count in group and dispatcher */
                osp_assert(_activeCount > 0);
                _activeCount--;
                osp_assert(cdisp->_activeCount > 0);
                cdisp->_activeCount--;
            }
        }

        /* get rid of any tasks still in the paused queue.  Note that
         * paused tasks don't count in the _activeCount counts.
         */
        for(taskp = _pausedTasks.head(); taskp; taskp=taskp->_dqNextp) {
            taskp->_inQueue = CDispTask::_queueNone;
        }
        stoppedTasks.concat(&_pausedTasks);

        /* free all stopped tasks */
        cdisp->_lock.release();
        while((taskp = stoppedTasks.pop()) != NULL) {
            delete taskp;
        }
        cdisp->_lock.take();

        /* more tasks might have been added while we're sleeping */
        if (cdisp->_pendingTasks.count() != 0)
            continue;

        /* if we're waiting for some tasks to finish in this group, wait for
         * any tasks to finish, and then recheck.
         */
        if (!noWait && !isAllDoneNL()) {
            cdisp->_waitingForActive = 1;
            cdisp->_activeCv.wait();
            continue;
        }
        break;
    }
    
    cdisp->_lock.release();
}

/* pause the group's tasks.  Wait for any associated tasks that are
 * actually running, if noWait is false.
 */
void
CDispGroup::pause(int noWait)
{
    CDisp *cdisp = _cdisp;
    CDispTask *taskp;
    CDispTask *ntaskp;
    CDispGroup *group;

    cdisp->_lock.take();

    _runMode = _PAUSED;

    while(1) {
        /* move the group's tasks out of _pendingTasks into the group's paused
         * list.
         */
        for(taskp = cdisp->_pendingTasks.head(); taskp; taskp = ntaskp) {
            ntaskp = taskp->_dqNextp;
            group = taskp->_group;
            if (group == this) {
                osp_assert(group->_activeCount > 0);
                group->_activeCount--;
                osp_assert(cdisp->_activeCount > 0);
                cdisp->_activeCount--;
                cdisp->_pendingTasks.remove(taskp);
                taskp->_inQueue = CDispTask::_queueGroupPaused;
                _pausedTasks.append(taskp);
            }
        }

        /* if we're waiting for some tasks to finish in this group, wait for
         * any tasks to finish, and then recheck.
         */
        if (!noWait && !isAllIdleNL()) {
            cdisp->_waitingForActive = 1;
            cdisp->_activeCv.wait();
            continue;
        }
        break;
    }
    
    cdisp->_lock.release();
}

void
CDispGroup::resume()
{
    CDisp *cdisp = _cdisp;
    CDispTask *taskp;

    cdisp->_lock.take();
    _runMode = _RUNNING;

    /* move the paused tasks back into the pending queue and then start things up */
    for(taskp = _pausedTasks.head(); taskp; taskp=taskp->_dqNextp) {
        taskp->_inQueue = CDispTask::_queuePending;
        cdisp->_activeCount++;
        _activeCount++;
    }
   cdisp->_pendingTasks.concat(&_pausedTasks);

    /* and start things up */
    cdisp->tryDispatches();

    cdisp->_lock.release();
    
}

int32_t
CDisp::init(uint32_t ntasks)
{
    uint32_t i;
    CDispHelper *helperp;
    CThreadHandle *hp;

    _nhelpers = ntasks;
    for(i=0;i<ntasks;i++) {
        helperp = new CDispHelper(&_lock);
        hp = new CThreadHandle();
        hp->init((CThread::StartMethod) &CDispHelper::start, helperp, this);
    }

    return 0;
}

void
CDisp::unthreadGroup(CDispGroup *group)
{
    _lock.take();
    _allGroups.remove(group);
    _lock.release();
}

int32_t
CDisp::stop(int noWait)
{
    CDispGroup *group;

    for(group = _allGroups.head(); group; group=group->_dqNextp) {
        group->stop(noWait);
    }

    return 0;
}

int32_t
CDisp::pause(int noWait)
{
    CDispGroup *group;

    for(group = _allGroups.head(); group; group=group->_dqNextp) {
        group->pause(noWait);
    }

    return 0;
}

int32_t
CDisp::resume()
{
    CDispGroup *group;

    for(group = _allGroups.head(); group; group=group->_dqNextp) {
        group->resume();
    }

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
        else if (_inQueue == _queueGroupPaused) {
            _group->_pausedTasks.remove(this);
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
    _lock.release();
}

int
CDisp::isAllDoneNL() {
    CDispGroup *group;
    for(group = _allGroups.head(); group; group=group->_dqNextp) {
        if (group->_pausedTasks.count() != 0)
            return 0;
    }
    if (!isAllIdleNL())
        return 0;
    return 1;
}
