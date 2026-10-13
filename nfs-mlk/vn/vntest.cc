#include "vntest.h"

// default just for testing.  TODO: move to TestVn soon.
int32_t
VfsTest::root(Vn **rootpp) {
    if (_rootp == nullptr) {
        _rootp = new VnTest(this);
        _rootp->_attrs.ftype = Vn::VN_DIR;
        _rootp->_attrs.device.major = 100;
        _rootp->_attrs.device.minor = 1;
        _rootp->_attrs.mode = 0775;
        _rootp->_attrs.fileid = 12345;
        _rootp->_attrs.nlinks = 3;
        _rootp->_attrs.uid = 0;
        _rootp->_attrs.gid = 0;
        _rootp->_attrs.rdev.major = 0;
        _rootp->_attrs.rdev.minor = 0;
        _rootp->_attrs.size = 0x4000;
        _rootp->_attrs.blocks = 4;
        _rootp->_attrs.blksize = 0x1000;
        _rootp->_attrs.gen = 1;
        _rootp->_attrs.ctime.secs = 30000000;
        _rootp->_attrs.ctime.nsecs = 1000;
        _rootp->_attrs.atime.secs = 31000000;
        _rootp->_attrs.atime.nsecs = 1000;
        _rootp->_attrs.mtime.secs = 32000000;
        _rootp->_attrs.mtime.nsecs = 1000;
        _rootp->_attrs.namedattrs = false;
        _rootp->_attrs.fsid.major = 2000;
        _rootp->_attrs.fsid.minor = 1;

        _rootp->_fh = std::string("ROOT");
    }

    *rootpp =  _rootp;
    _rootp->_vfsp = this;
    return 0;
}

int32_t
VfsTest::vget(std::string fh, Vn **vpp) {
    *vpp = _rootp;
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

VfsTest::VfsTest() {
    _fsAttrs.linkSupport = true;
    _fsAttrs.symlinkSupport = true;
    _fsAttrs.uniqueHandles = true;
    _fsAttrs.caseInsensitive = false;
    _fsAttrs.leaseSecs = 30;
    _fsAttrs.maxFileSize = 0x7FFFFFFFFFFFFFF;
    _fsAttrs.maxLink = 0x7FFFFFFF;
    _fsAttrs.maxName = 1024;
    _fsAttrs.maxReadBytes = 1024*1024;
    _fsAttrs.maxWriteBytes = 1024*1024;
}

VnTest::VnTest(VfsTest *vfsp) {
    _vfsp = vfsp;
}
