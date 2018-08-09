#include "cfs.h"

/* this file provides generic operations on cnodes */
/* static */ uint64_t
Cfs::fnvHash64(std::string *strp)
{
    uint64_t hash;
    uint64_t dataByte;
    uint8_t *datap = (uint8_t *) strp->c_str();
    uint32_t nchars = strp->length();
    uint32_t i;

    hash = 0xcbf29ce484222325ULL;
    for(i=0;i<nchars;i++) {
        dataByte = *datap++;
        hash = hash ^ dataByte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

int32_t
Cfs::splitPath(std::string path, std::string *dirPathp, std::string *namep)
{
    size_t slashPos;

    slashPos = path.rfind('/');
    if (slashPos == std::string::npos) {
        *dirPathp = "";
        *namep = path;
    }
    else {
        *dirPathp = path.substr(0, slashPos+1);
        *namep = path.substr(slashPos+1);
    }
    return 0;
}

/* static */ int32_t
Cfs::nameiCallback(void *cxp,
                   Cnode *nodep,
                   std::string name,
                   Cnode **outNodepp,
                   CEnv *envp)
{
    int32_t code;
    code = nodep->lookup(name, outNodepp, envp);
    return code;
}

/* static */ int32_t
Cfs::mkpathCallback(void *cxp,
                    Cnode *nodep,
                    std::string name,
                    Cnode **outNodepp,
                    CEnv *envp)
{
    int32_t code;
    Cnode *outNodep;

    code = nodep->lookup(name, outNodepp, envp);
    if (code == 0) {
        outNodep = *outNodepp;
        if (outNodep->_attrs._fileType == CAttr::DIR)
            return 0;
        else
            return -4;  /* wrong type in path */
    }
    else {
        /* try to create the dir at the next component */
        code = nodep->mkdir(name, outNodepp, envp);
        return code;
    }
}



int32_t
Cfs::namei( std::string path,
            Cnode **targetCnodepp,
            CEnv *envp)
{
    int32_t code;
    code = nameInt(path, &Cfs::nameiCallback, NULL, targetCnodepp, envp);
    return code;
}


int32_t
Cfs::mkpath( std::string path,
             Cnode **targetCnodepp,
             CEnv *envp)
{
    int32_t code;
    code = nameInt(path, &Cfs::mkpathCallback, NULL, targetCnodepp, envp);
    return code;
}


int32_t
Cfs::nameInt( std::string path,
              nameiProc *procp,
              void *nameiContextp,
              Cnode **targetCnodepp,
              CEnv *envp)
{
    Cnode *currentCnodep;
    int32_t code;
    size_t slashPos;
    std::string name;
    Cnode *nextCnodep;

    *targetCnodepp = NULL;       /* in case of error */
    code = root(&currentCnodep, envp);
    if (code) return code;

    code = 0;
    while(path.length() > 0) {
        /* eat multiple slashes */
        if (path.c_str()[0] == '/') {
            path = path.substr(1);
            continue;
        }

        /* pull out a component */
        slashPos = path.find('/');
        if (slashPos == std::string::npos) {
            name = path;
            path = "";
        }
        else {
            name = path.substr(0,slashPos);
            path = path.substr(slashPos+1);
        }

        /* now do a lookup on the name */
        code = procp(nameiContextp, currentCnodep, name, &nextCnodep, envp);
        currentCnodep->release();
        currentCnodep = NULL;
        if (code != 0) {
            break;
        }
        currentCnodep = nextCnodep;
    }

    if (code == 0) {
        osp_assert(currentCnodep != NULL);
        *targetCnodepp = currentCnodep;
    }
    else {
        if (currentCnodep) {
            currentCnodep->release();
            currentCnodep = NULL;
        }
    }

    return code;
}

int32_t
Cfs::stat(std::string path, CAttr *attrsp, CEnv *envp)
{
    int32_t code;
    Cnode *nodep;

    code = namei(path, &nodep, envp);
    if (code)
        return code;
    code = nodep->getAttr(attrsp, envp);
    nodep->release();
    return code;
}

int32_t
Cfs::sendFile( std::string path,
               CDataSource *sourcep,
               CEnv *envp)
{
    int32_t code;
    Cnode *dirNodep;
    std::string dirPath;
    std::string name;

    code = splitPath(path, &dirPath, &name);
    if (code)
        return code;
    code = namei(dirPath, &dirNodep, envp);
    if (code)
        return code;
    code = dirNodep->sendFile( name, sourcep, envp);
    dirNodep->release();
    return code;
}

int32_t
Cfs::mkdir(std::string path, Cnode **newDirpp, CEnv *envp)
{
    int32_t code;
    Cnode *dirNodep;
    std::string dirPath;
    std::string name;

    code = splitPath(path, &dirPath, &name);
    if (code)
        return code;
    code = namei(dirPath, &dirNodep, envp);
    if (code)
        return code;
    code = dirNodep->mkdir( name, newDirpp, envp);
    dirNodep->release();
    return code;
}
