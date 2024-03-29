#ifndef _CFS_H_ENV__
#define _CFS_H_ENV__ 1

#include "osp.h"
#include <string>
#include "sapilogin.h"

class Cfs;
class CnOps;
class Cnode;
class XApiPool;

static const int32_t CFS_ERR_OK = 0;
static const int32_t CFS_ERR_NOENT = 2;
static const int32_t CFS_ERR_IO = 5;
static const int32_t CFS_ERR_ACCESS = 13;
static const int32_t CFS_ERR_EXIST = 17;
static const int32_t CFS_ERR_EXDEV = 18;
static const int32_t CFS_ERR_NOTDIR = 20;
static const int32_t CFS_ERR_INVAL = 22;
static const int32_t CFS_ERR_TIMEDOUT = 60;
static const int32_t CFS_ERR_SERVER = 61;

class CfsStats {
 public:
    /* error stats */
    uint64_t _authRequired;
    uint64_t _overloaded5xx;
    uint64_t _busy429;
    uint64_t _busy409;
    uint64_t _bad400;   /* incremented on bogus mkdir 400s that we retry */
    uint64_t _duplicateReceived416;
    uint64_t _mysteryErrors;
    uint64_t _xapiErrors;

    /* call counter */
    uint64_t _getAttrCalls;
    uint64_t _totalCalls;
    uint64_t _getPathCalls;
    uint64_t _lookupCalls;
    uint64_t _sendSmallFilesCalls;
    uint64_t _sendLargeFilesCalls;
    uint64_t _sendDataCalls;
    uint64_t _fillAttrCalls;
    uint64_t _mkdirCalls;

    CfsStats() {
        memset(this, 0, sizeof(*this));
    }
};

class CfsLog {
    public:
    typedef enum {
        opGetAttr = 1,
        opLookup,
        opCreate,
        opMkdir,
        opOpen,
        opClose,
        opRead,
        opWrite,
        opSendFile,
        opPosix,
        opMisc,
    } OpType;

    virtual void logError( OpType type,
                           int32_t httpCode,
                           std::string errorString,
                           std::string longErrorString) = 0;
    virtual ~CfsLog() {
        return;
    }

    static std::string opToString(OpType type);
};

class CAttr {
 public:
    enum FileType {
        DIR,
        FILE,
        LINK,
        UNKNOWN};

    uint64_t _length;
    uint64_t _mtime;    /* mod time in nanoseconds since 1/1/70 */
    uint64_t _ctime;    /* change time in same */
    FileType _fileType;
};

class CDataSource {
 public:
    /* time returned in UTC */
    virtual int32_t getAttr(CAttr *attrp) = 0;
    virtual int32_t read( uint64_t offset, uint32_t count, char *bufferp) = 0;
    virtual int32_t close() = 0;
    virtual ~CDataSource () {
        return;
    }
};

class CFile {
 public:
    Cnode *_cp;

    CFile() {
        _cp = NULL;
    }
};

class CEnv {
 public:
    uint32_t _uid;

    CEnv() {
        _uid = ~0;
    }
};

/* Cnodes represent files in the local file system or the cloud; hard
 * references are held on the parent pointer.
 */
class Cnode {
 public:
    int32_t _refCount;
    uint8_t _valid;
    CAttr _attrs;       /* valid iff _valid is set */
    CThreadMutex _lock; /* protects fields in this, including refCount */

    /* read up to count bytes; only return a short read if you've hit EOF; you can
     * return 0 bytes to indicate EOF if you have nothing more to send.
     */
    typedef int32_t (fillProc)(void *contextp, uint64_t offset, uint32_t count, char *bufferp);

    virtual int32_t getAttr(CAttr *attrsp, CEnv *envp) = 0;
    virtual int32_t lookup(std::string name, int forceBackend, Cnode **nodepp, CEnv *envp) = 0;
    virtual int32_t create(std::string name, Cnode **nodepp, CEnv *envp) = 0;
    virtual int32_t mkdir(std::string name, Cnode **nodepp, CEnv *envp) = 0;
    virtual int32_t open(uint32_t flags, CFile **filepp) = 0;
    virtual int32_t close(CFile *filep) = 0;
    virtual int32_t write(CFile *cp, uint64_t offset, uint32_t length, CEnv *envp) = 0;
    virtual int32_t read(CFile *cp, uint64_t offset, uint32_t length, CEnv *envp) = 0;
    virtual int32_t sendFile( std::string name,
                              CDataSource *sourcep,
                              uint64_t *bytesCopiedp,
                              CEnv *envp) = 0;

    Cnode() {
        _refCount = 1;
        _valid = 0;
    }

    virtual void hold() {
        _lock.take();
        _refCount++;
        _lock.release();
    }

    virtual void release() {
        _lock.take();
        _refCount--;
        _lock.release();
    }
};


/* one of these per file system instance */
class Cfs {
 protected:
    CfsLog *_logp;
 public:

    typedef int32_t nameiProc( void *cxp,
                               Cnode *nodep,
                               std::string name,
                               int forceBackend,
                               Cnode **outNodep,
                               CEnv *envp);

    virtual CfsStats *getStats() = 0;

    virtual int32_t root(Cnode **rootpp, CEnv *envp) = 0;

    virtual int getStalling() = 0;

    virtual std::string legalizeIt(std::string ins) = 0;

    virtual void setLog(CfsLog *logp) {
        _logp = logp;
    }

    virtual XApiPool *getPool() = 0;

    int32_t splitPath(std::string path, std::string *dirPathp, std::string *namep);

    int32_t nameInt( std::string path,
                     nameiProc *procp,
                     void *nameiContextp,
                     int forceBackend,
                     Cnode **targetCnodepp,
                     CEnv *envp);

    int32_t namei( std::string path,
                   int forceBackend,
                   Cnode **targetCnodepp,
                   CEnv *envp);

    int32_t mkpath( std::string path,
                    Cnode **targetCnodepp,
                    CEnv *envp);

    static int32_t nameiCallback(void *cxp,
                                 Cnode *nodep,
                                 std::string name,
                                 int forceBackend,
                                 Cnode **outNodep,
                                 CEnv *envp);

    static int32_t mkpathCallback(void *cxp,
                                  Cnode *nodep,
                                  std::string name,
                                  int forceBackend,
                                  Cnode **outNodep,
                                  CEnv *envp);

    int32_t stat(std::string path, CAttr *attrsp, CEnv *envp);

    int32_t sendFile( std::string path,
                      CDataSource *sourcep,
                      uint64_t *sendFilep,
                      CEnv *envp);

    int32_t mkdir(std::string path, Cnode **newDirpp, CEnv *envp);

    int32_t getAttr(std::string path, CAttr *attrp, CEnv *envp);

    /* just a utility function for hashing IDs and/or names */
    static uint64_t fnvHash64(std::string *strp);

    Cfs() {
        _logp = NULL;
    }

    virtual ~Cfs() {
        return;
    }
};

#endif /* _CFS_H_ENV__ */
