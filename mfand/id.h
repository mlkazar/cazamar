#ifndef __ID_H_ENV__
#define __ID_H_ENV__ 1

#include <stdio.h>
#include <string>
#include "json.h"

class Id {
public:
    InStream *_inStreamp;

    int32_t init(InStream *inp);

    class Id3 {
        uint8_t _header[10];
        int32_t _bytesLeft;
        uint8_t _flagUnsync;
        uint8_t _flagExtendedHeader;

        Id *_idp;

    public:
        int32_t decodeString( InStream *inStreamp,
                              std::string *stringp,
                              uint32_t *asizep,
                              int skipType = 0);

        int32_t parse(Id *idp, FILE *outFilep, std::string *genreStrp);
    };
};

#endif/* __ID_H_ENV__ */
