#ifndef __MFANSOCKET_H_ENV__
#define __MFANSOCKET_H_ENV__ 1

#include <CoreFoundation/CoreFoundation.h>
#include <CoreFoundation/CFString.h>
#include <CoreFoundation/CFSocket.h>
#include "bufgen.h"

class MFANSocket : public BufGen {
 public:
    static const uint32_t _defaultBaseTimeoutMs = 30000;

    CFReadStreamRef _readStreamCF;
    CFWriteStreamRef _writeStreamCF;
    CFStringRef _hostCF;
    uint32_t _baseTimeoutMs;
    uint8_t _closed;
    uint8_t _connected;
    uint8_t _verbose;
    uint8_t _eof;
    OspMBuf *_inp;
    OspMBuf *_outp;
    int32_t _refCount;

    /* keep track of whether we can do a or a write safely; note that
     * since we get a callback notification when it is safe to read or
     * write, and that this indication gets reset when we actually do
     * a read or write, we need to ensure that for a given socket, we
     * only have one read or write in progress.
     */
    pthread_cond_t _cond;
    uint8_t _safeToRead;
    uint8_t _safeToWrite;
    uint8_t _readInProgress;
    uint8_t _writeInProgress;
    uint32_t _readersWaiting;
    uint32_t _writersWaiting;

    void init(struct sockaddr *sockAddrp, int socklen);

    int32_t listen();

    int32_t accept(BufGen **remotepp);

    void setup();

    int32_t fillFromSocket(OspMBuf *mbp);

    int32_t doSetup(uint16_t srcPort);

    void disconnect();

    void reopen();

    int32_t doConnect();

    int32_t getc();

    int32_t read(char *bufferp, int32_t acount);

    int32_t readLine(char *bufferp, int32_t acount);

    int32_t putc(const char tbuffer);

    int32_t condWait(pthread_cond_t *condp, pthread_mutex_t *mutexp);

    int32_t write(const char *tbuffer, int32_t acount);

    void setTimeoutMs(uint32_t ms);

    int32_t flush();

    int32_t getError() {
        return _error;
    }

    int atEof() {
        return (_eof || _closed);
    }

    static void readStreamCallback( CFReadStreamRef stream, CFStreamEventType ev, void *contextp);

    static void writeStreamCallback( CFWriteStreamRef stream,
                                     CFStreamEventType ev,
                                     void *contextp);

    void init(char *namep, uint32_t defaultPort);

    void release();

    void releaseNL();

    void holdNL();

    ~MFANSocket();
};

class MFANSocketFactory : public BufGenFactory {
    MFANSocket * allocate();
};
#endif /* __MFANSOCKET_H_ENV__ */
