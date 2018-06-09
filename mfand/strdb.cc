#include <stdlib.h>

#include "strdb.h"

int32_t
UserDb::init()
{
    int32_t code;
    std::string emailStr("email");
    int64_t nextUid;
    int64_t tuid;
    Json::Node *recordNodep;
    Json::Node *tnodep;

    code = Jsdb::init("AllUsers.db", &emailStr);

    nextUid = 100;
    if (code == 0) {
        for( recordNodep = getRoot()->_children.head();
             recordNodep;
             recordNodep=recordNodep->_dqNextp) {
            tnodep = recordNodep->searchForChild("uid");
            if (tnodep) {
                tuid = atoi(tnodep->_children.head()->_name.c_str());
                if (tuid+1 > nextUid)
                    nextUid = tuid+1;
            }
        }
        _nextUid = nextUid;
    }

    return code;
}

int32_t
UserDb::createUser(const char *userNamep, const char *emailp, const char *passwordp)
{
    int32_t code;
    Json::Node *rootNodep;
    Json::Node *tnodep;
    Json::Node *nameNodep;
    Jsdb::Tran *tranp;
    std::string key;
    int64_t uid;

    rootNodep = new Json::Node();
    rootNodep->initStruct();

    tnodep = new Json::Node();
    tnodep->initString(userNamep, 1);
    nameNodep = new Json::Node();
    nameNodep->initNamed("userName", tnodep);
    rootNodep->appendChild(nameNodep);

    tnodep = new Json::Node();
    tnodep->initString(emailp, 1);
    nameNodep = new Json::Node();
    nameNodep->initNamed("email", tnodep);
    rootNodep->appendChild(nameNodep);

    tnodep = new Json::Node();
    tnodep->initString(passwordp, 1);
    nameNodep = new Json::Node();
    nameNodep->initNamed("password", tnodep);
    rootNodep->appendChild(nameNodep);

    uid = _nextUid++;
    tnodep = new Json::Node();
    tnodep->initInt(uid);
    nameNodep = new Json::Node();
    nameNodep->initNamed("uid", tnodep);
    rootNodep->appendChild(nameNodep);

    tranp = new Jsdb::Tran();
    tranp->init(this);

    key = emailp;
    code = tranp->create(&key, rootNodep, /* excl */ 1);

    if (code == 0)
        code = tranp->commit();
    else
        tranp->abort();

    return code;
}

int32_t
UserDb::lookupUser( const char *emailp,         /* IN */
                    std::string *userNameStrp,  /* OUT */
                    std::string *passwordStrp,
                    std::string *uidStrp)  /* OUT */
{
    Json::Node *rootNodep;
    Jsdb::Tran *tranp;
    int32_t code;
    Json::Node *userNameNodep;
    Json::Node *passwordNodep;
    Json::Node *uidNodep;
    std::string emailStr;

    tranp = new Jsdb::Tran();
    tranp->init(this);

    emailStr = std::string(emailp);
    code = tranp->search(&emailStr, &rootNodep);
    if (code == 0) {
        userNameNodep = rootNodep->searchForChild("userName");
        passwordNodep = rootNodep->searchForChild("password");
        uidNodep = rootNodep->searchForChild("uid");
        if (userNameNodep && passwordNodep) {
            /* both fields are OK */
            if (userNameStrp)
                *userNameStrp = userNameNodep->_children.head()->_name;
            if (passwordStrp)
                *passwordStrp = passwordNodep->_children.head()->_name;
            if (uidStrp)
                *uidStrp = uidNodep->_children.head()->_name;
            code = 0;
        }
        else {
            code = Jsdb::err_io;
        }
    }

    tranp->commit();
    return code;
}

