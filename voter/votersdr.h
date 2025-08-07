#ifndef __VOTERSDR_H_ENV__
#define __VOTERSDR_H_ENV__ 1

#include <inttypes.h>
#include <uuid/uuid.h>

#include "sdr.h"

// Try things this frequently
static const uint64_t VoterShortMs = 5000;

// Give up on things with this period.
static const uint64_t VoterLongMs = 20000;

// One for Proposed vote and one for Committed.
class VoterData : public SdrSerialize{
public:
    uuid_t _epochId;
    uint32_t _counter;
    uint8_t _committed;

    VoterData() {
        memset(&_epochId, 0, sizeof(_epochId));
        _counter = 0;
        _committed = 0;
    }

    void generate() {
        uuid_generate(_epochId);
        _counter = 8;
        _committed = 0;
    }

    int32_t marshal(Sdr *sdrp, int marshal) {
        sdrp->copyUuid(&_epochId, marshal);
        sdrp->copyLong(&_counter, marshal);
        sdrp->copyChar(&_committed, marshal);
        return 0;
    }

    bool operator == (const VoterData &rhs) {
        if ( _counter == rhs._counter &&
             memcmp(&_epochId, &rhs._epochId, sizeof(uuid_t)) == 0)
            return true;
        else
            return false;
    }

    bool operator <(const VoterData &rhs) {
        if (_counter < rhs._counter)
            return true;
        else {
            int result = memcmp(&_epochId, &rhs._epochId, sizeof(uuid_t));
            if (_counter == rhs._counter) {
                if (result < 0)
                    return true;
                else
                    return false;
            } else {
                // counters are >= or ==
                return false;
            }
        }
    }

    bool operator > (const VoterData &rhs) {
        if (_counter > rhs._counter)
            return true;
        else {
            int result = memcmp(&_epochId, &rhs._epochId, sizeof(uuid_t));
            if (_counter == rhs._counter) {
                if (result > 0)
                    return true;
                else
                    return false;
            } else {
                // counters are >= or ==
                return false;
            }
        }
    }

    bool operator != (const VoterData &rhs) {
        return !((*this) == rhs);
    }

    bool operator <= (const VoterData &rhs) {
        return (*this) == rhs || (*this) < rhs;
    }

    bool operator >= (const VoterData &rhs) {
        return (*this) == rhs || (*this) > rhs;
    }
};

class VoterPingCall : public SdrSerialize  {
 public:
    static const uint32_t _opcode = 1;

    VoterData _callData;

    VoterPingCall() {
        return;
    }

    int32_t marshal(Sdr *sdrp, int marshal) {
        _callData.marshal(sdrp, marshal);
        return 0;
    }
};

class VoterPingResp : public SdrSerialize  {
 public:
    int32_t _error;
    VoterData _responseData;

    VoterPingResp() {
        return;
    }

    int32_t marshal(Sdr *sdrp, int marshal) {
        _responseData.marshal(sdrp, marshal);
        return 0;
    }
};

#endif // __VOTERSDR_H_ENV__
