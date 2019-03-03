#ifndef __JSON_H_ENV_
#define __JSON_H_ENV_ 1

#include <sys/types.h>
#include <string>

#include "dqueue.h"
#include "osp.h"

    /* incoming stream */
class InStream {
 public:
    virtual int top()= 0;

    virtual void next() = 0;

    int32_t read(char *bufferp, int32_t count) {
        int32_t i;
        int tc;
        for(i=0;i<count;i++) {
            tc = top();
            if (tc < 0)
                return tc;
            *bufferp++ = tc;
            next();
        }
        return 0;
    }

    int32_t skip(int32_t count) {
        int32_t i;
        int tc;
        for(i=0;i<count;i++) {
            tc = top();
            if (tc < 0)
                return tc;
            next();
        }
        return 0;
    }
};

class InStreamString : public InStream {
    char *_datap;
    char *_origDatap;
 public:
    InStreamString(char *datap) {
        _datap = _origDatap = datap;
    }

    int top() {
        return (*_datap) & 0xFF;
    }

    void next() {
        _datap++;
    }

    char *final() {
        return _datap;
    }
};

class InStreamFile : public InStream {
    FILE *_inFilep;
    int _buffer;

 public:
    int top();

    void next();

    void fillBuffer();

    InStreamFile(FILE *filep);

    virtual ~InStreamFile() {
        if (_inFilep)
            fclose(_inFilep);
    }
};

/* class for parsing XML and XML-like (including some SGML) data streams.
 * XML requires matching tags, but many SGML tags are terminated by 
 * newline characters.  By default, an unrecognized tag is treated as requiring
 * termination (a la XML).  However, behavior for tags can be provided by
 * an override, and the caller can also provide a default for child objects.
 * Defaults for child objects are always overridden by explicit defaults for 
 * objects found within the parent having the child object defaults.
 */
class Json {
    /* this stuff is whitespace, including line termination */
    static const char *_whiteSpacep;

    /* blank space, not including line termination */
    static const char *_blankSpacep;

    /* characters that are single tokens */
    static const char *_singleJsonTokensp;

 public:

    class Node;

    class TokenState {
    public:
        TokenState *_dqNextp;
        TokenState *_dqPrevp;
        std::string _tokenName;
        int _needsEnd;
    };

    /* Overall structure is that a named pair is represented with the
     * name in _name, and the value object in child =
     * _children.head().  If the value is a simple string or other
     * leaf, the string representation is in child->_name (and
     * child->_isLeaf is set).  If the value is an array or struct,
     * child->_isLeaf is false, and then _children list consists of
     * the subobjects (array) or pairs (struct) being referenced.
     */
    class Node {
    public:
        std::string _name;
        uint8_t _isLeaf;                /* leaf string */
        uint8_t _isQuoted;              /* true if _name was quoted */
        uint8_t _isArray;               /* if not leaf, this is an array */
        uint8_t _isStruct;              /* if not leaf, this is a structure */
        uint8_t _isNamed;               /* _name gives name of a pair; value is _children.head */

        /* values for bool */
        uint8_t _boolValue;
        uint64_t _intValue;
        double _floatValue;

        Node *_dqNextp;                 /* next and prev in child list */
        Node *_dqPrevp;                 /* next and prev in child list */
        Node *_parentp;
        dqueue<Node> _children;         /* list of all children */

        Node() {
            _isLeaf = 0;
            _isQuoted = 0;
            _isNamed = 0;
            _isArray = 0;
            _isStruct = 0;

            _boolValue = 0;
            _intValue = 0;
            _floatValue = 0.0;

            _dqNextp = NULL;
            _dqPrevp = NULL;
            _parentp = NULL;
        }

        ~Node() {
            Node *childp;
            Node *nchildp;

            for (childp = _children.head(); childp; childp=nchildp) {
                nchildp = childp->_dqNextp;
                delete childp;
            }
        }

        void init(const char *namep, int isLeaf) {
            _name = namep;
            _isLeaf = isLeaf;
        }

        /* initialize a node as an array; all the elements are inserted via
         * appendChild.
         */
        void initArray() {
            _isArray = 1;
        }

        /* initialize a structure, and then use appendChild to create its
         * substructures.
         */
        void initStruct() {
            _isStruct = 1;
        }

        /* create a named pair, with the specified name, and the node
         * as the valiue for the pair.
         */
        void initNamed(const char *namep, Node *nodep) {
            _name = namep;
            appendChild(nodep);
        }

        /* initialize a leaf node with a string value; isQuoted
         * means that we emit the token with quotes around it.
         */
        void initString(const char *namep, int isQuoted) {
            _name = namep;
            _isLeaf = 1;
            _isQuoted = isQuoted;
        }

        /* initialize a leaf node with an integer */
        void initInt(uint64_t value) {
            char tbuffer[128];
            sprintf(tbuffer, "%llu", (unsigned long long) value);
            initString(tbuffer, /* !quoted */ 0);
        }

        /* add a child */
        void appendChild(Node *childNodep) {
            _children.append(childNodep);
            childNodep->_parentp = this;
        }

        void removeChild(Node *childNodep) {
            osp_assert(childNodep->_parentp == this);
            _children.remove(childNodep);
            childNodep->_parentp = NULL;
        }

        void print();

        void unparse(std::string *resultp);

        void printInternal(std::string *resultp, int pretty, uint32_t level, int addComma);

        void printToCPP(std::string *resultp) {
            printInternal(resultp, 1, 0, 0);
        }

        void detach();

        Node *dive();

        static void appendStr(std::string *resultp, std::string data);

        Node *searchForChild(std::string name, int checkData = 0);

        Node *searchForLeaf();
    };

    int _defaultNeedsEnd;
    dqueue<TokenState> _allTokenState;

    Json() {
        /* no special initialization needed */
        _defaultNeedsEnd = 1;
    }

    ~Json() {
        /* no doubt will need this */
    }

    /* undefined on what happens with c == 0 */
    static int isWhitespace(int c) {
        return (index(_whiteSpacep, c) != NULL);
    }

    static int isBlankspace(int c) {
        return (index(_blankSpacep, c) != NULL);
    }

    static int isSingleJsonToken(int c) {
        return (index(_singleJsonTokensp, c) != NULL);
    }

    void setTokenDefault(std::string tokenName, int needsEnd);

    /* set the default behavior for a token */
    void setTokenDefault(int needsEnd);

    int32_t parseJsonChars(char **inDatapp, Node **nodepp);

    int32_t parseJsonPair(InStream *streamp, Node **nodepp);

    int32_t parseJsonValue(InStream *streamp, Node **nodepp);

    int32_t parseJsonComplex(InStream *streamp, std::string *firstToken, Node **nodepp);

    int32_t parseJsonFile(FILE *filep, Json::Node **nodepp);

    void parseFailed(const char *strp);

    /* return an std::string with the results of emitting the
     * data in the xgml object.
     */
    int32_t unparse(std::string *stringp, Node *nodep);

    void handleAmpString(std::string *stringp, char **inDatapp);

    void utf8Encode(std::string *stringp, unsigned long tval);

    /* return an std::string of the next token */
    int32_t getToken( InStream *streamp,
                      std::string *stringp,
                      int *isSinglep=0,
                      int *isQuotedp=0);

    /* copy out characters until we encounter a terminator */
    void copyTo(InStream *streamp, int terminator, std::string *stringp);

    int getTokenNeedsEnd(std::string *stringp) {
        TokenState *tsp;
        for(tsp = _allTokenState.head(); tsp; tsp=tsp->_dqNextp) {
            if (tsp->_tokenName == *stringp)
                return tsp->_needsEnd;
        }

        /* default is true */
        return _defaultNeedsEnd;
    }

    static void printIndent(std::string *resultp, uint32_t level);
};

class Pair {
 public:
    static const int32_t JSON_ERR_EOF = -2;
    static const int32_t JSON_ERR_FORMAT = -1;
    static const int32_t JSON_ERR_SUCCESS = 0;
    void skipNewline(InStream *inp);
    int32_t parseLine(InStream *inp, std::string *keyp, std::string *valuep);
};

#endif /* __JSON_H_ENV_ */
