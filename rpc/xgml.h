#ifndef __XGML_H_ENV__
#define  __XGML_H_ENV__ 1

#include <sys/types.h>

#include <string>
#ifdef __linux__
#include <strings.h>
#endif

#include "dqueue.h"

/* class for parsing XML and XML-like (including some SGML) data streams.
 * XML requires matching tags, but many SGML tags are terminated by 
 * newline characters.  By default, an unrecognized tag is treated as requiring
 * termination (a la XML).  However, behavior for tags can be provided by
 * an override, and the caller can also provide a default for child objects.
 * Defaults for child objects are always overridden by explicit defaults for 
 * objects found within the parent having the child object defaults.
 */
class Xgml {
    /* this stuff is whitespace */
    static const char *_whiteSpacep;

    /* characters that are single tokens */
    static const char *_singleTokensp;

 public:

    class Node;

    class TokenState {
    public:
        TokenState *_dqNextp;
        TokenState *_dqPrevp;
        std::string _tokenName;
        int _needsEnd;
    };

    class Attr {
    public:
        std::string _name;
        std::string _value;     /* with the quotes removed */
        Attr *_dqNextp;
        Attr *_dqPrevp;
        Node *_parentp;
        uint8_t _saveQuoted;

        Attr() {
            _dqNextp = NULL;
            _dqPrevp = NULL;
            _saveQuoted = 0;
        }

        void saveQuoted() {
            _saveQuoted = 1;
        }

        void init(const char *namep, const char *valuep);
    };

    class Node {
    public:
        std::string _name;
        uint8_t _isLeaf;                /* leaf string */
        uint8_t _needsEnd;              /* had an </end> style record */
        Node *_dqNextp;                 /* next and prev in child list */
        Node *_dqPrevp;                 /* next and prev in child list */
        Node *_parentp;
        dqueue<Attr> _attrs;
        dqueue<Node> _children;         /* list of all children */

        Node() {
            _isLeaf = 0;
            _needsEnd = 1;
            _dqNextp = NULL;
            _dqPrevp = NULL;
            _parentp = NULL;
        }

        ~Node() {
            Node *childp;
            Node *nchildp;
            Attr *attrp;
            Attr *nattrp;

            for (childp = _children.head(); childp; childp=nchildp) {
                nchildp = childp->_dqNextp;
                delete childp;
            }

            for (attrp = _attrs.head(); attrp; attrp=nattrp) {
                nattrp = attrp->_dqNextp;
                delete attrp;
            }
        }

        void init(const char *namep, int needsEnd, int isLeaf) {
            _name = namep;
            _isLeaf = isLeaf;
            _needsEnd = needsEnd;
        }

        void appendChild(Node *childNodep) {
            _children.append(childNodep);
            childNodep->_parentp = this;
        }

        void appendAttr(Attr *childAttrp) {
            _attrs.append(childAttrp);
            childAttrp->_parentp = this;
        }

        void print();

        void unparse(std::string *resultp);

        void printInternal(std::string *resultp, int pretty, uint32_t level);

        void printToCPP(std::string *resultp) {
            printInternal(resultp, 1, 0);
        }

        void detach();

        Node *dive();

        Node *searchForChild(std::string name, int checkData = 0);
    };

    int _defaultNeedsEnd;
    dqueue<TokenState> _allTokenState;

    Xgml() {
        /* no special initialization needed */
        _defaultNeedsEnd = 1;
    }

    ~Xgml() {
        /* no doubt will need this */
    }

    /* undefined on what happens with c == 0 */
    static int isWhitespace(int c) {
        return (index(_whiteSpacep, c) != NULL);
    }

    /* token */
    static int isSingleToken(int c) {
        return (index(_singleTokensp, c) != NULL);
    }

    static void appendStr(std::string *resultp, std::string data);

    static void decodeStr(std::string *targetp, std::string inString);

    void setTokenDefault(std::string tokenName, int needsEnd);

    /* set the default behavior for a token */
    void setTokenDefault(int needsEnd);

    int32_t parse(char **inDatapp, Node **nodepp);

    void parseFailed(const char *strp);

    /* return an std::string with the results of emitting the
     * data in the xgml object.
     */
    int32_t unparse(std::string *stringp, Node *nodep);

    static void handleAmpString(std::string *stringp, char **inDatapp);

    static void utf8Encode(std::string *stringp, unsigned long tval);

    /* return an std::string of the next token */
    int32_t getToken( char **idatapp,
                      std::string *stringp,
                      int *isSinglep=0,
                      int *isQuotedp=0);

    /* copy out characters until we encounter a terminator */
    void copyToLt(char **datapp, std::string *stringp);

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

#endif /* __XGML_H_ENV__ */
