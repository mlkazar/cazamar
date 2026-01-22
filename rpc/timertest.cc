#include "osptimer.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

CThreadMutex _mutex;
OspTimer *_mainTimerp;
OspTimer *_subTimerp;
uint32_t _lastToggle = 0;

void
subTimer(OspTimer *timerp, void *contextp)
{
    _mutex.take();
    if (_subTimerp->canceled()) {
        printf("subtimer lost race now=%lld\n", osp_time_ms());
        _mutex.release();
        return;
    }

    printf("subtimer fired %lld\n", osp_time_ms());
    _subTimerp = new OspTimer();
    _subTimerp->init(1000, &subTimer, NULL);
    _mutex.release();
}

void
mainTimer(OspTimer *timerp, void *contextp)
{
    uint64_t ms;

    _mutex.take();

    ms = osp_time_ms();

    if (!timerp->canceled()) {
        printf("Timer fired for us %lld\n", ms);
        _mainTimerp = NULL;

        _mainTimerp = new OspTimer();
        _mainTimerp->init(500, &mainTimer, NULL);
    }
    else {
        printf("Timer race codition\n");
    }

    if (ms - _lastToggle > 6000) {
        _lastToggle = ms;
        if (_subTimerp) {
            printf("\ncanceled subtimer now=%lld\n", ms);
            _subTimerp->cancel();
            _subTimerp = NULL;
        }
        else {
            printf("\nstarted subtimer now=%lld\n", ms);
            _subTimerp = new OspTimer();
            _subTimerp->init(1000, &subTimer, NULL);
        } 
    }

    _mutex.release();
}

int
main(int argc, char **argv)
{
    OspTimer *timerp;
    
    _subTimerp = _mainTimerp = NULL;

    _mutex.take();
    timerp = new OspTimer();
    timerp->init(1000, &mainTimer, NULL);
    _mutex.release();

    while(1) {
        sleep(1);
    }
}
        
