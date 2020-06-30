#include "rbuf.h"

/* default implementation of getStr; can override */
std::string 
Rbuf::getStr()
{
    std::string rvalue;
    char *datap;
    uint32_t tlen;

    setReadPosition(0);
    while(1) {
        tlen = scan(&datap);
        if (tlen == 0)
            break;
        rvalue += std::string(datap, tlen);
    }
    setReadPosition(0);

    return rvalue;
}
