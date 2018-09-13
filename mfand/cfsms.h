#ifndef _CFSMS_H_ENV__
#define _CFSMS_H_ENV__ 1

#include "osp.h"
#include "cfs.h"
#include "json.h"
#include "xapipool.h"

class Cfs;
class CnOps;
class Cnode;
class CfsMs;
class CnodeMs;
class CnodeLockSet;

/* Lock order: cnode lock (parent to child), hash lock, ref count lock.
 *
 * You can't remove a node from the parent/child tree without holding the refCount
 * lock, seeing that the child has a zero ref count, and that the node being
 * removed has no children.
 *
 * You can't modify a CnodeBackEntry without holding the parent's lock and a reference
 * on the child cnode (to prevent it from being freed).  Once you're doing this, you
 * can change the name or do other things to the entry, as long as its parent, child
 * and queue pointers all remain unchanged.  That is you can change the name, or turn
 * on or off the _valid flag, should we add one.
 *
 * You must hold a reference count on a child to advance the child
 * search to the new node.  Once you hold the reference, you can
 * release the _refLock.
 */
class CnodeBackEntry {
 public:
    std::string _name;
    CnodeMs *_parentp;                  /* soft reference */
    CnodeMs *_childp;                   /* soft reference */

    /* next entry belonging to this same child, i.e. another hard link to this same
     * file.
     */
    CnodeBackEntry *_nextSameChildp;

    /* list of all same parent */
    CnodeBackEntry *_dqNextp;
    CnodeBackEntry *_dqPrevp;

    CnodeBackEntry() {
        _parentp = NULL;
        _nextSameChildp = NULL;
    }
};

/* Cnodes represent files in the local file system or the cloud; _parentp is null
 * for the root.
 */
class CnodeMs : public Cnode {
    friend class CfsMs;

    std::string _id;

    CnodeMs *_nextHashp;
    CnodeMs *_nextFreep;                /* union with nextHashp? */
    CnodeBackEntry *_backEntriesp;      /* our names in our parent */
    dqueue<CnodeBackEntry> _children;   /* list of our children */
    uint32_t _hashIx;
    uint8_t _isRoot;
    uint8_t _inLru;
    uint8_t _inHash;

public:
    /* queue entries for CfsMs LRU queue */
    CnodeMs *_dqNextp;
    CnodeMs *_dqPrevp;

 protected:
    CfsMs *_cfsp;

 public:

    CnodeMs() {
        _cfsp = NULL;
        _nextHashp = NULL;
        _backEntriesp = NULL;
        _isRoot = 0;
        _inLru = 0;
        _hashIx = 0;
        _inHash = 0;
    }

    int recyclable() {
        if ( _refCount > 0 ||
             _children.count() > 0)
            return 0;
        else
            return 1;
    }

    int32_t fillAttrs( CEnv *envp, CnodeLockSet *lockSetp);

    int32_t nameSearch(std::string nanme, CnodeMs **childpp);

    void hold();

    void release();

    void holdNL();

    void releaseNL();

    void invalidateTree();

    void unthreadEntry(CnodeBackEntry *ep);

    /* virtual ops realized */
    int32_t getAttr(CAttr *attrsp, CEnv *envp);

    int32_t lookup(std::string name, int forceBackend, Cnode **nodepp, CEnv *envp);

    int32_t create(std::string name, Cnode **nodepp, CEnv *envp) {
        return -1;
    }
    int32_t mkdir(std::string name, Cnode **nodepp, CEnv *envp);

    int32_t open(uint32_t flags, CFile **filepp) {
        return -1;
    }
    int32_t close(CFile *filep) {
        return -1;
    }

    int32_t write(CFile *cp, uint64_t offset, uint32_t length, CEnv *envp) {
        return -1;
    }

    int32_t read(CFile *cp, uint64_t offset, uint32_t length, CEnv *envp) {
        return -1;
    }

    int32_t startSession(std::string name,
                         std::string *sessionUrlp);

    int32_t sendData( std::string *sessionUrlp,
                      CDataSource *sourcep,
                      uint64_t fileLength,
                      uint64_t byteOffset,
                      uint32_t byteCount);

    static int32_t abortSession( std::string *sessionUrlp);

    int32_t sendSmallFile(std::string name, CDataSource *sourcep, CEnv *envp);

    /* send the whole file, whose final size is 'size'.  Use fillProc to obtain
     * data to send.  Creates a file with specified name in dir cp.
     */
    int32_t sendFile( std::string name,
                      CDataSource *sourcep,
                      uint64_t *bytesCopiedp,
                      CEnv *envp);
    
    int32_t getPath(std::string *pathp, CEnv *envp);

    int32_t parseResults( Json::Node *jnodep,
                          std::string *idp,
                          uint64_t *sizep,
                          uint64_t *changeTimep,
                          uint64_t *modTimep,
                          CAttr::FileType *fileTypep,
                          uint8_t *allFoundp = NULL);
};

/* state for retryError calls */
class CfsRetryError {
 public:
    /* max # of retries for errors that by definition should be fatal immediately, but
     * which Azure nevertheless returns intermittently.  Don't make < 2, or we won't
     * be able to handle authentication handshakes triggered by auth errors.
     */
    static const uint32_t _maxRetries=6;
    uint32_t _retries;

    CfsRetryError() {
        _retries = 0;
    }
};

/* one of these per file system instance */
class CfsMs : public Cfs {
 public:
    static const uint32_t _hashSize = 997;
    static const uint32_t _maxCnodeCount = 1000;
    uint32_t _cnodeCount;
    SApiLoginCookie *_loginCookiep;
    CThreadMutex _lock;         /* protect hash table */
    CThreadMutex _refLock;         /* protect ref count and LRU */
    CnodeMs *_hashTablep[_hashSize];
    XApiPool *_xapiPoolp;
    CnodeMs *_rootp;
    uint8_t _verbose;
    dqueue<CnodeMs> _lruQueue;
    std::string _pathPrefix;
    CnodeMs *_freeListp;
    uint64_t _stalledErrors;
    CfsStats _stats;

    CfsMs(SApiLoginCookie *loginCookiep, std::string pathPrefix) {
        _pathPrefix = pathPrefix;
        _xapiPoolp = new XApiPool(pathPrefix);
        _loginCookiep = loginCookiep;
        _rootp = NULL;
        _verbose = 0;
        _cnodeCount = 0;
        _stalledErrors = 0;
        _freeListp = NULL;
        memset(_hashTablep, 0, sizeof(_hashTablep));
    }

    CfsStats *getStats() {
        return &_stats;
    }

    void setVerbose() {
        _verbose = 1;
    }

    XApiPool *getPool() {
        return _xapiPoolp;
    }

#if 0
    void setLogin(SApiLoginMS *loginp) {
        if (_loginp)
            delete _loginp;
        _loginp = loginp;
    }
#endif

    /* if any of the last 8 operations failed with a stalling code,
     * return that we're stalling.
     */
    int getStalling() {
        if ((_stalledErrors & 0xFF) == 0)
            return 0;
        else
            return 1;
    }

    int32_t root(Cnode **rootpp, CEnv *envp);
    
    CnodeMs *allocCnode(CThreadMutex *hashLockp);

    void recycle();

    void logError(int32_t code, std::string errorString);

    void checkRecycle();

    int32_t getCnode(std::string *idp, CnodeMs **cnodepp);

    int32_t retryError( CfsLog::OpType type,
                        XApi::ClientReq *reqp,
                        Json::Node **parsedNodepp,
                        CfsRetryError *statep);

    int32_t getCnodeLinked( CnodeMs *parentp,
                            std::string name,
                            std::string *idp,
                            CnodeMs **cnodepp,
                            CnodeLockSet *lockSetp);
};

class CnodeLockSet {
    static const uint32_t _maxLocks = 4;
    uint8_t _nlocks;
    CnodeMs *_cnodep[_maxLocks];

 public:

    CnodeLockSet() {
        uint32_t i;

        _nlocks = 0;
        for(i=0;i<_maxLocks;i++)
            _cnodep[i] = NULL;
    }

    /* returns 0 if caller doesn't need to revalidate locked data; for
     * now, we're always locking from parent down, so there are no
     * cases where we lock in wrong order.
     */
    int add(CnodeMs *cnodep) {
        int32_t best = -1;
        int32_t i;

        for(i=0;i<_maxLocks;i++) {
            if (_cnodep[i] == cnodep)
                return 0;

            if (_cnodep[i] == NULL && best < 0)
                best = i;
        }

        osp_assert(best >= 0);
        _cnodep[best] = cnodep;
        cnodep->_lock.take();
        return 0;
    }

    int remove(CnodeMs *cnodep) {
        uint32_t i;

        for(i=0;i<_maxLocks;i++) {
            if (_cnodep[i] == cnodep) {
                _cnodep[i]->_lock.release();
                _cnodep[i] = NULL;
                return 0;
            }
        }
        return -1;
    }

    void reset() {
        int32_t i;

        for(i=0;i<_maxLocks;i++) {
            if (_cnodep[i]) {
                _cnodep[i]->_lock.release();
                _cnodep[i] = NULL;
            }
        }
    }

    ~CnodeLockSet() {
        reset();
    }
};

#endif /* _CFSMS_H_ENV__ */
