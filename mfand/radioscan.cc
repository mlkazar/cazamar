#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>

#include "osp.h"
#include "radioscan.h"

#include "json.h"
#include "xgml.h"

/* stupid statics */
CThreadMutex RadioScan::_lock;

/* we supply a bufgen factory that can create sockets to URLs, and a
 * prefix string to add to all file names.  As such, it should either be
 * an empty string or end with a '/' character.  The directory will control 
 * where the stations.checked file will be downloaded, and consulted when
 * searching for radiosure-registered stations.
 */
void
RadioScan::init(BufGenFactory *factoryp, std::string dirPrefix)
{
    _factoryp = factoryp;
    _xapip = new XApi();

    _dirPrefix = dirPrefix;

    _dirBufp = factoryp->allocate(0);
    if (_dirBufp) {
        _dirBufp->init(const_cast<char *>("djtogoapp.duckdns.org"), 7701);
        _dirBufp->setTimeoutMs(15000);
        _dirConnp = _xapip->addClientConn(_dirBufp);
    }

    _stwBufp = factoryp->allocate(0);
    _stwBufp->init(const_cast<char *>("playerservices.streamtheworld.com"), 80);
    _stwBufp->setTimeoutMs(15000);
    _stwConnp = _xapip->addClientConn(_stwBufp);

    /* for browsing, we maintain a count of the # of lines in the radiosure file, which
     * we compute on demand.
     */
    _fileLineCount = -1;
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
    int truncatedRead = 0;

    while(1) {
        code = Rst::splitUrl(url, &host, &path, &defaultPort, &isSecure);
        if (code)
            return code;

        bufGenp = _scanp->_factoryp->allocate(isSecure);
        if (!bufGenp)
            return -2;

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
            else if (hasISubstr("audio/mpeg", contentType) ||
                     hasISubstr("audio/aacp", contentType) ||
                     hasISubstr("audio/aac", contentType) ||
                     hasISubstr("audio/mp3", contentType)) {
                isStream = 1;
            }
            else if (hasISubstr("audio/", contentType)) {
                /* this isn't a playlist or a URL list, and it isn't an
                 * audio type that we understand.  Print a warning and ignore it
                 */
                printf("Unrecognized content type '%s', ignored\n", contentType.c_str());
                delete reqp;
                return -1;
            }
        }
        else {
            /* got an error like 404, return failure */
            delete reqp;
            return -1;
        }

        if (isStream) {
            std::string brValue;

            /* before calling callback, set the stream rate and type in the station,
             * which the callback may use to define the stream.
             */
            _streamType = contentType;
            code = reqp->findIncomingHeader("icy-br", &brValue);
            if (code == 0) {
                _streamRateKb = atoi(brValue.c_str());
            }

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
            truncatedRead = 0;
            while(1) {
                nbytes = inPipep->read(tbuffer, sizeof(tbuffer));
                if (nbytes <= 0)
                    break;
                retrievedData.append(tbuffer, nbytes);
                if (retrievedData.size() > 0x10000) {
                    /* bullet proof against actually having a stream here */
                    truncatedRead = 1;
                    printf("Content type %s sent us too much data, probably a stream\n",
                           contentType.c_str());
                    break;
                }
            };

            if (truncatedRead) {
                /* just ignore this data; it is probably a stream of unknown type */
            }
            else if (isPls) {
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

        bufGenp = _factoryp->allocate(isSecure);
        if (!bufGenp) {
            return -1;
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
            reqp = NULL;
            delete bufGenp;
            bufGenp = NULL;
            if (code) {
                return code;
            }
            continue;
        }

        if (httpError < 200 || httpError >= 300) {
            delete reqp;
            delete bufGenp;
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

    tlen = (int32_t) strlen(resultp);
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
            code = (int32_t) strcspn(strp, "\r\n");
            strncpy(urlBuffer, strp, code);
            urlBuffer[code] = 0;

            /* skip over all text before the first newline character, and then use
             * skipPastEol to skip over whatever newline characters are present.
             */
            skipPastEol(&strp, &tlen);

            streamApply(std::string(urlBuffer), RadioScanStation::stwCallback, this);
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

    tlen = (int32_t) strlen(resultp);
    strp = resultp;
    found = 0;
    while(tlen > 0) {
        if (strncasecmp(strp, "http:", 5) == 0 || strncasecmp(strp, "https:", 6) == 0) {
            /* we've moved just past the '=' character; the rest of the line up to
             * the new line is the web address.  The strcspn function returns the
             * character array index of the first EOL or null character in strp.
             */
            code = (int32_t) strcspn(strp, "\r\n");
            strncpy(urlBuffer, strp, code);
            urlBuffer[code] = 0;

            /* skip over all chars before first EOL character, and then skip
             * over the EOL character(s) as well.
             */
            skipPastEol(&strp, &tlen);

            streamApply(std::string(urlBuffer), RadioScanStation::stwCallback, this);
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

/* all words in sp1 must be present in sp2 to return true */
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
    int match;

    /* divide both strings into words, and then match if case folded
     * version of every word in p1 is somewhere in p2.
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
            spaceIx = (int) (ntp-tp);
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
            spaceIx = (int)(ntp-tp);
            strncpy(p2Words[i], tp, spaceIx);
            p2Words[i][spaceIx] = 0;
            p2WordCount = i+1;
        }

        /* continue where we left off */
        tp = ntp;
    }

    /* every word in p1 words must be present as one of the words in p2's list
     * for us to return true.  Note that if p2 is empty, we should return false, not
     * true, though our condition is vacuously true.
     */
    if (p2WordCount == 0 || p1WordCount == 0)
        return 0;

    for(i=0;i<p1WordCount;i++) {
        match = 0;
        for(j=0;j<p2WordCount;j++) {
            if (strcasecmp(p1Words[i], p2Words[j]) == 0) {
                match = 1;
                break;
            }
        }
        /* if match is 0, we didn't find a match for p1Words[i] in
         * p2Words, so we return false.
         */
        if (!match)
            return 0;
    }

    return 1;
}

void
RadioScan::countLines()
{
    std::string checkedFileName;
    FILE *filep;
    char lineBuffer[4096];
    int32_t lineCount;
    char *tp;

    checkedFileName = _dirPrefix + "stations.checked";
    filep = fopen(checkedFileName.c_str(), "r");
    if (!filep)
        return;
    lineCount = 0;
    while(1) {
        tp = fgets(lineBuffer, sizeof(lineBuffer), filep);
        if (!tp)
            break;
        lineCount++;
    }
    _fileLineCount = lineCount;
    fclose(filep);
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

/* static */ std::string
RadioScanStation::upperCase(std::string name)
{
    uint32_t i;
    uint32_t len;
    std::string result;
    const char *tp;
    int tc;

    len = (uint32_t) name.length();
    for(i=0, tp = name.c_str(); i<len; i++, tp++) {
        tc = *tp;
        result.push_back(toupper(tc));
    }
    return result;
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
    std::string checkedFileName;

    checkedFileName = _scanp->_dirPrefix + "stations.checked";
    filep = fopen(checkedFileName.c_str(), "r");
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
            stationp->_stationSource = std::string("tunein file");

            for(i=0;i<6;i++) {
                if (addr[i][0] != '-') {
                    stationp->streamApply(addr[i], RadioScanStation::stwCallback, stationp);
                }
            } /* loop over all addresses */
        }
        if (isAborted())
            return -1;
    }

    return 0;
}

/* static */ void
RadioScan::scanSort(int32_t *datap, int32_t count)
{
    int32_t i;
    int32_t j;
    int32_t temp;
    int changed;

    /* shouldn't take more than count iterations to sort everything */
    for(i=0;i<count;i++) {
        changed = 0;
        for(j=0;j<count-1;j++) {
            if (datap[j] > datap[j+1]) {
                changed = 1;
                temp = datap[j];
                datap[j] = datap[j+1];
                datap[j+1] = temp;
            }
        }
        if (!changed)
            break;
    }
}

int32_t
RadioScanQuery::browseFile()
{
    static const int32_t maxEntries = 20;
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
    uint32_t count;
    char *parseArray[12];
    RadioScanStation *stationp;
    std::string checkedFileName;
    int32_t lineNumbers[maxEntries];
    int32_t i;
    int32_t lineNumber;
    int32_t randIx;
    int skipping;
    int checkedSome;

    _baseStatus = std::string("Starting browse");

    srandom((unsigned int) time(0));
    for(i=0;i<maxEntries;i++) {
        lineNumbers[i] = random() % _scanp->_fileLineCount;
    }

    RadioScan::scanSort(lineNumbers, maxEntries);

    checkedFileName = _scanp->_dirPrefix + "stations.checked";
    filep = fopen(checkedFileName.c_str(), "r");
    if (!filep)
        return -1;

    lineNumber = -1;    /* so that first increment goes to 0 */
    skipping = 1;       /* means we're skipping lines until we hit the next line number */
    randIx = 0;
    while(1) {
        /* move to the next line number */
        lineNumber++;

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

        if (skipping && lineNumber < lineNumbers[randIx])
            continue;

        sprintf(lineBuffer, "On line %d of %d", lineNumber, _scanp->_fileLineCount);
        _baseStatus = std::string(lineBuffer);

        /* no longer skipping, now we're searching for a match */
        skipping = 0;

        /* now see if we match the city (within the name), the country, or the genre,
         * for each non-null thing we're trying to match.  If we're not matching on
         * anything, then treat every line as matching.
         */
        matches = 0;
        checkedSome = 0;
        if ( _browseCity.length() > 0) {
            /* city or county name could show up in the name or the short description */
            checkedSome = 1;
            if ( RadioScanStation::queryMatch(_browseCity.c_str(), stationName) ||
                 RadioScanStation::queryMatch(_browseCity.c_str(), stationShortDescr)) {
                matches = 1;
            }
        }
        if ( _browseCountry.length() > 0) {
            checkedSome = 1;
            if ( RadioScanStation::queryMatch(_browseCountry.c_str(), stationCO))
                matches = 1;
        }
        if ( _browseGenre.length() > 0) {
            checkedSome = 1;
            if (RadioScanStation::queryMatch(_browseGenre.c_str(), stationGenre))
                matches = 1;
        }
        if (!checkedSome)
            matches = 1;

        if (matches) {
            stationp = new RadioScanStation();
            stationp->init(this);

            /* set these so that addEntry has some useful defaults */
            stationp->_stationName = std::string(stationName);
            stationp->_stationShortDescr = std::string(stationShortDescr);
            stationp->_stationSource = std::string("tunein file");

            for(i=0;i<6;i++) {
                if (addr[i][0] != '-') {
                    stationp->streamApply(addr[i], RadioScanStation::stwCallback, stationp);
                }
            } /* loop over all addresses */

            /* if we didn't find any streams, delete the station */
            if (stationp->_entries.count() == 0) {
                delete stationp;
                stationp = NULL;
            }

            /* now move to the next target line number */
            skipping = 1;
            randIx++;
            if (randIx >= maxEntries)
                break;
        }
        if (isAborted())
            return -1;
    }

    return 0;
}

int32_t
RadioScanQuery::searchShoutcast()
{
    int32_t code;
    char tbuffer[4096];
    const char *keyStringp = "HF3T2bjHaPcadpSG";
    std::string data;
    char *datap;
    const char *tp;
    int eatingSpaces;
    int32_t count;
    Xgml xgmlSys;
    Xgml::Node *xgmlNodep;
    Xgml::Node *childListp;
    Xgml::Attr *attrNodep;
    const char *stationNamep = NULL;
    const char *stationGenrep = NULL;
    const char *basep;
    uint32_t stationId=0;
    int tc;
    RadioScanStation *stationp;

    sprintf(tbuffer, "http://api.shoutcast.com/legacy/stationsearch?k=%s&limit=100&search=",
            keyStringp);

    /* concatenate words from search string, with spaces turned into '+' characters */
    eatingSpaces = 1;
    count = 0;
    for(tp = _query.c_str(); count < sizeof(tbuffer) - 1024; tp++) {
        tc = *tp;
        if (tc == 0)
            break;
        if (tc == ' ') {
            if (eatingSpaces)
                continue;
            else {
                eatingSpaces = 1;
                strcat(tbuffer, "+");
                count++;
            }
        }
        else {
            char dummy[2];
            dummy[0] = tc;
            dummy[1] = 0;
            strcat(tbuffer, dummy);
            count++;
            eatingSpaces = 0;
        }
    }

    code = _scanp->retrieveContents(std::string(tbuffer), &data);
    if (code) {
        printf("retrieval of '%s' failed\n", tbuffer);
        return -2;
    }

    datap = const_cast<char *>(data.c_str());
    code = xgmlSys.parse(&datap, &xgmlNodep);
    if (code) {
        printf("xml parse failed\n");
        return -1;
    }

    basep = "/sbin/tunein-station.pls";
    childListp = xgmlNodep->searchForChild("tunein");
    if (childListp) {
        for(attrNodep = childListp->_attrs.head(); attrNodep; attrNodep = attrNodep->_dqNextp) {
            if (strcmp(attrNodep->_name.c_str(), "base") == 0) {
                basep = attrNodep->_value.c_str();
                break;
            }
        }
    }

    for(childListp = xgmlNodep->_children.head(); childListp; childListp=childListp->_dqNextp) {
        if (strcmp(childListp->_name.c_str(), "station") == 0) {
            for( attrNodep = childListp->_attrs.head(); 
                 attrNodep; 
                 attrNodep = attrNodep->_dqNextp) {
                if (strcmp(attrNodep->_name.c_str(), "name") == 0) {
                    stationNamep = attrNodep->_value.c_str();
                }
                else if (strcmp(attrNodep->_name.c_str(), "genre") == 0) {
                    stationGenrep = attrNodep->_value.c_str();
                }
                else if (strcmp(attrNodep->_name.c_str(), "id") == 0) {
                    stationId = atoi(attrNodep->_value.c_str());
                }
            } /* loop over all attrs */

            /* now make a call to get the station attributes, including the stream URL */
            sprintf(tbuffer, "http://yp.shoutcast.com%s?id=%d", basep, stationId);

            stationp = new RadioScanStation();
            stationp->init(this);

            /* set these so that addEntry has some useful defaults */
            stationp->_stationName = std::string(stationNamep);
            stationp->_stationShortDescr = std::string(stationGenrep);
            stationp->_stationSource = std::string("shoutcast");

            stationp->streamApply(std::string(tbuffer), RadioScanStation::stwCallback, stationp);

            if (isAborted())
                return -1;
            /* tbuffer names the playlist file, so add the stream */
        } /* this is a station record */
    } /* loop over all stations */

    return 0;
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
    stationp->_stationName = RadioScanStation::upperCase(_query);
    stationp->_stationShortDescr = stationShortDescr;
    stationp->_stationSource = std::string("dar.fm");

    stationp->streamApply(url, RadioScanStation::stwCallback, stationp);

    return 0;
}

/* static */ int32_t
RadioScanStation::stwCallback(void *contextp, const char *urlp)
{
    RadioScanStation *stationp = (RadioScanStation *)contextp;
    const char *streamTypep;
    
    /* add streamRateKb, stream type; stored temporarily */
    if (stationp->_streamType == "audio/mp3" || stationp->_streamType == "audio/mpeg")
        streamTypep = "MP3";
    else if (stationp->_streamType == "audio/aacp")
        streamTypep = "AAC";
    else
        streamTypep = stationp->_streamType.c_str();

    stationp->addEntry(urlp, streamTypep, stationp->_streamRateKb);
    
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
    stationp->_stationName = RadioScanStation::upperCase(_query);
    stationp->_stationShortDescr = "FM Radio";
    stationp->_stationSource = std::string("StreamTheWorld FM");
    sprintf(tbuffer, "http://playerservices.streamtheworld.com/pls/%sFMAAC.pls", _query.c_str());
    code = stationp->streamApply(tbuffer, RadioScanStation::stwCallback, stationp);
    if (code || stationp->_entries.count() == 0) {
        delete stationp;
    }

    if (isAborted())
        return -1;

    /* try AM */
    stationp = new RadioScanStation();
    stationp->init(this);
    stationp->_stationName = RadioScanStation::upperCase(_query);
    stationp->_stationShortDescr = "AM Radio";
    stationp->_stationSource = std::string("StreamTheWorld AM");
    sprintf(tbuffer, "http://playerservices.streamtheworld.com/pls/%sAMAAC.pls", _query.c_str());
    code = stationp->streamApply(tbuffer, RadioScanStation::stwCallback, stationp);
    if (code || stationp->_entries.count() == 0) {
        delete stationp;
    }
    
    return 0;
}

void
RadioScanQuery::initBrowse(RadioScan *scanp, 
                           int32_t maxCount,
                           std::string country,
                           std::string city,
                           std::string genre)
{
    _browseMaxCount = maxCount;
    _browseCountry = country;
    _browseCity = city;
    _browseGenre = genre;
    _scanp = scanp;
}

std::string
RadioScanQuery::getStatus()
{
    std::string result;
    char tbuffer[1024];

    if (_stations.count() == 1)
        strcpy(tbuffer, " (1 station)");
    else
        sprintf(tbuffer, " (%ld stations)", _stations.count());

    result = _baseStatus + std::string(tbuffer);
    return result;
}

/* call initBrowse, and then browseStations with the resulting initialized query */
void
RadioScan::browseStations( RadioScanQuery *resp)
{
    resp->browseFile();

    /* this is only useful if we add another source of random entries */
    if (resp->isAborted())
        return;
}

/* external function to do a specific search */
void
RadioScan::searchStation(std::string query, RadioScanQuery **respp)
{
    RadioScanQuery *resp;
    RadioScanLoadTask *loadTaskp;

    resp = *respp;
    if (resp == NULL) {
        resp = new RadioScanQuery();
    }

    resp->init(this, query);
    *respp = resp;

    /* do this inline to prevent mysterious hangs on startup */
    resp->_baseStatus = std::string("Downloading directory file");
    loadTaskp = new RadioScanLoadTask();
    loadTaskp->init(this, _dirPrefix);
    loadTaskp->start(NULL);

    resp->_baseStatus = std::string("Searching StreamTheWorld");

    /* if we have 4 character call letters like WESA, we use call
     * letter lookup with streamtheworld.
     */ 
    if (query.size() <= 4) {
        resp->searchStreamTheWorld();
        if (resp->isAborted())
            return;
    }

    resp->_baseStatus = std::string("Searching RadioSure data");

    /* add entries from the file */
    resp->searchFile();
    if (resp->isAborted())
        return;

    resp->_baseStatus = std::string("Searching dar.fm");

    /* add entries from DAR.fm */
    resp->searchDar();
    if (resp->isAborted())
        return;

    resp->_baseStatus = std::string("Searching Shoutcast");

    resp->searchShoutcast();
    if (resp->isAborted())
        return;

    takeLock();
    
    releaseLock();
}

void
RadioScanLoadTask::start(void *argp)
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
    uint32_t httpError;
    int failed = 0;
    std::string checkedFileName;
    std::string newFileName;
    
    /* generate required names of final file and temp file name for download */
    checkedFileName = _dirPrefix + "stations.checked";
    newFileName = _dirPrefix + "stations.new";

    code = stat(checkedFileName.c_str(), &tstat);
    myTime = (uint32_t) time(0);
#ifdef __linux__
    fileMTime = tstat.st_mtim.tv_sec;
#else
    fileMTime = (uint32_t) tstat.st_mtimespec.tv_sec;
#endif
    if (code == 0 && fileMTime + 7*24*3600 > myTime) {
        _scanp->countLines();
        printf("No download -- stations.checked is recent (%d lines)\n", _scanp->_fileLineCount);
        return;
    }

    printf("Starting download\n");
    newFilep = fopen(newFileName.c_str(), "w");
    if (!newFilep) {
        printf("failed to create stations.new\n");
        return;
    }
    reqp = new XApi::ClientReq();
    reqp->startCall(_scanp->_dirConnp, "/get?id=stations.rsd", /* isGet */ XApi::reqGet);
    
    code = reqp->waitForHeadersDone();
    if (code == 0) {
        httpError=reqp->getHttpError();
    }
    else
        httpError = 999;

    inPipep = reqp->getIncomingPipe();

    totalBytes = 0;
    if (httpError < 200 || httpError >= 300) {
        delete reqp;
        reqp = NULL;
        failed = 1;
    }
    else {
        while(1) {
            nbytes = inPipep->read(tbuffer, sizeof(tbuffer));
            if (nbytes > 0) {
                code = (int32_t) fwrite(tbuffer, 1, nbytes, newFilep);
                totalBytes += code;
                if (code <= 0)
                    break;
            }
            else {
                if (nbytes < 0)
                    failed = 1;
                break;
            }
        } /* loop over all */
    }

    if (reqp)
        delete reqp;
    fclose(newFilep);
    newFilep = NULL;
    if (failed)
        unlink(newFileName.c_str());
    else {
        /* new file name is the rename source and ends '.new' */
        rename(newFileName.c_str(), checkedFileName.c_str());
    }

    /* count the lines in the newly loaded file */
    _scanp->countLines();
    
    printf("Received %d bytes, %d lines\n", totalBytes, _scanp->_fileLineCount);
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
RadioScanStation::addEntry(const char *streamUrlp, const char *typep, uint32_t streamRateKb)
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
    ep->_streamRateKb = streamRateKb;
    ep->_streamType = typep;
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

    keyLen = (int32_t) strlen(keyp);
    targetLen = (int32_t) target.length();
    if (keyLen > targetLen)
        return 0;
    targetp = target.c_str();
    for(i=0; i<=targetLen-keyLen; i++) {
        if (strncasecmp(keyp, targetp+i, keyLen) == 0)
            return 1;
    }
    return 0;
}
