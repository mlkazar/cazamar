#include "bufsocket.h"
#include <stdio.h>
#include <string>
#include <poll.h>
#include "rst.h"

/* Buffered socket abstraction */

FILE *BufSocket::_logFilep=0;

void
BufSocket::openLog()
{
    if (!_logFilep) {
        char tbuffer[100];
        sprintf(tbuffer, "log%d.log", getpid());
        _logFilep = fopen(tbuffer, "a");
        setlinebuf(_logFilep);
    }
}

void
BufSocket::init(struct sockaddr *sockAddrp, int socklen)
{
    BufGen::init(sockAddrp, socklen);

    _inp = OspMBuf::alloc(0);
    _outp = OspMBuf::alloc(0);
    _s = -1;

    _fullHostName = "_Accepted_";

    _error = 0;
    _closed = 0;
    _connected = 1;
    _listening = 0;
    _verbose = 0;
    _baseTimeoutMs = _defaultBaseTimeoutMs;
}

int32_t
BufSocket::listen()
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

int32_t
BufSocket::accept(BufGen **remotepp)
{
    int fd;
    int code;
    socklen_t sockLen;
    BufSocket *socketp;
    struct sockaddr_in peerAddr;

    fd = ::accept(_s, 0, 0);
    if (fd < 0) {
        perror("accept");
        *remotepp = NULL;
        return -1;
    }

    sockLen = sizeof(peerAddr);
    code = getpeername(fd, (struct sockaddr *) &peerAddr, &sockLen);
    if (code < 0) {
        printf("bufsocket close %d\n", fd);
        close(fd);
        return code;
    }

    socketp = new BufSocket();
    socketp->init((struct sockaddr *) &peerAddr, sizeof(peerAddr));
    socketp->_s = fd;

    *remotepp = socketp;

    return 0;
}

/* return 0 if read bytes or at EOF, and a negative error
 * code if something went wrong with the socket.
 */
int32_t
BufSocket::fillFromSocket(OspMBuf *mbp)
{
    int32_t code;
    struct pollfd pollFd;

    if (_closed)
        return -1;

    osp_assert(mbp->dataBytes() == 0);
    mbp->reset();

    code = doConnect();
    if (code) return code;

    pollFd.fd = _s;
    pollFd.events = POLLIN;
    pollFd.revents = 0;
    code = (int32_t) ::poll(&pollFd, 1, _baseTimeoutMs);
    if (code == 1)
        code = (int32_t) ::read(_s, mbp->data(), mbp->bytesAtEnd());
    else
        code = -1;

    if (code <= 0) {
        if (code < 0)
            _error = errno;
        _closed = 1;
    }
    else {
        /* read some data */
        if (_verbose) {
            printf("bufsocket: incoming data: %s\n", mbp->data());
        }
        mbp->pushNBytesNoCopy(code);
    }
        
    return (code >= 0? 0 : -1);
}

void
BufSocket::reopen()
{
    if (!_closed)
        return;

    _closed = 0;
    _connected = 0;
    if (_s != -1) {
        printf("bufsocket close fd=%d\n", _s);
        close(_s);
        _s = -1;
    }
}

int32_t
BufSocket::doSetup(uint16_t srcPort)
{
    int32_t code;
    int opt;
    socklen_t optLen;
    sockaddr_in srcAddr;

    /* if connection has been manually closed or aborted */
    if (_closed)
        return -1;

    if (_s != -1) {
        printf("bufsocket close fd=%d\n", _s);
        close(_s);
    }

    _s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (srcPort != 0) {
        opt = 1;
        code = setsockopt(_s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        if (code < 0) {
            perror("setsock0");
            return -errno;
        }
    }

    if (srcPort != 0) {
        memset(&srcAddr, 0, sizeof(srcAddr));
#ifndef __linux__
        srcAddr.sin_len = sizeof(struct sockaddr_in);
#endif
        srcAddr.sin_family = AF_INET;
        srcAddr.sin_addr.s_addr = 0;
        srcAddr.sin_port = htons(srcPort);
        code = ::bind(_s, (struct sockaddr *) &srcAddr, sizeof(srcAddr));
        if (code) {
            perror("bind");
            return -errno;
        }
    }

#ifndef __linux__
    opt = 1;
    code = setsockopt(_s, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
    if (code < 0) {
        perror("setsock1");
        return -errno;
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

void
BufSocket::disconnect()
{
    if (!_connected)
        return;

    _connected = 0;
    _closed = 1;
    printf("bufsocket close fd=%d\n", _s);
    close(_s);
    _s = -1;
}

int32_t
BufSocket::doConnect()
{
    int32_t code;

    if (_connected)
        return 0;

    if (_closed)
        return -1;

    code = doSetup(0);
    if (code < 0) {
        return code;
    }

    if (_verbose)
        printf("BufSocket: about to try to connect\n");
    code = ::connect(_s, (struct sockaddr *) &_destAddr, sizeof(_destAddr));
    if (code) {
        perror("connect");
        return -errno;
    }

    if (_verbose)
        printf("BufSocket: Connected!\n");
    _connected = 1;

    return 0;
}

/* return -1 on error or EOF (error will be 0 on normal EOF),
 * or a byte of data from the socket.
 */
int32_t
BufSocket::getc()
{
    char *datap;
    int32_t code;

    code = doConnect();
    if (code) return code;

    if (_inp->dataBytes() <= 0) {
        if (!_inp)
            _inp = OspMBuf::alloc(0);
        code = fillFromSocket(_inp);
        if (code != 0)
            return code;
        if (_closed)
            return -1;
    }

    datap = _inp->popNBytes(1);
    return * ((uint8_t *)datap);
}

/* return a negative error code, or the count of bytes transferred.  Count of
 * 0 means at EOF
 */
int32_t
BufSocket::read(char *bufferp, int32_t acount)
{
    int32_t tcount;
    char *datap;
    int32_t code;
    int32_t origCount;

    code = doConnect();
    if (code) return code;

    origCount = acount;
    while(acount > 0) {
        /* copy out any available data */
        if (_inp->dataBytes() > 0) {
            tcount = _inp->dataBytes();
            if (tcount > acount)
                tcount = acount;
            datap = _inp->popNBytes(tcount);
            memcpy(bufferp, datap, tcount);
            acount -= tcount;
            bufferp += tcount;
            continue;
        }
        else {
            /* buffer is empty, refill */
            code = fillFromSocket(_inp);
            if (code != 0)
                return code;
            if (_closed) {
                /* read a number of bytes before hitting EOF */
                return origCount - acount;
            }
        }
    }

    return origCount;
}

/* read a line, terminating after \n */
int32_t
BufSocket::readLine(char *bufferp, int32_t acount)
{
    int tc;
    int32_t origCount = acount;
    
    while(acount > 0) {
        tc = getc();
        if (tc == '\r')
            continue;
        if (tc == '\n')
            break;
        if (tc < 0) {
            /* error or EOF */
            if (_error == 0) {
                /* hit EOF, so terminate the buffer and return success if we've already
                 * received some data.  Otherwise, return ERR_EOF.
                 */
                if (acount == origCount)
                    return RST_ERR_EOF;
                break;
            }
            else {
                /* got an error */
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
        return -1;
    }

    return origCount - acount;
}

/* return 0 for success, or negative error code */
int32_t
BufSocket::putc(const char tbuffer)
{
    int32_t code;

    code = doConnect();
    if (code) return code;

    if (_outp->bytesAtEnd() <= 0) {
        code = flush();
        if (code) return code;
    }

    _outp->pushNBytes((char *) &tbuffer, 1);
    return 0;
}

/* return count written for success, or negative error code if
 * something went wrong
 */
int32_t
BufSocket::write(const char *tbuffer, int32_t acount)
{
    int32_t tcount;
    int32_t code;
    int32_t origCount;

    if(_verbose) {
        printf("bufsocket: sending data '%s'\n", tbuffer);
    }

    code = doConnect();
    if (code) return code;

    origCount = acount;
    while(acount > 0) {
        tcount = _outp->bytesAtEnd();
        if (tcount == 0) {
            /* flush buffer */
            code = flush();
            if (code) return code;
        }

        if (tcount > acount)
            tcount = acount;
        _outp->pushNBytes((char *) tbuffer, tcount);
        acount -= tcount;
        tbuffer += tcount;
    }

    return origCount;
}

/* return 0 for success or -1 failure */
int32_t
BufSocket::flush()
{
    int32_t code;
    int32_t nbytes;

    nbytes = _outp->dataBytes();
    code = (int32_t) ::write(_s, _outp->data(), nbytes);
    if (code != nbytes) {
        return -1;
    }
    else {
        _outp->reset();
        return 0;
    }
}

void
BufSocket::init(char *namep, uint32_t defaultPort)
{
    BufGen::init(namep, defaultPort);
    _inp = OspMBuf::alloc(0);
    _outp = OspMBuf::alloc(0);
    _s = -1;

    if (namep)
        _fullHostName = std::string(namep);
    else
        _fullHostName = "Local Listening";
    _defaultPort = defaultPort;

    _baseTimeoutMs = _defaultBaseTimeoutMs;
    _error = 0;
    _closed = 0;
    _connected = 0;
    _listening = (namep? 0 : 1);
    _verbose = 0;
}


BufSocket::~BufSocket()
{
    delete _inp;
    delete _outp;
    if (_s >= 0) {
        printf("bufsocket close fd=%d\n", _s);
        ::close(_s);
        _s = -1;
    }
}
