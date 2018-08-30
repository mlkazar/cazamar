#ifndef _CFS_H_ENV__
#define _CFS_H_ENV__ 1

#include "osp.h"
#include <string>
#include "sapilogin.h"

class Cfs;
class CnOps;
class Cnode;

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
 public:
    typedef int32_t nameiProc( void *cxp,
                               Cnode *nodep,
                               std::string name,
                               int forceBackend,
                               Cnode **outNodep,
                               CEnv *envp);

    virtual int32_t root(Cnode **rootpp, CEnv *envp) = 0;

    virtual int getStalling() = 0;

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
};

#endif /* _CFS_H_ENV__ */
