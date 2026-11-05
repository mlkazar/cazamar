#include "bufsocket.h"

#include <netdb.h>
#include <stdio.h>
#include <string>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>

#include "MFANSocket.h"
#include "rsterror.h"

/* Buffered socket abstraction; this is an implementation of the
 * BufGen abstract interface, implemented in terms of the CFStream
 * API.  This is necessitated by Apple's breaking BSD sockets +
 * cellular service in iOS 10.2.  Thanks, losers.
 *
 * Our read and write calls wait for notifications from CFStream that
 * data is ready, that room is available in the output stream, or that
 * an error occurred.
 *
 * The condWait function always returns after the base timeout passed.
 *
 * This module only implements the client side of the connection
 * library, since we don't use the server side API.
 */
pthread_mutex_t MFANSocket_mutex;
int MFANSocket_didStaticInit = 0;

void
MFANSocket::init(struct sockaddr *sockAddrp, int socklen)
{
    osp_assert(0);
}

int32_t
MFANSocket::listen()
{
    osp_assert(0);
    return -1;
}

int32_t
MFANSocket::accept(BufGen **remotepp)
{
    osp_assert(0);
    return -1;
}

/* return 0 on successful wait, -1 if base time exceeded */
int32_t
MFANSocket::condWait(pthread_cond_t *condp, pthread_mutex_t *mutexp, struct timeval *startTvp)
{
    int32_t code;
    struct timespec endTs;
    struct timeval tv;

    code = 0;
    endTs.tv_sec = startTvp->tv_sec + _baseTimeoutMs / 1000;
    endTs.tv_nsec = startTvp->tv_usec * 1000;
    while(1) {
        code = pthread_cond_timedwait(condp, mutexp, &endTs);
        if (code == 0)
            return 0;
        gettimeofday(&tv, NULL);
        if (tv.tv_sec >= endTs.tv_sec) {
            return -1;
        }
    }
}

/* return 0 if read bytes or at EOF, and a negative error code if
 * something went wrong with the socket.  Sets _error to 0 if EOF, or
 * -1 if a failure occurred.
 */
int32_t
MFANSocket::fillFromSocket(OspMBuf *mbp)
{
    int32_t code;
    int safeToRead;
    struct timeval tv;

    if (_closed)
        return (_error == 0? 0 : -1);

    osp_assert(mbp->dataBytes() == 0);
    mbp->reset();

    pthread_mutex_lock(&MFANSocket_mutex);
    code = 0;
    gettimeofday(&tv, nullptr);
    while(true) {
        safeToRead = CFReadStreamHasBytesAvailable(_readStreamCF);
        if (safeToRead || _error != 0 || _closed)
            break;
        _waiters++;
        code = condWait(&_cond, &MFANSocket_mutex, &tv);
        _waiters--;
        if (code != 0) {
            // timeout
            _error = -1;
            break;
        }
    }
    pthread_mutex_unlock(&MFANSocket_mutex);

    if (_closed == 0 && safeToRead && _error == 0) {
        // we should only be here if we think there's data available.
        code = (int32_t) CFReadStreamRead(_readStreamCF,
                                          (UInt8 *) mbp->data(),
                                          mbp->bytesAtEnd());
    } else {
        printf("- MFANSocket read wait timed out / failed (err=%d)\n", _error);
        code = -1;
    }

    pthread_mutex_lock(&MFANSocket_mutex);
    if (_waiters > 0)
        pthread_cond_broadcast(&_cond);

    if (code <= 0) {
        if (code < 0)
            _error = -1;
        else
            _error = 0;         // EOF
        _closed = 1;
        pthread_mutex_unlock(&MFANSocket_mutex);
        if (_waiters > 0)
            pthread_cond_broadcast(&_cond);
    }
    else {
        /* read some data */
        pthread_mutex_unlock(&MFANSocket_mutex);
        if (_verbose) {
            printf("bufsocket: incoming data: %s\n", mbp->data());
        }
        mbp->pushNBytesNoCopy(code);
    }
        
    return (code >= 0? 0 : -1);
}

/* by holding the mutex over the upcalls, as well as here, where we
 * mark the streams as closed, which we always do before calling
 * release, we're guaranteed that an upcall will always return without
 * accessing our socket, if we've free the underlying storage.
 */
void
MFANSocket::disconnect()
{
    pthread_mutex_lock(&MFANSocket_mutex);

    if (_readStreamCF) {
        CFReadStreamUnscheduleFromRunLoop( _readStreamCF,
                                           CFRunLoopGetMain(),
                                           kCFRunLoopCommonModes);
        CFReadStreamClose(_readStreamCF);
    }
    if (_writeStreamCF) {
        CFWriteStreamUnscheduleFromRunLoop( _writeStreamCF,
                                            CFRunLoopGetMain(),
                                            kCFRunLoopCommonModes);
        CFWriteStreamClose(_writeStreamCF);
    }

    _closed = 1;

    pthread_mutex_unlock(&MFANSocket_mutex);

    if (_waiters > 0)
        pthread_cond_broadcast(&_cond);
}

void
MFANSocket::releaseNL()
{
    if (--_refCount <= 0) {
        if (_readStreamCF) {
            CFRelease(_readStreamCF);
            _readStreamCF = NULL;
        }
        if (_writeStreamCF) {
            CFRelease(_writeStreamCF);
            _writeStreamCF = NULL;
        }

        if (_hostCF) {
            CFRelease(_hostCF);
            _hostCF = NULL;
        }

        if (_socket >= 0) {
            close(_socket);
            _socket = -1;
        }
    }
}

void
MFANSocket::holdNL()
{
    _refCount++;
}

void
MFANSocket::release()
{
    pthread_mutex_lock(&MFANSocket_mutex);
    releaseNL();
    pthread_mutex_unlock(&MFANSocket_mutex);
}

void
MFANSocket::reopen()
{
    if (_closed) {
        disconnect();
        setup();
    }
}

/* return -1 on error or EOF (error will be 0 on normal EOF),
 * or a byte of data from the socket.
 */
int32_t
MFANSocket::getc()
{
    char *datap;
    int32_t code;

    if (_inp->dataBytes() <= 0) {
        if (!_inp)
            _inp = OspMBuf::alloc(0);
        code = fillFromSocket(_inp);    // returns 0 or -1 on error
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
MFANSocket::read(char *bufferp, int32_t acount)
{
    int32_t tcount;
    char *datap;
    int32_t code;
    int32_t origCount;

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
                return -1;
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
MFANSocket::readLine(char *bufferp, int32_t acount)
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
                return 0;
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
MFANSocket::putc(const char tbuffer)
{
    int32_t code;

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
MFANSocket::write(const char *tbuffer, int32_t acount)
{
    int32_t tcount;
    int32_t code;
    int32_t origCount;

    if(_verbose) {
        printf("- MFANSocket: sending data '%s'\n", tbuffer);
    }

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
MFANSocket::flush()
{
    int32_t code;
    int32_t nbytes;
    struct timeval tv;

    if (_error != 0)
        return -1;

    nbytes = _outp->dataBytes();

    code = 0;
    gettimeofday(&tv, nullptr);

    char *tp = _outp->data();
    int32_t bytesLeft = nbytes;

    pthread_mutex_lock(&MFANSocket_mutex);

    while(bytesLeft > 0) {
        if (CFWriteStreamCanAcceptBytes(_writeStreamCF))
            code = (int32_t) CFWriteStreamWrite( _writeStreamCF, (UInt8 *) tp, nbytes);
        else
            code = 0;

        if (code < 0 || _error != 0) {
            CFErrorRef eref = CFWriteStreamCopyError(_writeStreamCF);
            CFIndex errorCode = CFErrorGetCode(eref);
            printf("=s= error %ld\n", errorCode);
            CFRelease(eref);
            _error = -1;
            break;
        }

        if (code > 0) {
            bytesLeft -= code;
            tp += code;
            continue;
        }

        // code is zero (no room in socket) and no error
        _waiters++;
        code = condWait(&_cond, &MFANSocket_mutex, &tv);
        _waiters--;
        if (code < 0) {
            _error = -1;
            break;
        }
    }

    pthread_mutex_unlock(&MFANSocket_mutex);

    if (_error != 0) {
        return -1;
    }
    else {
        _outp->reset();
        return 0;
    }
}

/* static */ void
MFANSocket::readStreamCallback( CFReadStreamRef stream, CFStreamEventType ev, void *contextp)
{
    MFANSocket *socketp = (MFANSocket *) contextp;

    pthread_mutex_lock(&MFANSocket_mutex);

    /* if we're passed a closed stream, our own structure might be freed */
    if (CFReadStreamGetStatus(stream) == kCFStreamStatusClosed) {
        printf("- MFANSocket callback sees read closed stream\n");
        pthread_mutex_unlock(&MFANSocket_mutex);
        return;
    }

    if ( (ev & kCFStreamEventHasBytesAvailable) ||
         (ev & kCFStreamEventEndEncountered)) {
        if (socketp->_waiters) {
            pthread_cond_broadcast(&socketp->_cond);
        }
    }

    if ( (ev & kCFStreamEventErrorOccurred)) {
        printf("- MFANSocket callback read error flags 0x%x\n", (int) ev);
        socketp->_error = -1;
        if (socketp->_waiters) {
            pthread_cond_broadcast(&socketp->_cond);
        }
    }

    pthread_mutex_unlock(&MFANSocket_mutex);
}

/* static */ void
MFANSocket::writeStreamCallback( CFWriteStreamRef stream, CFStreamEventType ev, void *contextp)
{
    MFANSocket *socketp = (MFANSocket *) contextp;

    pthread_mutex_lock(&MFANSocket_mutex);

    /* if we're passed a closed stream, our own structure might be freed */
    if (CFWriteStreamGetStatus(stream) == kCFStreamStatusClosed) {
        printf("- MFANSocket: callback sees write closed stream\n");
        pthread_mutex_unlock(&MFANSocket_mutex);
        return;
    }

    if ( (ev & kCFStreamEventCanAcceptBytes) ||
         (ev & kCFStreamEventEndEncountered)) {
        if (socketp->_waiters) {
            pthread_cond_broadcast(&socketp->_cond);
        }
    }

    if ( (ev & kCFStreamEventErrorOccurred)) {
        printf("- MFANSocket callback write error ev=0x%x\n", (int) ev);
        socketp->_error = -1;
        if (socketp->_waiters) {
            pthread_cond_broadcast(&socketp->_cond);
        }
    }

    pthread_mutex_unlock(&MFANSocket_mutex);
}

// Written by Claude due to my laziness.
int
MFANSocket::connectWithSecs(int sockFd,
                            const struct sockaddr *addr,
                            socklen_t addrLen,
                            int timeoutSec) {
    // 1. Get current flags and set non-blocking
    int flags = fcntl(sockFd, F_GETFL, 0);
    fcntl(sockFd, F_SETFL, flags | O_NONBLOCK);

    int res = connect(sockFd, addr, addrLen);
    if (res < 0) {
        if (errno == EINPROGRESS) {
            fd_set myset;
            struct timeval tv;
            FD_ZERO(&myset);
            FD_SET(sockFd, &myset);

            tv.tv_sec = timeoutSec;
            tv.tv_usec = 0;

            // 2. Wait for socket to be writable
            res = select(sockFd + 1, NULL, &myset, NULL, &tv);
            if (res > 0) {
                // Socket selected, check for any underlying socket errors
                int sockerr = 0;
                socklen_t len = sizeof(sockerr);
                if (getsockopt(sockFd, SOL_SOCKET, SO_ERROR, &sockerr, &len) < 0 || sockerr != 0) {
                    errno = sockerr ? sockerr : ECONNREFUSED;
                    res = -1;
                } else
                    res = 0;
            } else if (res == 0) {
                // Timeout reached
                errno = ETIMEDOUT;
                res = -1;
            } else {
                // select() error
                res = -1;
            }
        }
    }

    if (res != 0) {
        printf("=s= mfansocket connect failed errno=%d\n", errno);
    }

    // 3. Restore original flags
    fcntl(sockFd, F_SETFL, flags);
    return res;
}

void
MFANSocket::markFailed() {
    // close all FD resources.
    if (_socket >= 0) {
        close(_socket);
        _socket = -1;
    }
    _closed = true;

    // and mark the socket as failed
    _error = -1;
}

void
MFANSocket::setup()
{
    int result;
    CFStreamClientContext readContext;
    CFStreamClientContext writeContext;
    struct hostent *hostp;
    int code;

    _waiters = 0;

    _error = 0;
    _eof = 0;
    _closed = 0;
    _listening = 0;
    _verbose = 0;
    _refCount = 1;
    
    hostp = gethostbyname2(_hostName.c_str(), AF_INET);
    if (hostp == nullptr) {
        printf("=s= MFANSocket::setup can't find host '%s'\n", _hostName.c_str());
        markFailed();
        return;
    }

    struct sockaddr_in taddr;

    memset(&taddr, 0, sizeof(taddr));
    taddr.sin_family = AF_INET;
    memcpy(&taddr.sin_addr, hostp->h_addr_list[0], sizeof(taddr.sin_addr));
    taddr.sin_port = htons(_port);

    _socket = socket(PF_INET, SOCK_STREAM, 0);
    code = connectWithSecs(_socket,
                           (struct sockaddr *) &taddr,
                           sizeof(taddr),
                           _baseTimeoutMs/1000);
    if (code != 0) {
        printf("=s= connect failed\n");
        markFailed();
        return;
    }

    _hostCF = CFStringCreateWithCString(NULL, _hostName.c_str(), kCFStringEncodingUTF8);

    CFStreamCreatePairWithSocket( kCFAllocatorDefault,
                                  _socket,
                                  &_readStreamCF,
                                  &_writeStreamCF);

    if (_readStreamCF == nullptr || _writeStreamCF == nullptr) {
        printf("=s= CreatePairWithSocket failed\n");
        markFailed();
        return;
    }

    readContext.version = 0;
    readContext.info = this;
    readContext.retain = NULL;
    readContext.release = NULL;
    readContext.copyDescription = NULL;

    writeContext.version = 0;
    writeContext.info = this;
    writeContext.retain = NULL;
    writeContext.release = NULL;
    writeContext.copyDescription = NULL;

    if (_secure) {
        CFReadStreamSetProperty(_readStreamCF,
                                kCFStreamPropertySocketSecurityLevel, 
                                kCFStreamSocketSecurityLevelNegotiatedSSL);

        // Create a dictionary with the settings to disable SSL certificate checking
        CFMutableDictionaryRef sslSettings =
            CFDictionaryCreateMutable( kCFAllocatorDefault,
                                       0,
                                       &kCFTypeDictionaryKeyCallBacks,
                                       &kCFTypeDictionaryValueCallBacks);

        CFDictionarySetValue(sslSettings, kCFStreamSSLValidatesCertificateChain, kCFBooleanFalse);
        CFDictionarySetValue(sslSettings, kCFStreamSSLPeerName, _hostCF);


        // set the properties from the dicctionary
        CFReadStreamSetProperty(_readStreamCF, kCFStreamPropertySSLSettings, sslSettings);

        // Clean up our local dictionary reference
        CFRelease(sslSettings);
    }

    result = CFReadStreamSetClient( _readStreamCF,
                                    ( kCFStreamEventHasBytesAvailable |
                                      kCFStreamEventErrorOccurred |
                                      kCFStreamEventEndEncountered),
                                    &MFANSocket::readStreamCallback,
                                    &readContext);
    if (result) {
        CFReadStreamScheduleWithRunLoop( _readStreamCF,
                                         CFRunLoopGetMain(),
                                         kCFRunLoopDefaultMode);
    }

    result = CFWriteStreamSetClient( _writeStreamCF,
                                     ( kCFStreamEventCanAcceptBytes |
                                       kCFStreamEventErrorOccurred |
                                       kCFStreamEventEndEncountered),
                                     &MFANSocket::writeStreamCallback,
                                     &writeContext);
    if (result) {
        CFWriteStreamScheduleWithRunLoop( _writeStreamCF,
                                          CFRunLoopGetMain(),
                                          kCFRunLoopDefaultMode);
    }

    result = CFReadStreamOpen(_readStreamCF);
    result = CFWriteStreamOpen(_writeStreamCF);
}

void
MFANSocket::init(char *namep, uint32_t defaultPort)
{
    BufGen::init(namep, defaultPort);
    _inp = OspMBuf::alloc(0);
    _outp = OspMBuf::alloc(0);

    if (!MFANSocket_didStaticInit) {
        MFANSocket_didStaticInit = 1;
        pthread_mutex_init(&MFANSocket_mutex, NULL);
    }

    pthread_cond_init(&_cond, NULL);

    if (namep)
        _fullHostName = std::string(namep);
    else
        _fullHostName = "Local Listening";
    _defaultPort = defaultPort;

    setup();
}

int
MFANSocket::isConnected() {
    return (_error == 0);
}

MFANSocket::MFANSocket() {
    _secure = false;
    _baseTimeoutMs = _defaultBaseTimeoutMs;
}

MFANSocket::~MFANSocket()
{
    delete _inp;
    delete _outp;
    disconnect();
}

void
MFANSocket::setTimeoutMs(uint32_t ms)
{
    _baseTimeoutMs = ms;
}

BufGen *
MFANSocketFactory::allocate(int secure)
{
    MFANSocket *socketp = new MFANSocket();
    if (secure)
        socketp->setSecure();
    return socketp;
}

void
MFANSocket::abort()
{
    _error = -1;
    disconnect();
}
