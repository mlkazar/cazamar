#ifndef __UPNP_H_ENV_
#define __UPNP_H_ENV_ 1

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/stat.h>

#if 0
#include <netinet/udp.h>
#endif

#include <errno.h>
#include <string>
#include <stdio.h>

#include "dqueue.h"
#include "bufsocket.h"
#include "rst.h"
#include "json.h"
#include "xapi.h"
#include "oasha1.h"
#include "xgml.h"
#include "radixtree.h"

class UpnpDevice {

public:
    std::string _name;
    std::string _host;
    std::string _deviceRelativePath;    /* used to get device details, controlRelativePath */
    std::string _controlRelativePath;   /* used for browsing */
    uint32_t _tag;
    uint8_t _controlPathKnown;     /* we read the device file */

    UpnpDevice *_dqNextp;
    UpnpDevice *_dqPrevp;

    static uint32_t _nextTag;

    UpnpDevice() {
        _dqNextp = NULL;
        _dqPrevp = NULL;
        _controlPathKnown = 0;
        _tag = _nextTag++;
    }
};

class UpnpProbe {
public:
    XApi *_xapip;
    dqueue<UpnpDevice> _allDevices;

    UpnpDevice *findDeviceByHost(std::string *hostp) {
        UpnpDevice *devp;

        for(devp = _allDevices.head(); devp; devp=devp->_dqNextp) {
            if (strcasecmp(devp->_host.c_str(), hostp->c_str()) == 0)
                return devp;
        }
        return NULL;
    }

    int32_t parseProbeResponse(char *respBufferp, std::string *locationStrp);

    int32_t scanForService(Xgml::Node *rootNodep,
                           std::string *controlRelativePathp,
                           std::string *namep);

    int32_t init ();

    UpnpDevice *getFirstDevice() {
        return _allDevices.head();
    }

    int32_t count() {
        uint32_t i;
        UpnpDevice *devp;
        for(i=0, devp=_allDevices.head(); devp; i++, devp=devp->_dqNextp)
            ;
        return i;
    }

    void deleteNthDevice(int32_t ix) {
        UpnpDevice *devp;

        devp = getNthDevice(ix);
        if (devp) {
            _allDevices.remove(devp);
            delete devp;
        }
    }

    UpnpDevice *getNthDevice(int32_t ix) {
        uint32_t i;
        UpnpDevice *devp;
        for(i=0, devp=_allDevices.head(); devp; i++, devp=devp->_dqNextp)
            if (i == (unsigned) ix)
                return devp;
        return NULL;
        
    }

    UpnpDevice *getDeviceByTag(uint32_t tag) {
        UpnpDevice *devp;

        for(devp = _allDevices.head(); devp; devp=devp->_dqNextp) {
            if (devp->_tag == tag)
                return devp;
        }
        return NULL;
    }

    void clearAll() {
        UpnpDevice *devp;
        UpnpDevice *ndevp;
        for(devp=_allDevices.head(); devp; devp=ndevp) {
            ndevp = devp->_dqNextp;
            delete devp;
        }
        UpnpDevice::_nextTag = 1;
        _allDevices.init();
    }

    int32_t contactAllDevices() {
        UpnpDevice *devp;
        int32_t code;
        int32_t rcode;

        rcode = 0;
        for(devp = _allDevices.head(); devp; devp=devp->_dqNextp) {
            code = contactDevice(devp);
            if (code != 0 && rcode == 0)
                rcode = code;
        }

        return rcode;
    }

    /* get the control port */
    int32_t contactDevice(UpnpDevice *devp);

    int32_t saveToFile(const char *fileNamep);

    int32_t restoreFromFile(const char *fileNamep);
};

/* media database */
class UpnpDBase {
public:
    class Record {
    public:
        std::string _url;
        std::string _title;
        std::string _artist;
        std::string _album;
        std::string _genre;
        std::string _artUrl;

        uint32_t _tag;
        uint8_t _inArtistTree;
        uint8_t _inAlbumTree;
        uint8_t _inGenreTree;
        uint64_t _userFlags;
        uint64_t _userContext;

        Record *_urlNextp;
        Record *_titleNextp;
        Record *_artistNextp;
        Record *_albumNextp;

        RadixTree::Item _urlLinks;
        RadixTree::Item _titleLinks;
        RadixTree::Item _artistLinks;
        RadixTree::Item _albumLinks;
        RadixTree::Item _genreLinks;

        Record() {
            _urlNextp = NULL;
            _titleNextp = NULL;
            _artistNextp = NULL;
            _albumNextp = NULL;
            _tag = 0;
            _inArtistTree = 0;
            _inAlbumTree = 0;
            _inGenreTree = 0;
            _userFlags = 0;
            _userContext = 0;
        }
    };

    /* class variables */
    RadixTree _urlTree;
    RadixTree _titleTree;
    RadixTree _artistTree;
    RadixTree _albumTree;
    RadixTree _genreTree;
    long _version;
    uint32_t _deleteTag;

    /* stuff from iteration for saveToFile */
    Json::Node *_rootNodep;

    UpnpDBase() {
        _version = 1;
    }

    Record *findRecord(std::string *urlStrp) {
        void *objp;
        int32_t code;

        code = _urlTree.lookup(urlStrp, &objp);
        if (code != 0)
            return NULL;
        else
            return static_cast<Record *>(objp);
    }

    int32_t addRecord( std::string *urlStrp,
                       std::string *titleStrp,
                       std::string *albumStrp,
                       std::string *artistStrp,
                       std::string *genreStrp,
                       std::string *artUrlStrp,
                       uint32_t tag) {
        Record *recordp;

        if (findRecord(urlStrp))
            return -2;  /* exists */

        recordp = new UpnpDBase::Record();
        recordp->_url = *urlStrp;
        recordp->_tag = tag;

        _urlTree.insert(&recordp->_url, &recordp->_urlLinks, recordp);

        /* all records are visible through the title radix tree */
        recordp->_title = *titleStrp;
        _titleTree.insert(&recordp->_title, &recordp->_titleLinks, recordp);

        if (albumStrp) {
            recordp->_album = *albumStrp;
            _albumTree.insert(&recordp->_album, &recordp->_albumLinks, recordp);
            recordp->_inAlbumTree = 1;
        }
        if (artistStrp) {
            recordp->_artist = *artistStrp;
            _artistTree.insert(&recordp->_artist, &recordp->_artistLinks, recordp);
            recordp->_inArtistTree = 1;
        }
        if (genreStrp) {
            recordp->_genre = *genreStrp;
            _genreTree.insert(&recordp->_genre, &recordp->_genreLinks, recordp);
            recordp->_inGenreTree = 1;
        }
        if (artUrlStrp) {
            recordp->_artUrl = *artUrlStrp;
        }

        _version++;

        return 0;
    }

    static int32_t tagCallback(void *callbackContextp, void *recordContextp);

    RadixTree *titleTree() {
        return &_titleTree;
    }

    RadixTree *artistTree() {
        return &_artistTree;
    }

    RadixTree *albumTree() {
        return &_albumTree;
    }

    RadixTree *urlTree() {
        return &_urlTree;
    }

    RadixTree *genreTree() {
        return &_genreTree;
    }

    void deleteByTag(uint32_t tag);

    void deleteAll();

    void apply(RadixTree::Callback *procp, void *contextp, int onlyUnique = 0);

    void applyTree( RadixTree *treep,
                    RadixTree::Callback *procp,
                    void *contextp,
                    int onlyUnique = 0);

    static int32_t saveToFileCallback(void *callbackContextp, void *recordContextp);

    int32_t saveToFile(const char *fileNamep);

    int32_t restoreFromFile(const char *fileNamep);

    long getVersion() {
        return _version;
    }
};

class UpnpAv {
    class RecurseCheck {
    public:
        std::string _id;
        RecurseCheck *_dqNextp;
        RecurseCheck *_dqPrevp;
    };

    std::string _host;
    XApi::ClientConn *_connp;
    XApi::ClientReq *_reqp;
    XApi *_xapip;
    BufSocket *_socketp;
    std::string _browseTemplate;
    uint8_t _recurse;
    uint8_t _musicHack;
    uint8_t _canceled;
    UpnpDevice *_devp;
    uint32_t _loadedPages;
    uint32_t _loadedItems;

public:
    UpnpAv() {
        _recurse = 1;
        _canceled = 0;
        return;
    }

    void cancel() {
        _canceled = 1;
    }

    uint32_t loadedPages() {
        return _loadedPages;
    }

    uint32_t loadedItems() {
        return _loadedItems;
    }

    void setRecurse(uint8_t x) {
        _recurse = x;
    }

    /* filter was id,dc:title,upnp:artist,upnp:album,res instead of * */
    int32_t init(UpnpDevice *devp) {
        _musicHack = 1;
        _loadedPages = 0;
        _loadedItems = 0;

        _host = std::string(devp->_host);
        _devp = devp;
        _browseTemplate = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n\
<s:Envelope s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\" xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">\r\n\
<s:Body>\r\n\
<u:Browse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">\r\n\
<ObjectID>%s</ObjectID>\r\n\
<BrowseFlag>BrowseDirectChildren</BrowseFlag>\r\n\
<Filter>*</Filter>\r\n\
<StartingIndex>%d</StartingIndex>\r\n\
<RequestedCount>%d</RequestedCount>\r\n\
<SortCriteria></SortCriteria>\r\n\
</u:Browse>\r\n\
</s:Body>\r\n\
</s:Envelope>\r\n";

        _xapip = new XApi();
        /* note that the 1900 below is just a default which can be overridden by host string */
        _socketp = new BufSocket();
        _socketp->init(const_cast<char *>(_host.c_str()), 1900);
        // _socketp->setVerbose();
        _connp = _xapip->addClientConn(_socketp);
        return 0;
    }

    /* do a DFS adding items from tree */
    int32_t addItemsFromTree( UpnpDBase *dbasep,
                              const char *servicep,
                              const char *obIdp,
                              Xgml::Node *nodep,
                              int rlevel);

    void computeHash(const char *datap, char *resultp, uint32_t resultLen) {
        /* compute a sha1 checksum */
        uint8_t *hashp;
        uint32_t i;
        sha1nfo sha1State;
        uint8_t tval;

        sha1_init(&sha1State);
        sha1_write(&sha1State, datap, strlen(datap));
        hashp = sha1_result(&sha1State);

        for (i=0; i<20; i++) {
            tval = hashp[i] >> 4;
            if (tval < 10)
                tval += '0';
            else
                tval += ('A' - 10);
            *resultp++ = tval;

            tval = hashp[i] & 0xF;
            if (tval < 10)
                tval += '0';
            else
                tval += ('A' - 10);
            *resultp++ = tval;
        }
        *resultp++ = 0;
    }

    int32_t browse(UpnpDBase *dbasep, const char *idp, int rlevel = 0);
};
#endif /* __UPNP_H_ENV_ */
