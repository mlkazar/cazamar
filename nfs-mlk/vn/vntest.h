#ifndef _VNTEST_H_ENV__
#define _VNTEST_H_ENV__ 1

#include "vn.h"

class VfsTest;

class VnTest : public Vn {
public:
    VfsTest *_vfsp;
    Attr _attrs;

    int32_t getAttr(Attr *attrp);

    VnTest(VfsTest *vfsp) {
        _vfsp = vfsp;
    };
};

class VfsTest : public Vfs {
public:
    VnTest *_rootp;
    FsAttr _fsAttrs;

    int32_t root(Vn **vnpp);

    int32_t getFsAttr(FsAttr *attrp);

    VfsTest() {
        
    }
};

#endif // _VNTEST_H_ENV__
