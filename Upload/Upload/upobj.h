#ifndef __UPOBJ_H_ENV__
#define __UPOBJ_H_ENV__ 1

#include "sapi.h"
#include "sapilogin.h"
#include "cfsms.h"

class Upload {
 public:
    typedef void (notifyProc)(Upload *uploadp, void *contextp, uint32_t event);

    SApi *_sapip;
    SApiLoginMS *_loginMSp;

    notifyProc *_notifyProcp;
    void *_notifyContextp;
    std::string _pathPrefix;

    CfsMs *_cfsp;

    void init(notifyProc *procp, void *notifyContextp);

    void runTests();

    /* pthread callback */
    static void *server(void *contextp);
};

#endif /* __UPOBJ_H_ENV__ */
