#include <stdio.h>
#include <sys/types.h>
#include <inttypes.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "json.h"

/* stupid rabbit */
const char *Json::_whiteSpacep = " \t\r\n";
const char *Json::_blankSpacep = " \t";

/* characters that are single tokens; note that '/' characters are treated as regular
 * text.
 */
const char *Json::_singleJsonTokensp = "=:[]{}()*&%#!@,\'";

/* static */ int32_t
Json::getToken(InStream *streamp, std::string *stringp, int *isSinglep, int *isQuotedp)
{
    int tc;
    int foundAny;
    int isSingle = 0;
    int inQuote = 0;
    int sawQuote = 0;
    uint32_t i;
    uint32_t val;

    stringp->clear();
    foundAny = 0;

    while (1) {
        tc = streamp->top();
        if (tc == 0) {
            /* if nothing comes back, return EOF */
            if (stringp->length() == 0)
                return -1;
            break;
        }

        /* check this after advancing idatap past the single character token */
        if (inQuote) {
            if (tc == '"') {
                streamp->next();
                foundAny = 1;
                break;
            }
            else if (tc == '\\') {
                streamp->next();       /* skip '\' */
                tc = streamp->top();
                if (tc == '\\' || tc == '/' || tc == '"') /* these are included directly */
                    stringp->append(1, tc);
                else if (tc == 'n')
                    stringp->append(1, '\n');
                else if (tc == 'r')
                    stringp->append(1, '\r');
                else if (tc == 'b')
                    stringp->append(1, '\b');
                else if (tc == 't')
                    stringp->append(1, '\t');
                else if (tc == 'f')
                    stringp->append(1, '\f');
                else if (tc == 'u') {
                    /* next 4 digits are a hex number for a unicode character, which may
                     * be several UTF8 characters.
                     */
                    val = 0;
                    for(i=0;i<4;i++) {
                        val <<= 8;
                        streamp->next();
                        tc = streamp->top();
                        val += tc;
                    }
                    utf8Encode(stringp, val);
                }
            }
            else {
                stringp->append(1, tc);
            }
        }
        else {
            if (isWhitespace(tc)) {
                /* whitespace after a real character means we're done
                 * with this token, otherwise we just skip the
                 * whitespace and continue.
                 */
                if (foundAny)
                    break;
            }
            else if (tc == '"') {
                inQuote = 1;
                sawQuote = 1;
                foundAny = 1;
            }
            else if (isSingleJsonToken(tc)) {
                /* single character token; stop on it if we alread have
                 * been accumulating another token, or terminate immediately
                 * if this is the first character in the token.
                 */
                if (foundAny)
                    break;
                stringp->append(1, tc);
                isSingle = 1;
                foundAny = 1;
                streamp->next();
                break;
            }
            else {
                /* normal character */
                stringp->append(1, tc);
                foundAny = 1;
            }
        }

        streamp->next();
    }

    if (isSinglep)
        *isSinglep = isSingle;
    if (isQuotedp)
        *isQuotedp = sawQuote;
    return 0;
}

void
Json::parseFailed(const char *strp)
{
    printf("!Parse failed: %s\n", strp);
}

/* A JSON pair is a pair of objects, the first of which is a JSON
 * string object, or a complex JSON object, separated by a ':'.  A
 * complex JSON object is an array of JSON objects, or a set of JSON
 * objects.  A JSON value is a token or a JSON complex object.
 */
int32_t
Json::parseJsonPair(InStream *streamp, Node **nodepp)
{
    std::string token;
    std::string key;
    int isSingle;
    int isQuoted;
    int code = 0;
    Json::Node *nameNodep;
    Json::Node *nodep;

    getToken(streamp, &token, &isSingle, &isQuoted);
    if (!isQuoted) {
        printf("Unquoted key for '%s'\n", token.c_str());
        parseFailed("unquoted key");
        return -1;
    }
    if (!isSingle) {
        /* parse "name" : <jsonValue> */
        key = token;

        /* next, we should see a ":" */
        getToken(streamp, &token, &isSingle);
        if (token != ":") {
            parseFailed("missing ':' token");
            return -1;
        }

        /* now parse the object */
        code = parseJsonValue(streamp, &nodep);
        if (code)
            return code;

        /* code is 0, add a name node with the name */
        nameNodep = new Json::Node();
        nameNodep->_name = key;
        nameNodep->_isQuoted = 1;
        nameNodep->_isNamed = 1;
        nameNodep->appendChild(nodep);
        *nodepp = nameNodep;
    }
    else {
        /* don't expect any other single characters here */
        parseFailed("unexpected single char");
        code = -1;
    }

    return code;
}

/* external function: parse a quoted string or complex object */
int32_t
Json::parseJsonValue(InStream *streamp, Node **nodepp)
{
    std::string token;
    std::string value;
    int isSingle;
    int isQuoted;
    Json::Node *nodep;
    int32_t code;

    code = getToken(streamp, &token, &isSingle, &isQuoted);
    if (code < 0) {
        return code;
    }

    if (!isSingle) {
        /* parse "name"; code is 0, add an attr with the name */
        nodep = new Json::Node();
        nodep->_name = token;
        nodep->_isLeaf = 1;
        nodep->_isQuoted = isQuoted;
        *nodepp = nodep;
        return 0;
    }
    else if (token == "[" || token == "{") {
        code = parseJsonComplex(streamp, &token, nodepp);
        return code;
    }
    else {
        parseFailed("json: bad token");
        return -1;
    }
}

int32_t
Json::parseJsonComplex(InStream *streamp, std::string *firstTokenp, Node **nodepp)
{
    std::string token;
    Json::Node *parentp;
    Json::Node *childp;
    int32_t code;
    int isSingle;
    int tc;

    parentp = new Json::Node();

    if (*firstTokenp == "[") {
        parentp->_name = std::string("_Array");
        parentp->_isArray = 1;
    }
    else if (*firstTokenp == "{") {
        parentp->_name = std::string("_Set");
        parentp->_isStruct = 1;
    }
    else {
        parseFailed("json: bad token");
        return -1;
    }

    /* skip whitespace and then peek at next char; if it is a '}'
     * or a ']', we're not going to parse, we're going to
     * terminate.  We do this check here so we can handle empty
     * lists.
     */
    while(isWhitespace(streamp->top())) {
        streamp->next();
    }
    tc = streamp->top();

    if (tc == '}' || tc == ']') {
        /* consume the terminating character */
        streamp->next();
        *nodepp = parentp;
        return 0;
    }

    while(1) {
        if (parentp->_isArray)
            code = parseJsonValue(streamp, &childp);
        else
            code = parseJsonPair(streamp, &childp);
        if (code)
            return code;

        parentp->appendChild(childp);

        getToken(streamp, &token, &isSingle);
        if (token == "]" || token == "}") {
            /* we're at the end */
            *nodepp = parentp;
            return 0;
        }
        if (token != ",") {
            /* bad terminator; includes EOF (empty string) */
            parseFailed("json: bad token");
            return -1;
        }
    }
}

void
Json::utf8Encode(std::string *stringp, unsigned long tval)
{
    if (tval < 128)
        stringp->append(1, tval);
    else if (tval < 0x800) {
        stringp->append(1, 0xC0+(tval>>6));
        stringp->append(1, 0x80+(tval&0x3F));
    }
    else if (tval < 0x10000) {
        stringp->append(1, 0xE0+(tval>>12));
        stringp->append(1, 0x80+((tval>>6)&0x3F));
        stringp->append(1, 0x80+(tval&0x3F));
    }
}

void
Json::setTokenDefault(std::string tokenName, int needsEnd)
{
    TokenState *tsp = new TokenState();
    tsp->_tokenName = tokenName;
    tsp->_needsEnd = needsEnd;
    _allTokenState.append(tsp);
}

/* set the default behavior for a token */
void
Json::setTokenDefault(int needsEnd)
{
    _defaultNeedsEnd = needsEnd;
}

void
Json::Node::print()
{
    std::string result;

    printInternal(&result, 1, 0, 0);

    printf("%s\n", result.c_str());
}

void
Json::Node::unparse(std::string *resultp)
{
    printInternal(resultp, 0, 0, 0);
    resultp->append("\n");
}

void
Json::Node::detach() {
    Node *parentp;

    parentp = _parentp;
    if (!parentp) return;

    _parentp = NULL;
    parentp->_children.remove(this);
}

Json::Node *
Json::Node::dive()
{
    Json::Node *childp;

    childp = _children.head();
    if (childp)
        return childp->dive();
    else
        return this;
}

Json::Node *
Json::Node::searchForChild(std::string cname, int checkData)
{
    Node *childp;
    Node *resultp;

    /* do a depth first search for child with name 'cname', but
     * ignore leaves, whose names are just data, if checkData is false
     * (the default)
     */
    if ((checkData || !_isLeaf) && _name == cname)
        return this;

    for(childp = _children.head(); childp; childp=childp->_dqNextp) {
        resultp = childp->searchForChild(cname, checkData);
        if (resultp)
            return resultp;
    }

    return NULL;
}

void
Json::printIndent(std::string *resultp, uint32_t level)
{
    uint32_t i;

    for(i=0;i<4*level;i++) {
        resultp->append(" ");
    }
}

/* static */ void
Json::Node::appendStr(std::string *targetp, std::string newData)
{
    int tc;

    const char *tp = newData.c_str();
    while((tc = *tp++) != 0) {
        if (tc == '"') {
            targetp->append("\\\"");
        }
        else {
            targetp->append(1, tc);
        }
    }
}

void
Json::Node::printInternal(std::string *resultp, int pretty, uint32_t level, int addComma)
{
    Node *childp;

    if (_isLeaf) {
        if (pretty)
            printIndent(resultp, level);
        if (_isQuoted)
            resultp->append("\"");
        appendStr(resultp, _name);
        if (_isQuoted)
            resultp->append("\"");
        if (addComma)
            resultp->append(",");
        if (pretty)
            resultp->append("\n");
    }
    else if (_isStruct) {
        if (pretty)
            printIndent(resultp, level);
        resultp->append("{\n");
        for(childp = _children.head(); childp; childp=childp->_dqNextp) {
            childp->printInternal(resultp,
                                  pretty,
                                  level+1,
                                  (childp->_dqNextp? 1 : 0));
        }
        if (pretty)
            printIndent(resultp, level);
        resultp->append("}");
        if (addComma)
            resultp->append(",");
        resultp->append("\n");
    }
    else if (_isArray) {
        if (pretty)
            printIndent(resultp, level);
        resultp->append("[\n");
        for(childp = _children.head(); childp; childp=childp->_dqNextp) {
            childp->printInternal(resultp,
                                  pretty,
                                  level+1,
                                  (childp->_dqNextp? 1 : 0));
        }
        if (pretty)
            printIndent(resultp, level);
        resultp->append("]");
        if (addComma)
            resultp->append(",");
        resultp->append("\n");
    }
    else {
        /* not array, leaf or struct, so it is a named pair */
        if (pretty)
            printIndent(resultp, level);
        resultp->append("\"");          /* these are always quoted strings */
        appendStr(resultp, _name);
        childp = _children.head();
        if (_children.count() != 1) {
            printf("bad count for pair\n");
        }

        if (childp->_isLeaf) {
            resultp->append("\" : ");
            childp->printInternal(resultp, pretty, /* level */ 0, addComma);
        }
        else {
            resultp->append("\" :\n");      /* close quote and print ':' */
            childp->printInternal(resultp, pretty, level+1, addComma);
        }
    }
}

/* one of the external functions */
int32_t
Json::parseJsonChars(char **inDatapp, Json::Node **nodepp)
{
    InStreamString inStream(*inDatapp);
    int32_t code;

    code = parseJsonValue(&inStream, nodepp);
    if (code == 0) {
        *inDatapp = inStream.final();   /* get final position of string and return it */
    }
    else {
        *nodepp = NULL;
    }   

    return code;
}

/* external function; note that the FILE * gets closed and freed on both error
 * and success returns.
 */
int32_t
Json::parseJsonFile(FILE *filep, Json::Node **nodepp)
{
    int32_t code;
    InStreamFile inStream(filep);

    code = parseJsonValue(&inStream, nodepp);
    if (code) {
        *nodepp = NULL;
    }

    return code;
}

void
Pair::skipNewline(InStream *inp)
{
    int tc;
    while((tc = inp->top()) != 0) {
        if (tc == '\r' || tc == '\n') {
            inp->next();
            continue;
        }
        else
            break;
    }
}

int32_t
Pair::parseLine(InStream *inp, std::string *keyp, std::string *valuep)
{
    char tc;
    int parsingKey;

    (*keyp).erase();
    (*valuep).erase();

    parsingKey = 1;
    while((tc = inp->top()) != 0) {
        if (parsingKey) {
            if (tc == '\r' || tc == '\n') {
                skipNewline(inp);
                return JSON_ERR_FORMAT;
            }
            else if (tc == ':') {
                parsingKey = 0;
            }
            else if (Json::isBlankspace(tc)) {
                /* nothing to do but skip the character below */
            }
            else {
                /* non-white space character */
                keyp->append(1, tc);
            }
        }
        else {
            /* passed the key part of the line */
            if (Json::isBlankspace(tc)) {
                /* nothing to do */
            }
            else if (tc == '\r' || tc == '\n') {
                /* consume any trailing newline-like stuff */
                skipNewline(inp);
                /* and we're done */
                return JSON_ERR_SUCCESS;
            }
            else {
                valuep->append(1, tc);
            }
        } /* not parsingKey */
        inp->next();
    } /* while loop over all */

    /* we get here if we hit EOF */
    return JSON_ERR_EOF;
}

InStreamFile::InStreamFile(FILE *filep)
{
    _inFilep = filep;

    fillBuffer();
}

void
InStreamFile::fillBuffer()
{
    int tc;

    tc = fgetc(_inFilep);
    if (tc < 0)
        _buffer = 0;
    else
        _buffer = tc;
}

int
InStreamFile::top()
{
    return _buffer;
}

void
InStreamFile::next()
{
    fillBuffer();
}

