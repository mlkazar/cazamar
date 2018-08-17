#ifndef __RST_H_ENV__
#define __RST_H_ENV__ 1

#include <string>

#include "osp.h"
#include "dqueue.h"
#include "bufgen.h"

#define RST_ERR_IO                      (-1)
#define RST_ERR_EOF                     (-2)
#define RST_ERR_HEADER_FORMAT           (-3)
#define RST_ERR_RESET                   (-4)

class Rst {
 public:
    /* forward dcls */
    class Call;
    class Request;
    class Common;

    class Hdr {
    public:
        Hdr *_dqNextp;
        Hdr *_dqPrevp;
        std::string _key;
        std::string _value;

        Hdr() {}

        Hdr(const char *keyp, const char *valuep) {
            _key = std::string(keyp);
            _value = std::string(valuep);
        }

        ~Hdr() {
            return;
        }
    };

    typedef dqueue<Hdr> HdrQueue;

    /* when sending data (supplying data to Rst) bufferSize represents the max available
     * to send in this one buffer.  You can send less (but not zero), by adjusting *bufferSizep
     * down.  If you have more data to supply, set *morep to true, otherwise set it to false
     * (the default).
     *
     * when receiving data (from Rst), buffer has data and *bufferSizep has the count.  You
     * must consume all the data.  In this direction, the morep parameter is ignored.
     */
    typedef int32_t CopyProc(void *contextp,
                             Common *commonp,
                             char *bufferp,
                             int32_t *bufferSizep,
                             uint8_t *morep);

    /* if errorCode is 0, httpCode is from HTTP request */
    typedef void CompletionProc(void *contextp,
                                Common *commonp,
                                int32_t errorCode,
                                int32_t httpCode);

    class Common {
    protected:
        int32_t _refCount;
        uint8_t _closed;
        Rst *_rstp;

    public:
        CopyProc *_sendProcp;
        HdrQueue *_sendHeadersp;
        CopyProc *_rcvProcp;
        HdrQueue *_rcvHeadersp;
        CompletionProc *_headersDoneProcp;
        CompletionProc *_inputDoneProcp;
        void *_contextp;
        int32_t _error;
        int32_t _httpError;
        std::string _cookieId;
        std::string _cookieValue;
    protected:
        std::string _rcvContentType;
        std::string _rcvHost;
        int32_t _rcvContentLength;
        int32_t _sendContentLength;
        uint8_t _setPlaylistHost;
        uint8_t _inboundData;   /* PUT or POST */
        uint8_t _outboundData;  /* GET */
        uint8_t _isPost;

        int32_t rcvData();

        int32_t sendData();

        int32_t readCommonHeaders();

        int32_t parseCommonHeaders();

    public:

        int hasOutboundData() {
            return _outboundData;
        }

        int hasInboundData() {
            return _inboundData;
        }

        /* set to -1 for chunked transfer size, 0 for no data */
        void setSendContentLength(int32_t length);

        /* for use with icecast, which doesn't send back a content length */
        void setRcvContentLength(int32_t length) {
            _rcvContentLength = length;
        }

        int32_t getRcvContentLength() {
            return _rcvContentLength;
        }

        Rst *getRst() {
            return _rstp;
        }

        std::string *getRcvHost() {
            return &_rcvHost;
        }

        BufGen *getSocket() {
            return _rstp->_bufGenp;
        }

        void hold();

        void release();

        void close();

        int isClosed() {
            return _closed;
        }

        Common();
    };

    class Call : public Common {
        const char *_relPathp;
        std::string _playlistHost;
        std::string _op;
        
    public:
        /* headersDoneProcp is called after receiving the headers in the response */
        int32_t init( const char *relPathp,
                      CopyProc *sendProcp,
                      HdrQueue *sendHeadersp,
                      CopyProc *rcvProcp,
                      HdrQueue *rcvHeadersp,
                      CompletionProc *headersDoneProcp,
                      void *contextp);

        /* abort a call in progress */
        void abort(int32_t httpCode);

        int32_t sendOperation();

        Call(Rst *rstp);

        ~Call();

        std::string *inContentType() {
            return &_rcvContentType;
        }

        void doPost() {
            _op = "POST";
        }

        void doPut() {
            _op = "PUT";
        }

        int32_t httpError() {
            return _httpError;
        }

        void setPlaylistHost() {
            _setPlaylistHost = 1;
        }

        std::string *getPlaylistHost() {
            if (_setPlaylistHost)
                return &_playlistHost;
            else
                return NULL;
        }
    };

    class Request : public Common {
        std::string _url;
        std::string _baseUrl;
        std::string _op;
        std::string _sessionCookie;
        dqueue<Hdr> _urlPairs;

    public:
        int32_t init( CopyProc *sendProcp,
                      HdrQueue *sendHeadersp,
                      CopyProc *rcvProcp,
                      HdrQueue *rcvHeadersp,
                      CompletionProc *headersDoneProcp,
                      CompletionProc *inputDoneProcp,
                      void *contextp);

        void abort(int32_t httpCode);

        /* must be called in headersDone callback or inputDone callback */
        void setHttpError(int32_t httpError) {
            _httpError = httpError;
        }

        std::string *getRcvOp() {
            return &_op;
        }

        std::string *getRcvUrl() {
            return &_url;
        }

        std::string *getBaseUrl() {
            return &_baseUrl;
        }

        dqueue<Hdr> *getUrlPairs() {
            return &_urlPairs;
        }

        void resetUrlPairs() {
            Hdr *hdrp;
            while((hdrp = _urlPairs.head()) != NULL) {
                delete hdrp;
            }
        }

        ~Request();

        Request(Rst *rstp);
    };

    BufGen *_bufGenp;
    std::string _hostName;
    dqueue<Hdr> _baseHeaders;
    uint8_t _verbose;

    int32_t init(BufGen *sockp) {
        _bufGenp = sockp;
        _verbose = 0;
        return 0;
    }

    void setVerbose() {
        _verbose = 1;
    }

    void resetBaseHeaders() {
        Hdr *hdrp;
        while((hdrp = _baseHeaders.head()) != NULL) {
            delete hdrp;
        }
    }

    void addBaseHeader(const char *keyp, const char *valuep) {
        Hdr *newp;
        newp = new Hdr;
        newp->_key = std::string(keyp);
        newp->_value = std::string(valuep);
        _baseHeaders.append(newp);
    }

    static int whiteSpace(int tc);

    BufGen *getBufGen() {
        return _bufGenp;
    }

    /* static utility function to split "http://foo:800/bar" into "foo:8000" and "/bar"
     *
     * splits "foo" into "foo" and "/", i.e. special-cases non-existent relative path
     * to be "/".
     */
    static int32_t splitUrl( std::string url,
                             std::string *hostp,
                             std::string *pathp,
                             uint16_t *defaultPortp = 0);

    static void freeHeaders(HdrQueue *hqp);

    static void dumpBytes(char *bufferp,  uint64_t inOffset, uint32_t count);

    static std::string urlEncode(std::string *inp);

    static std::string urlPathEncode(std::string inStr);

    static std::string urlDecode(std::string *inp);
};

#endif /*  __RST_H_ENV__ */
