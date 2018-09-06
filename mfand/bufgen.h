#ifndef _BUFGEN_H_ENV__
#define _BUFGEN_H_ENV__ 1

#include "osp.h"
#include "cthread.h"
#include "dqueue.h"
#include <string>

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <netdb.h>

#if 0
#include <sys/socketvar.h>
#include <netinet/in_pcb.h>
#include <netinet/tcp_var.h>
#endif

class BufGen {
 protected:
    uint32_t _port;
    uint32_t _defaultPort;
    std::string _fullHostName;
    std::string _hostName;
    struct sockaddr_in _destAddr;
    uint8_t _listening;
    uint8_t _server;
    int32_t _error;

 public:
    virtual void init(struct sockaddr *sockAddrp, int socklen);

    virtual void init(char *hostNamep, uint32_t defaultPort);

    virtual int32_t listen() = 0;

    /* if the socket is already open, this is a noop, otherwise we reconnect
     * a client socket.
     */
    virtual void reopen() = 0;

    virtual int32_t accept(BufGen **remotepp) = 0;

    virtual int32_t getc() = 0;

    virtual int32_t read(char *tbuffer, int32_t count) = 0;

    virtual int32_t readLine(char *tbuffer, int32_t count) = 0;

    virtual int32_t putc(const char tbuffer) = 0;

    virtual int32_t write(const char *tbuffer, int32_t count) = 0;

    virtual void setTimeoutMs(uint32_t ms) = 0;

    virtual void abort() = 0;

    std::string *getHostname() {
        return &_hostName;
    }

    std::string *getHostAndPort() {
        return &_fullHostName;
    }

    virtual int32_t flush() = 0;

    virtual int32_t getError() = 0;

    virtual int atEof() = 0;

    virtual void disconnect() = 0;

    /* you can override this to provide your own memory management scheme, as is
     * done in MFANSocket.mm
     */
    virtual void release() {
        delete this;
    }

    std::string *getFullHostName() {
        return &_fullHostName;
    }

    uint32_t getPort() {
        return _port;
    }

    BufGen();

    virtual ~BufGen();
};

class BufGenFactory {
 public:
    virtual BufGen *allocate()= 0;
};
#endif /* _BUFGEN_H_ENV__ */

