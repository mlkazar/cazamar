#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <poll.h>
#include <errno.h>
#include <stdio.h>
#include <string>
#include <sys/types.h>
#include <inttypes.h>

#include "cthread.h"
#include "osp.h"
#include "dqueue.h"
#include "rst.h"

#include "bufsocket.h"

#define RST_COMMON_MAX_BYTES    16384

CThreadMutex Rst::Call::_timerMutex;

/* at the point this function is called, we know the # of bytes to transfer or we're
 * doing using chunked transfer encoding.
 */
int32_t
Rst::Common::rcvData()
{
    char tbuffer[RST_COMMON_MAX_BYTES];
    int32_t tlen;
    int32_t nbytes;
    int32_t chunkBytes;
    int32_t code;
    BufGen *socketp;
    uint8_t more;
    int indicatedEof = 0;

    socketp = _rstp->_bufGenp;

    if (_rcvContentLength == 0) {
        return 0;
    }

    hold();
    if (_rcvContentLength == -1) {
        indicatedEof = 0;
        while(1) {
            /* read a chunk of data: a counter in hex, that many bytes, and a crlf; if
             * the counter is 0, this is an indication of EOF, and there's still a CRLF
             * at the end of that chunk.  And there's *also* a CRLF at the end of the
             * transfer.
             */
            code = socketp->readLine(tbuffer, sizeof(tbuffer)-1);
            if (code < 0) {
                release();
                return code;
            }
            _sawDataRecently = 1;
            tbuffer[code] = 0;
            chunkBytes = nbytes = (int32_t) strtol(tbuffer, NULL, 16);

            /* now we know how many bytes to read */
            while (nbytes > 0) {
                tlen = (nbytes > (signed) sizeof(tbuffer)? sizeof(tbuffer) : nbytes);
                code = socketp->read(tbuffer, tlen);
                if (code != tlen) {
                    printf("rst: bad read chunked data %d should be %d\n", code, tlen);
                    release();
                    return RST_ERR_IO;
                }

                /* call our user */
                if (_rcvProcp && !_closed) {
                    osp_assert(tlen > 0);
                    code = _rcvProcp(_contextp, this, tbuffer, &tlen, &more);
                    if (code < 0) {
                        release();
                        return code;
                    }
                }

                nbytes -= tlen;
            } /* while reading bytes in chunk */

            /* read CRLF at end of each chunk, including EOF chunk */
            code = socketp->read(tbuffer, 2);       /* skip CRLF */
            if (code != 2 || tbuffer[0] != '\r' || tbuffer[1] != '\n') {
                printf("rst: missing crlf after chunked data\n");
                release();
                return RST_ERR_IO;
            }

            /* watch for "EOF" chunk */
            if (chunkBytes == 0) {
                break;
            }

        } /* loop over all chunks */
        if (!indicatedEof) {
            indicatedEof = 1;
            tlen = 0;
            if (!_closed) {
                code = _rcvProcp(_contextp, this, tbuffer, &tlen, &more);
                if (code < 0) {
                    release();
                    return code;
                }
            }
        }

        release();
        return 0;
    }
    else {
        nbytes = _rcvContentLength;
        while(nbytes > 0) {
            tlen = (nbytes > (signed) sizeof(tbuffer)? sizeof(tbuffer) : nbytes);
            code = socketp->read(tbuffer, tlen);
            if (code < 0) {
                release();
                return code;
            }
            else if (code == 0) {
                /* short read, we'll treat that as OK for now */
                break;
            }
            _sawDataRecently = 1;
            tlen = code;

            if (_rcvProcp && !_closed) {
                osp_assert(tlen > 0);
                code = _rcvProcp(_contextp, this, tbuffer, &tlen, &more);
            }
            else
                code = 0;

            if (code < 0) {
                release();
                return code;
            }

            /* otherwise, we'e copied tlen bytes */
            nbytes -= tlen;
        }

        /* we've hit EOF, so tell the receiver */
        if (!indicatedEof && _rcvProcp) {
            tlen = 0;
            if (!_closed)
                _rcvProcp(_contextp, this, tbuffer, &tlen, &more);
            indicatedEof = 1;
            /* once we've done this, all structures and our caller's context
             * may be freed.
             */
        }
        release();
        return 0;
    } /* else */
}

/* send data as required, returning an error code if something went wrong, which will
 * abort the connection.
 */
int32_t
Rst::Common::sendData()
{
    char tbuffer[RST_COMMON_MAX_BYTES];
    int32_t nbytes;
    int32_t code;
    uint8_t more;
    int32_t tlen;
    int32_t remaining;
    char tline[32];
    BufGen *socketp;

    socketp = _rstp->_bufGenp;

    if (_sendContentLength == 0)
        return 0;

    /* don't set the send content length if you're not going to
     * provide a way to provide the data.
     */
    osp_assert(_sendProcp != NULL);

    if (_sendContentLength == -1) {
        /* used chunked transfer mode; send a line with hex byte
         * count, followed by that many bytes, followed by a CRLF.
         * The last chunk is a 0 followed by a CRLF.  After that, we
         * have an additional CRLF terminating the chunk body.
         */
        while( 1) {
            nbytes = sizeof(tbuffer);
            code = _sendProcp(_contextp, this, tbuffer, &nbytes, &more);
            if (code < 0) {
                return code;
            }

            /* we're done */
            if (nbytes == 0)
                break;

            osp_assert(nbytes <= (signed) sizeof(tbuffer));
            sprintf(tline, "%x\r\n", nbytes);
            tlen = (int32_t) strlen(tline);
            code = socketp->write(tline, tlen);
            if (code != tlen) {
                return RST_ERR_IO;
            }
            code = socketp->write(tbuffer, nbytes);
            if (code != nbytes) {
                return RST_ERR_IO;
            }
            code = socketp->write("\r\n", 2);
            if (code != 2)
                return RST_ERR_IO;
            _sawDataRecently = 1;
        }
        code = socketp->write("0\r\n", 3);      /* last chunk */
        if (code != 3)
            return RST_ERR_IO;
        /* trailer is empty */
        code = socketp->write("\r\n", 2);       /* CRLF at end of transfer */
        if (code != 2)
            return RST_ERR_IO;
        socketp->flush();
        return 0;
    }
    else {
        /* fixed encoding, just send exactly the specified number of bytes */
        remaining = _sendContentLength;
        while(remaining > 0) {
            nbytes = sizeof(tbuffer);
            code = _sendProcp(_contextp, this, tbuffer, &nbytes, &more);
            if (code < 0) {
                return code;
            }

            /* we're done */
            if (nbytes == 0)
                break;

            osp_assert(nbytes <= remaining);
            remaining -= nbytes;
            osp_assert(nbytes <= (signed) sizeof(tbuffer));
            code = socketp->write(tbuffer, nbytes);
            if (code != nbytes) {
                return RST_ERR_IO;
            }
            _sawDataRecently = 1;
        }
        socketp->flush();
        osp_assert(remaining == 0);
        return 0;
    }
}

int32_t
Rst::Common::readCommonHeaders()
{
    Hdr *hdrp;
    int32_t code = 0;
    BufGen *socketp = _rstp->_bufGenp;
    int tc;
    char *tp;
    char tbuffer[RST_COMMON_MAX_BYTES];

    while(1) {
        code = socketp->readLine(tbuffer, sizeof(tbuffer));
        if (code < 0) {
            /* failed read */
            return code;
        }

        if (code <= 1) {
            /* blank line */
            code = 0;
            break;
        }

        /* otherwise, we have a line of the form 'x: y', parse and add
         * to received lines.
         */
        tp = strchr(tbuffer, ':');
        if (tp == NULL) {
            printf("Rst: !!missing ':' in header line %s\n", tbuffer);
            return RST_ERR_HEADER_FORMAT;
        }

        hdrp = new Hdr();
        hdrp->_key = std::string(tbuffer, tp-tbuffer);
        tp++;   /* skip ':' */

        /* skip spaces */
        while(1) {
            tc = *tp;
            if (whiteSpace(tc)) {
                tp++;
                continue;
            }
            break;
        }
        hdrp->_value = std::string(tp);

        _rcvHeadersp->append(hdrp);
    }
    return code;
}

int32_t
Rst::Common::parseCommonHeaders()
{
    Hdr *hdrp;
    size_t pos;
    std::string cookieLine;

    /* if we get here, all of the headers have been received; parse out the interesting ones */
    _rcvContentLength = 0;
    for(hdrp = _rcvHeadersp->head(); hdrp; hdrp=hdrp->_dqNextp) {
        if (strcasecmp(hdrp->_key.c_str(), "content-type") == 0) {
            _rcvContentType = hdrp->_value;
        }
        else if (strcasecmp(hdrp->_key.c_str(), "content-length") == 0) {
            _rcvContentLength = (int32_t) strtol(hdrp->_value.c_str(), NULL, 10);
        }
        else if (strcasecmp(hdrp->_key.c_str(), "transfer-encoding") == 0) {
            if (strcasecmp(hdrp->_key.c_str(), "identity") != 0)
                _rcvContentLength = -1;
        }
        else if (strcasecmp(hdrp->_key.c_str(), "host") == 0) {
            _rcvHost = hdrp->_value;
        }
        else if (strcasecmp(hdrp->_key.c_str(), "cookie") == 0) {
            pos = hdrp->_value.find("id=");
            if (pos != std::string::npos) {
                _cookieId = "id";
                cookieLine = hdrp->_value.substr(pos+3);
                pos = cookieLine.find(';');
                if (pos == std::string::npos) 
                    _cookieValue = cookieLine;
                else
                    _cookieValue = cookieLine.substr(0, pos);
            }
        }
    }

    return 0;
}

void
Rst::Common::setSendContentLength(int32_t length) {
    _sendContentLength = length;
}

Rst::Common::Common()
{
    _refCount = 1;
    _closed = 0;
    _rstp = NULL;
    _sendProcp = NULL;
    _sendHeadersp = NULL;
    _rcvProcp = NULL;
    _rcvHeadersp= NULL;
    _headersDoneProcp = NULL;
    _contextp = NULL;
    _error = 0;
    _sawDataRecently = 0;
    _httpError = 0;
    _rcvContentLength = 0;
    _sendContentLength = 0;
    _setPlaylistHost = 0;
    _inboundData = 0;
    _outboundData = 0;
}

void
Rst::Common::close()
{
    BufGen *socketp = _rstp->_bufGenp;
    _closed = 1;
    if (socketp) {
        socketp->disconnect();
    }
    release();
}

void
Rst::Common::hold()
{
    _refCount++;
}

void
Rst::Common::release()
{
    if (--_refCount == 0) {
        delete this;
    }
}

/* host comes from Rst object.  For calls that send data, you must
 * call setSendContentLength on the call before calling init (although you
 * can set it to -1 for chunked transfers).  For calls that receive
 * data, the transfer length will be set in the call before
 * headersDoneProcp is called, after which rcvProcp will be invoked as
 * data arrives.
 */
int32_t
Rst::Call::init( const char *relPathp,
                 CopyProc *sendProcp,
                 HdrQueue *sendHeadersp,
                 CopyProc *rcvProcp,
                 HdrQueue *rcvHeadersp,
                 CompletionProc *headersDoneProcp,
                 CompletionProc *allDoneProcp,
                 void *contextp)
{
    int32_t code;

    _relPathp = relPathp;
    _sendProcp = sendProcp;
    _sendHeadersp = sendHeadersp;
    _rcvProcp = rcvProcp;
    _rcvHeadersp = rcvHeadersp;
    _headersDoneProcp = headersDoneProcp;
    _allDoneProcp = allDoneProcp;
    _inputDoneProcp = NULL;
    _contextp = contextp;
    _timerp = NULL;

    _error = 0;
    _httpError = 200;
    _setPlaylistHost = 0;

    code = sendOperation();

    return code;
}

/* static */ int32_t
Rst::splitUrl(std::string url, std::string *hostp, std::string *pathp, uint16_t *defaultPortp)
{
    char *tp;
    char *urlStrp;
    char *portp;
    uint16_t defaultPort;

    urlStrp = const_cast<char *>(url.c_str());

    if (strncasecmp(urlStrp, "http://", 7) == 0) {
        url.erase(0,7);
        urlStrp = const_cast<char *>(url.c_str());
        defaultPort = 80;
    }
    else if (strncasecmp(urlStrp, "https://", 8) == 0) {
        url.erase(0,8);
        urlStrp = const_cast<char *>(url.c_str());
        defaultPort = 443;
    }
    else {
        defaultPort = 80;
    }

    tp = strchr(urlStrp, '/');
    if (tp == NULL) {
        /* no '/', so relPath is / and everything else is hostname */
        *hostp = url;
        *pathp = std::string("/");
    }
    else {
        *hostp = std::string(urlStrp, tp-urlStrp);
        *pathp = std::string(tp);
    }

    /* if there's a ':' in the hostname, it is the TCP port to use */
    portp = index(const_cast<char *>(hostp->c_str()), ':');
    if (portp != NULL) {
        defaultPort = atoi(portp+1);
    }

    if (defaultPortp)
        *defaultPortp = defaultPort;
    return 0;
}

int32_t
Rst::Call::sendOperation()
{
    std::string firstLine;
    int32_t code;
    char tbuffer[RST_COMMON_MAX_BYTES];
    Hdr *hdrp;
    BufGen *socketp = _rstp->_bufGenp;
    char *tp;

    /* start timer */
    _timerp = new OspTimer();
    _timerp->init(60000, &Rst::Call::callExpired, this);

    firstLine = _op;
    firstLine += " ";
    firstLine += _relPathp;
    firstLine += " HTTP/1.1\r\n";

    firstLine += "Host: ";
    firstLine += *socketp->getHostAndPort();
    firstLine += "\r\n";

#if 0
    firstLine += "Range: 0-\r\n";

    firstLine += "Connection: close\r\n";

    firstLine += "Icy-MetaData: 1\r\n";
#endif

    for(hdrp = _rstp->_baseHeaders.head(); hdrp; hdrp=hdrp->_dqNextp) {
        firstLine += (hdrp->_key + ": " + hdrp->_value + "\r\n");
    }

    if (_sendHeadersp) {
        for(hdrp = _sendHeadersp->head(); hdrp; hdrp=hdrp->_dqNextp) {
            firstLine += (hdrp->_key + ": " + hdrp->_value + "\r\n");
        }
    }

    if (_sendProcp != NULL && _sendContentLength != 0x7FFFFFFF) {
        sprintf(tbuffer, "Content-Length:%d\r\n", _sendContentLength);
        firstLine += std::string(tbuffer);
        if (_sendContentLength == -1) {
            firstLine += std::string("Transfer-Encoding:chunked\r\n");
        }
    }

    /* put final double blank line out to separate headers from request */
    firstLine += "\r\n";

    /* message body, if any, goes here, using sendProcp */

    code = (int32_t) socketp->write((char *) firstLine.c_str(),
                                    (int32_t) firstLine.length());
    if (code < 0) {
        finished();
        return code;
    }

    code = socketp->flush();
    if (code < 0) {
        _error = 100;
        finished();
        return code;
    }

    if ((_op == "PUT" || _op == "POST") && _sendContentLength != 0) {
        code = sendData();
        if (code < 0) {
            _error = 100;
            finished();
            return code;
        }
    }

    code = socketp->readLine(tbuffer, sizeof(tbuffer));
    if (code < 0) {
        _error = 100;
        finished();
        return code;
    }

    /* skip "HTTP/1.x or ICY " */
    if (strncasecmp(tbuffer, "ICY", 3) != 0 &&
        strncasecmp(tbuffer, "HTTP", 4) != 0) {
        /* serious protocol error, reset the connection and fail.  It will get reopened
         * on the next call.
         */
        _error = 200;
        socketp->disconnect();
        finished();
        return -2;
    }

    tp = strchr(tbuffer, ' ');
    if (tp)
        code = (int32_t) strtol(tp, NULL, 10);
    else
        code = (int32_t) strtol(tbuffer, NULL, 10);
    _httpError = code;

    if (_rstp->_verbose && (code < 200 || code >= 300)) {
        printf("Rst: **error response with HTTP code=%d for %s\n", code, _relPathp);
    }

    /* read header lines */
    code = readCommonHeaders();
    if (code < 0) {
        finished();
        return code;
    }

    parseCommonHeaders();

    /* before upcalling the data, make sure that doneProc has been
     * called so that protocol specific header processing can happen
     * first.
     */
    if (_headersDoneProcp) {
        _headersDoneProcp(_contextp, this, 0, _httpError);
    }

    if (_rcvContentLength != 0) {
        code = rcvData();
        if (code < 0) {
            code = 300;
            finished();
            return code;
        }
    }

    /* call socketp->disconnect() here if you want to close after each call */

    /* mark call done */
    finished();

    return 0;
}

/* static */ void
Rst::Call::callExpired(OspTimer *timerp, void *contextp)
{
    Rst::Call *callp = (Rst::Call *) contextp;

    /* we need a mutex that will remain allocated even if the call is freed and 
     * the timer canceled (while timer thread is preparing the callback).  It's held
     * over very short periods, so we just make it static.
     */
    _timerMutex.take();
    if (timerp->canceled()) {
        /* don't reference callp in this branch since it may be free; note that
         * _timerMutex is static for this reason.
         */
        _timerMutex.release();
        return;
    }
    
    /* if data is still flowing for this call, either in or out, don't
     * abort based on a timeout.
     */
    if (callp->_sawDataRecently) {
        callp->_timerp = new OspTimer();
        callp->_timerp->init(60000, &Rst::Call::callExpired, callp);
        _timerMutex.release();
        return;
    }

    /* canceled by timer system on callback */
    callp->_timerp = NULL;

    printf("timer expired -- call=%p bufgen=%p\n", callp, callp->_rstp->_bufGenp);

    /* abort the call; this will close the socket and mark it to be reopened,
     * which should abort all pending reads and writes to the socket.
     */
    callp->_rstp->_bufGenp->abort();

    /* and set a new timer, in case the next try through gets messed up; note that this
     * must happen while still holding timerMutex, so that we don't race with
     * the call 'finished' code.
     */
    callp->_timerp = new OspTimer();
    callp->_timerp->init(60000, &Rst::Call::callExpired, callp);

    _timerMutex.release();
}

void
Rst::Call::finished()
{
    /* stop timer */
    _timerMutex.take();
    if (_timerp) {
        _timerp->cancel();
        _timerp = NULL;
    }
    _timerMutex.release();

    if (_allDoneProcp) {
        _allDoneProcp(_contextp, this, _error, _httpError);
    }
    
}

Rst::Call::Call(Rst *rstp)
{
    _rstp = rstp;
    _op = "GET";
}

Rst::Call::~Call()
{
    osp_assert(_timerp == NULL);

    /* delete rcvHeaders and sendHeaders */
    if (_rcvHeadersp) {
        freeHeaders(_rcvHeadersp);
        _rcvHeadersp = NULL;
    }

    /* leave sendHeaders alone, since they're owned by our caller */
}

/* Call this to receive an incoming call from a connection.  If the connection
 * is closed, you'll get a RST_ERR_CLOSED return code from init.
 *
 * For requests that receive incoming data, the count will be stored in the
 * Request structure by the time the headersDoneProcp is called; it is called
 * once the request header has been received and parsed.  The count may of course
 * be -1 if we have a chunked transfer encoding.
 *
 * For requests that send outgoing data, the headersDoneProcp is
 * responsible for calling setSendContentLength on the Request
 * structure once the callback has examined the request information.
 */
int32_t
Rst::Request::init( CopyProc *sendProcp,
                    HdrQueue *sendHeadersp,
                    CopyProc *rcvProcp,
                    HdrQueue *rcvHeadersp,
                    CompletionProc *headersDoneProcp,
                    CompletionProc *inputDoneProcp,
                    void *contextp)
{
    std::string key;
    std::string value;
    char tbuffer[RST_COMMON_MAX_BYTES];
    int code;
    char *tp;
    BufGen *socketp;
    char *methodp;
    char *urlp;
    std::string firstLine;
    Rst::Hdr *hdrp;
    size_t qpos;
    std::string urlItem;
    std::string urlParms;

    _sendProcp = sendProcp;
    _sendHeadersp = sendHeadersp;
    _rcvProcp = rcvProcp;
    _rcvHeadersp = rcvHeadersp;
    _headersDoneProcp = headersDoneProcp;
    _inputDoneProcp = inputDoneProcp;
    _contextp = contextp;
    _httpError = 200;
    socketp = _rstp->_bufGenp;

    while(1) {
        code = socketp->readLine(tbuffer, sizeof(tbuffer)-1);
        if (code <= 0) {
            abort(code);
            return code;
        }

        /* note that readline returns the # of bytes, *including* the terminating
         * null char.  And also note that some HTTP clients incorrectly send a
         * blank line at the end of POST operations, which manifests itself as a
         * blank line (1 character for the null) here.  So, skip those.
         */
        if (code > 1)
            break;
    }

    /* we have METHOD <SP> URI <SP> version */
    methodp = tbuffer;
    tp = strchr(tbuffer, ' ');
    if (!tp) {
        printf("Rst: no method line\n");
        abort(RST_ERR_HEADER_FORMAT);
        return RST_ERR_HEADER_FORMAT;
    }
    *tp=0;
    _op = std::string(tbuffer);
    urlp = tp+1;
    tp = strchr(urlp, ' ');
    if (!tp) {
        printf("Rst: no URL in first line\n");
        abort(RST_ERR_HEADER_FORMAT);
        return RST_ERR_HEADER_FORMAT;
    }
    *tp = 0;
    _url = std::string(urlp);

    code = readCommonHeaders();
    if (code) {
        abort(code);
        return code;
    }

    parseCommonHeaders();

    /* split a URL like "/foo?bar=modes&a=b" into _baseUrl==/foo with urlPairs
     * have two Hdr items, one with _key==bar value==modes, and one with _key==a
     * and _value == b.
     */
    resetUrlPairs();
    qpos = _url.find('?');
    if (qpos != std::string::npos) {
        _baseUrl = _url.substr(0,qpos);
        urlParms = _url.substr(qpos+1);

        /* now parse the KV pairs; if there's no equal sign in an
         * item, just set the key to the data, and leave value empty.
         */
        while(urlParms.length() > 0) {
            qpos = urlParms.find('&');
            if (qpos == std::string::npos) {
                urlItem = urlParms;
                urlParms.clear();
            }
            else  {
                /* copy in the item */
                urlItem = urlParms.substr(0, qpos);
                urlParms = urlParms.substr(qpos+1);
            }

            /* at this point, urlItem has either something of the form 'a=b' or just 'c';
             * we create an Rst::Hdr item holding either the key and the value, or just
             * a key in the 'c' case.
             */
            qpos = urlItem.find('=');
            hdrp = new Rst::Hdr();
            if (qpos == std::string::npos) {
                hdrp->_key = urlItem;
                /* value is still empty after construction */
            }
            else {
                hdrp->_key = urlItem.substr(0, qpos);
                hdrp->_value = urlItem.substr(qpos+1);
            }
            _urlPairs.append(hdrp);
        }
    }
    else {
        _baseUrl = _url;
    }

    if (_op == "POST" || _op == "PUT")
        _inboundData = 1;

    /* call out to our user */
    _headersDoneProcp(contextp, this, 0, 200);

    /* GETs shouldn't be sending us data, but PUTs and POSTs can also return data */
    if (_inboundData && _rcvContentLength != 0) {
        rcvData();
    }

    /* notify our user that all data has been delivered to the receive proc */
    if (_inputDoneProcp) {
        _inputDoneProcp(contextp, this, 0, 200);
    }

    if (_httpError == 200) {
        firstLine = "HTTP/1.1 200 OK\r\n";
    }
    else {
        sprintf(tbuffer, "HTTP/1.1 %d NotOK\r\n", _httpError);
        firstLine = tbuffer;
    }

    if (_sendContentLength != 0) {
        if (_sendContentLength != -1) {
            sprintf(tbuffer, "Content-Length:%d\r\n", _sendContentLength);
            firstLine += std::string(tbuffer);
        }
        else {
            firstLine += std::string("Transfer-Encoding:chunked\r\n");
        }
    }
    else {
        firstLine += "Content-Length:0\r\n";
    }
    for(hdrp = _sendHeadersp->head(); hdrp; hdrp=hdrp->_dqNextp) {
        firstLine += (hdrp->_key + ":" + hdrp->_value + "\r\n");
    }
    firstLine += "\r\n";

    code = (int32_t) socketp->write( firstLine.c_str(),
                                     (int32_t) firstLine.length());

    if (_sendContentLength != 0) {
        sendData();
    }

    socketp->flush();

    return 0;
}

void
Rst::Request::abort(int32_t httpCode)
{
    return;
}

Rst::Request::Request(Rst *rstp)
{
    _rstp = rstp;
}

Rst::Request::~Request()
{
    /* sendHeadersp belongs to caller */
    freeHeaders(_rcvHeadersp);
    return;
}

/* static */ void
Rst::freeHeaders(HdrQueue *hqp)
{
    Hdr *hdrp;
    while((hdrp = hqp->pop()) != NULL) {
        delete hdrp;
    }
}

/* static */ void
Rst::dumpBytes(char *bufferp, uint64_t inOffset, uint32_t count)
{
    uint32_t i;
    uint32_t lineMax;
    uint32_t ix;
    char tbuffer[256];
    unsigned char inc;
    unsigned char outc;
    static const uint32_t maxPerLine = 16;
    char *hexOutp;
    char *charOutp;

    for(ix = 0; ix<count;) {
        lineMax = (count > maxPerLine? maxPerLine : count);
        memset(tbuffer, ' ', sizeof(tbuffer));
        hexOutp = tbuffer;
        charOutp = tbuffer + 3*maxPerLine + maxPerLine/4+2;
        for(i=0; i<lineMax; i++) {
            inc = bufferp[ix+i];

            /* put out first character */
            if ((inc&0xF0) < 0xA0)
                outc = '0' + (inc>>4);
            else
                outc = 'A' + ((inc>>4) - 0xa);
            *hexOutp++ = outc;

            /* put out second character */
            if ((inc&0xF) < 0xA)
                outc = '0' + (inc&0xF);
            else
                outc = 'A' + ((inc&0xF) - 0xa);
            *hexOutp++ = outc;

            *hexOutp++ = ' ';
            if ((i&0x3) == 0x3)
                *hexOutp++ = ' ';

            /* and put out the character at end */
            outc = inc;
            if (outc < 0x20 || outc >= 0x7f) {
                outc = '.';
            }
            *charOutp++ = outc;
            if ((i&0x3) == 0x3)
                *charOutp++ = ' ';
        }

        /* null terminate and print the string */
        *charOutp++ = 0;
        printf("%08llx: %s\n", (long long) inOffset + ix, tbuffer);

        ix += lineMax;
    }
}

/* static */ std::string
Rst::urlDecode(std::string *inStrp)
{
    const char *inp = inStrp->c_str();
    std::string result;
    int tc;
    int uc;

    while((tc = *inp++) != 0) {
        if (tc == '%') {
            /* decode next two chars */
            uc = *inp++;
            if (uc >= '0' && uc <= '9')
                tc = (uc - '0');
            else if (uc >= 'A' && uc <= 'F')
                tc = uc - 'A' + 10;
            else if (uc >= 'a' && uc <= 'f')
                tc = uc - 'a' + 10;
            else
                tc = 0;
            tc = tc<<4;         /* first digit of hex number */

            uc = *inp++;
            if (uc >= '0' && uc <= '9')
                tc += (uc - '0');
            else if (uc >= 'A' && uc <= 'F')
                tc += uc - 'A' + 10;
            else if (uc >= 'a' && uc <= 'f')
                tc += uc - 'a' + 10;

            result.append(1, tc);

        }
        else
            result.append(1, tc);
    }

    return result;
}

/* static */std::string
Rst::urlEncode(std::string *inStrp)
{
    const char *inp = inStrp->c_str();
    std::string result;
    int tc;
    int digit;

    while((tc = *inp++) != 0) {
        if ( (tc >= '0' && tc <= '9') ||
             (tc >= 'a' && tc <= 'z') ||
             (tc >= 'A' && tc <= 'Z') ||
             tc == '-' ||
             tc == '.' ||
             tc == '_' ||
             tc == '~') {
            /* these must never be encoded */
            result.append(1, tc);
        }
        else {
            /* encode all others */
            result.append(1,'%');

            digit = (tc & 0xF0) >> 4;
            if (digit < 10)
                result.append(1, '0' + digit);
            else
                result.append(1, 'A' + digit - 10);

            digit = (tc & 0xF);
            if (digit < 10)
                result.append(1, '0' + digit);
            else
                result.append(1, 'A' + digit - 10);
        }
    }
    return result;
}

/* static */std::string
Rst::urlPathEncode(std::string inStr)
{
    const char *inp = inStr.c_str();
    std::string result;
    int tc;
    int digit;

    while((tc = *inp++) != 0) {
        if ( (tc >= '0' && tc <= '9') ||
             (tc >= 'a' && tc <= 'z') ||
             (tc >= 'A' && tc <= 'Z') ||
             tc == '/' ||
             tc == '-' ||
             tc == '.' ||
             tc == '_' ||
             tc == '~' ||
             tc == '@' ||
             tc == '!' ||
             tc == '$' ||
             tc == '&' ||
             tc == '\'' ||
             tc == '(' ||
             tc == ')' ||
             tc == '*' ||
             tc == '+' ||
             tc == ',' ||
             tc == '=') {
            /* these are listed as not to be encoded in MS paths.
             * Supposedly ':' and ';' also fit the bill, but we're
             * ignoring that for now.  Colon is a delimeter in the MS
             * paths, and ';' isn't listed in the Apple code they
             * recommend.
             */
            result.append(1, tc);
        }
        else {
            /* encode all others */
            result.append(1,'%');

            digit = (tc & 0xF0) >> 4;
            if (digit < 10)
                result.append(1, '0' + digit);
            else
                result.append(1, 'A' + digit - 10);

            digit = (tc & 0xF);
            if (digit < 10)
                result.append(1, '0' + digit);
            else
                result.append(1, 'A' + digit - 10);
        }
    }
    return result;
}

int
Rst::whiteSpace(int tc)
{
    if (tc == ' '||tc == '\t' || tc == '\r' || tc == '\n')
        return 1;
    else
        return 0;
}
