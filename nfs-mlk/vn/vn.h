#ifndef __VN_H_ENV__
#define __VN_H_ENV__ 1

#include <string>

#include <inttypes.h>

class Vfs;

// per file attributes
class Vn {
public:
    enum Ftype {
        VN_REG = 1,
        VN_DIR = 2,
        VN_SYMLINK = 3,
        VN_CHARDEV = 4,
        VN_BLOCKDEV = 5,
        VN_SOCK = 6,
        VN_FIFO = 7,
        VN_NAMED_ATTR_DIR = 8,
        VN_NAMED_ATTR = 9
    };

    struct SpecData {
        uint32_t major;
        uint32_t minor;
    };

    struct TimeVal {
        int64_t secs;
        uint32_t nsecs;
    };

    typedef uint32_t uid_t;

    class Attr {
    public:
        Ftype ftype;            // file type; used to be in modes
        SpecData device;        // containing device
        uint32_t mode;          // mode bits
        uint32_t nlinks;
        uint64_t fileid;        // [XSI] File serial number
        uid_t uid;              // [XSI] User ID of the file
        uid_t gid;              // [XSI] Group ID of the file
        SpecData rdev;          // [XSI] Device ID
        uint64_t size;          // file size in bytes
        uint64_t blocks;        // [XSI] blocks allocated for file.
        uint32_t blksize;       // [XSI] optimal blocksize for I/O
        uint32_t gen;           // file generation number
        TimeVal ctime;          // also usable as unique change time */
        TimeVal atime;
        TimeVal mtime;
        bool namedattrs;        // true if file has named attributes
        SpecData fsid;          // file system ID
        // don't set the attribute 14: archive bit
        // don't set changepolicy
        // don't set dacl
        // don't set dir*_notify_delay
    };

    std::string _fh;            // no null termination
    Vfs *_vfsp;

    // whole file system attributes that aren't obvious
    virtual int32_t getAttr(Attr *attrp) = 0;

    virtual void release() = 0;
};

class Vfs {
public:
    class FsAttr {
    public:
        // we only return FH4_PERSISTENT for expire type
        bool linkSupport;
        bool symlinkSupport;
        bool uniqueHandles;     // we always provide unique file handles, so property 9 is true
        bool caseInsensitive;
        uint32_t leaseSecs;
        // bitmap4 suppattr_exclcreat exclusive create is always supported
        // acl support is false in first release
        // cansettime is true
        // caseinsensitive is true (make option?)
        // casepreserving is true
        // chownrestricted is true
        // set fs_charset_cap to 2 (allows utf8)
        // set homogenous to true (files with same fsid share properties)
        uint64_t maxFileSize;
        uint32_t maxLink;
        uint32_t maxName;       // (max entry? name)
        uint64_t maxReadBytes;       //(max bytes in a read)
        uint64_t maxWriteBytes;      //(max bytes in a read)
    };

    virtual int32_t vget(std::string fh, Vn **vpp) = 0;

    virtual int32_t root(Vn **rootpp) = 0;

    virtual int32_t getFsAttr(FsAttr *fsAttrp) = 0;
};

#endif /* __VN_H_ENV__ */
