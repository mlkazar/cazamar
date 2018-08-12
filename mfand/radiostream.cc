#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <poll.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>

#include "cthread.h"
#include "dqueue.h"
#include "rst.h"
#include "radiostream.h"

RadioStream::RadioStream()
{
    /* init stuff that must be changed right after construction */
    _saveIcyData = 0;
    _closed = 0;
    _refCount = 1;
    _baseTimeoutMs = 16000;     /* default */
    return;
}

RadioStream::~RadioStream()
{
    return;
}

int32_t
RadioStream::scanForLocationHeader( Rst::Call *callp, const char *hostNamep, std::string *resultp)
{
    Rst::HdrQueue *queuep = callp->_rcvHeadersp;
    Rst::Hdr *hdrp;

    if (!queuep) {
        resultp->erase();
        return -1;
    }

    for(hdrp = queuep->head(); hdrp; hdrp = hdrp->_dqNextp) {
        if (strcasecmp(hdrp->_key.c_str(), "location") == 0) {
            /* found location keyword */
            if (strncmp(hdrp->_value.c_str(), "http:", 5) == 0) {
                *resultp = hdrp->_value;
            }
            else if (strncmp(hdrp->_value.c_str(), "https:", 6) == 0) {
                // *resultp = hdrp->_value;
                /* HTTPS unsupported at present */
                printf("radiostream: https unsupported in '%s'\n", hdrp->_value.c_str());
                resultp->erase();
                return -1;
            }
            else {
                /* relative */
                *resultp = std::string(hostNamep) + hdrp->_value;
            }
            return 0;
        }
    }

    /* no Location: header found */
    resultp->erase();
    return -1;
}

void
RadioStream::scanForFile(char *bufferp, std::string *resultp)
{
    int startOfLine = 1;
    char *tp;
    char *startp;
    int tc;
    std::string result;

    tp = bufferp;
    while(1) {
        if (startOfLine && strncasecmp(tp, "file", 4) == 0) {
            /* get line after = */
            break;
        }
        else {
            tc = *tp++;
            if (tc == 0)
                return;
            else if (tc == '\n')
                startOfLine = 1;
            else
                startOfLine = 0;
        }
    }

    /* found ^FILE, look for data between '=' and newline (CR or LF) */
    while(1) {
        tc = *tp;
        if (tc == 0) { 
            /* reached end of string without seeing '=', so fail */
            return;
        }
        else if (tc == '=') {
            tp++;
            break;
        }
        tp++;
    }

    /* tp is now one char past the '=' */
    startp = tp;

    while(1) {
        tc = *tp;
        if (tc == 0) { 
            /* reached end of string without seeing newline, so fail */
            return;
        }
        else if (tc == '\r' || tc == '\n') {
            break;
        }
        tp++;
    }

    /* chars between startp up to but not including tp are returned */
    result = std::string(startp, tp-startp);
    *resultp = result;
}

void
RadioStream::scanForURL(char *bufferp, std::string *resultp)
{
    int startOfLine = 1;
    char *tp;
    char *startp;
    int tc;
    std::string result;

    tp = bufferp;
    while(1) {
        if (startOfLine && strncasecmp(tp, "http", 4) == 0) {
            /* get line after = */
            break;
        }
        else {
            tc = *tp++;
            if (tc == 0)
                return;
            else if (tc == '\n')
                startOfLine = 1;
            else
                startOfLine = 0;
        }
    }

    /* remember char position of first character in line */
    startp = tp;

    /* found ^http, look for data until newline (CR or LF) */
    while(1) {
        tc = *tp;
        if (tc == 0 || tc == '\r' || tc == '\n') { 
            break;
        }
        tp++;
    }

    /* chars between startp up to but not including tp are returned */
    result = std::string(startp, tp-startp);
    *resultp = result;
}

void
RadioStream::haveHeaders(void *contextp, Rst::Common *creqp, int32_t error, int32_t httpError)
{
    Rst::Hdr *hdrp;
    RadioStream *radiop = (RadioStream *) contextp;

    if (httpError < 200 || httpError >= 300) {
        return;
    }

    for(hdrp = radiop->_rcvHeaders.head(); hdrp; hdrp=hdrp->_dqNextp) {
        if (strcasecmp(hdrp->_key.c_str(), "icy-metaint") == 0) {
            radiop->_icyMetaInt = atoi(hdrp->_value.c_str());
            radiop->_icyDataRemaining = radiop->_icyMetaInt;
        }
    }

    radiop->_icyReadingMeta = 0;
    radiop->_icyMetaRemaining = 0;
    radiop->_controlBytesReceived = 0;

    /* make the stream look infinite if length is set to 0 */
    if (creqp->getRcvContentLength() == 0)
        creqp->setRcvContentLength(0x7FFFFFFF);
}

void
RadioStream::upcallMetaData()
{
    EvSongChangedData changedData;
    int32_t metaLen;
    int32_t i;
    int32_t j;
    int tc;
    int found;

    if (!_controlProcp)
        return;

    /* see if we can find a stream title in the meta data */
    metaLen = (int32_t) strlen(_icyMetap);
    found = 0;
    for(i=0; i<metaLen - 12; i++) {
        if (strncasecmp(_icyMetap+i, "StreamTitle='", 13) == 0) {
            found = 1;
            /* pull out the actual title, ending with a single quote */
            for(j=i+13;j<metaLen;j++) {
                tc = _icyMetap[j];
                if (tc == 0 || tc == '\'')
                    break;
                changedData._song.append(1, tc);
            }
        }
    }

    if (!found)
        changedData._song = _icyMetap;
    _controlProcp(_contextp, this, eventSongChanged, &changedData);
}

/* receive the metadata -- upcalls ICY metadata information to the
 * metadata callback, but note that dataproc  upcall doesn't include
 * the ICY metadata; it's just a pure music stream, although it may include
 * parts of a packet/record.
 */
/* static */ int32_t
RadioStream::rcv( void *contextp,
                  Rst::Common *commonReqp,
                  char *abufferp,
                  int32_t *lenp,
                  uint8_t *morep)
{
    char *bufferp;
    uint32_t len = *lenp;
    uint32_t tlen;
    std::string targetStr;
    RadioStream *radiop = (RadioStream *) contextp;
    Rst::Call *callp;
    int32_t contentLength;
    int32_t httpError;

    /* before doing *anything* with the radiop context, it is possible
     * that we've been deleted, and yet a pending callback is coming
     * up from the Rst module.  If that's the case, the closed flag
     * will be set on the commonReqp, and we may crash if we actually
     * access *radiop at all.  So, check this case now, and bail out
     * if that's the case.
     *
     * We should really be holding our own mutex over our own close
     * call, and between the isClosed check below and the hold, so
     * that if we pass the closed check, we won't free our storage
     * until the upcall has completed.
     */
    if (commonReqp->isClosed())
        return -1;

    callp = radiop->_callp;

    httpError = callp->httpError();

    if (len > 0)
        radiop->_failedCallsSinceData = 0;

    /* keep object around */
    radiop->hold();

    contentLength = callp->getRcvContentLength();

    if (httpError < 200 || httpError >= 300) {
        return -1;
    }

    /* acknowledge EOF */
    if (len == 0) {
        return 0;
    }

    if (strncasecmp(callp->inContentType()->c_str(), "audio/x-mpegurl", 15) == 0) {
        radiop->_controlBytesReceived += len;
        if (radiop->_controlBytesReceived > _maxControlBytes) {
            return -1;
        }
        bufferp = (char *) malloc(len+1);
        memcpy(bufferp, abufferp, len);
        bufferp[len] = 0;

        /* search for pure web URL */
        radiop->_playlistHost.erase();
        radiop->scanForURL(bufferp, &radiop->_playlistHost);
        radiop->_isIcecast = 1;

        free(bufferp);
    }
    else if ( strncasecmp(callp->inContentType()->c_str(), "audio/x-scpls", 13) == 0 ||
              strncasecmp(callp->inContentType()->c_str(), "application/pls", 15) == 0 ||
              strncasecmp(callp->inContentType()->c_str(), "application/octet-stream", 24) == 0 ||
              (contentLength > 0 && contentLength < 100000)) {
        radiop->_controlBytesReceived += len;
        if (radiop->_controlBytesReceived > _maxControlBytes) {
            return -1;
        }
        bufferp = (char *) malloc(len+1);
        memcpy(bufferp, abufferp, len);
        bufferp[len] = 0;

        /* search for "file<n>=<URL>" */
        radiop->_playlistHost.erase();
        radiop->scanForFile(bufferp, &radiop->_playlistHost);
        radiop->_isIcecast = 1;

        free(bufferp);
    }
    else {
        radiop->_streaming = 1;
        if (radiop->_icyMetaInt) {
            /* we have periodic ICY metadata */
            while(len > 0) {
                if (radiop->_icyReadingMeta) {
                    if (radiop->_icyMetaRemaining == ~0) {
                        /* at start of header; parse the length byte */
                        tlen = (*abufferp) & 0xFF;
                        if (tlen == 0) {
                            /* switch back to data */
                            radiop->_icyReadingMeta = 0;
                            radiop->_icyDataRemaining = radiop->_icyMetaInt;
                        }
                        else {
                            /* we're going to start parsing metadata */
                            radiop->_icyMetaRemaining = tlen<<4;
                            radiop->_icyMetap = radiop->_icyMetaBuffer;
                        }

                        /* skip length character */
                        abufferp++;
                        len--;
                        continue;
                    }

                    /* here we process incoming metadata */
                    tlen = (radiop->_icyMetaRemaining < len? radiop->_icyMetaRemaining : len);
                    memcpy(radiop->_icyMetap, abufferp, tlen);
                    radiop->_icyMetaRemaining -= tlen;

                    if (radiop->_icyMetaRemaining == 0) {
                        radiop->upcallMetaData();

                        /* and switch back to data processing */
                        radiop->_icyReadingMeta = 0;
                        radiop->_icyDataRemaining = radiop->_icyMetaInt;
                    }
                }
                else {
                    /* not processing metadata here */
                    tlen = (radiop->_icyDataRemaining < len? radiop->_icyDataRemaining : len);
                    if (!radiop->_closed) {
                        radiop->_dataProcp(radiop->_contextp, radiop, abufferp, tlen);
                    }
                    radiop->_icyDataRemaining -= tlen;

                    if (radiop->_icyDataRemaining == 0) {
                        /* switch to meta data processing, with an unknown length at the
                         * start.
                         */
                        radiop->_icyReadingMeta = 1;
                        radiop->_icyMetaRemaining = ~0;
                    }
                }
                abufferp += tlen;
                len -= tlen;
            }
        }
        else {
            /* no icy metadata present in stream; write the data out directly */
            if (!radiop->_closed) {
                radiop->_dataProcp(radiop->_contextp, radiop, abufferp, len);
            }
        }
    }

    radiop->_inOffset += len;

    radiop->release();

    return 0;
}

void
RadioStream::cleanup()
{
    Rst::Hdr *hdrp;

    if (_callp) {
        delete _callp;
        _callp = NULL;
    }

    if (_rstp) {
        delete _rstp;
        _rstp = NULL;
    }

    if (_bufSocketp) {
        _bufSocketp->release();
        _bufSocketp = NULL;
    }

    while((hdrp = _sendHeaders.pop()) != NULL) {
        delete hdrp;
    }
}

void
RadioStream::setupIcecast()
{
    if (!_icecastSetup) {
        _isIcecast = 1;
        _icecastSetup = 1;
        _defaultPort = 8000;
    }
}

int32_t
RadioStream::init( BufGenFactory *factoryp,
                   char *urlp,
                   dataProc *dataProcp,
                   controlProc *controlProcp,
                   void *contextp)
{
    const char *hostNamep;
    const char *pathp;
#if 0
    Rst::Hdr *hdrp;
#endif
    int32_t code;
    std::string *playlistUrlp;
    std::string hostName;
    std::string path;
    std::string url;
    std::string *icyUrlp;
    int32_t loops;

    /* parse incoming string */
    url = urlp;
    _defaultPort = 80;
    Rst::splitUrl(url, &hostName, &path, &_defaultPort);
    hostNamep = hostName.c_str();
    pathp = path.c_str();
    
    _isIcecast = 0;
    _icecastSetup = 0;
    _failedCallsSinceData = 0;
    _streaming = 0;     /* have we started streaming audio yet */
    _bufSocketp = NULL;
    _callp = NULL;

    /* save parameters */
    _dataProcp = dataProcp;
    _controlProcp = controlProcp;
    _contextp = contextp;

    /* hold a reference as long as this task is running */
    hold();

    loops = 0;
    while(1) {
        if (++loops > 8) {
            code = -2;
            break;
        }

        icyUrlp = NULL;

        _bufSocketp = factoryp->allocate();
        _bufSocketp->init((char *) hostNamep, _defaultPort);
        _bufSocketp->setTimeoutMs(_baseTimeoutMs);
        if ((code = _bufSocketp->getError()) != 0) {
            /* basically, constructor failed */
            printf("RadioStream: socket failed for host '%s'\n", hostNamep);
            break;
        }

        _rstp = new Rst();
        _rstp->addBaseHeader("User-Agent", "Firefox");
        _rstp->addBaseHeader("Accept", "*/*");
        _rstp->addBaseHeader("Connection", "close");
        _rstp->addBaseHeader("Range", "bytes=0-");
        _rstp->addBaseHeader("Icy-MetaData", "1");
        _rstp->init(_bufSocketp);
        /* host line automatically created for caller */

        _callp = new Rst::Call(_rstp);

        _inOffset = 0;  /* for printing debug */
        _icyDataRemaining=0;    /* bytes remaining before metadata starts */
        _icyMetaRemaining = 0;  /* meta data bytes to read */
        _icyReadingMeta = 0;    /* true iff reading metad data */

        _streamUrl = hostName + path;      /* save a copy for GUI */
        code = _callp->init( pathp,
                             NULL,
                             &_sendHeaders,
                             &rcv,
                             &_rcvHeaders,
                             &haveHeaders,
                             this);
        if (code < 0) {
            break;
        }

        /* if radiostream is closed, _callp should be null */
        if (_closed) {
            code = -1;
            break;
        }

        _icyMetaInt = 0;
#if 0
        for(hdrp = _rcvHeaders.head(); hdrp; hdrp=hdrp->_dqNextp){
            printf("RadioStream: header %s: %s\n", hdrp->_key.c_str(), hdrp->_value.c_str());
        }
        printf("RadioStream: >>> headers done\n");
#endif

        /* if a call terminated while streaming, we may want to restart it */
        if (_callp->_httpError >= 301 && _callp->_httpError <= 302) {
            _playlistHost.erase();
            scanForLocationHeader(_callp, hostNamep, &_playlistHost);
        }
        else if (_streaming) {
            /* if we failed while streaming, it could just be that we switched
             * networks, so we try again a few times.
             */
            if (++_failedCallsSinceData > 4) {
                /* too many failures since we've received data */
                code = -1;
                break;
            }

            /* otherwise, we want to just restart this connection */
            _controlProcp(_contextp, this, eventResync, NULL);
            sleep(2);
            cleanup();
            continue;
        }
        else if (code != 0) {
            /* not streaming, so an error is fatal */
            break;
        }

        /* check for icecast-style playlist */
        setupIcecast();

        playlistUrlp = &_playlistHost;
        if ( playlistUrlp->length() > 0) {
            /* playlist received and directs us to a new host */
            code = Rst::splitUrl(*playlistUrlp, &hostName, &path, &_defaultPort);
            cleanup();
            if (code) {
                break;
            }

            /* successfully parsed result of playlist-generated redirect */
            hostNamep = hostName.c_str();
            pathp = path.c_str();
            continue;
        }

        /* here, we've succeeded without loading a playlist or getting redirected, so we're
         * done.
         */
        code = 0;
        break;
    }

    release();  /* release the reference from above */
    return code;
}

void RadioStream::release()
{
    if (--_refCount <= 0) {
        cleanup();
        delete this;
    }
}

void
RadioStream::close() {
    _closed = 1;
    if (_callp) {
        _callp->close();        /* does release also */
        _callp = NULL;
    }
    release();
}
