#ifndef _BUFTLS_H_ENV__
#define _BUFTLS_H_ENV__ 1

#include "osp.h"
#include <string>

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <sys/socketvar.h>

#ifndef __linux__
#include <netinet/in_pcb.h>
#include <netinet/tcp_var.h>
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "bufgen.h"
#include "cthread.h"

class BufTls : public BufGen {
    static const uint32_t _defaultBaseTimeoutMs = 60000;
    static pthread_once_t _once;
    int _s;

    OspMBuf *_outp;
    int32_t _error;
    uint8_t _closed;
    uint8_t _connected;
    uint8_t _verbose;
    uint8_t _server;
    SSL_CTX *_sslClientContextp;

    static SSL_CTX *_sslServerContextp;
    static const SSL_METHOD *_sslClientMethodp;
    static const SSL_METHOD *_sslServerMethodp;
    static std::string _pathPrefix;
    static CThreadMutex _mutex;
    SSL *_sslp;
    uint32_t _baseTimeoutMs;

    int32_t fillFromSocket(OspMBuf *mbp);

 public:
    static void mainInit();

    void init(struct sockaddr *sockAddrp, int socklen);

    int getSocket() {
        return _s;
    }

    int32_t listen();

    int32_t accept(BufGen **remotepp);

    void showCerts(SSL *sslp);

    int32_t getc();

    void abort();

    int32_t read(char *tbuffer, int32_t count);

    int32_t readLine(char *tbuffer, int32_t count);

    int32_t putc(const char tbuffer);

    int32_t write(const char *tbuffer, int32_t count);

    void init(char *hostNamep, uint32_t defaultPort);

    int32_t doConnect();

    int32_t doSetup(uint16_t srcPort);

    int32_t flush();

    void reopen();

    void disconnect();

    void setTimeoutMs(uint32_t ms) {
        return;
    }

    void setVerbose() {
        _verbose = 1;
    }

    int32_t getError() {
        return _error;
    }

    int atEof() {
        return _closed;
    }

    BufTls(std::string pathPrefix) {
        /* since _pathPrefix is static, we don't want to have multiple assigners
         * running at once.  All the strings are identical, so ordering doesn't
         * matter, but serialization might (depends upon std::string atomicity)
         */
        _mutex.take();
        _pathPrefix = pathPrefix;
        _mutex.release();

        _s = -1;
        _error = 0;
        _connected = 0;
        _verbose = 0;
        _sslp = NULL;
        _baseTimeoutMs = _defaultBaseTimeoutMs;
    }

    ~BufTls();
};

class BufTlsFactory : public BufGenFactory {
 public:
    BufGen *allocate(int secure) {
        if (secure)
            return new BufTls("");
        else
            return NULL;
    }
};

#endif /* _BUFTLS_H_ENV__ */
