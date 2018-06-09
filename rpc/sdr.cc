#include "sdr.h"

int32_t
Sdr::copyString(char **adatapp, int isMarshal)
{
    uint32_t tsize;
    int32_t code;
    char *datap;

    if (isMarshal) {
        datap = *adatapp;
        if (datap == NULL)
            datap = (char *) "";
        tsize = (uint32_t) strlen(const_cast<char *>(datap)) + 1;
        code = copyLong(&tsize, isMarshal);
        if (code)
            return code;
        code = copyCountedBytes(datap, tsize, isMarshal);
        return code;
    }
    else {
        code = copyLong(&tsize, isMarshal);
        if (code)
            return code;
        datap = new char[tsize];
        if (!datap)
            return -1;
        code = copyCountedBytes(datap, tsize, isMarshal);
        if (code) {
            delete [] datap;
            *adatapp = NULL;
            return code;
        }
        else {
            *adatapp = datap;
            return 0;
        }
    }
}

int32_t
Sdr::copyChar(uint8_t *adatap, int isMarshal)
{
    return copyCountedBytes((char *) adatap, sizeof(uint8_t), isMarshal);
}

int32_t
Sdr::copyShort(uint16_t *adatap, int isMarshal)
{
    return copyCountedBytes((char *) adatap, sizeof(uint16_t), isMarshal);
}


int32_t
Sdr::copyLong(uint32_t *adatap, int isMarshal)
{
    return copyCountedBytes((char *) adatap, sizeof(uint32_t), isMarshal);
}


int32_t
Sdr::copyLongLong(uint64_t *adatap, int isMarshal)
{
    return copyCountedBytes((char *) adatap, sizeof(uint64_t), isMarshal);
}


int32_t
Sdr::copyUuid(uuid_t *uuidp, int isMarshal)
{
    return copyCountedBytes((char *) uuidp, sizeof(uuid_t), isMarshal);
}
