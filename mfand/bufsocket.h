#ifndef _BUFSOCKET_H_ENV__
#define _BUFSOCKET_H_ENV__ 1

#include "osp.h"
#include <string>

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <netdb.h>

#include "bufgen.h"

class BufSocket : public BufGen {
    static const uint32_t _defaultBaseTimeoutMs = 60000;
    int _s;

    OspMBuf *_inp;
    OspMBuf *_outp;
    int32_t _error;
    uint8_t _closed;
    uint8_t _connected;
    uint8_t _verbose;
    uint8_t _server;    /* server side connection */
    uint32_t _baseTimeoutMs;

    int32_t fillFromSocket(OspMBuf *mbp);

 public:
    static FILE *_logFilep;

    void setTimeoutMs(uint32_t ms) {
        _baseTimeoutMs = ms;
    }

    int32_t listen();

    int32_t accept(BufGen **remotepp);

    int32_t getc();

    int32_t read(char *tbuffer, int32_t count);

    void reopen();

    int32_t readLine(char *tbuffer, int32_t count);

    int32_t putc(const char tbuffer);

    int32_t write(const char *tbuffer, int32_t count);

    void setVerbose() {
        _verbose = 1;
    }

    void init(char *hostNamep, uint32_t defaultPort);

    void init(struct sockaddr *sockAddrp, int socklen);

    int32_t doSetup(uint16_t srcPort);

    int32_t doConnect();

    int32_t flush();

    void openLog();

    void disconnect();    /* can be reopened on next use */

    int32_t getError() {
        return _error;
    }

    int atEof() {
        return _closed;
    }

    ~BufSocket();
};

class BufSocketFactory : public BufGenFactory {
    BufGen *allocate() {
        return new BufSocket();
    }
};

#endif /* _BUFSOCKET_H_ENV__ */
