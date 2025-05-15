#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <string.h>
#include <string>

#include "restcall.h"
#include "xgml.h"

/* callback from easy_perform called as the response arrives */
/* static */ size_t
RestCall::receiveData( void  *ptr,
                     size_t  size,
                     size_t  nmemb,
                     void *stream)
{
    RestCall *callp = (RestCall *) stream;
    uint32_t nbytes = nmemb * size;

    callp->_responseBufferp->append((char *)ptr, nbytes);
    return nbytes;
}

/* callback from easy_perform called to copy the request to our caller */
/* static */ size_t
RestCall::sendData( void  *ptr,
                  size_t  size,
                  size_t  nmemb,
                  void *stream)
{
    uint32_t nbytes = nmemb * size;
    RestCall *callp = (RestCall *) stream;
    uint32_t length = callp->_requestBuffer.size();

    if (nbytes > length)
        nbytes = length;

    memcpy(ptr, callp->_requestBuffer.c_str(), nbytes);
    callp->_requestBuffer.erase(0, nbytes);

    return nbytes;
}

/* get an OFX XGML object with the right keywords for OFX 1.0.2 preloaded; this can
 * be used to parse the XML / SGML parts of a response, or to generate the output
 * from an Xgml (== SGML / XML) node.
 */
Xgml *
RestCall::initXgml()
{
    Xgml *xgmlp;

    /* now parse the results */
    xgmlp = new Xgml();
    _xgmlp = xgmlp;

    return xgmlp;
}

/* setup the state for a call to the RestCall entity's web site. */
int32_t
RestCall::setupCurl()
{
    /* we have to free the old curl object? */
    _curlp = curl_easy_init();
    if (_curlp) {
        curl_easy_setopt(_curlp, CURLOPT_URL, _url.c_str());
    }
    else {
        printf("can't create curl object\n");
        return -1;
    }

    curl_easy_setopt(_curlp, CURLOPT_READDATA, this);
    curl_easy_setopt(_curlp, CURLOPT_READFUNCTION, sendData);

    curl_easy_setopt(_curlp, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(_curlp, CURLOPT_WRITEFUNCTION, receiveData);

    _requestBuffer.clear();

    return 0;
}

/* static */ char *
RestCall::getNonce()
{
    static char tbuffer[64];
    static uint64_t counter = 0;
    if (counter == 0)
        counter = ((uint64_t) time(0))<<32;
    snprintf(tbuffer, sizeof(tbuffer), "%16llx", counter++);
    return tbuffer;
}

/* static */ char *
RestCall::getTimeSecs()
{
    static char tbuffer[64];
    snprintf(tbuffer, sizeof(tbuffer), "%d", (int) time(NULL));
    return tbuffer;
}

/* make a call, returning the parsed XML structure as an output parameter; we marshall
 * the inodep to the target, and parse the response.
 */
int32_t
RestCall::makeCall( std::string *requestStringp, std::string *resultStringp)
{
    _requestBuffer.clear();
    _responseBufferp = resultStringp;
    _responseBufferp->clear();

    curl_easy_perform(_curlp);

    /* cleanup everything */
    curl_slist_free_all(_slistp); /* free the list again */
    curl_easy_cleanup(_curlp);
    _slistp = NULL;
    _curlp = NULL;

    /* look for <OFX> in received data (i.e. skip OFX headers) */
    (void) _responseBufferp->c_str();
    (void) _responseBufferp->length();

    return 0;
}

/* call this if you can't call makecall for some reason */
void
RestCall::cleanupCall()
{
    curl_slist_free_all(_slistp); /* free the list again */
    curl_easy_cleanup(_curlp);

    _slistp = NULL;
    _curlp = NULL;
}
