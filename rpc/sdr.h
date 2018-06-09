#ifndef __SDR_H_ENV__
#define __SDR_H_ENV__ 1

#include "osp.h"
#include <uuid/uuid.h>

class Sdr;

/* superclass for each type that can marshal or unmarshal itself */
class SdrSerialize {
 public:
    virtual int32_t marshal(Sdr *sdrp, int isMarshal) = 0;
};

/* these are subclassed by classes that can receive or hold marshaled data; isMarshal
 * is equivalent to isWrite to a pipe.
 */
class Sdr {
 public:
    virtual int32_t copyCountedBytes(char *targetp, uint32_t nbytes, int isMarshal) = 0;

    virtual uint32_t bytes() = 0;

    virtual int32_t copyChar(uint8_t *datap, int isMarshal);

    virtual int32_t copyShort(uint16_t *datap, int isMarshal);

    virtual int32_t copyLong(uint32_t *datap, int isMarshal);

    virtual int32_t copyLongLong(uint64_t *datap, int isMarshal);

    virtual int32_t copyUuid(uuid_t *uuidp, int isMarshal);

    int32_t copyString(char **adatap, int isMarshal);
};

#endif /* __SDR_H_ENV__ */

