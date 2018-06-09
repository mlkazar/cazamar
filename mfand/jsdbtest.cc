#include <stdlib.h>

#include "json.h"
#include "jsdb.h"

/* simple test program with a few commands:
 *
 * cr[eate] user val1 val2
 *
 * get user
 *
 * del user
 *
 * add user val1incr val2incr
 */

Json _json;
Jsdb _jsdb;

int32_t
doCreate(char *userp, int32_t aval, int32_t bval)
{
    Json::Node *newNodep;
    Json::Node *intNodep;
    Json::Node *leafNodep;
    Json::Node *arrayNodep;
    Json::Node *tnodep;
    std::string userName;
    Jsdb::Tran *tranp;
    int32_t code;

    userName = userp;

    newNodep = new Json::Node();
    newNodep->initStruct();

    leafNodep = new Json::Node();
    leafNodep->initString(userName.c_str(), 1);
    tnodep = new Json::Node();
    tnodep->initNamed("user", leafNodep);
    newNodep->appendChild(tnodep);

    arrayNodep = new Json::Node();
    arrayNodep->initArray();
    intNodep = new Json::Node();
    intNodep->initInt(aval);
    arrayNodep->appendChild(intNodep);
    intNodep = new Json::Node();
    intNodep->initInt(bval);
    arrayNodep->appendChild(intNodep);

    tnodep = new Json::Node();
    tnodep->initNamed("vals", arrayNodep);
    newNodep->appendChild(tnodep);

    tranp = new Jsdb::Tran();
    tranp->init(&_jsdb);
    code = tranp->create(&userName, newNodep, 1);
    printf("record create code=%d\n", code);
    if (code)
        return code;
    code = tranp->commit();
    printf("record commit code=%d\n", code);
    return code;
}

int32_t
doGet(char *userp)
{
    return 0;
}

int
main(int argc, char **argv)
{
    int32_t code;
    std::string primaryKey;

    if (argc<2) {
        printf("usage: cr/get/del/add (see src)\n");
        return -1;
    }

    primaryKey = "user";
    code = _jsdb.init("jsdbtest.db", &primaryKey);
    if (code != 0) {
        printf("failed to init jsdbtest.db, code=%d\n", code);
        return code;
    }

    if (!strcmp(argv[1], "cr")) {
        code = doCreate(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    else if(!strcmp(argv[1], "get")) {
        code = doGet(argv[2]);
    }

    return 0;
}
