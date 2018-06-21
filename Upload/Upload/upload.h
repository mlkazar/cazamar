#ifndef __UPLOAD_H_ENV__
#define __UPLOAD_H_ENV__ 1

#include "sapi.h"
#include "sapilogin.h"

class Upload {
 public:
    typedef void (notifyProc)(Upload *uploadp, void *contextp, uint32_t event);

    SApi *_sapip;
    SApiLogin *_msLoginp;

    notifyProc *_notifyProcp;
    void *_notifyContextp;

    void init(notifyProc *procp, void *notifyContextp);

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
