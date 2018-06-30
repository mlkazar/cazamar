#ifndef __UPLOAD_H_ENV__
#define __UPLOAD_H_ENV__ 1

#include "sapi.h"
#include "sapilogin.h"
#include "cfsms.h"

class DataSourceFile : public CDataSource {
    int _fd;
    CThreadMutex _lock;

 public:
    int32_t open(char *fileNamep);

    int32_t getAttr(CAttr *attrp);

    int32_t read( uint64_t offset, uint32_t count, char *bufferp);

    int32_t close() {
        if (_fd >= 0)
            ::close(_fd);
        return 0;
    }

    DataSourceFile() {
        _fd = -1;
    }

    ~DataSourceFile() {
        close();
    }
};

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

    static int32_t generateTestData(void *contextp,
                                    uint64_t offset,
                                    uint32_t count,
                                    char *bufferp);

    /* pthread callback */
    static void *server(void *contextp);
};

/* This class presents the basic application test screen.  It gets you
 * to the login screen using SApiLoginApple if you aren't logged in
 * (if there's no saved token).  Otherwise, it presents the
 * application screen from login-home.html.
 *
 * The class runs the startMethod when the registered URL is reached.
 * That's '/' for this application.
 */
class HomeScreen : public SApi::ServerReq {
public:
    static SApi::ServerReq *factory(std::string *opcode, SApi *sapip);

    HomeScreen(SApi *sapip) : SApi::ServerReq(sapip) {
        return;
    }

    void startMethod();
};

#endif /* __UPLOAD_H_ENV__ */
