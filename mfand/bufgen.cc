#include <stdlib.h>

#include "bufgen.h"

BufGen::BufGen()
{
    /* init does all this work */
    return;
}

void
BufGen::init(struct sockaddr *sockAddrp, int socklen)
{
    if (socklen > (signed) sizeof(_destAddr)) {
        printf("BufGen: incoming socket length too large (%d > %d)\n",
               socklen, (int) sizeof(_destAddr));
        osp_assert(0);
    }

    memcpy(&_destAddr, sockAddrp, socklen);

    _hostName = std::string("Incoming");

    _listening = 0;
    _server = 1;
}

void
BufGen::init(char *namep, uint32_t defaultPort)
{
    struct hostent *hostp;
    uint32_t hostAddr;
    uint16_t destPort;
    char *portp;

    memset(&_destAddr, 0, sizeof(_destAddr));
#ifndef __linux__
    _destAddr.sin_len = sizeof(_destAddr);
#endif
    _destAddr.sin_family = AF_INET;

    if (namep != NULL) {
        portp = index(namep, ':');
        _hostName = std::string(namep);
        if (portp == NULL) {
            destPort = defaultPort;
        }
        else {
            destPort = atoi(portp+1);
            _hostName = _hostName.substr(0, portp - namep);
        }

        CThread::_libcMutex.take();
        hostp = gethostbyname(_hostName.c_str());
        if (hostp) {
            memcpy(&_destAddr.sin_addr.s_addr, hostp->h_addr_list[0], 4);
        }
        else {
            CThread::_libcMutex.release();
            printf("BufSocket: can't lookup host '%s'\n", _hostName.c_str());
            _destAddr.sin_addr.s_addr = 0;
            _error = -1;
            return;
        }
        CThread::_libcMutex.release();
    }
    else {
        /* null hostname, typically only used for listening sockets */
        destPort = defaultPort;
    }

    _port = destPort;

    _destAddr.sin_port = htons(destPort);

    hostAddr = htonl(_destAddr.sin_addr.s_addr);

    /* set some defaults; they may be changed */
    _listening = 0;
    _server = 0;
}

BufGen::~BufGen()
{
    return;
}

