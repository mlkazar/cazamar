#ifndef __JSDB__H_ENV
#define __JSDB__H_ENV 1

#include <stdio.h>
#include <string>

#include "json.h"
#include "osp.h"
#include "cthread.h"

class JsdbInStream : public InStream {
    FILE *_inFilep;
    int _buffer;

 public:
    int top();

    void next();

    void fillBuffer();

    JsdbInStream(FILE *filep);

    virtual ~JsdbInStream() {
        if (_inFilep)
            fclose(_inFilep);
    }
};

/* one shard for now */
class Jsdb {
 public:
    static const int32_t err_ok = 0;
    static const int32_t err_io = -1;
    static const int32_t err_noent = -2;
    static const int32_t err_exist = -3;

 private:
    JsdbInStream *_inStreamp;
    Json _json;

    /* name of file that stores an array of structs */
    char *_fileNamep;

    /* the name of the primary key for searches */
    std::string _primaryKeyName;

    /* a flag that says that we're doing a search */
    uint8_t _didInit;

    /* the array of structs */
    Json::Node *_rootArrayp;

 public:
    /* protecting the whole thing */
    CThreadMutex _tranMutex;

 public:
    Jsdb() {
        _didInit = 0;
        _rootArrayp = new Json::Node();
        _rootArrayp->initArray();
        return;
    }

    int32_t loadDatabase();

    Json::Node *getRoot() {
        return _rootArrayp;
    }

    int32_t init(const char *fileNamep, std::string *primaryKeyNamep);

    class Tran {
        Jsdb *_jsdbp;
        uint8_t _modified;

    public:
        Tran() {
            _jsdbp = NULL;
            _modified = 0;
        }

        int32_t init(Jsdb *jsdbp);

        int32_t commit();

        void abort();

        int32_t search(std::string *keyp, Json::Node **nodepp);

        int32_t create(std::string *keyp, Json::Node *nodep, int excl);

    private:
        int matches(std::string *keyValuep, Json::Node *recordNodep);
    };
};

#endif /* __JSDB__H_ENV */
