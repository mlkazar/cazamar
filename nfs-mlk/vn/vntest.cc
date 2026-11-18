#include "vntest.h"

// default just for testing.  TODO: move to TestVn soon.
int32_t
VfsTest::root(Vn **rootpp) {
    if (_rootp == nullptr) {
        VnTestFh rootFh(0, 0);
        _rootp = simpleFile(&rootFh, this);
    }

    *rootpp =  _rootp;
    return 0;
}

VnTest *
VfsTest::simpleFile(VnTestFh *fhp, VfsTest *vfsp) {
    VnTestFh::Info fhData;
    memcpy(&fhData, fhp->data(), fhp->length());
    bool isDir = (fhData._fileId == 0);

    VnTest *vp = new VnTest(this);
    vp->_attrs.ftype = (isDir? Vn::VN_DIR : Vn::VN_REG);
    vp->_attrs.device.major = 100;
    vp->_attrs.device.minor = 1;
    vp->_attrs.mode = (isDir? 0775 : 0664);
    vp->_attrs.fileid = 12346;
    vp->_attrs.nlinks = (isDir? 3 : 1);
    vp->_attrs.uid = 0;
    vp->_attrs.gid = 0;
    vp->_attrs.rdev.major = 0;
    vp->_attrs.rdev.minor = 0;
    vp->_attrs.size = 0x4000;
    vp->_attrs.blocks = 4;
    vp->_attrs.blksize = 0x1000;
    vp->_attrs.gen = 1;
    vp->_attrs.ctime.secs = 30000000;
    vp->_attrs.ctime.nsecs = 1000;
    vp->_attrs.atime.secs = 31000000;
    vp->_attrs.atime.nsecs = 1000;
    vp->_attrs.mtime.secs = 32000000;
    vp->_attrs.mtime.nsecs = 1000;
    vp->_attrs.namedattrs = false;
    vp->_attrs.fsid.major = 2000;
    vp->_attrs.fsid.minor = 1;

    vp->_fh = *fhp;

    if (!isDir) {
        char tbuffer[256];
        snprintf(tbuffer, sizeof(tbuffer),
                 "This is file %d.%d",
                 fhp->_info._parentId, fhp->_info._fileId);
        vp->_data = std::string(tbuffer);
    }

    _allVns.push_back(vp);

    return vp;
}

int32_t
VfsTest::vget(std::string afh, Vn **vpp) {
    std::list<VnTest *>::iterator it;
    VnTestFh fh;
    int32_t code;

    code = fh.initFromString(afh);
    if (code) {
        printf("vget returns failure %ld for %s\n", afh.length(), afh.c_str());
        return code;
    }

    bool found = false;
    VnTest *vp;

    for(it = _allVns.begin(); it != _allVns.end(); it++) {
        vp = *it;
        printf("vget comparing with vp=%p fh=%d.%d\n",
               vp, vp->_fh._info._parentId, vp->_fh._info._fileId);
        if (memcmp(&vp->_fh._info, afh.data(), 8) == 0) {
            found = true;
            printf("vget found vp=%p\n", vp);
            break;
        }
    }

    if (!found) {
        *vpp = simpleFile(&fh, this);
        printf("vget not found, vp=%p\n", *vpp);
    } else {
        *vpp = vp;
        printf("vget found, vp=%p\n", *vpp);
    }

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

int32_t
VnTest::access(uint32_t inFlags, uint32_t *outFlagsp) {
    uint32_t resultFlags = 0;
    if ((inFlags & 4) && (_attrs.mode & 0400)) {
        resultFlags |= 4;
    }
    if ((inFlags & 2) && (_attrs.mode & 0200)) {
        resultFlags |= 2;
    }
    if ((inFlags & 1) && (_attrs.mode & 0100)) {
        resultFlags |= 1;
    }

    *outFlagsp = resultFlags;

    return 0;
}

int32_t
VnTest::lookup(std::string name, Vn **vpp) {
    int32_t code;

    printf("vntest: lookup of '%s' in %d.%d\n",
           name.c_str(), _fh._info._parentId, _fh._info._fileId);
    if (name == std::string("FooFile")) {
        VnTestFh fh(0, 1);
        code = _vfsp->vget(fh.getString(), vpp);
        return code;
    } else if (name == std::string(".") || name == std::string("..")) {
        *vpp = this;
        return 0;
    } else {
        return -1;
    }
}

std::string
VnTest::getFh() {
    printf("vntest::getfh returning %d.%d\n", _fh._info._parentId, _fh._info._fileId);
    return _fh.getString();
}

int32_t
VnTest::readdir(uint64_t cookie, Vn::DirEnt *entp) {
    if (cookie == 0) {
        entp->_name = std::string("FooFile");
        entp->_fileId = 17;
        entp->_cookie = 1;
    } else if (cookie == 1) {
        entp->_name = std::string("");
    }
    return 0;
}

int32_t
VnTest::create(std::string name, SetAttr *setAttrs, Vn **vpp) {
    printf("vntest create '%s'\n", name.c_str());
    return 2;
}

int32_t
VnTest::read(char *datap, uint64_t offset, uint32_t count) {
    uint64_t fileLen = _data.length();
    if (offset >= fileLen) {
        return 0;
    }

    // figure out how many of the requested bytes are actually present
    // in the file.
    uint64_t bytesLeft = fileLen - offset;
    if (bytesLeft > count) {
        bytesLeft = count;
    }

    // copy out the data and return the count of the # of bytes
    // transferred.
    memcpy(datap, _data.data() + offset, bytesLeft);
    return bytesLeft;
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

    memset(_cookieVerifier, 0xaa, sizeof(_cookieVerifier));
}

VnTest::VnTest(VfsTest *vfsp) {
    _vfsp = vfsp;
}
