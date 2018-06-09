#ifndef __NETREST_H_ENV__
#define __NETREST_H_ENV__ 1

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <string.h>
#include <string>
#include "xgml.h"

class RestCall {
    std::string _url;
    std::string _requestBuffer;
    std::string *_responseBufferp;
    struct curl_slist *_slistp;
    CURL *_curlp;
    Xgml *_xgmlp;

 public:
    RestCall() {
        curl_global_init(CURL_GLOBAL_NOTHING);  /* use CURL_GLOBAL_SSL if want SSL */
        _curlp = NULL;
        _slistp = NULL;
        _xgmlp = initXgml();
    }

    ~RestCall() {
        if (_curlp) {
            curl_easy_cleanup(_curlp);
            _curlp = NULL;
        }

        curl_global_cleanup();
    }

    static size_t receiveData( void  *ptr,
                               size_t  size,
                               size_t  nmemb,
                               void *stream);

    static size_t sendData( void  *ptr,
                            size_t  size,
                            size_t  nmemb,
                            void *stream);

    Xgml *initXgml();

    Xgml *getXgml() {
        return _xgmlp;
    }

    static char *getNonce();

    static char *getTimeSecs();

    int32_t makeCall( std::string *requestStringp, std::string *resultStringp);

    void setUrl(char *urlp) {
        _url = urlp;
        setupCurl();
    }

    void cleanupCall();

    int32_t setupCurl();

    int32_t getPrice(double *pricep);
};

#endif /* __NETREST_H_ENV__ */
