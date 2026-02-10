#include <vector>

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

    _stwBufp = factoryp->allocate(0);
    _stwBufp->init(const_cast<char *>("playerservices.streamtheworld.com"), 80);
    _stwBufp->setTimeoutMs(15000);
    _stwConnp = _xapip->addClientConn(_stwBufp);
}

/* start with the object at the URL, and resolve it until we get to an
 * actual stream URL.  In the mean time, handle the case where we get
 * a 3XX redirect, or we get a bunch of text in PLS format, or a URL
 * in URL format.  Whenever we get a real stream, call the
 * streamUrlProc with the URL and some context information (station name, description
 * and URL context).
 */
int32_t
RadioScanStation::streamApply( std::string url,
                               streamUrlProc *urlProcp,
                               void *urlContextp,
                               RadioScanQuery *queryp)
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
    uint32_t redirects = 0;

    while(1) {
        code = Rst::splitUrl(url, &host, &path, &defaultPort, &isSecure);
        if (code)
            return code;

        if (queryp != nullptr)
            queryp->_verifyingUrl = url;

        bufGenp = _scanp->_factoryp->allocate(isSecure);
        if (!bufGenp) {
            if (isSecure) {
                /* try forcing http instead of https */
                code = Rst::splitUrl(url, &host, &path, &defaultPort, &isSecure,
                                     /* force http */ 1);
                if (code)
                    break;

                bufGenp = _scanp->_factoryp->allocate( isSecure);
                if (!bufGenp) {
                    code = -2;
                    break;
                }
            }
            else {
                code = -2;
                break;
            }
        }

        bufGenp->init(const_cast<char *>(host.c_str()), defaultPort);
        bufGenp->setTimeoutMs(10000);
        clientConnp = _scanp->_xapip->addClientConn(bufGenp);

        reqp = new XApi::ClientReq();
        reqp->startCall(clientConnp, path.c_str(), /* isGet */ XApi::reqGet);
        code = reqp->waitForHeadersDone();
        if (code != 0) {
            delete reqp;
            break;
        }

        /* if we have a 2XX HTTP error code, we can retrieve the data.  If we have
         * a 3XX error code, it means we've been redirected, and should just reparse
         * starting at the location header's value.
         */
        httpError = reqp->getHttpError();
        if (httpError >= 300 && httpError < 400) {
            /* redirect; location header has our new URL */
            code = reqp->findIncomingHeader("location", &url);
            if (code) {
                delete reqp;
                break;
            }
            if (queryp->isAborted()) {
                return -1;
            }
            if ( ++redirects >= RadioScan::_maxRedirects) {
                code = -3;
                delete reqp;
                break;
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
                break;
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
                code = -1;
                break;
            }
        }
        else {
            /* got an error like 404, return failure */
            delete reqp;
            code = -1;
            break;
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
            code = 0;
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
                    code = -4;
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
                          urlContextp,
                          queryp);
            }
            else if (isUrl) {
                /* file full of URLs starting with http: or https:; pay attention
                 * only to those lines
                 */
                parseUrl(retrievedData.c_str(),
                         urlProcp, 
                         urlContextp,
                         queryp);
            }
        }

        delete reqp; 
        code = 0;
        break;
    }

    if (queryp != nullptr)
        queryp->_verifyingUrl = "";
    return code;
    
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
    uint32_t redirects = 0;

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
            if (++redirects >= RadioScan::_maxRedirects) {
                /* redirect loop, return failure */
                delete reqp;
                delete bufGenp;
                return -1;
            }

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
                            void *urlContextp,
                            RadioScanQuery *queryp)
{
    int32_t tlen;
    char urlBuffer[1024];
    int found;
    int tc;
    const char *strp;
    int32_t code;
    uint32_t workingStreams = 0;

    tlen = (int32_t) strlen(resultp);
    strp = resultp;
    found = 0;
    while(tlen > 0) {
        if (queryp->isAborted()) {
            return false;
        }
        if (workingStreams >= _maxStreamsPerUrl)
            return false;       // !found

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

            code = streamApply(std::string(urlBuffer), RadioScanStation::stwCallback,
                               this, queryp);
            if (code == 0)
                workingStreams++;
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
                            void *urlContextp,
                            RadioScanQuery *queryp)
{
    int32_t tlen;
    char urlBuffer[1024];
    int found;
    const char *strp;
    int32_t code;
    uint32_t workingStreams = 0;

    tlen = (int32_t) strlen(resultp);
    strp = resultp;
    found = 0;
    while(tlen > 0) {
        if (queryp->isAborted()) {
            return false;
        }

        if (workingStreams >= _maxStreamsPerUrl)
            return false;

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

            code = streamApply(std::string(urlBuffer), RadioScanStation::stwCallback,
                               this, queryp);
            if (code == 0)
                workingStreams++;

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

int32_t
RadioScanQuery::searchFile() {
    std::string queryResults;
    char *datap;
    int32_t code;
    Json::Node *rootNodep = nullptr;
    Json::Node *stationNodep = nullptr;
    Json::Node *urlNodep = nullptr;
    Json::Node *iconNodep = nullptr;
    Json::Node *nameNodep = nullptr;
    Json::Node *tagNodep = nullptr;
    Json jsonSys;
    RadioScanStation *stationp;

    if (isAborted())
        return -1;

    // other options replace 'all' with fi1, de1, de2
    std::string url = ("http://all.api.radio-browser.info/json/stations/byname/" + _query);
    code = _scanp->retrieveContents(url, &queryResults);
    if (code)
        return code;

    datap = const_cast<char *>(queryResults.c_str());
    code = jsonSys.parseJsonChars(&datap, &rootNodep);
    if (!rootNodep) {
        printf("json parse failed '%s'\n", datap);
        return -1;
    }

    // queryResults in an array of station descriptors
    for(stationNodep = rootNodep->_children.head();
        stationNodep != nullptr;
        stationNodep = stationNodep->_dqNextp) {
        urlNodep = stationNodep->searchForChild("url");
        if (isAborted()) {
            printf("searchFile aborted\n");
            return -1;
        }
        if (urlNodep == nullptr)
            continue;
        url = urlNodep->_children.head()->_name;

        std::string name;
        nameNodep = stationNodep->searchForChild("name");
        if (nameNodep != nullptr) {
            name = nameNodep->_children.head()->_name;
        } else {
            name = RadioScanStation::upperCase(_query);
        }

        std::string urlResolved;
        urlNodep = stationNodep->searchForChild("url_resolved");
        if (urlNodep != nullptr)
            urlResolved = urlNodep->_children.head()->_name;
        

        stationp = new RadioScanStation();
        stationp->init(this);

        std::string iconUrl;
        iconNodep = stationNodep->searchForChild("favicon");
        if (iconNodep != nullptr)
            iconUrl = iconNodep->_children.head()->_name;
        stationp->_iconUrl = iconUrl;

        tagNodep = stationNodep->searchForChild("tags");
        if (tagNodep != nullptr) {
            stationp->_stationShortDescr = std::string("Playing ") +
                RadioScanStation::extractFields(tagNodep->_children.head()->_name, 2);
        }

        /* set these so that addStreamEntry has some useful defaults */
        stationp->_stationName = name;
        stationp->_stationSource = std::string("radio-browser");

        code = stationp->streamApply(url, RadioScanStation::stwCallback, stationp, this);
        if (code == 0) {
            returnStation(stationp);
            continue;
        }

       // if we can't handle the url perhaps url_resolved will work
        code = stationp->streamApply(urlResolved, RadioScanStation::stwCallback,
                                     stationp, this);
        if (code == 0) {
            returnStation(stationp);
            continue;
        }

        // delete it
        delete stationp;
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

void
RadioScanQuery::freeUnusedStations(std::vector<RadioScanStation *> *stationsp) {
    uint32_t tsize = (uint32_t) stationsp->size();
    RadioScanStation *stationp;
    for(uint32_t i = 0;i<tsize;i++) {
        stationp = (*stationsp)[i];
        if (stationp->_inQueryList)
            continue;
        delete stationp;
    }
}

void
RadioScanQuery::returnStation(RadioScanStation *stationp) {
    stationp->_inQueryList = true;
    _stations.append(stationp);
}

int32_t
RadioScanQuery::browseFile() {
    std::string queryResults;
    char *datap;
    int32_t code;
    Json::Node *rootNodep = nullptr;
    Json::Node *stationNodep = nullptr;
    Json::Node *childNodep = nullptr;
    Json jsonSys;
    RadioScanStation *stationp;
    uint32_t i;

    std::string url = ("http://all.api.radio-browser.info/json/stations/search?");
    if (_browseCountry.size() > 0) {
        url.append(std::string("countrycode=") + _browseCountry + "&");
    }
    if (_browseGenre.size() > 0) {
        url.append(std::string("tag=") + _browseGenre + "&");
    }
    if (_browseState.size() > 0) {
        url.append(std::string("state=") + _browseState + "?");
    }
    // easy way to have a standard termination
    url.append("limit=100000");

    code = _scanp->retrieveContents(url, &queryResults);
    if (code)
        return code;

    datap = const_cast<char *>(queryResults.c_str());
    code = jsonSys.parseJsonChars(&datap, &rootNodep);
    if (!rootNodep) {
        printf("json parse failed '%s'\n", datap);
        return -1;
    }

    // queryResults in an array of station descriptors
    std::vector<RadioScanStation *> stations;
    for(stationNodep = rootNodep->_children.head();
        stationNodep != nullptr;
        stationNodep = stationNodep->_dqNextp) {

        if (isAborted()) {
            delete rootNodep;
            freeUnusedStations(&stations);
            return -1;
        }

        childNodep = stationNodep->searchForChild("url");
        if (childNodep == nullptr)
            continue;
        url = childNodep->_children.head()->_name;

        std::string name;
        childNodep = stationNodep->searchForChild("name");
        if (childNodep != nullptr) {
            name = childNodep->_children.head()->_name;
        } else {
            name = RadioScanStation::upperCase(_query);
        }

        std::string iconUrl;
        childNodep = stationNodep->searchForChild("favicon");
        if (childNodep != nullptr) {
            iconUrl = childNodep->_children.head()->_name;
        }

        std::string codec;
        childNodep = stationNodep->searchForChild("codec");
        if (childNodep != nullptr) {
            codec = childNodep->_children.head()->_name;
        } else {
            codec = "UNK";
        }

        uint32_t bitRate;
        childNodep = stationNodep->searchForChild("bitrate");
        if (childNodep != nullptr) {
            bitRate = atoi(childNodep->_children.head()->_name.c_str());
        } else {
            bitRate = 0;
        }

        stationp = new RadioScanStation();
        stationp->init(this);
        stations.push_back(stationp);

        childNodep = stationNodep->searchForChild("tags");
        if (childNodep != nullptr) {
            stationp->_stationShortDescr = std::string("Playing ") +
                RadioScanStation::extractFields(childNodep->_children.head()->_name, 2);
        }

        /* set these so that addStreamEntry has some useful defaults */
        stationp->_stationName = name;
        stationp->_stationSource = std::string("radio-browser");
        stationp->_sourceUrl = url;
        stationp->_iconUrl = iconUrl;
        // these are defaults if the stream doesn't have a header
        stationp->_streamRateKb = bitRate;
        stationp->_streamType = codec;
    }

    delete rootNodep;
    rootNodep = nullptr;

    uint32_t arraySize;
    uint32_t maxReturned;
    uint32_t returnedCount;
    uint32_t randomIx;
    uint32_t randomizeLimit;

    // don't try to return more than are available
    arraySize = (uint32_t) stations.size();
    if (arraySize < _browseMaxCount)
        maxReturned = arraySize;
    else
        maxReturned = _browseMaxCount;

    // randomly exchange elements in the area we'll be returning
    // stations from.  But since we may end up with some dead
    // stations, we'll bail on our randomizing a little later than
    // maxReturned (that's where the multiplier on maxReturned comes
    // from).
    randomizeLimit = 3 * maxReturned / 2;
    if (randomizeLimit > arraySize)
        randomizeLimit = arraySize;
    for(i=0;i<randomizeLimit;i++) {
        randomIx = (uint32_t) (random() % arraySize);
        stationp = stations[randomIx];
        stations[randomIx] = stations[i];
        stations[i] = stationp;
    }

    returnedCount = 0;
    for(i=0;i<arraySize;i++) {
        if (isAborted()) {
            freeUnusedStations(&stations);
            return -1;
        }

        stationp = stations[i];
        code = stationp->streamApply(stationp->_sourceUrl,
                                     RadioScanStation::stwCallback,
                                     stationp,
                                     this);
        if (code == 0) {
            if (stationp->_entries.head() != nullptr) {
                returnedCount++;
                returnStation(stationp);
            }
        }
        if (returnedCount > maxReturned)
            break;
    }

    freeUnusedStations(&stations);

    return 0;
}

bool
RadioScanQuery::isPrefix(std::string prefix, std::string target) {
    const char *prefixp = prefix.c_str();
    const char *targetp = target.c_str();
    uint32_t prefixLen = (uint32_t) prefix.size();
    uint32_t targetLen = (uint32_t) target.size();
    int32_t count = targetLen - prefixLen;

    // slide prefix over i bytes and check if they match.
    for(uint32_t i=0;i<count;i++) {
        if (strncasecmp(prefixp, targetp+i, prefixLen) == 0)
            return true;
    }

    return false;
}

int32_t
RadioScanQuery::searchRadioTime()
{
    char tbuffer[1024];
    int32_t code;
    std::string data;

    Xgml xgmlSys;
    Xgml::Node *xgmlNodep;
    Xgml::Node *childListp;
    Xgml::Attr *attrNodep;
    std::string textString;
    std::string imageUrl;
    int foundBitrate;
    int foundText;
    std::string urlString;
    const char *qpos;
    const char *tp;
    char *datap;
    int64_t tlen;
    RadioScanStation *stationp;

    strcpy(tbuffer, "http://opml.radiotime.com/Search.ashx?query=");
    strcat(tbuffer, _query.c_str());
    strcat(tbuffer, "&types=station&format=mp3,aac");

    code = _scanp->retrieveContents(std::string(tbuffer), &data);
    if (code) {
        printf("retrieval of '%s' failed\n", tbuffer);
        return -2;
    }

    /* data is an xml string */
    datap = const_cast<char *>(data.c_str());
    code = xgmlSys.parse(&datap, &xgmlNodep);
    if (code) {
        printf("xml parse failed\n");
        return -1;
    }

    /* we're returned a bunch of "outline" nodes, each of which has
     * attrs that include a URL attribute and a bitrate attribute.  If
     * the bitrate isn't present, we skip this outline node, as it is
     * likely to be something telling us that the station doesn't
     * stream, or it isn't available in our nation.
     */
    childListp = xgmlNodep->searchForChild("outline");
    bool first;
    for(first=true; childListp; childListp = childListp->_dqNextp, first=false) {
        if (isAborted()) {
            delete xgmlNodep;
            return -1;
        }
        foundBitrate = 0;
        foundText = 0;
        for(attrNodep = childListp->_attrs.head(); attrNodep; attrNodep = attrNodep->_dqNextp) {
            if (strcmp(attrNodep->_name.c_str(), "bitrate") == 0) {
                foundBitrate = 1;
            }
            else if (strcmp(attrNodep->_name.c_str(), "URL") == 0) {
                urlString = attrNodep->_value;
            }
            else if (strcmp(attrNodep->_name.c_str(), "text") == 0) {
                textString = attrNodep->_value;
                foundText = 1;
            } else if (strcmp(attrNodep->_name.c_str(), "image") == 0) {
                imageUrl = attrNodep->_value;
            }
        }

        if (!foundBitrate) {
            continue;
        }

        // check if name is substr, skip if not, unless this is the first
        // element.
        if (!first && !isPrefix(_query, textString))
            continue;

        /* here if we found both the URL and bitrate, we have a real radio station and the
         * URL's contents are actually another URl (barf).
         */
        code = _scanp->retrieveContents(urlString, &data);
        if (code) {
            printf("retrieval of '%s' failed\n", tbuffer);
            continue;
        }

        /* now data is actually the URL.  Sometimes the url has a "?" field
         * that we should remove before saving it
         */
        tp = data.c_str();
        qpos = strchr(tp, '?');
        if (qpos) {
            tlen = qpos - tp;
            data.erase(tlen);
        }

        if (!foundText)
            textString = _query;

        /* here, qpos is the radio station's actual streaming URL */
        stationp = new RadioScanStation();
        stationp->init(this);
        stationp->_stationName = textString;
        stationp->_stationShortDescr = textString;
        stationp->_stationSource = std::string("radio time");
        stationp->_iconUrl = imageUrl;

        code = stationp->streamApply(data.c_str(), RadioScanStation::stwCallback, stationp, this);
        if (code || stationp->_entries.count() == 0) {
            delete stationp;
        } else {
            returnStation(stationp);
        }
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
    const char *iconUrlp = NULL;
    const char *basep;
    uint32_t stationId=0;
    int tc;
    RadioScanStation *stationp;

    snprintf(tbuffer, sizeof(tbuffer),
             "http://api.shoutcast.com/legacy/stationsearch?k=%s&limit=100&search=",
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
        if (isAborted()) {
            delete xgmlNodep;
            return -1;
        }

        if (strcmp(childListp->_name.c_str(), "station") == 0) {
            for( attrNodep = childListp->_attrs.head(); 
                 attrNodep; 
                 attrNodep = attrNodep->_dqNextp) {
                if (strcmp(attrNodep->_name.c_str(), "name") == 0) {
                    stationNamep = attrNodep->_value.c_str();
                }
                else if (strcmp(attrNodep->_name.c_str(), "genre") == 0) {
                    stationGenrep = attrNodep->_value.c_str();
                } else if (strcmp(attrNodep->_name.c_str(), "logo") == 0) {
                    iconUrlp = attrNodep->_value.c_str();
                }
                else if (strcmp(attrNodep->_name.c_str(), "id") == 0) {
                    stationId = atoi(attrNodep->_value.c_str());
                }
            } /* loop over all attrs */

            /* now make a call to get the station attributes, including the stream URL */
            snprintf(tbuffer, sizeof(tbuffer),
                     "http://yp.shoutcast.com%s?id=%d", basep, stationId);

            stationp = new RadioScanStation();
            stationp->init(this);

            /* set these so that addStreamEntry has some useful defaults */
            stationp->_stationName = std::string(stationNamep);
            stationp->_stationShortDescr = std::string(stationGenrep);
            if (iconUrlp != nullptr)
                stationp->_iconUrl = std::string(iconUrlp);
            stationp->_stationSource = std::string("shoutcast");

            code = stationp->streamApply(std::string(tbuffer), RadioScanStation::stwCallback,
                                         stationp, this);
            if (code == 0) {
                returnStation(stationp);
            } else {
                delete stationp;
            }
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

    snprintf(tbuffer, sizeof(tbuffer),
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

    snprintf(tbuffer, sizeof(tbuffer),
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

    delete rootNodep;

    stationp = new RadioScanStation();
    stationp->init(this);

    /* set these so that addStreamEntry has some useful defaults */
    stationp->_stationName = RadioScanStation::upperCase(_query);
    stationp->_stationShortDescr = stationShortDescr;
    stationp->_stationSource = std::string("dar.fm");

    code = stationp->streamApply(url, RadioScanStation::stwCallback, stationp, this);
    if (code == 0) {
        returnStation(stationp);
    }
    else
        delete stationp;

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

    stationp->addStreamEntry(urlp, streamTypep, stationp->_streamRateKb);
    
    return 0;
}

int32_t
RadioScanQuery::searchStreamTheWorld()
{
    char tbuffer[1024];
    int32_t code;
    std::string result;
    RadioScanStation *stationp;

    if (isAborted())
        return -1;

    /* try FM */
    stationp = new RadioScanStation();
    stationp->init(this);
    stationp->_stationName = RadioScanStation::upperCase(_query);
    stationp->_stationShortDescr = "FM Radio";
    stationp->_stationSource = std::string("StreamTheWorld FM");
    snprintf(tbuffer, sizeof(tbuffer),
             "http://playerservices.streamtheworld.com/pls/%sFMAAC.pls", _query.c_str());
    code = stationp->streamApply(tbuffer, RadioScanStation::stwCallback, stationp, this);
    if (code || stationp->_entries.count() == 0) {
        delete stationp;
    } else {
        returnStation(stationp);
    }

    if (isAborted())
        return -1;

    /* try AM */
    stationp = new RadioScanStation();
    stationp->init(this);
    stationp->_stationName = RadioScanStation::upperCase(_query);
    stationp->_stationShortDescr = "AM Radio";
    stationp->_stationSource = std::string("StreamTheWorld AM");
    snprintf(tbuffer, sizeof(tbuffer),
             "http://playerservices.streamtheworld.com/pls/%sAMAAC.pls", _query.c_str());
    code = stationp->streamApply(tbuffer, RadioScanStation::stwCallback, stationp, this);
    if (code || stationp->_entries.count() == 0) {
        delete stationp;
    } else {
        returnStation(stationp);
    }
    
    return 0;
}

void
RadioScanQuery::initBrowse(RadioScan *scanp, 
                           int32_t maxCount,
                           std::string country,
                           std::string state,
                           std::string city,
                           std::string genre)
{
    _browseMaxCount = maxCount;
    _browseCountry = country;
    _browseState = state;
    _browseCity = city;
    _browseGenre = genre;
    _scanp = scanp;
}

std::string
RadioScanQuery::getStatus()
{
    std::string result;
    char tbuffer[1024];
    std::string status = _baseStatus;
    if (_verifyingUrl.size() != 0) {
        status += ", verifying ";
        status += _verifyingUrl;
        status += " ";
    }

    if (_stations.count() == 1)
        strcpy(tbuffer, " (1 station)");
    else
        snprintf(tbuffer, sizeof(tbuffer), " (%ld stations)", _stations.count());

    result = status + std::string(tbuffer);
    return result;
}

/* call initBrowse, and then browseStations with the resulting initialized query */
void
RadioScan::browseStations( RadioScanQuery *resp)
{
    resp->_baseStatus = std::string("Browsing radio-browser.info");
    resp->browseFile();
}

/* external function to do a specific search */
void
RadioScan::searchStation(std::string query, RadioScanQuery **respp)
{
    RadioScanQuery *resp;

    resp = *respp;
    if (resp == NULL) {
        resp = new RadioScanQuery();
    }

    resp->init(this, query);
    *respp = resp;

    // Most stations have no entries, but some have a whole bunch.
    //
    // TODO: Not clear if it is worth it, but perhaps limit to first 4 results
    if (query.size() <= 4) {
        resp->_baseStatus = std::string("Searching StreamTheWorld");
        resp->searchStreamTheWorld();
        if (resp->isAborted())
            return;
    }

    // This one replaces the file download, which is now generated
    // from this site.
    resp->_baseStatus = std::string("Searching radio-browser.info");
    resp->searchFile();
    if (resp->isAborted())
        return;

    // TODO: this can return a whole bunch of things, so search for
    // the query string in the name before doing more work.
    resp->_baseStatus = std::string("Searching RadioTime");
    resp->searchRadioTime();
    if (resp->isAborted())
        return;

#if 1
    // TODO: sometimes really slow; only use uberstation URL
    // Get rid of this
    /* add entries from DAR.fm */
    resp->_baseStatus = std::string("Searching dar.fm");
    resp->searchDar();
    if (resp->isAborted())
        return;
#endif


#if 1
    // doesn't seem to work for station names, just genres
    resp->_baseStatus = std::string("Searching Shoutcast");
    resp->searchShoutcast();
    if (resp->isAborted())
        return;
#endif

    takeLock();
    
    releaseLock();
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
RadioScanStation::addStreamEntry(const char *streamUrlp, const char *typep, uint32_t streamRateKb)
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

/* static */ std::string
RadioScanStation::extractFields(std::string tags, int32_t fields) {
    int32_t commaCount = 0;
    const char *datap = tags.c_str();
    const char *origDatap = datap;
    bool foundComma = false;
    int tc;

    for(tc = *datap; tc != 0; tc = *(++datap)) {
        if (tc == ',') {
            commaCount++;
        }
        if (commaCount == fields) {
            foundComma = true;
            break;
        }
    }

    if (foundComma) {
        return std::string(origDatap, datap - origDatap);
    } else {
        return tags;
    }
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

RadioScanQuery::~RadioScanQuery() {
    RadioScanStation *nextp;
    RadioScanStation *stationp;
    for(stationp = _stations.head(); stationp != nullptr; stationp = nextp) {
        osp_assert(stationp->_inQueryList);
        nextp = stationp->_dqNextp;
        delete stationp;
    }
}

RadioScanStation::~RadioScanStation() {
    RadioScanStation::Entry *entryp;
    RadioScanStation::Entry *nextp;
    for(entryp = _entries.head(); entryp != nullptr; entryp = nextp) {
        nextp = entryp->_dqNextp;
        delete entryp;
    }
}
