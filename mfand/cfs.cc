#include "cfs.h"

/* this file provides generic operations on cnodes */
/* static */ uint64_t
Cfs::fnvHash64(std::string *strp)
{
    uint64_t hash;
    uint64_t dataByte;
    uint8_t *datap = (uint8_t *) strp->c_str();
    uint32_t nchars = strp->length();
    uint32_t i;

    hash = 0xcbf29ce484222325ULL;
    for(i=0;i<nchars;i++) {
        dataByte = *datap++;
        hash = hash ^ dataByte;
        hash *= 1099511628211ULL;
    }
    return hash;
}
