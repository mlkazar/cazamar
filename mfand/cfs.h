#ifndef _CFS_H_ENV__
#define _CFS_H_ENV__ 1

#include "osp.h"
#include <string>
#include "sapilogin.h"

class Cfs;
class CnOps;
class Cnode;

class Cattr {
 public:
    uint64_t _length;
    uint64_t _mtime;    /* in nanoseconds since 1/1/70 */
    uint64_t _ctime;    /* ditto */
};

class Cfile {
 public:
    Cnode *_cp;

    Cfile() {
        _cp = NULL;
    }
};

class Cenv {
 public:
    uint32_t _uid;

    Cenv() {
        _uid = ~0;
    }
};

/* Cnodes represent files in the local file system or the cloud; hard
 * references are held on the parent pointer.
 */
class Cnode {
 public:
    int32_t _refCount;
    Cnode *_parentp;

    virtual int32_t getAttr(Cattr *attrsp, Cenv *envp) = 0;
    virtual int32_t lookup(std::string name, Cnode **nodepp, Cenv *envp) = 0;
    virtual int32_t create(std::string name, Cnode **nodepp, Cenv *envp) = 0;
    virtual int32_t mkdir(std::string name, Cnode **nodepp, Cenv *envp) = 0;
    virtual int32_t open(uint32_t flags, Cfile **filepp) = 0;
    virtual int32_t close(Cfile *filep) = 0;
    virtual int32_t write(Cfile *cp, uint64_t offset, uint32_t length, Cenv *envp) = 0;
    virtual int32_t read(Cfile *cp, uint64_t offset, uint32_t length, Cenv *envp) = 0;

    Cnode() {
        _refCount = 1;
        _parentp = NULL;
    }
};


/* one of these per file system instance */
class Cfs {
 public:
    virtual int32_t root(Cnode **rootpp, Cenv *envp) = 0;
};

#endif /* _CFS_H_ENV__ */
