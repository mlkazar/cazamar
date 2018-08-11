#ifndef __XAPIPOOL_H_ENV__
#define __XAPIPOOL_H_ENV__ 1

#include "osp.h"
#include "dqueue.h"
#include "bufsocket.h"
#include "xapi.h"

/* manage a pool of TLS and regular connections, creating new
 * connections up to a configured maximum if connections are busy.
 */
class XApiPool {
 public:
    static const uint32_t _maxConns = 4;        /* per address */
    class Entry {
    public:
        Entry *_dqNextp;
        Entry *_dqPrevp;
        XApi::ClientConn *_connp;
        BufGen *_bufGenp;
        uint8_t _isTls;

        Entry() {
            _dqNextp = _dqPrevp = NULL;
            _connp = NULL;
            _bufGenp = NULL;
            _isTls = 0;
        }
    };

    XApi *_xapip;
    dqueue<Entry> _allConns;
    CThreadMutex _lock;
    uint16_t _loop;

    XApiPool() {
        _xapip = new XApi();
        _loop = 0;
        return;
    }

    XApi::ClientConn *getConn( std::string fullHostName,
                               uint32_t port,
                               uint8_t isTls);
};

#endif /* __XAPIPOOL_H_ENV__ */
