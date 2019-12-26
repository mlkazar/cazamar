#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

#include "osp.h"
#include "radioscan.h"
#include "buftls.h"
#include "json.h"

/* stupid statics */
CThreadMutex RadioScan::_lock;

void
RadioScan::init(CDisp *disp)
{
    _cdisp = disp;
    _cdispGroup = new CDispGroup();
    _cdispGroup->init(_cdisp);

    _xapip = new XApi();

    _dirBufp = new BufTls("");
    _dirBufp->init(const_cast<char *>("djtogoapp.duckdns.org"), 7700);
    _dirConnp = _xapip->addClientConn(_dirBufp);

    _stwBufp = new BufSocket();
    _stwBufp->init(const_cast<char *>("playerservices.streamtheworld.com"), 80);
    _stwConnp = _xapip->addClientConn(_stwBufp);

    RadioScanLoadTask *loadTaskp;
    loadTaskp = new RadioScanLoadTask();
    loadTaskp->init(this);
    _cdispGroup->queueTask(loadTaskp);
}

/* start with the object at the URL, and resolve it until we get to an
 * actual stream URL.  In the mean time, handle the case where we get
 * a 3XX redirect, or we get a bunch of text in PLS format, or a URL
 * in URL format.  Whenever we get a real stream, call the
 * streamUrlProc with the URL and some context information (station name, description
 * and URL context).
 */
int32_t
RadioScanStation::streamApply( std::string url, streamUrlProc *urlProcp, void *urlContextp)
{
    XApi::ClientReq *reqp;
    int32_t code;
    CThreadPipe *inPipep;
    char tbuffer[1024];
    int32_t nbytes;
    std::string host;
    std::string path;
    std::string retrievedData;
    uint16_t defaultPort;
    int isSecure;
    BufGen *bufGenp;
    XApi::ClientConn *clientConnp;
    int32_t httpError;
    std::string contentType;
    int isStream = 0;
    int isPls = 0;
    int isUrl = 0;

    while(1) {
        code = Rst::splitUrl(url, &host, &path, &defaultPort, &isSecure);
        if (code)
            return code;

        if (isSecure) {
            bufGenp = new BufTls("");
        }
        else {
            bufGenp = new BufSocket();
        }
        bufGenp->init(const_cast<char *>(host.c_str()), defaultPort);
        bufGenp->setTimeoutMs(10000);
        clientConnp = _scanp->_xapip->addClientConn(bufGenp);

        reqp = new XApi::ClientReq();
        reqp->startCall(clientConnp, path.c_str(), /* isGet */ XApi::reqGet);
        code = reqp->waitForHeadersDone();
        if (code != 0) {
            delete reqp;
            return code;
        }

        /* if we have a 2XX HTTP error code, we can retrieve the data.  If we have
         * a 3XX error code, it means we've been redirected, and should just reparse
         * starting at the location header's value.
         */
        httpError = reqp->getHttpError();
        if (httpError >=300 && httpError < 400) {
            /* redirect; location header has our new URL */
            code = reqp->findIncomingHeader("location", &url);
            if (code) {
                delete reqp;
                return code;
            }
            continue;
        }
        else if (httpError >= 200 && httpError < 300) {
            /* it is possible that the radio stream is actually playing now; if so, shut it
             * down and make one of the calls to urlProcp.  If the content type contains
             * the string URL, then we parse out the URLs; if it contains PLS, then we 
             * assume that we'll have file<n>=<url> lines in the file.  Otherwise, 
             * we assume we're actually streaming data.
             */
            code = reqp->findIncomingHeader("content-type", &contentType);
            if (code) {
                delete reqp;
                return code;
            }

            if (hasISubstr("pls", contentType))
                isPls = 1;
            else if (hasISubstr("url", contentType))
                isUrl = 1;
            else 
                isStream = 1;
        }
        else {
            /* got an error like 404, return failure */
            delete reqp;
            return -1;
        }

        if (isStream) {
            /* this will terminate the RST call when data is next delivered on
             * the socket and RST attempts to deliver it to us.
             */
            inPipep = reqp->getIncomingPipe();
            inPipep->eof();

            urlProcp(urlContextp, url.c_str());
            /* deleted below */
        }
        else {
            inPipep = reqp->getIncomingPipe();
            retrievedData.erase();
            while(1) {
                nbytes = inPipep->read(tbuffer, sizeof(tbuffer));
                if (nbytes <= 0)
                    break;
                retrievedData.append(tbuffer, nbytes);
            };

            if (isPls) {
                /* recurses on other URLs */
                parsePls( retrievedData.c_str(),
                          urlProcp, 
                          urlContextp);
            }
            else if (isUrl) {
                /* file full of URLs starting with http: or https:; pay attention
                 * only to those lines
                 */
                parseUrl(retrievedData.c_str(),
                         urlProcp, 
                         urlContextp);
            }
        }

        delete reqp; 
       break;
    }

    return 0;
    
}

/* utility to retrieve the contents of a URL.  Better not pass in a
 * stream URL (we can bullet-proof by watching for audio/aacp or
 * audio/mp3 as content-type and aborting in that case), or it will be
 * stuck forever.
 */
int32_t
RadioScan::retrieveContents(std::string url , std::string *strp)
{
    XApi::ClientReq *reqp;
    int32_t code;
    CThreadPipe *inPipep;
    char tbuffer[1024];
    int32_t nbytes;
    std::string host;
    std::string path;
    uint16_t defaultPort;
    int isSecure;
    XApi::ClientConn *connp;
    BufGen *bufGenp;
    int32_t httpError;

    while(1) {
        code = Rst::splitUrl(url, &host, &path, &defaultPort, &isSecure);
        if (code)
            return code;

        if (isSecure) {
            bufGenp = new BufTls("");
        }
        else {
            bufGenp = new BufSocket();
        }
        bufGenp->init(const_cast<char *>(host.c_str()), defaultPort);
        bufGenp->setTimeoutMs(10000);
        connp = _xapip->addClientConn(bufGenp);

        reqp = new XApi::ClientReq();
        reqp->startCall(connp, path.c_str(), /* isGet */ XApi::reqGet);
        code = reqp->waitForHeadersDone();
        if (code != 0) {
            delete reqp;
            return code;
        }
        httpError = reqp->getHttpError();
        if (httpError >= 300 && httpError < 400) {
            /* redirect */
            code = reqp->findIncomingHeader("location", &url);
            delete reqp;
            delete bufGenp;
            if (code) {
                return code;
            }
            continue;
        }

        if (httpError < 200 || httpError >= 300) {
            return -1;
        }

        inPipep = reqp->getIncomingPipe();
        strp->erase();
        while(1) {
            nbytes = inPipep->read(tbuffer, sizeof(tbuffer));
            if (nbytes <= 0)
                break;
            strp->append(tbuffer, nbytes);
        };

        /* clean up and return; strp has already been set */
        delete reqp;
        delete bufGenp;
        return 0;
    }

    /* not reached */
    return -1;
}

/* input parameters are a pointer into a string and count of the
 * number of non-null remaining characters.
 *
 * We search for a \r or \n character, and then consume characters until
 * we encounter something that *isn't* such a character.  We update
 * *datap and *lenp to describe the remainder of the string starting at the
 * first character after the last of the newline-type characters.
 */
/* static */ int
RadioScanStation::skipPastEol(const char **datap, int32_t *lenp) 
{
    int32_t tlen = *lenp;
    const char *strp = *datap;
    int found = 0;
    int tc;

    while(tlen > 0) {
        tc = *strp;
        if (tc == '\n' || tc == '\r') {
            found = 1;
        }
        else {
            if (found) {
                break;
            }
        }
        strp++; tlen --;
    }

    *datap = strp;
    *lenp = tlen;

    return 0;
}

/* parse a playlist response from streamtheworld.com's search
 * mechanism; there are a number of lines like:
 *
 * File3=http://17273.live.streamtheworld.com:3690/WYEPFMAAC_SC
 *
 * where the URL is the URL of an MP3 or AAC stream.
 *
 * Apply the urlProcp procedure to any of the streams.
 */
int
RadioScanStation::parsePls( const char *resultp,
                            streamUrlProc *urlProcp, 
                            void *urlContextp)
{
    int32_t tlen;
    char urlBuffer[1024];
    int found;
    int tc;
    const char *strp;
    int32_t code;

    tlen = strlen(resultp);
    strp = resultp;
    found = 0;
    while(tlen > 0) {
        if (strncasecmp(strp, "file", 4) == 0) {
            /* skip to after '=' */
            code = 0;
            while(tlen > 0) {
                tc = *strp++;
                tlen --;
                if (tc == '=')
                    break;
                else if (tc == '\n') {
                    /* something is wrong; continue at the next line */
                    code = skipPastEol(&strp, &tlen);
                    if (code)
                        break;
                    else
                        continue;
                }
            }
            if (code)
                break;
            /* we've moved just past the '=' character; the rest of the line up to
             * the new line is the web address.  The strcspn function returns the
             * character array index of the first EOL or null character in strp.
             */
            code = strcspn(strp, "\r\n");
            strncpy(urlBuffer, strp, code);
            urlBuffer[code] = 0;

            /* skip over all text before the first newline character, and then use
             * skipPastEol to skip over whatever newline characters are present.
             */
            skipPastEol(&strp, &tlen);

            (*urlProcp)(urlContextp, urlBuffer);
            found = 1;
        }
        else {
            /* skip to end of line; may be both CR and LF present */
            code = skipPastEol(&strp, &tlen);
            if (code) {
                break;
            }
        }
    }

    return !found;
}

/* look for lines starting with http: or https: */
int
RadioScanStation::parseUrl( const char *resultp,
                            streamUrlProc *urlProcp, 
                            void *urlContextp)
{
    int32_t tlen;
    char urlBuffer[1024];
    int found;
    const char *strp;
    int32_t code;

    tlen = strlen(resultp);
    strp = resultp;
    found = 0;
    while(tlen > 0) {
        if (strncasecmp(strp, "http:", 5) == 0 || strncasecmp(strp, "https:", 6) == 0) {
            /* we've moved just past the '=' character; the rest of the line up to
             * the new line is the web address.  The strcspn function returns the
             * character array index of the first EOL or null character in strp.
             */
            code = strcspn(strp, "\r\n");
            strncpy(urlBuffer, strp, code);
            urlBuffer[code] = 0;

            /* skip over all chars before first EOL character, and then skip
             * over the EOL character(s) as well.
             */
            skipPastEol(&strp, &tlen);

            (*urlProcp)(urlContextp, urlBuffer);
            found = 1;
        }
        else {
            /* skip to end of line; may be both CR and LF present */
            code = skipPastEol(&strp, &tlen);
            if (code) {
                break;
            }
        }
    }

    return !found;
}

/* static */int
RadioScanStation::queryMatch(const char *sp1, const char *sp2)
{
    static const int maxWords=16;
    char p1Words[maxWords][128];
    char p2Words[maxWords][128];
    int p1WordCount;
    int p2WordCount;
    int i;
    int j;
    const char *tp;
    const char *ntp;
    int spaceIx;
    int tc;

    /* divide both strings into words, and then match if case folded version of any match
     * with any other.
     */
    p1WordCount = 0;
    p2WordCount = 0;
    
    tp = sp1;
    for(i=0;i<maxWords;i++) {
        while(1) {
            tc = *tp;
            if (tc != ' ' && tc != '\t')
                break;
            tp++;
        }
        if (*tp == 0)
            break;

        ntp = strpbrk(tp, " \t");       /* break up on white space */
        if (ntp == NULL) {
            strcpy(p1Words[i], tp);
            p1WordCount = i+1;
            break;
        }
        else {
            spaceIx = ntp-tp;
            strncpy(p1Words[i], tp, spaceIx);
            p1Words[i][spaceIx] = 0;
            p1WordCount = i+1;
        }

        /* continue where we left off */
        tp = ntp;
    }

    /* do same for 2nd line */
    tp = sp2;
    for(i=0;i<maxWords;i++) {
        while(1) {
            tc = *tp;
            if (tc != ' ' && tc != '\t')
                break;
            tp++;
        }
        if (*tp == 0)
            break;

        ntp = strpbrk(tp, " \t");
        if (ntp == NULL) {
            strcpy(p2Words[i], tp);
            p2WordCount = i+1;
            break;
        }
        else {
            spaceIx = ntp-tp;
            strncpy(p2Words[i], tp, spaceIx);
            p2Words[i][spaceIx] = 0;
            p2WordCount = i+1;
        }

        /* continue where we left off */
        tp = ntp;
    }

    /* now do the n*k comparisons */
    for(i=0;i<p1WordCount;i++) {
        for(j=0;j<p2WordCount;j++) {
            if (strcasecmp(p1Words[i],p2Words[j]) == 0) {
                /* found a match */
                return 1;
            }
        }
    }

    /* no matches here */
    return 0;
}

/* split line by \t characters; last may be terminated by \r, \n or
 * null; returns count of entries found.  This is used to parse the
 * stations.rsd file enumerating so many radio stations.
 */
/* static */ int32_t
RadioScanStation::splitLine(const char *bufferp, uint32_t count, char **targetsp)
{
    int tc;
    uint32_t itemCount;
    const char *strp;
    char *targetp;
    int targetTerminated;

    /* watch for trivial case */
    if (count == 0)
        return 0;

    strp = bufferp;

    /* setup for first item */
    targetTerminated = 0;
    targetp = targetsp[0];
    itemCount = 1;
    while(1) {
        /* at this point, we're about to consume a new character */
        tc = *strp;

        if (tc == '\t' || tc == '\r' || tc == '\n' || tc == 0) {
            if (!targetTerminated) {
                *targetp++ = 0;
                targetTerminated = 1;
                /* note that we don't switch to the next target string
                 * until we encounter the next real character after
                 * setting targetTerminated.  Until then, we just go
                 * through this branch, skipping tabs, newlines and
                 * nulls.
                 */
            }
                
            /* no more fields in this line */
            if (tc == 0 || tc == '\n' || tc == '\r')
                break;

            /* see if we've finished the last item we're asked for */
            if (itemCount >= count)
                break;
        }
        else {
            /* got a real character */
            if (targetTerminated) {
                /* this is the first character after the last field terminator */
                targetTerminated = 0;
                targetp = targetsp[itemCount];
                itemCount++;
            }
            *targetp++ = tc;
        }

        /* done processing character at *strp */
        strp++;
    }
    return itemCount;
}

/* search a stations.rsd file, generating a series of stations, one for
 * each station matching the query string.
 */
int32_t
RadioScanQuery::searchFile()
{
    FILE *filep;
    char *tp;
    char lineBuffer[4096];
    char addr[6][4096];
    char stationName[4096];
    char stationShortDescr[4096];
    char stationGenre[128];
    char stationCO[128];
    char stationLang[128];
    int matches;
    uint32_t i;
    uint32_t count;
    char *parseArray[12];
    RadioScanStation *stationp;

    filep = fopen("stations.checked", "r");
    if (!filep)
        return -1;

    while(1) {
        tp = fgets(lineBuffer, sizeof(lineBuffer), filep);
        if (!tp)
            break;
        /* strings are separated by real tab characters */
        parseArray[0] = stationName;
        parseArray[1] = stationShortDescr;
        parseArray[2] = stationGenre;
        parseArray[3] = stationCO;      /* country */
        parseArray[4] = stationLang;    /* language */
        parseArray[5] = addr[0];
        parseArray[6] = addr[1];
        parseArray[7] = addr[2];
        parseArray[8] = addr[3];
        parseArray[9] = addr[4];
        parseArray[10] = addr[5];
        count = RadioScanStation::splitLine(lineBuffer, 11, parseArray);
        if (count != 11)
            continue;

        /* now see if we match the station name, or short description */
        matches = 0;
        if (RadioScanStation::queryMatch(_query.c_str(), stationName))
            matches = 1;
        else if (RadioScanStation::queryMatch(_query.c_str(), stationShortDescr))
            matches = 1;
        if (matches) {
            stationp = new RadioScanStation();
            stationp->init(this);

            /* set these so that addEntry has some useful defaults */
            stationp->_stationName = std::string(stationName);
            stationp->_stationShortDescr = std::string(stationShortDescr);

            for(i=0;i<6;i++) {
                if (addr[i][0] != '-') {
                    stationp->streamApply(addr[i], RadioScanStation::stwCallback, stationp);
                }
            } /* loop over all addresses */
        }
    }

    return 0;
}

int32_t
RadioScanQuery::searchShoutcast()
{
    return -1;
}

int32_t
RadioScanQuery::searchDar()
{
    char tbuffer[1024];
    int32_t code;
    Json jsonSys;
    char *datap;
    Json::Node *rootNodep;
    std::string data;
    Json::Node *tnodep;
    RadioScanStation *stationp;
    std::string stationShortDescr;
    std::string url;

    sprintf(tbuffer,
            "http://api.dar.fm/uberstationurl.php?callsign=%s&callback=json&partner_token=6670654103",
            _query.c_str());
    code = _scanp->retrieveContents(std::string(tbuffer), &data);
    if (code != 0)
        return code;
    
    datap = const_cast<char *>(data.c_str());
    code = jsonSys.parseJsonChars(&datap, &rootNodep);
    if (!rootNodep) {
        printf("json parse failed '%s'\n", datap);
        return -1;
    }

    tnodep = rootNodep->searchForChild(std::string("url"));
    if (!tnodep)
        return -3;
    url = tnodep->_children.head()->_name;

    sprintf(tbuffer, 
            "http://api.dar.fm/playlist.php?callsign=%s&callback=json&partner_token=6670654103",
            _query.c_str());
    code = _scanp->retrieveContents(std::string(tbuffer), &data);
    if (code == 0) {
        datap = const_cast<char *>(data.c_str());
        code = jsonSys.parseJsonChars(&datap, &rootNodep);
        if (code == 0) {
            tnodep = rootNodep->searchForChild("genre");
            if (tnodep)
                stationShortDescr = tnodep->_children.head()->_name;
        }
    }

    stationp = new RadioScanStation();
    stationp->init(this);

    /* set these so that addEntry has some useful defaults */
    stationp->_stationName = _query;
    stationp->_stationShortDescr = stationShortDescr;
    stationp->streamApply(url, RadioScanStation::stwCallback, stationp);

    return 0;
}

/* static */ int32_t
RadioScanStation::stwCallback(void *contextp, const char *urlp)
{
    RadioScanStation *stationp = (RadioScanStation *)contextp;
    
    stationp->addEntry(urlp);
    
    return 0;
}

int32_t
RadioScanQuery::searchStreamTheWorld()
{
    char tbuffer[1024];
    int32_t code;
    std::string result;
    RadioScanStation *stationp;

    /* try FM */
    stationp = new RadioScanStation();
    stationp->init(this);
    stationp->_stationName = _query;
    stationp->_stationShortDescr = "FM Radio";
    sprintf(tbuffer, "http://playerservices.streamtheworld.com/pls/%sFMAAC.pls", _query.c_str());
    code = stationp->streamApply(tbuffer, RadioScanStation::stwCallback, stationp);
    if (code || stationp->_entries.count() == 0) {
        delete stationp;
    }

    /* try AM */
    stationp = new RadioScanStation();
    stationp->init(this);
    stationp->_stationName = _query;
    stationp->_stationShortDescr = "AM Radio";
    sprintf(tbuffer, "http://playerservices.streamtheworld.com/pls/%sAMAAC.pls", _query.c_str());
    code = stationp->streamApply(tbuffer, RadioScanStation::stwCallback, stationp);
    if (code || stationp->_entries.count() == 0) {
        delete stationp;
    }
    
    return 0;
}

void
RadioScan::searchStation(std::string query, RadioScanQuery **respp)
{
    RadioScanQuery *resp;

    resp = new RadioScanQuery();
    resp->init(this, query);
    *respp = resp;

    /* if we have 4 character call letters like WESA, we use call
     * letter lookup with streamtheworld.
     */ 
    if (query.size() <= 4) {
        resp->searchStreamTheWorld();
    }

    /* add entries from the file */
    resp->searchFile();

    /* add entries from DAR.fm */
    resp->searchDar();

    resp->searchShoutcast();

    takeLock();
    
    releaseLock();

    *respp = resp;
}

int32_t
RadioScanLoadTask::start()
{
    FILE *newFilep;
    char tbuffer[4096];
    XApi::ClientReq *reqp;
    CThreadPipe *inPipep;
    int32_t code;
    int32_t nbytes;
    int32_t totalBytes;
    struct stat tstat;
    uint32_t myTime;
    uint32_t fileMTime;
    
    code = stat("stations.checked", &tstat);
    myTime = time(0);
#ifdef __LINUX__
    fileMTime = tstat.st_mtim.tv_sec;
#else
    fileMTime = tstat.st_mtimespec.tv_sec;
#endif
    if (code == 0 && fileMTime + 24*3600 > myTime) {
        printf("No download -- stations.checked is recent\n");
        return 0;
    }

    printf("Starting download\n");
    newFilep = fopen("stations.new", "w");
    reqp = new XApi::ClientReq();
    reqp->startCall(_scanp->_dirConnp, "/get?id=stations.rsd", /* isGet */ XApi::reqGet);
    
    code = reqp->waitForHeadersDone();

    inPipep = reqp->getIncomingPipe();

    totalBytes = 0;
    if (code) {
        delete reqp;
    }
    else {
        while(1) {
            nbytes = inPipep->read(tbuffer, sizeof(tbuffer));
            if (nbytes > 0) {
                code = fwrite(tbuffer, 1, nbytes, newFilep);
                totalBytes += code;
                if (code <= 0)
                    break;
            }
            else
                break;
        } /* loop over all */
    }

    delete inPipep;
    delete reqp;
    fclose(newFilep);
    newFilep = NULL;
    rename("stations.new", "stations.checked");
    printf("Received %d bytes\n", totalBytes);

    return 0;
}

RadioScanStation::Entry *
RadioScanStation::getFirstEntry()
{
    RadioScanStation::Entry *ep;

    RadioScan::takeLock();
    ep = _entries.head();
    RadioScan::releaseLock();

    return ep;
}

RadioScanStation::Entry *
RadioScanStation::getNextEntry(RadioScanStation::Entry *aep)
{
    RadioScanStation::Entry *ep;

    RadioScan::takeLock();
    ep = aep->_dqNextp;
    RadioScan::releaseLock();

    return ep;
}


void
RadioScanStation:: hold()
{
    RadioScan::takeLock();
    ++_refCount;
    RadioScan::releaseLock();
}


void 
RadioScanStation::release()
{
    RadioScan::takeLock();
    osp_assert(_refCount > 0);
    ++_refCount;
    RadioScan::releaseLock();
}

void
RadioScanStation::del()
{
    RadioScan::takeLock();
    _deleted = 1;
    RadioScan::releaseLock();
}

/* called with a short name for the radio station (eg WYEP), a longer
 * description (WYEP 91.3 Pittsburgh) and a URL like
 * 'http://foo.com/bar.pls'
 */
void
RadioScanStation::addEntry(const char *streamUrlp) 
{
    Entry *ep;

    for(ep = _entries.head(); ep; ep=ep->_dqNextp) {
        if (strcasecmp(streamUrlp, ep->_streamUrl.c_str()) == 0) {
            return;
        }
    }

    ep = new Entry();
    ep->_streamUrl = std::string(streamUrlp);
    ep->_alive = -1;
    _scanp->takeLock();
    _entries.append(ep);
    _scanp->releaseLock();
}

/* static */ int
RadioScanStation::hasISubstr(const char *keyp, std::string target)
{
    int32_t keyLen;
    int32_t targetLen;
    const char *targetp;
    int32_t i;

    keyLen = strlen(keyp);
    targetLen = target.length();
    if (keyLen > targetLen)
        return 0;
    targetp = target.c_str();
    for(i=0; i<=targetLen-keyLen; i++) {
        if (strncasecmp(keyp, targetp+i, keyLen) == 0)
            return 1;
    }
    return 0;
}
