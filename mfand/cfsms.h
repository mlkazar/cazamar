#ifndef _CFSMS_H_ENV__
#define _CFSMS_H_ENV__ 1

#include "osp.h"
#include "cfs.h"
#include "json.h"

class Cfs;
class CnOps;
class Cnode;
class CfsMs;

/* Cnodes represent files in the local file system or the cloud; _parentp is null
 * for the root.
 */
class CnodeMs : public Cnode {
    friend class CfsMs;

    std::string _id;

 protected:
    CfsMs *_cfsp;

 public:

    CnodeMs() {
        _cfsp = NULL;
    }

    /* virtual ops realized */
    int32_t getAttr(Cattr *attrsp, Cenv *envp);

    int32_t lookup(std::string name, Cnode **nodepp, Cenv *envp) {
        return -1;
    }

    int32_t create(std::string name, Cnode **nodepp, Cenv *envp) {
        return -1;
    }
    int32_t mkdir(std::string name, Cnode **nodepp, Cenv *envp);

    int32_t open(uint32_t flags, Cfile **filepp) {
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

    int32_t startSession(std::string name,
                         std::string *sessionUrlp);

    int32_t sendData( std::string *sessionUrlp,
                      Cnode::fillProc *fillProcp,
                      void *fillContextp,
                      uint64_t fileLength,
                      uint64_t byteOffset,
                      uint32_t byteCount);

    /* send the whole file, whose final size is 'size'.  Use fillProc to obtain
     * data to send.  Creates a file with specified name in dir cp.
     */
    int32_t sendFile( Cnode *cp,
                      std::string name,
                      Cnode::fillProc *fillProcp,
                      void *contextp,
                      uint64_t size,
                      Cenv *envp);
    
    int32_t getPath(std::string *pathp, Cenv *envp);

    int32_t parseResults(Json::Node *jnodep, std::string *idp, uint64_t *sizep, time_t *modTimep);
};


/* one of these per file system instance */
class CfsMs : public Cfs {
 public:
    SApiLoginMS *_loginp;

    CfsMs(SApiLoginMS *loginp) {
        _loginp = loginp;
    }

    void setLogin(SApiLoginMS *loginp) {
        if (_loginp)
            delete _loginp;
        _loginp = loginp;
    }

    int32_t root(Cnode **rootpp, Cenv *envp);
    
    int32_t getCnode(std::string *idp, CnodeMs **cnodepp);
};

#endif /* _CFSMS_H_ENV__ */
