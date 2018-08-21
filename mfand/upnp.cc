#include "upnp.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

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

/* static */
uint32_t UpnpDevice::_nextTag = 1;

int32_t
UpnpProbe::parseProbeResponse( char *respBufferp, std::string *locationStrp)
{
    /* Look for ST: line and location line */
    InStreamString inStream(respBufferp);
    Pair _pairParser;
    std::string key;
    std::string value;
    std::string location;
    int foundContentDir;
    int32_t code;

    foundContentDir = 0;
    while(1) {
        code = _pairParser.parseLine(&inStream, &key, &value);
        if (code == Pair::JSON_ERR_SUCCESS) {
            if ( !strcasecmp(key.c_str(), "st") &&
                 !strcasecmp(value.c_str(), "urn:schemas-upnp-org:service:ContentDirectory:1"))
                foundContentDir = 1;
            else if (!strcasecmp(key.c_str(), "location")) {
                location = value;
            }
        }
        else if (code == Pair::JSON_ERR_EOF) {
            break;
        }
    }

    if (!foundContentDir)
        return -3;

    /* otherwise, location is URL of a file with the contact info */
    *locationStrp = location;
    return 0;
}

int32_t
UpnpProbe::scanForService( Xgml::Node *rootNodep,
                           std::string *controlRelativePathp,
                           std::string *namep) {
    Xgml::Node *servicesRootNodep;
    Xgml::Node *nameNodep;
    Xgml::Node *serviceNodep;
    Xgml::Node *serviceTypeNodep;
    Xgml::Node *controlURLNodep;

    servicesRootNodep = rootNodep->searchForChild("serviceList");
    if (!servicesRootNodep) {
        printf("serviceList missing from service response\n");
        return -1;
    }

    nameNodep = rootNodep->searchForChild("friendlyName");
    if (nameNodep) {
        *namep = nameNodep->_children.head()->_name;
    }
    else
        *namep = "[No name]";

    controlURLNodep = NULL;
    for( serviceNodep = servicesRootNodep->_children.head();
         serviceNodep;
         serviceNodep=serviceNodep->_dqNextp) {

        serviceTypeNodep = serviceNodep->searchForChild("serviceType");
        if (strcasecmp( serviceTypeNodep->_children.head()->_name.c_str(),
                        "urn:schemas-upnp-org:service:ContentDirectory:1") != 0) {
            /* not the service we're looking for */
            continue;
        }

        controlURLNodep = serviceNodep->searchForChild("controlURL");
        break;
    }

    /* control node, if set, is what we return */
    if (controlURLNodep) {
        *controlRelativePathp = controlURLNodep->_children.head()->_name;
        return 0;
    }
    else
        return -1;
}
        
int32_t
UpnpProbe::init () {
    int s;
    std::string request;
    char responseBuffer[1024];
    struct sockaddr_in destAddr;
    struct sockaddr_in recvAddr;
    int code;
    int msgLen;
    socklen_t recvAddrLen;
    int inIp;
    struct timeval rcvTimeval;
    long now;
    int enable;
    std::string location;
    std::string host;
    std::string relativePath;
    std::string response;
    Xgml xgml;
    std::string neatString;
    std::string controlRelativePath;
    UpnpDevice *devicep;

    // const char *mediaServerUrn = "urn:schemas-upnp-org:device:MediaServer:1";
    // const char *contenDirectoryUrn = "urn:schemas-upnp-org:service:ContentDirectory:1";

    _xapip = new XApi();

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s<0) {
        perror("socket");
        return -1;
    }

    memset(&destAddr, 0, sizeof(destAddr));
    destAddr.sin_family = AF_INET;
    destAddr.sin_addr.s_addr = 0;
    destAddr.sin_port = htons(0);
#ifndef __linux__
    destAddr.sin_len = sizeof(destAddr);
#endif
    code = bind(s, (struct sockaddr *) &destAddr, sizeof(destAddr));
    if (code < 0) {
        perror("bind");
        return -1;
    }

    destAddr.sin_addr.s_addr = htonl(0xeffffffa);
    destAddr.sin_port = htons(1900);

    request = "M-SEARCH * HTTP/1.1\r\n";
    request += "HOST: 239.255.255.250:1900\r\n";
    request += "MAN: \"ssdp:discover\"\r\n";
    request += "MX: 10\r\n";
    request += "ST: ssdp:all\r\n";
    request += "\r\n";
    msgLen = (int) request.length();

    rcvTimeval.tv_sec = 4;
    rcvTimeval.tv_usec = 0;
    code = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&rcvTimeval, sizeof(rcvTimeval));
    if (code < 0) {
        perror("setsockopt");
        /* fall through */
    }

    /* enable multicast loopback, just for testing convenience */
    enable = 1;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, &enable, sizeof(enable));
    
    code = (int32_t) sendto( s,
                             request.c_str(),
                             msgLen,
                             0,
                             (struct sockaddr *) &destAddr,
                             sizeof(destAddr));
    if (code < 0) {
        perror("sendto");
        return -1;
    }

    now = time(0);
    while(1) {
        recvAddrLen = sizeof(recvAddr);
        code = (int32_t) recvfrom( s,
                                   responseBuffer,
                                   sizeof(responseBuffer)-1,
                                   0,
                                   (struct sockaddr *)&recvAddr,
                                   &recvAddrLen);
        if (code < 0) {
            printf("rcv timeout\n");
            break;
        }
        else {
            responseBuffer[code] = 0;
            inIp = ntohl(recvAddr.sin_addr.s_addr);
            code = parseProbeResponse(responseBuffer, &location);
            
            if (code == 0) {
                /* now parse things */
                Rst::splitUrl(location, &host, &relativePath, NULL);
                
                devicep = findDeviceByHost(&host);
                if (!devicep) {
                    devicep = new UpnpDevice();
                    devicep->_host = host;
                    devicep->_deviceRelativePath = relativePath;
                    _allDevices.append(devicep);
                }
            } /* 0 code from parse */
        }
    }

    return 0;
}

int32_t
UpnpProbe::contactDevice(UpnpDevice *devp) {
    std::string request;
    char responseBuffer[1024];
    int code;
    std::string location;
    std::string host;
    std::string relativePath;
    XApi::ClientReq *apiReqp;
    CThreadPipe *inPipep;
    std::string response;
    const char *responsep;
    BufSocket *socketp;
    XApi::ClientConn *connp;
    Xgml xgml;
    Xgml::Node *rootNodep;
    std::string neatString;
    std::string controlRelativePath;
    std::string friendlyName;

    /* no need to call if we've already done this one */
    if (devp->_controlPathKnown)
        return 0;

    /* note that the 80 below is just a default which can be
     * overridden by host string (and usually is; I'm not sure
     * there's a default value).
     */
    socketp = new BufSocket();
    socketp->init(const_cast<char *>(devp->_host.c_str()), 80);
    // socketp->setVerbose();
    connp = _xapip->addClientConn(socketp);

    apiReqp = new XApi::ClientReq();
    apiReqp->addHeader("Content-Type", "text/xml");
    apiReqp->startCall(connp, devp->_deviceRelativePath.c_str(), /* !isPost */ XApi::reqGet);
    
    inPipep = apiReqp->getIncomingPipe();
    
    code = apiReqp->waitForHeadersDone();
    
    response = "";
    while(1) {
        code = inPipep->read(responseBuffer, sizeof(responseBuffer)-1);
        if (code <= 0)
            break;
        responseBuffer[code] = 0;  /* null terminate the string */
        response += responseBuffer;
    }
    
    responsep = response.c_str();
    
    /* we're done with the call now */
    apiReqp->waitForAllDone();
    delete apiReqp;
    
    code = xgml.parse(const_cast<char **>(&responsep), &rootNodep);
    if (code != 0) {
        printf("parse code=%d\n", code);
        printf("whole response=%s\nremainder=%s\n", response.c_str(), responsep);
        return code;
    }
    
    code = scanForService(rootNodep, &controlRelativePath, &friendlyName);
    
    if (code == 0) {
        devp->_controlRelativePath = controlRelativePath;
        devp->_controlPathKnown = 1;
        devp->_name = friendlyName;
    }
    
    delete rootNodep;
    return 0;
}

int32_t
UpnpProbe::saveToFile(const char *fileNamep)
{
    /* build a tree with the server info in it: Each node has a name,
     * a host name, two relative paths, and an integer tag.
     */
    Json::Node *rootNodep;
    Json::Node *tnodep;
    Json::Node *devNodep;
    Json::Node *unodep;
    UpnpDevice *devp;
    FILE *filep;
    std::string resultStr;
    const char *tp;
    int32_t code;

    rootNodep = new Json::Node();
    rootNodep->initArray();

    for(devp = _allDevices.head(); devp; devp=devp->_dqNextp) {
        devNodep = new Json::Node();
        devNodep->initStruct();

        unodep = new Json::Node();
        unodep->initString(devp->_name.c_str(), 1);
        tnodep = new Json::Node();
        tnodep->initNamed("Name", unodep);
        devNodep->appendChild(tnodep);

        unodep = new Json::Node();
        unodep->initString(devp->_host.c_str(), 1);
        tnodep = new Json::Node();
        tnodep->initNamed("Host", unodep);
        devNodep->appendChild(tnodep);

        unodep = new Json::Node();
        unodep->initString(devp->_deviceRelativePath.c_str(), 1);
        tnodep = new Json::Node();
        tnodep->initNamed("DeviceRelativePath", unodep);
        devNodep->appendChild(tnodep);

        unodep = new Json::Node();
        unodep->initString(devp->_controlRelativePath.c_str(), 1);
        tnodep = new Json::Node();
        tnodep->initNamed("ControlRelativePath", unodep);
        devNodep->appendChild(tnodep);

        unodep = new Json::Node();
        unodep->initInt(devp->_tag);
        tnodep = new Json::Node();
        tnodep->initNamed("Tag", unodep);
        devNodep->appendChild(tnodep);

        /* and append the device description to the root */
        rootNodep->appendChild(devNodep);
    }

    filep = fopen(fileNamep, "w");
    rootNodep->printToCPP(&resultStr);
    tp = resultStr.c_str();
    code = (int32_t) fwrite(tp, strlen(tp), 1, filep);
    fclose(filep);

    /* cleanup */
    delete rootNodep;

    /* return success */
    return ((code == 1)? 0 : -1);
}

int32_t
UpnpProbe::restoreFromFile(const char *fileNamep)
{
    FILE *filep;
    char *ioBufferp;
    int32_t code;
    Json::Node *rootNodep;
    Json::Node *devNodep;
    Json::Node *tnodep;
    std::string *nameStrp;
    std::string *hostStrp;
    std::string *controlStrp;
    std::string *devStrp;
    std::string *tagStrp;
    uint32_t tag;
    Json _json;
    UpnpDevice *devp;

    filep = fopen(fileNamep, "r");
    if (!filep)
        return -1;

    ioBufferp = (char *) malloc(1024*1024);
    InStreamString inStream(ioBufferp);

    code = (int32_t) fread(ioBufferp, 1, 1024*1024-1, filep);
    if (code >= 0)
        ioBufferp[code] = 0;
    else {
        free(ioBufferp);
        return code;
    }

    code = _json.parseJsonValue(&inStream, &rootNodep);
    if (code == 0) {
        /* walk over tree, creating devices */
        for(devNodep = rootNodep->_children.head(); devNodep; devNodep = devNodep->_dqNextp) {
            nameStrp = NULL;
            hostStrp = NULL;
            devStrp = NULL;
            controlStrp = NULL;
            tagStrp = NULL;
            tag = 0;
            for(tnodep = devNodep->_children.head(); tnodep; tnodep=tnodep->_dqNextp) {
                if (tnodep->_name == "Name") {
                    nameStrp = &tnodep->_children.head()->_name;
                }
                else if (tnodep->_name == "Host") {
                    hostStrp = &tnodep->_children.head()->_name;
                }
                else if (tnodep->_name == "DeviceRelativePath") {
                    devStrp = &tnodep->_children.head()->_name;
                }
                else if (tnodep->_name == "ControlRelativePath") {
                    controlStrp = &tnodep->_children.head()->_name;
                }
                else if (tnodep->_name == "Tag") {
                    tagStrp = &tnodep->_children.head()->_name;
                    tag = atoi(tagStrp->c_str());
                }
            } /* loop over all properties for device */

            /* create device */
            devp = new UpnpDevice();
            devp->_name = *nameStrp;
            devp->_host = *hostStrp;
            devp->_deviceRelativePath = *devStrp;
            devp->_controlRelativePath = *controlStrp;
            devp->_tag = tag;
            if (tag >= UpnpDevice::_nextTag)
                UpnpDevice::_nextTag = tag+1;
            devp->_controlPathKnown = 1;
            _allDevices.append(devp);
        } /* loop over all devices */
    }

    free(ioBufferp);

    return code;
}

void
UpnpDBase::apply(RadixTree::Callback *procp, void *contextp, int onlyUnique)
{
    _urlTree.apply(procp, contextp, onlyUnique);
}

void
UpnpDBase::applyTree( RadixTree *treep,
                      RadixTree::Callback *procp,
                      void *contextp,
                      int onlyUnique)
{
    treep->apply(procp, contextp, onlyUnique);
}

/* called with each matching record -- caller must increment _version */
/* static */ int32_t
UpnpDBase::tagCallback(void *callbackContextp, void *recordContextp)
{
    UpnpDBase *dbasep = (UpnpDBase *) callbackContextp;
    UpnpDBase::Record *recordp = (UpnpDBase::Record *) recordContextp;

    /* _deleteTag of 0 means delete all entries */
    if (dbasep->_deleteTag != 0 && recordp->_tag != dbasep->_deleteTag)
        return 0;

    dbasep->_urlTree.remove(&recordp->_url, recordp);
    dbasep->_titleTree.remove(&recordp->_title, recordp);
    if (recordp->_inArtistTree)
        dbasep->_artistTree.remove(&recordp->_artist, recordp);
    if (recordp->_inAlbumTree)
        dbasep->_albumTree.remove(&recordp->_album, recordp);
    if (recordp->_inGenreTree)
        dbasep->_genreTree.remove(&recordp->_genre, recordp);
    delete recordp;

    return 0;
}

void
UpnpDBase::deleteByTag(uint32_t tag)
{
    _deleteTag = tag;
    _urlTree.apply(tagCallback, this);
    _version++;
}

void
UpnpDBase::deleteAll()
{
    _deleteTag = 0;
    _urlTree.apply(tagCallback, this);
    _version++;
}

/* static */ int32_t
UpnpDBase::saveToFileCallback(void *callbackContextp, void *recordContextp)
{
    UpnpDBase *dbasep = (UpnpDBase *) callbackContextp;
    UpnpDBase::Record *recp = (UpnpDBase::Record *) recordContextp;
    Json::Node *tnodep;
    Json::Node *recNodep;
    Json::Node *unodep;

    recNodep = new Json::Node();
    recNodep->initStruct();

    unodep = new Json::Node();
    unodep->initString(recp->_url.c_str(), 1);
    tnodep = new Json::Node();
    tnodep->initNamed("Url", unodep);
    recNodep->appendChild(tnodep);

    unodep = new Json::Node();
    unodep->initString(recp->_title.c_str(), 1);
    tnodep = new Json::Node();
    tnodep->initNamed("Title", unodep);
    recNodep->appendChild(tnodep);

    unodep = new Json::Node();
    unodep->initString(recp->_artUrl.c_str(), 1);
    tnodep = new Json::Node();
    tnodep->initNamed("ArtUrl", unodep);
    recNodep->appendChild(tnodep);

    if (recp->_inAlbumTree) {
        unodep = new Json::Node();
        unodep->initString(recp->_album.c_str(), 1);
        tnodep = new Json::Node();
        tnodep->initNamed("Album", unodep);
        recNodep->appendChild(tnodep);
    }

    if (recp->_inArtistTree) {
        unodep = new Json::Node();
        unodep->initString(recp->_artist.c_str(), 1);
        tnodep = new Json::Node();
        tnodep->initNamed("Artist", unodep);
        recNodep->appendChild(tnodep);
    }

    if (recp->_inGenreTree) {
        unodep = new Json::Node();
        unodep->initString(recp->_genre.c_str(), 1);
        tnodep = new Json::Node();
        tnodep->initNamed("Genre", unodep);
        recNodep->appendChild(tnodep);
    }

    unodep = new Json::Node();
    unodep->initInt(recp->_tag);
    tnodep = new Json::Node();
    tnodep->initNamed("Tag", unodep);
    recNodep->appendChild(tnodep);

    unodep = new Json::Node();
    unodep->initInt(recp->_userFlags);
    tnodep = new Json::Node();
    tnodep->initNamed("UserFlags", unodep);
    recNodep->appendChild(tnodep);

    unodep = new Json::Node();
    unodep->initInt(recp->_userContext);
    tnodep = new Json::Node();
    tnodep->initNamed("UserContext", unodep);
    recNodep->appendChild(tnodep);

    /* and append the device description to the root */
    dbasep->_rootNodep->appendChild(recNodep);

    return 0;
}

int32_t
UpnpDBase::saveToFile(const char *fileNamep)
{
    /* build a tree with the server info in it: Each node has a name,
     * a host name, two relative paths, and an integer tag.
     */
    FILE *filep;
    std::string resultStr;
    const char *tp;
    int32_t code;

    _rootNodep = new Json::Node();
    _rootNodep->initArray();

    apply(saveToFileCallback, this);

    filep = fopen(fileNamep, "w");
    _rootNodep->printToCPP(&resultStr);
    tp = resultStr.c_str();
    code = (int32_t) fwrite(tp, strlen(tp), 1, filep);
    fclose(filep);

    /* cleanup */
    delete _rootNodep;

    /* return success */
    return ((code == 1)? 0 : -1);
}

int32_t
UpnpDBase::restoreFromFile(const char *fileNamep)
{
    FILE *filep;
    char *ioBufferp;
    int32_t code;
    Json::Node *rootNodep;
    Json::Node *recNodep;
    Json::Node *tnodep;
    std::string *urlStrp;
    std::string *titleStrp;
    std::string *albumStrp;
    std::string *artistStrp;
    std::string *genreStrp;
    std::string *tagStrp;
    std::string *userContextStrp;
    std::string *userFlagsStrp;
    std::string *artUrlStrp;
    uint32_t tag;
    uint64_t userFlags;
    uint64_t userContext;
    Json _json;
    UpnpDBase::Record *recp;
    struct stat tstat;
    uint32_t nbytes;

    filep = fopen(fileNamep, "r");
    if (!filep)
        return -1;

    code = fstat(fileno(filep), &tstat);
    if (code < 0) {
        fclose(filep);
        return code;
    }

    nbytes = (uint32_t) (tstat.st_size+1);
    if (nbytes > 20000000) {
        return -3;
    }

    ioBufferp = (char *) malloc(nbytes);
    InStreamString inStream(ioBufferp);

    code = (int32_t) fread(ioBufferp, 1, nbytes-1, filep);
    if (code >= 0)
        ioBufferp[code] = 0;
    else {
        free(ioBufferp);
        return code;
    }

    code = _json.parseJsonValue(&inStream, &rootNodep);
    if (code == 0) {
        /* walk over tree, creating database records */
        for(recNodep = rootNodep->_children.head(); recNodep; recNodep = recNodep->_dqNextp) {
            urlStrp = NULL;
            titleStrp = NULL;
            albumStrp = NULL;
            artistStrp = NULL;
            genreStrp = NULL;
            tagStrp = NULL;
            artUrlStrp = NULL;
            tag = 0;
            userFlags = 0;
            userContext = 0;
            for(tnodep = recNodep->_children.head(); tnodep; tnodep=tnodep->_dqNextp) {
                if (tnodep->_name == "Url") {
                    urlStrp = &tnodep->_children.head()->_name;
                }
                else if (tnodep->_name == "ArtUrl") {
                    artUrlStrp = &tnodep->_children.head()->_name;
                }
                else if (tnodep->_name == "Title") {
                    titleStrp = &tnodep->_children.head()->_name;
                }
                else if (tnodep->_name == "Album") {
                    albumStrp = &tnodep->_children.head()->_name;
                }
                else if (tnodep->_name == "Artist") {
                    artistStrp = &tnodep->_children.head()->_name;
                }
                else if (tnodep->_name == "Genre") {
                    genreStrp = &tnodep->_children.head()->_name;
                }
                else if (tnodep->_name == "Tag") {
                    tagStrp = &tnodep->_children.head()->_name;
                    tag = atoi(tagStrp->c_str());
                }
                else if (tnodep->_name == "UserFlags") {
                    userFlagsStrp = &tnodep->_children.head()->_name;
                    userFlags = atol(userFlagsStrp->c_str());
                }
                else if (tnodep->_name == "UserContext") {
                    userContextStrp = &tnodep->_children.head()->_name;
                    userContext = atol(userContextStrp->c_str());
                }
            } /* loop over all properties for device */

            /* create record */
            recp = new UpnpDBase::Record();
            recp->_url = *urlStrp;
            recp->_title = *titleStrp;
            if (artistStrp)
                recp->_artist = *artistStrp;
            if (albumStrp)
                recp->_album = *albumStrp;
            if (genreStrp)
                recp->_genre = *genreStrp;
            if (artUrlStrp)
                recp->_artUrl = *artUrlStrp;
            recp->_tag = tag;
            recp->_userFlags = userFlags;
            recp->_userContext = userContext;

            /* now hash the record in; all records are visible through the title radix tree */
            _urlTree.insert(&recp->_url, &recp->_urlLinks, recp);
            _titleTree.insert(&recp->_title, &recp->_titleLinks, recp);

            if (albumStrp && albumStrp->length() > 0) {
                _albumTree.insert(&recp->_album, &recp->_albumLinks, recp);
                recp->_inAlbumTree = 1;
            }
            if (artistStrp && artistStrp->length() > 0) {
                _artistTree.insert(&recp->_artist, &recp->_artistLinks, recp);
                recp->_inArtistTree = 1;
            }
            if (genreStrp && genreStrp->length() > 0) {
                _genreTree.insert(&recp->_genre, &recp->_genreLinks, recp);
                recp->_inGenreTree = 1;
            }

            _version++;
        } /* loop over all records */
    }

    free(ioBufferp);

    return code;
}

int32_t
UpnpAv::browse(UpnpDBase *dbasep, const char *idp, int rlevel) {
    CThreadPipe *inPipep;
    CThreadPipe *outPipep;
    char tbuffer[128];
    std::string response;
    const char *responsep;
    Xgml xgml;
    Xgml::Node *rootNodep;
    std::string neatString;
    std::string service;
    char browseSendData[1024];
    char *browseSendDatap;
    int32_t browseSendCount;
    dqueue<Rst::Hdr> sendHeaders;
    dqueue<Rst::Hdr> recvHeaders;
    uint32_t obIx;
    Xgml::Node *tnodep;
    static const uint32_t numAtOnce = 999;
    uint32_t numReturned;
    uint32_t totalMatches;
    Xgml::Node *resultNodep;
    
    if (rlevel == 0) {
        _loadedPages = 0;
        _loadedItems = 0;
        _canceled = 0;
    }

    if (_canceled) {
        return -1;
    }

    for(obIx = 0;; obIx += numReturned) {
        {
            int i;
            for(i=0;i<rlevel;i++)
                printf("    ");
            printf("%s %d\n", idp, obIx);
        }
        
        service = _devp->_controlRelativePath;
        sprintf(browseSendData, _browseTemplate.c_str(), idp, obIx, numAtOnce);
        browseSendCount = (int32_t) strlen(browseSendData);
        browseSendDatap = browseSendData;
        int32_t code;
        
        _reqp = new XApi::ClientReq();
        _reqp->addHeader("Content-Type", "text/xml");
        _reqp->addHeader("SOAPACTION",
                         "\"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\"");
        _reqp->setSendContentLength(browseSendCount);
        _reqp->startCall(_connp, service.c_str(), /* isPost */ XApi::reqPost);
        
        inPipep = _reqp->getIncomingPipe();
        outPipep = _reqp->getOutgoingPipe();
        
        code = outPipep->write(browseSendDatap, browseSendCount);
        if (code != browseSendCount) {
            printf("Write code=%d should be %d\n", code, browseSendCount);
        }
        
        code = _reqp->waitForHeadersDone();
        
        response = "";
        while(1) {
            code = inPipep->read(tbuffer, sizeof(tbuffer)-1);
            if (code <= 0)
                break;
            tbuffer[code] = 0;  /* null terminate the string */
            response += tbuffer;
        }

        if ((code = _reqp->getError()) != 0) {
            delete _reqp;
            _reqp = NULL;
            return code;
        }

        responsep = response.c_str();
        
        _reqp->waitForAllDone();
        delete _reqp;
        _reqp = NULL;
        
        _loadedPages++;

        code = xgml.parse(const_cast<char **>(&responsep), &rootNodep);
        if (code != 0) {
            printf("parse code=%d\n", code);
            printf("whole response=%s\nremainder=%s\n", response.c_str(), responsep);
            return code;
        }
        
        if ( !_recurse) {
            rootNodep->printToCPP(&neatString);
            printf("%s\n", neatString.c_str());
        }
        
        tnodep = rootNodep->searchForChild("NumberReturned");
        if (tnodep) {
            numReturned = atoi(tnodep->_children.head()->_name.c_str());
        }
        else
            numReturned = 0;
        tnodep = rootNodep->searchForChild("TotalMatches");
        if (tnodep) {
            totalMatches = atoi(tnodep->_children.head()->_name.c_str());
        }
        else
            totalMatches = 0;
        
        /* Result is a XML document encoded as a string; the parse command decoded
         * the &amp; encoding, but we still have to parse the resulting string as
         * an XML document, and recurse with it.
         */
        tnodep = rootNodep->searchForChild("Result");
        if (tnodep) {
            char *resultp = const_cast<char *>(tnodep->_children.head()->_name.c_str());
            code = xgml.parse(&resultp, &resultNodep);
            /* now do a DFS of the tree, looking for <item> records, and creating database
             * records for them, including title, album, artist and url.  Use url for
             * the primary key.
             */
            if (code == 0) {
                if ( !_recurse) {
                    std::string neatString;
                    printf("Result details:\n");
                    resultNodep->printToCPP(&neatString);
                    printf("%s\n", neatString.c_str());
                }
                addItemsFromTree(dbasep, service.c_str(), idp, resultNodep, rlevel);
                delete resultNodep;
            }
        }
        
        delete rootNodep;

        /* we don't have more to do */
        if ( numReturned < numAtOnce || numReturned == totalMatches)
            break;
    }

    return 0;
}

int32_t
UpnpAv::addItemsFromTree( UpnpDBase *dbasep,
                          const char *servicep,
                          const char *obIdp,
                          Xgml::Node *nodep,
                          int rlevel) {
    Xgml::Node *tnodep;
    Xgml::Node *titleNodep;
    Xgml::Node *albumNodep;
    Xgml::Node *artistNodep;
    Xgml::Node *urlNodep;
    Xgml::Attr *attrp;
    Xgml::Node *dcTitleNodep;
    Xgml::Node *classNodep;
    Xgml::Node *genreNodep;
    Xgml::Node *artUrlNodep;
    int32_t tpLength;
    std::string title;
    std::string album;
    std::string artist;
    std::string url;
    std::string childId;
    std::string parentId;
    int32_t code;
    int32_t rcode;
    int32_t minLen;
    uint8_t isAudioTrack;
    const char *tp;
    const char *up;
    
    rcode = 0;
    if (_recurse && !nodep->_isLeaf && nodep->_name == "container") {
        for(attrp = nodep->_attrs.head(); attrp; attrp=attrp->_dqNextp) {
            if (attrp->_name == "id") {
                childId = attrp->_value;
            }
            else if (attrp->_name == "parentID") {
                parentId = attrp->_value;
            }
        }
        
        if (rlevel == 0 && _musicHack) {
            /* watch for dc:title, and if it has "photo" or "video" as part of
             * it, and skip it.
             */
            dcTitleNodep = nodep->searchForChild("dc:title");
            if (dcTitleNodep) {
                tp = dcTitleNodep->_children.head()->_name.c_str();
                tpLength = (int32_t) strlen(tp);
                tp += tpLength; /* points at terminating null */
                if (tpLength >= 5) {
                    if (strncasecmp(tp-5, "photo", 5) == 0)
                        return 0;
                    if (strncasecmp(tp-5, "video", 5) == 0)
                        return 0;
                }
                if (tpLength >= 6) {
                    if (strncasecmp(tp-5, "photos", 6) == 0)
                        return 0;
                    if (strncasecmp(tp-5, "videos", 6) == 0)
                        return 0;
                }
                if (tpLength >= 8) {
                    if (strncasecmp(tp-8, "pictures", 8) == 0)
                        return 0;
                }
            }
        }

        /* if the parent ID matches us, then recurse; note that some media
         * servers "hard link" some names to other objects, and you don't
         * want to recurse into those, or you can loop.  In those cases,
         * the parentID doesn't match the ID of the object containing the
         * container item.
         */
        if (strcmp(parentId.c_str(), obIdp) == 0 && childId.length() > 0) {
            code = browse(dbasep, childId.c_str(), rlevel+1);
            if (code != 0 && rcode == 0)
                rcode = code;
        }
        else {
            printf("skipping browse of %s -- hard link\n", childId.c_str());
        }
    }
    else if (!nodep->_isLeaf && nodep->_name == "item") {
        {
            std::string neatString;
            nodep->printToCPP(&neatString);
            printf("%s\n", neatString.c_str());
        }
        /* dig up the dc:title, upnp:album, upnp:artist and res items */
        titleNodep = NULL;
        albumNodep = NULL;
        artistNodep = NULL;
        urlNodep = NULL;
        genreNodep = NULL;
        artUrlNodep = NULL;
        isAudioTrack = 1;
        for(tnodep = nodep->_children.head(); tnodep; tnodep=tnodep->_dqNextp) {
            if (tnodep->_name == "dc:title") {
                titleNodep = tnodep->_children.head();
            }
            else if (tnodep->_name == "upnp:album") {
                albumNodep = tnodep->_children.head();
            }
            else if (tnodep->_name == "upnp:artist") {
                artistNodep = tnodep->_children.head();
            }
            else if (tnodep->_name == "res") {
                urlNodep = tnodep->_children.head();
            }
            else if (tnodep->_name == "upnp:class") {
                classNodep = tnodep->_children.head();
                tp = classNodep->_name.c_str();
                up = "object.item.audioItem";
                minLen = (int32_t) strlen(up);
                /* must start object.item.audioItem to match */
                if (strlen(tp) < minLen)
                    isAudioTrack = 0;
                else if (strncmp(tp, up, minLen) != 0)
                    isAudioTrack = 0;
            }
            else if (tnodep->_name == "upnp:genre") {
                genreNodep = tnodep->_children.head();
            }
            else if (tnodep->_name == "upnp:albumArtURI") {
                artUrlNodep = tnodep->_children.head();
            }
        }
        
        if (!urlNodep || !titleNodep) {
            printf("No URL or no title, skipping\n");
            return -1;
        }
        else {
            tp = urlNodep->_name.c_str();
            tpLength = (int32_t) strlen(tp);
            tp += tpLength;     /* points past end */
            if ( isAudioTrack &&
                 tpLength >= 5 &&
                 (strncasecmp(tp-4, ".mp3", 4) == 0 ||
                  strncasecmp(tp-4, ".aac", 4) == 0 ||
                  strncasecmp(tp-4, ".m4a", 4) == 0 ||
                  strncasecmp(tp-4, ".wav", 4) == 0 ||
                  strncasecmp(tp-4, ".aif", 4) == 0 ||
                  strncasecmp(tp-4, ".mp4", 4) == 0 ||
                  strncasecmp(tp-5, ".aiff", 5) == 0)) {
                code = dbasep->addRecord( &urlNodep->_name,
                                          &titleNodep->_name,
                                          (albumNodep? &albumNodep->_name : NULL),
                                          (artistNodep? &artistNodep->_name : NULL),
                                          (genreNodep? &genreNodep->_name : NULL),
                                          (artUrlNodep? &artUrlNodep->_name: NULL),
                                          _devp->_tag);
                if (code == 0)
                    _loadedItems++;
            }
            return 0;
        }
    }
    
    if (!nodep->_isLeaf) {
        for(tnodep = nodep->_children.head(); tnodep; tnodep=tnodep->_dqNextp) {
            addItemsFromTree(dbasep, servicep, obIdp, tnodep, rlevel);
        }
    }
    
    return rcode;
}
