#include <string>
#include <sys/errno.h>

#include "jsdb.h"
#include "json.h"

/* XXX Note that error recovery is not implemented, beyond aborting on the first failed
 * create call.
 */

JsdbInStream::JsdbInStream(FILE *filep)
{
    _inFilep = filep;

    fillBuffer();
}

void
JsdbInStream::fillBuffer()
{
    int tc;

    tc = fgetc(_inFilep);
    if (tc < 0)
        _buffer = 0;
    else
        _buffer = tc;
}

int
JsdbInStream::top()
{
    return _buffer;
}

void
JsdbInStream::next()
{
    fillBuffer();
}

int32_t
Jsdb::loadDatabase()
{
    int32_t code;
    Json::Node *recordNodep;

    if (_rootArrayp) {
        delete _rootArrayp;
        _rootArrayp = NULL;
    }

    _rootArrayp = new Json::Node();
    _rootArrayp->initArray();

    while(1) {
        code = _json.parseJsonValue(_inStreamp, &recordNodep);
        if (code < 0) {
            break;
        }
        _rootArrayp->appendChild(recordNodep);
    }

    return 0;
}

int32_t
Jsdb::init(const char *fileNamep, std::string *primaryKeyNamep)
{
    FILE *ifilep;
    int32_t code;

    _fileNamep = new char[strlen(fileNamep) + 1];
    strcpy(_fileNamep,  fileNamep);
    _primaryKeyName = *primaryKeyNamep;

    ifilep = fopen(_fileNamep, "r");
    if (!ifilep) {
        if (errno != ENOENT) {
            printf("jsdb: can't open file '%s'\n", _fileNamep);
            return err_noent;
        }
        /* ENOENT represents an empty database */
    }
    else {
        /* wrap a parser stream */
        _inStreamp = new JsdbInStream(ifilep);

        /* parse records from the database */
        code = loadDatabase();
        if (code < 0)
            return code;

        delete _inStreamp;
        _inStreamp = NULL;
    }

    _didInit = 1;

    return err_ok;
}

int32_t
Jsdb::Tran::init(Jsdb *jsdbp)
{
    _jsdbp = jsdbp;
    jsdbp->_tranMutex.take();
    return 0;
}

int32_t
Jsdb::Tran::search(std::string *keyp, Json::Node **nodepp)
{
    Json::Node *recordNodep;

    for( recordNodep = _jsdbp->_rootArrayp->_children.head();
         recordNodep;
         recordNodep=recordNodep->_dqNextp) {
        if (matches(keyp, recordNodep)) {
            *nodepp = recordNodep;
            return 0;
        }
    }
    *nodepp = 0;
    return Jsdb::err_noent;
}

int
Jsdb::Tran::matches(std::string *keyValuep, Json::Node *recordNodep)
{
    Json::Node *nodep;
    std::string keyName;

    keyName = _jsdbp->_primaryKeyName;

    for( nodep = recordNodep->_children.head();
         nodep;
         nodep = nodep->_dqNextp) {
        if (nodep->_name == keyName) {
            /* this is the field we're looking for; see if the value matches */
            if (*keyValuep == nodep->_children.head()->_name) {
                return 1;
            }
        }
    }

    return 0;
}

int32_t
Jsdb::Tran::create(std::string *keyValuep, Json::Node *nodep, int excl)
{
    Json::Node *tempNodep;
    int32_t code;
    Json::Node *rootNodep = _jsdbp->_rootArrayp;

    osp_assert(matches(keyValuep, nodep));
    _modified = 1;

    code = search(keyValuep, &tempNodep);
    if (code == 0) {
        if (excl) {
            return err_exist;
        }
        /* remove old record with this key, and insert nodep */
        rootNodep->removeChild(tempNodep);
        rootNodep->appendChild(nodep);
        delete tempNodep;
    }
    else if (code == Jsdb::err_noent) {
        /* success */
        rootNodep->appendChild(nodep);
    }
    else {
        return code;
    }

    return 0;
}

int32_t
Jsdb::Tran::commit()
{
    std::string outBuffer;
    Json::Node *nodep;
    FILE *ofilep;
    int32_t code;
    int32_t tempCode;

    ofilep = fopen(_jsdbp->_fileNamep, "w");
    if (!ofilep) {
        printf("jsdb: can't create database file %s\n", _jsdbp->_fileNamep);
        return Jsdb::err_io;
    }

    code = 0;
    nodep = _jsdbp->_rootArrayp->_children.head();
    for(; nodep; nodep=nodep->_dqNextp) {
        outBuffer.erase();
        nodep->unparse(&outBuffer);
        code = fwrite(outBuffer.c_str(), outBuffer.length(), 1, ofilep);
        if (code != 1) {
            code =  Jsdb::err_io;
            break;
        }
        else {
            code = 0;
        }
    }

    tempCode = fclose(ofilep);
    if (code == 0)
        code = tempCode;

    /* save this shard */
    _jsdbp->_tranMutex.release();
    delete this;
    return code;
}

/* no need for a return code */
void
Jsdb::Tran::abort()
{
    delete this;
}
