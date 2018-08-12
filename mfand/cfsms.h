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

class CnodeBackEntry {
 public:
    std::string _name;
    CnodeMs *_parentp;                  /* hard reference */
    CnodeMs *_childp;                   /* sort reference */

    /* next entry belonging to this same child, i.e. another hard link to this same
     * file.
     */
    CnodeBackEntry *_nextSameChildp;

    /* TBD: do we need to have a list of all entries belonging to a particular parent? */
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

    CnodeMs *_nextIdHashp;
    CnodeBackEntry *_backEntriesp;      /* our names in our parent */
    dqueue<CnodeBackEntry> _children;   /* list of our children */
    uint8_t _isRoot;

 protected:
    CfsMs *_cfsp;

 public:

    CnodeMs() {
        _cfsp = NULL;
        _nextIdHashp = NULL;
        _backEntriesp = NULL;
        _isRoot = 0;
    }

    int32_t fillAttrs( CEnv *envp, CnodeLockSet *lockSetp);

    int32_t nameSearch(std::string nanme, CnodeMs **childpp);

    void hold();

    void release();

    /* virtual ops realized */
    int32_t getAttr(CAttr *attrsp, CEnv *envp);

    int32_t lookup(std::string name, Cnode **nodepp, CEnv *envp);

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


/* one of these per file system instance */
class CfsMs : public Cfs {
 public:
    static const uint32_t _hashSize = 997;
    SApiLoginMS *_loginp;
    CThreadMutex _lock;         /* protect hash table */
    CThreadMutex _refLock;         /* protect hash table */
    CnodeMs *_hashTablep[_hashSize];
    XApiPool *_xapiPoolp;
    CnodeMs *_rootp;

    CfsMs(SApiLoginMS *loginp) {
        _xapiPoolp = new XApiPool();
        _loginp = loginp;
        _rootp = NULL;
        memset(_hashTablep, 0, sizeof(_hashTablep));
    }

    void setLogin(SApiLoginMS *loginp) {
        if (_loginp)
            delete _loginp;
        _loginp = loginp;
    }

    int32_t root(Cnode **rootpp, CEnv *envp);
    
    int32_t getCnode(std::string *idp, CnodeMs **cnodepp);

    int32_t retryError(XApi::ClientReq *reqp, Json::Node *parsedNodep);

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
