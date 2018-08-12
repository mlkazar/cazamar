#include "buftls.h"
#include <stdio.h>
#include <string>
#ifdef __linux__
#include <string.h>
#include <signal.h>
#endif
#include "rst.h"
#include "cthread.h"

SSL_CTX *BufTls::_sslServerContextp;
const SSL_METHOD *BufTls::_sslClientMethodp;
const SSL_METHOD *BufTls::_sslServerMethodp;
CThreadMutex BufTls::_mutex;

/* server side */
void
BufTls::init(struct sockaddr *sockAddrp, int socklen)
{
    BufGen::init(sockAddrp, socklen);
    _outp = OspMBuf::alloc(0);
    _s = -1;

    _fullHostName = "_Accepted_";

    _error = 0;
    _closed = 0;
    _connected = 1;
    _listening = 0;
    _verbose = 0;
    _server = 1;
    _sslClientContextp = NULL;
}

int32_t
BufTls::listen()
{
    int32_t code;

    code = doSetup(_defaultPort);
    if (code < 0)
        return code;

    code = ::listen(_s, 10);
    if (code < 0) {
        perror("listen");
        return -1;
    }
    else {
        return 0;
    }
}

void
BufTls::reopen()
{
    if (!_closed)
        return;

    disconnect();
}

int32_t
BufTls::accept(BufGen **remotepp)
{
    int fd;
    int code;
    socklen_t sockLen;
    BufTls *socketp;
    struct sockaddr_in peerAddr;
    SSL *sslp;

    fd = ::accept(_s, 0, 0);
    if (fd < 0) {
        perror("accept");
        *remotepp = NULL;
        return -1;
    }

    sockLen = sizeof(peerAddr);
    code = getpeername(fd, (struct sockaddr *) &peerAddr, &sockLen);
    if (code < 0) {
        printf("buftls %p close accept fd=%d\n", this, fd);
        close(fd);
        return code;
    }

    socketp = new BufTls(_pathPrefix);
    socketp->init((struct sockaddr *) &peerAddr, sizeof(peerAddr));
    socketp->_s = fd;

    sslp = SSL_new(_sslServerContextp);
    SSL_set_fd(sslp, fd);
    SSL_accept(sslp);
    socketp->_sslp = sslp;

    *remotepp = socketp;

    return 0;
}

/* Buffered socket abstraction for TLS connections; note that buffering occurs
 * on the input side, but each SSL_write is unbuffered, so we provide buffering
 * on that side.
 */

void
BufTls::showCerts(SSL *sslp)
{
    X509 *certp;
    char *datap;

    certp = SSL_get_peer_certificate(sslp);
    if ( certp != NULL ) {
        printf("Server certificates:\n");
        datap = X509_NAME_oneline(X509_get_subject_name(certp), 0, 0);
        printf("Subject: %s\n", datap);
        free(datap);       /* free the malloc'ed string */
        datap = X509_NAME_oneline(X509_get_issuer_name(certp), 0, 0);
        printf("Issuer: %s\n", datap);
        free(datap);       /* free the malloc'ed string */
        X509_free(certp);     /* free the malloc'ed certificate copy */
    }
    else {
        printf("No peer certs\n");
    }
}

int32_t
BufTls::doSetup(uint16_t srcPort)
{
    int32_t code;
    int opt;
    socklen_t optLen;
    struct sockaddr_in srcAddr;
#ifdef __linux__
    static int sigpipeDisabled = 0;
#endif

    if (_s != -1) {
        printf("buftls %p close in doSetup fd=%d\n", this, _s);
        close(_s);
    }

    _s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    printf("buftls %p new socket %d\n", this, _s);

    if (srcPort != 0) {
        opt = 1;
        code = setsockopt(_s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (code < 0) {
            perror("setsock0");
            return -errno;
        }
    }

#ifndef __linux__
    srcAddr.sin_len = sizeof(struct sockaddr_in);
#endif
    srcAddr.sin_family = AF_INET;
    srcAddr.sin_addr.s_addr = 0;
    srcAddr.sin_port = htons(srcPort);
    code = ::bind(_s, (struct sockaddr *) &srcAddr, sizeof(srcAddr));
    if (code) {
        perror("bindb");
        return -errno;
    }

#ifndef __linux__
    opt = 1;
    code = setsockopt(_s, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
    if (code < 0) {
        perror("setsock1");
        return -errno;
    }
#else
    if (!sigpipeDisabled) {
        sigpipeDisabled = 1;
        signal(SIGPIPE, SIG_IGN);
    }
#endif

    opt = 1;
    code = setsockopt(_s, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    if (code < 0) {
        perror("setsock1");
        return -errno;
    }

    optLen = 1024*1024;
    code = setsockopt(_s, SOL_SOCKET, SO_RCVBUF, &optLen, sizeof(optLen));
    if (code < 0) {
        perror("setsock0");
        return -errno;
    }

    optLen = 1024*1024;
    code = setsockopt(_s, SOL_SOCKET, SO_SNDBUF, &optLen, sizeof(optLen));
    if (code < 0) {
        perror("setsock3");
        return -errno;
    }

#ifndef __linux__
    opt = _baseTimeoutMs/1000;
    code = setsockopt(_s, IPPROTO_TCP, TCP_CONNECTIONTIMEOUT, &opt, sizeof(opt));
    if (code < 0) {
        perror("setsock4");
        return -errno;
    }

    opt = 5;
    code = setsockopt(_s, IPPROTO_TCP, TCP_KEEPALIVE, &opt, sizeof(opt));
    if (code < 0) {
        perror("setsock5");
        return -errno;
    }
#endif

    opt = 5;
    code = setsockopt(_s, IPPROTO_TCP, TCP_KEEPINTVL, &opt, sizeof(opt));
    if (code < 0) {
        perror("setsock6");
        return -errno;
    }

    opt = _baseTimeoutMs / 5000;
    if (opt <= 0)
        opt = 1;
    code = setsockopt(_s, IPPROTO_TCP, TCP_KEEPCNT, &opt, sizeof(opt));
    if (code < 0) {
        perror("setsock7");
        return -errno;
    }

    return 0;
}

int32_t
BufTls::doConnect()
{
    int32_t code;

    if (_connected)
        return 0;

    code = doSetup(0);
    if (code < 0)
        return code;

    code = ::connect(_s, (struct sockaddr *) &_destAddr, sizeof(_destAddr));
    if (code) {
        perror("connect");
        return -errno;
    }

    _sslClientContextp = SSL_CTX_new(_sslClientMethodp);   /* Create new context */
    if ( !_sslClientContextp) {
        ERR_print_errors_fp(stdout);
        osp_assert(0);
    }

    _sslp = SSL_new(_sslClientContextp);
    if (!_sslp){
        printf("BufTls: failed to create SSL client connection state\n");
        return -1;
    }
    SSL_set_fd(_sslp, _s);

    code = SSL_connect(_sslp);
    if (code < 0) {
        ERR_print_errors_fp(stdout);
        return -1;
    }

    // showCerts(_sslp);        /* get any certs */
    _connected = 1;

    return 0;
}

void
BufTls::disconnect()
{
    _connected = 0;
    if (_sslp) {
        SSL_shutdown(_sslp);
        _sslp = NULL;
    }

    printf("buftls %p close in disconnect fd=%d\n", this, _s);

    if (_s >= 0) {
        close(_s);
        _s = -1;
    }
    if (_sslClientContextp) {
        SSL_CTX_free(_sslClientContextp);
        _sslClientContextp = NULL;
    }
}

/* return -1 on error or EOF (error will be 0 on normal EOF),
 * or a byte of data from the socket.
 */
int32_t
BufTls::getc()
{
    int32_t code;
    unsigned char tc;

#if 0
    // don't need to connect on a read, and might mess things up on protocol error recovery
    code = doConnect();
    if (code) return code;
#endif

    while(1) {
        code = SSL_read(_sslp, (char *) &tc, sizeof(tc));
        if (code == 1) {
            if (_verbose) {
                printf("%c", tc);
            }
            return tc;
        }
        else {
            int sslError;
            sslError = SSL_get_error(_sslp, code);
            if (sslError == SSL_ERROR_WANT_READ) {
                printf(" SSL read terminate (cont) with want read, code=%d sslError=%d",
                       code, sslError);
                continue;
            }

            printf(" SSL read terminate with code=%d sslError=%d\n", code, sslError);
            if (code == 0 && sslError == SSL_ERROR_ZERO_RETURN) {
                return -2;
                // continue;
            }
            else {
                disconnect();   /* so we reconnect at next write */
                return -1;
            }
        }
    }
}

/* return a negative error code, or the count of bytes transferred.  Count of
 * 0 means at EOF
 */
int32_t
BufTls::read(char *bufferp, int32_t acount)
{
    int32_t i;
    int tc;

    if (_verbose)
        printf("TLS=%p read start ct=%d:", this, acount);
    for(i=0;i<acount;i++) {
        tc = getc();
        if (tc == -1) {
            if (_verbose)
                printf("TLS=%p error after %d bytes\n", this, i);
            return tc;
        }
        else if (tc == -2) {
            /* hit EOF; return count of characters actually read */
            if (_verbose)
                printf("TLS=%p read done at eof, ret=%d\n", this, i);
            return i;
        }
        else {
            *bufferp++ = tc;
        }
    }

    if (_verbose)
        printf("TLS=%p read done at count, ret=%d\n", this, acount);
    return acount;
}

/* read a line, terminating after \n */
int32_t
BufTls::readLine(char *bufferp, int32_t acount)
{
    int tc;
    int32_t origCount = acount;
    
    if (_verbose)
        printf("TLS=%p readline start ct=%d:", this, acount);
    while(acount > 0) {
        tc = getc();
        if (tc == '\r')
            continue;
        if (tc == '\n') {
            break;
        }
        if (tc < 0) {
            /* error or EOF */
            if (tc == -2) {
                if (_verbose)
                    printf("TLS=%p readline hit eof (breaking) after %d bytes\n",
                           this, origCount - acount);
                /* hit EOF, so terminate the buffer and return success */
                if (acount == origCount)
                    return RST_ERR_EOF;
                break;
            }
            else {
                /* got an error */
                if (_verbose)
                    printf("TLS=%p readline returning error\n", this);
                return -1;
            }
        }
        *bufferp++ = tc;
        acount--;
    }

    /* try to null terminate the string, if possible */
    if (acount > 0) {
        *bufferp++ = 0;
        acount--;
    }
    else {
        /* can't null terminate, so fail the request */
        if (_verbose)
            printf("TLS=%p readline returning no room for term (read %d bytes)\n",
                   this, origCount-acount);
        return -1;
    }

    if (_verbose) {
        printf("TLS=%p readline returning ct=%d\n", this, origCount-acount);
    }

    return origCount - acount;
}

/* return 0 for success, or negative error code */
int32_t
BufTls::putc(const char tbuffer)
{
    int32_t code;

    if (_verbose) {
        printf("%c", tbuffer);
    }

    code = doConnect();
    if (code) return code;

    if (_outp->bytesAtEnd() <= 0) {
        code = flush();
        if (code) return code;
    }

    _outp->pushNBytes((char *) &tbuffer, 1);

    return 0;
}

/* return count written for success, or negative error code if something went wrong */
int32_t
BufTls::write(const char *abufferp, int32_t acount)
{
    int32_t code;
    char tc;
    int32_t i;
    int32_t origCount;

    if (_verbose)
        printf("TLS=%p write start count=%d\n", this, acount);

    origCount = acount;
    code = doConnect();
    if (code) {
        if (_verbose)
            printf("TLS=%p write connect failed\n", this);
        return code;
    }

    if (_closed) {
        if (_verbose)
            printf("TLS=%p closed in write\n", this);
        return -1;
    }

    for(i=0;i<acount;i++) {
        tc = *abufferp++;
        code = putc(tc);
        if (code != 0) {
            if (_verbose)
                printf("TLS=%p write failed code=%d\n", this, code);
            return code;
        }
    }

    if (_verbose)
        printf("TLS=%p write done returning %d\n", this, origCount);
    return origCount;
}

int32_t
BufTls::flush()
{
    int32_t code;
    int32_t nbytes;
    int sslError;

    nbytes = _outp->dataBytes();
    if (nbytes == 0)
        return 0;

    while(1) {
        code = SSL_write(_sslp, _outp->data(), nbytes);
        if (code != nbytes) {
            sslError = SSL_get_error(_sslp, code);
            printf("TLS=%p flush failed code=%d sslError=%d\n", this, code, sslError);
            if (sslError == SSL_ERROR_WANT_READ || sslError == SSL_ERROR_WANT_WRITE) {
                printf("  retrying...\n");
                /* retry with same parameters */
                continue;
            }

            disconnect();

            if (sslError == SSL_ERROR_SYSCALL) {
                code = doConnect();
                if (code == 0) {
                    printf("  retrying after ERROR_SYSCALL...\n");
                    continue;
                }
                return -3;
            }

            return -1;
        }
        else {
            _outp->reset();
            return 0;
        }
    }
}

/* client side */
void
BufTls::init(char *namep, uint32_t defaultPort)
{
    static int didMainInit = 0;
    std::string certPath;
    std::string keyPath;

    BufGen::init(namep, defaultPort);

    if (!didMainInit) {
        didMainInit = 1;

        certPath = _pathPrefix + "test_cert.pem";
        keyPath = _pathPrefix + "test_key.pem";

        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();

        /* setup client side */
#ifdef __linux__
        _sslClientMethodp = TLS_client_method();  /* use for client conns */
#else
        _sslClientMethodp = TLSv1_2_client_method();  /* use for client conns */
#endif

        /* setup server side */
#ifdef __linux__
        _sslServerMethodp = TLS_server_method();
#else
        _sslServerMethodp = TLSv1_2_server_method();
#endif
        _sslServerContextp = SSL_CTX_new(_sslServerMethodp);

        if (SSL_CTX_load_verify_locations( _sslServerContextp,
                                           certPath.c_str(),
                                           keyPath.c_str()) != 1) {
            printf("failing location test\n");
            ERR_print_errors_fp(stdout);
            osp_assert(0);
        }
        if (SSL_CTX_set_default_verify_paths(_sslServerContextp) != 1) {
            printf("failing path verification\n");
            ERR_print_errors_fp(stdout);
            osp_assert(0);
        }
        if (SSL_CTX_use_certificate_file(_sslServerContextp,
                                         certPath.c_str(),
                                         SSL_FILETYPE_PEM) <= 0) {
            printf("failing use cert\n");
            ERR_print_errors_fp(stdout);
            osp_assert(0);
        }
        if (SSL_CTX_use_PrivateKey_file(_sslServerContextp,
                                        keyPath.c_str(),
                                        SSL_FILETYPE_PEM) <= 0) {
            printf("failing use key\n");
            ERR_print_errors_fp(stdout);
            osp_assert(0);
        }
        if (!SSL_CTX_check_private_key(_sslServerContextp)) {
            printf("failing final check\n");
            ERR_print_errors_fp(stdout);
            osp_assert(0);
        }
    }

    _sslClientContextp = SSL_CTX_new(_sslClientMethodp);   /* Create new context */
    if ( !_sslClientContextp) {
        ERR_print_errors_fp(stdout);
        osp_assert(0);
    }

    _outp = OspMBuf::alloc(0);
    _s = -1;

    if (namep)
        _fullHostName = std::string(namep);
    else
        _fullHostName = "Local Listening";
    _defaultPort = defaultPort;

    _error = 0;
    _closed = 0;
    _connected = 0;
    _listening = (namep? 0 : 1);
    _server = 0;
}


BufTls::~BufTls()
{
    delete _outp;

#if 0
    if (_sslServerContextp) {
        SSL_CTX_free(_sslServerContextp);
        _sslServerContextp = NULL;
    }
#endif

    if (_sslClientContextp) {
        SSL_CTX_free(_sslClientContextp);
        _sslClientContextp = NULL;
    }

    if (_s >= 0) {
        if (_verbose)
            printf("TLS=%p closing socket %d\n", this, _s);
        printf("buftls %p  destructor close fd=%d\n", this, _s);
        ::close(_s);
        _s = -1;
    }
}
