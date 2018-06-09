#include <unistd.h>
#include <string>

#include "rpc.h"
#include "mfand.h"
#include "restcall.h"
#include "xgml.h"

/* make sure we don't have more in our list than we're allowed to collect */
void
MfServer::checkCount()
{
    MfEntry *ep;

    while(_allPlaying.count() > _maxQueued) {
        /* remove from tail */
        ep = _allPlaying.tail();
        _allPlaying.remove(ep);
        delete ep;
    }
}

/* always make the RPC */
int32_t
MfServer::translateLocation(uint32_t ipAddr, std::string *resultp)
{
    char tstring[100];
    RestCall *restp;
    char *tdatap;
    Xgml::Node *rootNodep;
    Xgml::Node *nodep;
    int32_t code;
    std::string result;
    std::string request;
    std::string countryCode;

    restp = new RestCall();
    sprintf(tstring, "http://ip-api.com/xml/%d.%d.%d.%d",
            (ipAddr>>24)&0xFF,
            (ipAddr>>16)&0xFF,
            (ipAddr>>8)&0xFF,
            ipAddr&0xFF);
    restp->setUrl(tstring);
    code = restp->makeCall(&request, &result);
    delete restp;

    if (code != 0) {
        *resultp = std::string("[Location lookup failed]");
        return 0;
    }

    tdatap = (char *) result.c_str();
    tdatap = strchr(tdatap, '>');
    if (!tdatap) {
        *resultp = std::string("[Bad loookup response]");
        return 0;
    }

    tdatap++;

    code = _xgmlp->parse(&tdatap, &rootNodep);
    if (code == 0) {
        resultp->clear();

        nodep = rootNodep->searchForChild("countryCode");
        if (nodep)
            countryCode = nodep->_children.head()->_name;
        else {
            *resultp = std::string("[Unknown location]");
            return 0;
        }

        nodep = rootNodep->searchForChild("city");
        if (nodep) {
            resultp->append(nodep->_children.head()->_name);
        }
        nodep = rootNodep->searchForChild("region");
        if (nodep) {
            resultp->append(", ");
            resultp->append(nodep->_children.head()->_name);
        }
        nodep = rootNodep->searchForChild("countryCode");
        if (nodep) {
            resultp->append(" [");
            resultp->append(nodep->_children.head()->_name);
            resultp->append("]");
        }
    }
    else {
        printf("parse of xml failed %d\n", code);
        rootNodep->print();
        *resultp = std::string("[Unknown location]");
    }
    return 0;
}

int32_t
MfServer::findLocation(uint32_t ipAddr, std::string *resultp)
{
    MfLocation *locp;

    for(locp = _locationCache.head(); locp; locp=locp->_dqNextp) {
        if (locp->_ipAddr == ipAddr) {
            *resultp = locp->_locationString;
            return 0;
        }
    }
    return -1;
}

void
MfServer::addLocation(uint32_t ipAddr, std::string *resultp)
{
    MfLocation *locp;

    for(locp = _locationCache.head(); locp; locp=locp->_dqNextp) {
        if (locp->_ipAddr == ipAddr) {
            locp->_locationString = *resultp;
            return;
        }
    }

    locp = new MfLocation();
    locp->_ipAddr = ipAddr;
    locp->_locationString = *resultp;
    _locationCache.append(locp);
}

/* must be called with lock held */
int32_t
MfServer::getLocation(uint32_t ipAddr, std::string *resultp)
{
    int32_t code;

    code = findLocation(ipAddr, resultp);
    if(code == 0)
        return 0;

    /* drop lock over rest call */
    _lock.release();
    code = translateLocation(ipAddr, resultp);
    printf("translated location: '%s'\n", resultp->c_str());
    _lock.take();
    if (code == 0) {
        addLocation(ipAddr, resultp);
    }
    return code;
}

int32_t
MfServer::MfAnnounceContext::serverMethod(RpcServer *serverp, Sdr *inDatap, Sdr *outDatap) {
    char *artistp;
    char *songp;
    char *userp;
    uint32_t songType;
    uint32_t acl;
    int32_t code;
    std::string userStr;
    std::string finalUserStr;
    MfEntry *ep;
    uint32_t callingIpAddr;
    uint32_t userLen;
    char date[32];
    char decodedTime[100];
    time_t baseTime;

    _serverp->_lock.take();

    code = inDatap->copyString(&userp, /* !doMarshal */ 0);
    if (code) {
        _serverp->_lock.release();
        return code;
    }
    code = inDatap->copyString(&songp, /* !doMarshal */ 0);
    if (code) {
        _serverp->_lock.release();
        return code;
    }
    code = inDatap->copyString(&artistp, /* !doMarshal */ 0);
    if (code) {
        _serverp->_lock.release();
        return code;
    }
    code = inDatap->copyLong(&songType, /* !doMarshal */ 0);
    if (code) {
        _serverp->_lock.release();
        return code;
    }
    code = inDatap->copyLong(&acl, /* !doMarshal */ 0);
    if (code) {
        _serverp->_lock.release();
        return code;
    }

    /* translate calling address into a string, and prepend "<uname>@".
     * The userp variable needs to be freed, and a newly allocated char
     * array needs to be passed to MfEntry's constructor.
     */
    getPeerAddr(&callingIpAddr);
    _serverp->translateLocation(callingIpAddr, &userStr);
    finalUserStr = std::string(userp) + std::string("@") + userStr;
    delete userp;
    userLen = strlen(finalUserStr.c_str()) + 1; /* including terminating null */
    userp = new char [userLen];
    memcpy(userp, finalUserStr.c_str(), userLen);

    baseTime = time(0);
    ctime_r(&baseTime, date);
    date[24] = 0;

    if (acl < 3)
        strcpy(decodedTime, "[old time]");
    else {
        sprintf(decodedTime, "id=%08x (downloaded %d seconds ago)",
                acl, (int)(osp_time_sec() - acl));
    }
    printf("%s: saw %s: %s/%s type=0x%x %s\n",
           date, userp, songp, artistp, songType, decodedTime);

    if (acl != 0) {
        ep = new MfEntry(userp, songp, artistp, songType, acl);
        _serverp->_allPlaying.prepend(ep);
    }

    _serverp->checkCount();

    _serverp->_lock.release();

    getConn()->reverseConn();

    return 0;
}

int32_t
MfServer::MfListAllContext::serverMethod(RpcServer *serverp, Sdr *inDatap, Sdr *outDatap) {
    int32_t code;
    uint32_t maxCount;
    uint32_t currentCount;
    uint32_t i;
    MfEntry *ep;

    printf("ListAllContext starts\n");

    /* IN parameter: 32 bit max count.  Out parameter is a 32 bit
     * count of string triples, followed by that many triples of who,
     * song and artist, each of which is an sdr string.
     */
    code = inDatap->copyLong(&maxCount, /* !doMarshal */ 0);
    if (code) {
        return code;
    }

    printf("ListAllContext count=%d\n", maxCount);

    getConn()->reverseConn();

    _serverp->_lock.take();

    /* response is count of entries returned, followed by 3 strings per
     * entry, each set giving who, song and artist.
     */

    currentCount = _serverp->_allPlaying.count();
    if (currentCount > maxCount)
        currentCount = maxCount;

    code = outDatap->copyLong( &currentCount, /* doMarshal */ 1);
    if (code) {
        _serverp->_lock.release();
        printf("ListAllContext failed code=%d\n", code);
        return code;
    }

    i = 0;
    for(ep = _serverp->_allPlaying.head(); ep; ep=ep->_dqNextp, i++) {
        if (i >= currentCount)
            break;
        code = outDatap->copyString(&ep->_whop, 1);
        if (code) {
            _serverp->_lock.release();
            printf("ListAllContext failed code=%d\n", code);
            return code;
        }
        code = outDatap->copyString(&ep->_songp, 1);
        if (code) {
            _serverp->_lock.release();
            printf("ListAllContext failed code=%d\n", code);
            return code;
        }
        code = outDatap->copyString(&ep->_artistp, 1);
        if (code) {
            _serverp->_lock.release();
            printf("ListAllContext failed code=%d\n", code);
            return code;
        }
        code = outDatap->copyLong(&ep->_songType, 1);
        if (code) {
            _serverp->_lock.release();
            printf("ListAllContext failed code=%d\n", code);
            return code;
        }
        code = outDatap->copyLong(&ep->_acl, 1);
        if (code) {
            _serverp->_lock.release();
            printf("ListAllContext failed code=%d\n", code);
            return code;
        }
    }

    _serverp->_lock.release();

    printf("ListAllContext done successfully\n");

    return 0;
}

int
main(int argc, char **argv)
{
    Rpc *rpcp;
    MfServer *serverp;
    uuid_t serviceId;
    RpcListener *listenerp;

    setvbuf(stdout, NULL, _IOLBF, 0);

    rpcp = new Rpc();
    rpcp->init();

    Rpc::uuidFromLongId(&serviceId, 7);
    serverp = new MfServer(rpcp);
    rpcp->addServer(serverp, &serviceId);

    /* create an endpoint for the server */
    listenerp = new RpcListener();
    listenerp->init(rpcp, serverp, 7711);

    while(1) {
        sleep(1);
    }
}
