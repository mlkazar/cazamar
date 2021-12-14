#include <curl/curl.h>
#include <string>

#include "json.h"

const char *main_keyp = "Au7NZ15R3LDTT_Lpgt0Pn5JCVqkDyCZjxcKt7GgQZ547CmI1Tku9A00D1YN-yCOz";

size_t
main_saver(void *ptrp, size_t abytes, size_t amem, void *sp)
{
    long byteCount = abytes*amem;
    std::string resultStr;
    Json json;

    resultStr = std::string((const char *)ptrp, byteCount);

    printf("read: %s\n", resultStr.c_str());

    return byteCount;
}

int
main(int argc, char **argv)
{
    CURL *curlp;
    CURLcode res;
    std::string url;
    
    printf("foo\n");
    curlp = curl_easy_init();
    printf("back with curl=%p\n", curlp);

    url = "https://dev.virtualearth.net/REST/v1/Locations?CountryRegion=US&adminDistrict=NY&Locality=Great%20Neck&addressLine=77%20Radnor%20Rd&key=" +std::string(main_keyp);

    curl_easy_setopt(curlp, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curlp, CURLOPT_WRITEFUNCTION, main_saver);
    res = curl_easy_perform(curlp);
    printf("result=%d\n", res);
    curl_easy_cleanup(curlp);
    printf("All done\n");
}
