#ifndef __STREAM_H_ENV__
#define __STREAM_H_ENV__ 1

#include "bufgen.h"
#include "rst.h"

class RadioStream {
 public:
    static const uint32_t _maxMeta = 4096;
    static const uint32_t _maxControlBytes = 32*1024;

    enum EvType {
        eventSongChanged = 10018,
        eventError = 10019,
        eventResync = 10020,    /* resync the stream */
    };

    class EvSongChangedData {
    public:
        std::string _group;
        std::string _song;
    };

    class EvErrorData {
    public:
        int32_t _errorCode;
    };

    typedef int32_t dataProc(void *contextp, RadioStream *radiop, char *bufferp, int32_t nbytes);
    typedef int32_t controlProc(void *contextp, RadioStream *radiop, EvType event, void *evDatap);
    int32_t init( BufGenFactory *factoryp, 
                  char *urlp,
                  dataProc *dataProcp,
                  controlProc *controlProcp,
                  void *contextp);
    
    int32_t scanForLocationHeader( Rst::Call *callp, const char *hostNamep, std::string *resultp);

    void scanForFile(char *bufferp, std::string *resultp);

    void scanForURL(char *bufferp, std::string *resultp);

    static int32_t rcv(void *contextp,
                       Rst::Common *commonReqp,
                       char *abufferp,
                       int32_t *lenp,
                       uint8_t *morep);

    static void haveHeaders(void *context, Rst::Common *creqp, int32_t error, int32_t httpError);

    /* instance variables */
    uint32_t _refCount;
    Rst::Call *_callp;
    Rst *_rstp;
    BufGen *_bufSocketp;
    char *_urlp;
    dataProc *_dataProcp;
    uint8_t _saveIcyData;               /* true if our dataproc should include metadata */
    controlProc *_controlProcp;
    void *_contextp;
    dqueue<Rst::Hdr> _sendHeaders;
    dqueue<Rst::Hdr> _rcvHeaders;
    uint32_t _defaultPort;
    uint64_t _inOffset;
    char _icyMetaBuffer[_maxMeta+4];
    char *_icyMetap;
    uint8_t _closed;
    uint32_t _baseTimeoutMs;
    uint32_t _controlBytesReceived;
    std::string _playlistHost;
    std::string _streamUrl;

    /* flag goes on if we've made it to the point where we're streaming data */
    uint8_t _streaming;

    /* # of failed calls since we last transferred data successfully; use this to
     * determine if we just switched networks (wifi to 3g) or if something
     * bigger is wrong.
     */
    uint16_t _failedCallsSinceData;

    /* icecast state, setup after header processed */
    uint8_t _isIcecast;
    uint8_t _icecastSetup;
    uint32_t _icyMetaInt;

    /* state for parsing out the meta data from the stream, into metaBuffer
     * above.
     */
    uint32_t _icyDataRemaining; /* bytes before meta data */
    uint32_t _icyMetaRemaining; /* bytes before data starts again */
    uint8_t _icyReadingMeta;    /* reading meta data now */

    RadioStream();

    /* don't change this once transfer has started */
    void setSaveIcyData(uint8_t val=1) {
        _saveIcyData = val;
    }

    void cleanup();

    void setTimeout(uint32_t timeoutMs) {
        _baseTimeoutMs = timeoutMs;
    }

    void upcallMetaData();

    std::string *getContentType() {
        if (_callp == NULL)
            return NULL;
        else {
            return _callp->inContentType();
        }
    }

    void setupIcecast();

    int isClosed() {
        return _closed;
    }

    void hold() {
        _refCount++;
    }

    std::string *getStreamUrl() {
        return &_streamUrl;
    }

    void close();

    void release();

    ~RadioStream();
};

#endif /* __STREAM_H_ENV__ */
