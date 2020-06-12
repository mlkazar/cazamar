#ifndef __LOCK_H_ENV
#define __LOCK_H_ENV 1

#include <unistd.h>
#include <atomic>

class SpinLock {
 public:
    std::atomic<uint32_t> _pid;

    void take() {
        uint32_t pid;
        uint32_t compValue;
        pid = (uint32_t) getpid();         /* replace with something cheaper */
        while(1) {
            compValue = 0;
            if (atomic_compare_exchange_strong(&_pid, &compValue, pid)) {
                /* we have the lock */
                return;
            }
        }
    }

    int tryLock() {
        uint32_t pid;
        uint32_t compValue;
        pid = (uint32_t) getpid();         /* replace with something cheaper */
        compValue = 0;
        if (atomic_compare_exchange_strong(&_pid, &compValue, pid)) {
            /* we have the lock */
            return 1;
        }
        else {
            return 0;
        }
    }
    
    void release() {
        _pid.store(0);
    }

    SpinLock() {
        _pid = 0;
    }
};

#endif /*  __LOCK_H_ENV */
