#ifndef _CFSMS_H_ENV__
#define _CFSMS_H_ENV__ 1

#include "osp.h"
#include "cfs.h"

class Cfs;
class CnOps;
class Cnode;

/* Cnodes represent files in the local file system or the cloud; _parentp is null
 * for the root.
 */
class CnodeMs : public Cnode {
    std::string _id;

 public:

    /* virtual ops realized */
    int32_t getAttr(Cnode *cp, Cattr *attrsp, Cenv *envp) {
        return -1;
    }

    int32_t lookup(Cnode *cp, std::string name, Cnode **nodepp, Cenv *envp) {
        return -1;
    }

    int32_t create(Cnode *cp, std::string name, Cnode **nodepp, Cenv *envp) {
        return -1;
    }
    int32_t mkdir(Cnode *cp, std::string name, Cnode **nodepp, Cenv *envp);

    int32_t open(Cnode *cp, uint32_t flags, Cfile **filepp) {
        return -1;
    }
    int32_t close(Cfile *filep) {
        return -1;
    }

    int32_t write(Cfile *cp, uint64_t offset, uint32_t length, Cenv *envp) {
        return -1;
    }

    int32_t read(Cfile *cp, uint64_t offset, uint32_t length, Cenv *envp) {
        return -1;
    }

    int32_t getPath(Cnode *cp, std::string *pathp, Cenv *envp);
};


/* one of these per file system instance */
class CfsMs {
 public:
    virtual int32_t root(Cnode **rootpp, Cenv *envp) = 0;
};

#endif /* _CFSMS_H_ENV__ */
