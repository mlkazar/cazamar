#ifndef __TASK_H_ENV__
#define __TASK_H_ENV__ 1

#include "disp.h"

class Task {
public:
    typedef void (Task::*RunMethod)(void);
    RunMethod _runMethod;

   template <typename T>void setRunMethod(T genericMethod) {
	_runMethod = static_cast<RunMethod> (genericMethod);
    }

    void queue() {
        Disp::queue(this);
    }

    void queueForce() {
        Disp::queue(this);
    }

    void run() {
        (this->*(_runMethod))();
    }
};
#endif /*  __TASK_H_ENV__*/
