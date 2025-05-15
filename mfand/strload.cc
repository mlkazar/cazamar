#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include "radiostream.h"
#include "bufsocket.h"
#include "xapi.h"
#include "oauth.h"
#include "json.h"
#include "strdb.h"

int _strFd;

void *listenConn(void *cxp);

/**************** REST factory ****************/

#define RIVO_SESSION_ID_SIZE    8
#define RIVO_SESSION_KEY_SIZE   20

class RivoSession {
public:
    RivoSession *_dqNextp;
    RivoSession *_dqPrevp;
    uint8_t _sessionId[RIVO_SESSION_ID_SIZE];
    uint8_t _sessionKey[RIVO_SESSION_KEY_SIZE];
    std::string _emailStr;
};

/* one of these for each request type */
class RivoSimpleRequest : public XApi::ServerReq {
    UserDb *_userDbp;
    dqueue<RivoSession> _allSessions;

public:
    void startMethod();

    int32_t createAcctMethod(Json::Node *rootNodep);

    int32_t loginUserMethod(Json::Node *rootNodep);

    void returnNode(Json::Node *returnNodep, int32_t code);

    int32_t init();
};

XApi::ServerReq *
restFactory(std::string *opcodep)
{
    RivoSimpleRequest *rivop;

    /* based on the opcode, we create the right request type */
    rivop = new RivoSimpleRequest();
    rivop->init();

    return rivop;
}

int32_t
RivoSimpleRequest::init()
{
    int32_t code;
    _userDbp = new UserDb();
    code = _userDbp->init();
    return code;
}

void
RivoSimpleRequest::returnNode(Json::Node *responseNodep, int32_t code)
{
    CThreadPipe *outPipep;
    int tlen;
    Json json;
    Json::Node *rootNodep;
    Json::Node *tnodep;
    Json::Node *nameNodep;
    std::string returnStr;
    const char *returnp;
    char tbuffer[1024];

    outPipep = getOutgoingPipe();

    rootNodep = new Json::Node();
    rootNodep->initStruct();

    tnodep = new Json::Node();
    tnodep->initString("0", 1);    /* XXX use real value from incoming call */
    nameNodep = new Json::Node();
    nameNodep->initNamed("ts", tnodep);
    rootNodep->appendChild(nameNodep);

    tnodep = new Json::Node();
    snprintf(tbuffer, sizeof(tbuffer), "%u", code);
    tnodep->initString(tbuffer, 1);
    nameNodep = new Json::Node();
    nameNodep->initNamed("code", tnodep);
    rootNodep->appendChild(nameNodep);

    if (responseNodep != NULL) {
        nameNodep = new Json::Node();
        nameNodep->initNamed("parm", responseNodep);
        rootNodep->appendChild(nameNodep);
    }

    rootNodep->unparse(&returnStr);
    returnp = returnStr.c_str();

    tlen = strlen(returnp);
    setSendContentLength(tlen);
    inputReceived();
    outPipep->write(returnp, tlen);
}

void
RivoSimpleRequest::startMethod()
{
    int32_t incomingBytes = getRcvContentLength();
    CThreadPipe *inPipep;
    CThreadPipe *outPipep;
    char tbuffer[2048];
    int32_t code;
    int32_t tlen;
    Json json;
    Json::Node *rootNodep;
    Json::Node *parmNodep;
    Json::Node *opNodep;
    char *inputp;

    printf("rivo: in startmethod with %d incoming bytes avilable for reading\n",
           incomingBytes);

    inPipep = getIncomingPipe();
    outPipep = getOutgoingPipe();

    printf("rivo: back from sleep; responding\n");

    /* read the data */
    code = inPipep->read(tbuffer, sizeof(tbuffer)-1);
    if (code >= 0) {
        tbuffer[code] = 0;
        printf("read %d bytes: '%s'\n", code, tbuffer);
        inputp = tbuffer;
        rootNodep = NULL;
        code = json.parseJsonChars(&inputp, &rootNodep);
        printf("json parse code=%d\n", code);
    }
    else {
        printf("read failed code %d\n", code);
        rootNodep = NULL;
    }

    opNodep = rootNodep->searchForChild("op");
    parmNodep = rootNodep->searchForChild("parm");

    if (!opNodep || !parmNodep) {
        returnNode(NULL, 0);
        return;
    }

    if (opNodep->_children.head()->_name == "createAcct") {
        code = createAcctMethod(parmNodep);
    }
    else if (opNodep->_children.head()->_name == "loginUser") {
        code = loginUserMethod(parmNodep);
    }
    else {
        code = -1;
    }

    snprintf(tbuffer, sizeof(tbuffer), "return code=%d", code);

    /* on error, we generate the response string */
    if (code != 0) {
        tlen = strlen(tbuffer);
        setSendContentLength(tlen);
        outPipep->write(tbuffer, tlen);
    }

    outPipep->eof();     /* can we make this optional for fixed size responses? */

    /* tell our caller that the request is finished; this frees the request
     * and allows the task to go back into the pool.
     */
    requestDone();
}

int32_t
RivoSimpleRequest::createAcctMethod(Json::Node *rootNodep)
{
    CThreadPipe *outPipep;
    Json json;
    Json::Node *tnodep;
    int32_t resultCode;
    std::string *userStrp;
    std::string *emailStrp;
    std::string *passwordStrp;

    printf("rivo: in createAcct\n");

    outPipep = getOutgoingPipe();

    tnodep = rootNodep->searchForChild("user");
    if (tnodep != NULL) {
        userStrp = &tnodep->_children.head()->_name;
        tnodep = rootNodep->searchForChild("email");
    }
    if (tnodep != NULL) {
        emailStrp = &tnodep->_children.head()->_name;
        tnodep = rootNodep->searchForChild("password");
    }
    if (tnodep != NULL) {
        passwordStrp = &tnodep->_children.head()->_name;
    }

    if (tnodep != NULL) {
        printf("u=%s e=%s p=%s\n", userStrp->c_str(), emailStrp->c_str(), passwordStrp->c_str());
        resultCode = _userDbp->createUser( userStrp->c_str(),
                                           emailStrp->c_str(),
                                           passwordStrp->c_str());
    }
    else
        resultCode = 1;

    returnNode(NULL, resultCode);

    outPipep->eof();     /* can we make this optional for fixed size responses? */

    /* tell our caller that the request is finished; this frees the request
     * and allows the task to go back into the pool.
     */
    requestDone();

    return 0;
}

int32_t
RivoSimpleRequest::loginUserMethod(Json::Node *rootNodep)
{
    CThreadPipe *outPipep;
    Json json;
    Json::Node *tnodep;
    std::string *emailStrp = NULL;
    std::string *passwordStrp = NULL;
    std::string dbPassword;
    std::string dbUserId;
    int32_t returnCode;
    Json::Node *parmNodep = NULL;
    Json::Node *nameNodep;
    char keyData[RIVO_SESSION_KEY_SIZE];
    char idData[RIVO_SESSION_ID_SIZE];
    uint32_t i;
    uint32_t temp;
    char *outp;
    RivoSession *sessionp;

    printf("rivo: in createAcct\n");

    outPipep = getOutgoingPipe();

    tnodep = rootNodep->searchForChild("email");
    if (tnodep != NULL) {
        emailStrp = &tnodep->_children.head()->_name;
    }

    tnodep = rootNodep->searchForChild("password");
    if (tnodep != NULL) {
        passwordStrp = &tnodep->_children.head()->_name;
    }

    if (passwordStrp != NULL && emailStrp != NULL) {
        printf("u=%s p=%s\n", emailStrp->c_str(), passwordStrp->c_str());
        returnCode = _userDbp->lookupUser( emailStrp->c_str(),
                                           /* emailStr */ NULL,
                                           &dbPassword,
                                           &dbUserId);
        if (returnCode == 0) {
            /* compare password, and if matches, return success and a
             * session key.
             */
            if (dbPassword == *passwordStrp) {
                parmNodep = new Json::Node();
                parmNodep->initStruct();

                /* we need 160 bits of key for SHA1.  We're going to
                 * encode it with 6 bits per 8 bytes of key, so it
                 * requires 27 bytes, but in reality, we're going to
                 * generate 28 bytes of encoded stuff corresponding to
                 * 168 bits of key, of which we're only going to use
                 * the first 160 bits.
                 */
                tnodep = new Json::Node();
                for(i=0; i<RIVO_SESSION_KEY_SIZE/4; i++) {
                    temp = rand();
                    keyData[4*i] = temp&0xFF;
                    keyData[4*i+1] = (temp>>8) & 0xFF;
                    keyData[4*i+2] = (temp>>16) & 0xFF;
                    keyData[4*i+3] = (temp>>24) & 0xFF;
                }
                outp = oauth_encode_base64(RIVO_SESSION_KEY_SIZE, (const unsigned char *) keyData);
                tnodep->initString(outp, 1);
                free(outp);
                nameNodep = new Json::Node();
                nameNodep->initNamed("sessionKey", tnodep);
                parmNodep->appendChild(nameNodep);

                tnodep = new Json::Node();
                for(i=0;i<RIVO_SESSION_ID_SIZE/4; i++) {
                    temp = rand();
                    idData[4*i] = temp & 0xFF;
                    idData[4*i+1] = (temp>>8) & 0xFF;
                    idData[4*i+2] = (temp>>16) & 0xFF;
                    idData[4*i+3] = (temp>>24) & 0xFF;
                }
                outp = oauth_encode_base64(RIVO_SESSION_ID_SIZE, (const unsigned char *) idData);
                tnodep->initString(outp, 1);
                free(outp);
                nameNodep = new Json::Node();
                nameNodep->initNamed("sessionId", tnodep);
                parmNodep->appendChild(nameNodep);

                sessionp = new RivoSession();
                memcpy(sessionp->_sessionId, idData, RIVO_SESSION_ID_SIZE);
                memcpy(sessionp->_sessionKey, keyData, RIVO_SESSION_KEY_SIZE);
                sessionp->_emailStr = *emailStrp;

                /* XXXX locking! for _allSessions */
                _allSessions.append(sessionp);
            } /* password matches */
            else {
                returnCode = -2;
            }
        } /* email lookup succeeded */
        else {
            returnCode = -3;
        }
    }
    else
        returnCode = 1;

    returnNode(parmNodep, returnCode);

    outPipep->eof();     /* can we make this optional for fixed size responses? */

    /* tell our caller that the request is finished; this frees the request
     * and allows the task to go back into the pool.
     */
    requestDone();

    return 0;
}

/**************** Music transfer code ****************/
int32_t
strDataProc(void *contextp, RadioStream *radiop, char *bufferp, int32_t length)
{
    int32_t code;

    if (radiop->isClosed())
        return -1;

    // Rst::dumpBytes(bufferp, 0, length);
    code = write(_strFd, bufferp, length);
    if (code < length) {
        perror("write");
        return -1;
    }
    return 0;
}

int32_t
strControlProc(void *contextp, RadioStream *radiop, RadioStream::EvType evType, void *evDatap)
{
    if (radiop->isClosed())
        return -1;

    if (evType == RadioStream::eventSongChanged) {
        RadioStream::EvSongChangedData *changedDatap = (RadioStream::EvSongChangedData *) evDatap;
        printf("StrLoad: music changed event %s\n", changedDatap->_song.c_str());
    }
    return 0;
}

int
main(int argc, char **argv)
{
    RadioStream radio;
    int32_t code;
    char *urlp;
    XApi *xapip;
    BufSocketFactory socketFactory;

    _strFd = open("rcv.mp3", O_CREAT | O_TRUNC | O_RDWR, 0666);

    if (_strFd < 0) {
        perror("open");
        return 0;
    }

    // string 'wmfo-duke.orgs.tufts.edu:8000/listen.pls' for example */
    // http://wyep.streamguys.net/listen.pls
    // http://provisioning.streamtheworld.com/pls/WBRUFMAAC.pls
    if (argc > 1)
        urlp = argv[1];
    else
        urlp = (char *) "http://wmfo-duke.orgs.tufts.edu:8000/listen.pls";

    xapip = new XApi();
    xapip->registerFactory(&restFactory);
    xapip->initWithPort(8234);

#if 0
    /* test oauth */
    {
        char *signaturep;
        signaturep = oauth_sign_hmac_sha1_raw ("testString", 10,
                                               "keystring", 9);
        printf("signature is '%s' (%d)\n", signaturep, (int) strlen(signaturep));
        free(signaturep);

        signaturep = oauth_encode_pass("foo", "mlk2");
        printf("encrypted password is '%s' (%d bytes)\n", signaturep, (int) strlen(signaturep));
        free(signaturep);
    }
#endif

    code = radio.init(&socketFactory, urlp, &strDataProc, &strControlProc, /* context */ NULL);
    printf("StrLoad: radio code is %d\n", code);

    while(1) {
        sleep(1);
    }

    return 0;
}
