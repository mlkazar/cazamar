#ifndef _VNTEST_H_ENV__
#define _VNTEST_H_ENV__ 1

#include "vn.h"

#include <list>

class VfsTest;

// our directories have fileId == 0 and their children have non-zero
// file IDs.  Aside from dirId == 0 for root, all other dirs are in
// root directory.
class VnTestFh {
public:
    struct  Info {
        uint32_t _parentId;
        uint32_t _fileId;
    } _info;

    VnTestFh() {
        _info._parentId = 0;
        _info._fileId = 0;
    }

    VnTestFh(uint32_t parentId, uint32_t fileId) {
        _info._parentId = parentId;
        _info._fileId = fileId;
    }

    void setIds(uint32_t parentId, uint32_t fileId) {
        _info._parentId = parentId;
        _info._fileId = fileId;
    }

    char *data() {
        return (char *) &_info;
    }

    std::string getString() {
        return std::string(data(), length());
    }

    int32_t length() {
        return sizeof(_info);
    }

    int32_t initFromString(std::string afh) {
        if (afh.length() == 8) {
            memcpy(&_info, afh.data(), afh.length());
        } else {
            return -1;
        }

        return 0;
    }

};

class VnTest : public Vn {
public:
    Attr _attrs;
    VnTestFh _fh;
    std::string _data;

    int32_t create(std::string name, SetAttr *setAttrs, Vn **vpp);

    int32_t lookup(std::string name, Vn **vpp);

    int32_t getAttr(Attr *attrp);

    int32_t access(uint32_t inFlags, uint32_t *outFlagsp);

    int32_t readdir(uint64_t cookie, Vn::DirEnt *ep);

    int32_t read(char *datap, uint64_t offset, uint32_t count);

    VnTest(VfsTest *vfsp);

    std::string getFh();

    void release() {};
};

class VfsTest : public Vfs {
public:
    std::list<VnTest *> _allVns;

    VnTest *_rootp;
    FsAttr _fsAttrs;
    char _cookieVerifier[8];

    int32_t root(Vn **vnpp);

    int32_t getFsAttr(FsAttr *attrp);

    int32_t vget(std::string fh, Vn **vpp);

    VnTest * simpleFile(VnTestFh *fh, VfsTest *vfsp);

    VfsTest();

    std::string getCookieVerifier() {
        return std::string(_cookieVerifier, sizeof(_cookieVerifier));
    }
};

#endif // _VNTEST_H_ENV__
