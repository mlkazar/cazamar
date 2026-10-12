#include "vntest.h"

// default just for testing.  TODO: move to TestVn soon.
int32_t
VfsTest::root(Vn **rootpp) {
    if (_rootp == nullptr) {
        _rootp = new VnTest(this);
    }

    *rootpp =  _rootp;
    return 0;
}

int32_t
VfsTest::getFsAttr(FsAttr *fsAttrp) {
    memcpy(fsAttrp, &_fsAttrs, sizeof(_fsAttrs));
    return 0;
}

int32_t
VnTest::getAttr(Attr *attrp) {
    memcpy(attrp, &_attrs, sizeof(_attrs));
    return 0;
}
