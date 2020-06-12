#include "disp.h"
#include "task.h"

uint32_t Disp::_dispIx;
std::atomic<uint32_t> Disp::_dispAddIx;
Disp *Disp::_dispArray[_maxDisps];

void
Disp::dispatchTasks()
{
    QueueElt *qep;
    while(1) {
        qep = _queue.pop();
        if (qep) {
            qep->_taskp->run();
            delete qep;
        }
        else {
            pause();
        }
    }
}

void 
Disp::append(Task *taskp) {
    QueueElt *eltp;
    eltp = new QueueElt();
    eltp->_taskp = taskp;

    _queue.append(eltp);
}

/* static */ void
 Disp::queue(Task *taskp) {
    Disp *disp;
    int32_t ix = _dispIx % _dispAddIx;
        
    disp = _dispArray[ix];
    disp->append(taskp);
}
