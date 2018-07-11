#ifndef __WALKDISP_H_ENV__
#define __WALKDISP_H_ENV__ 1

#include <osp.h>
#include <dqueue.h>
#include <cdisp.h>
#include <string>
#include <sys/stat.h>

class WalkTask : public CDispTask {
    typedef int32_t (CallbackProc) (void *contextp, std::string *pathp, struct stat *statp);
    std::string _path;

    CallbackProc *_callbackProcp;
    void *_callbackContextp;

 public:
    int32_t start();

    int32_t initWithPath(std::string path) {
        _path = path;
        return 0;
    }

    /* works even for null pointers */
    void setCallback(CallbackProc *procp, void *contextp) {
        _callbackProcp = procp;
        _callbackContextp = contextp;
    }

    WalkTask() {
        _callbackProcp = NULL;
        _callbackContextp = NULL;
        return;
    }
};

#endif /* __WALKDISP_H_ENV__ */
