/* to add (http://w3schools.com/xml)
 * &lt; (IN TOKENIZER); also &#abcd; for unicode
 */

#include <stdio.h>
#include <sys/types.h>
#include <inttypes.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "xgml.h"

/* stupid rabbit */
const char *Xgml::_whiteSpacep = " \t\r\n";

/* characters that are single tokens; note that '/' characters are treated as regular
 * text.
 */
const char *Xgml::_singleTokensp = "<>=[]{}()*&^%!@,";

/* isSingle really means that the character is a syntax live character, not quoted
 * in any way.
 */
/* static */ int32_t
Xgml::getToken(char **idatapp, std::string *stringp, int *isSinglep, int *isQuotedp)
{
    int tc;
    int foundAny;
    char *idatap = *idatapp;
    int isSingle = 0;
    int inQuote = 0;
    int sawQuote = 0;
    int charSize;

    stringp->clear();
    foundAny = 0;

    while (1) {
        tc = *idatap;
        if (tc == 0)
            break;

        /* before we do any processing, convert & characters; I don't
         * think they're transformed while in quotes.
         */
        charSize = 1;
        if (tc == '&') {
            /* when parsing XML, '<' and '>' shouldn't trigger
             * recursion; at least we've seen this crap embedded in
             * <description> .... </description> sections of RSS
             * feeds, where what's embedded is really HTML (with <br>
             * described as &lt;br&rt; which triggered us to search
             * for "/br" which isn't there, since HTML unlike XML doesn't
             * require matching tokens.
             */
            if (strncmp(idatap+1, "lt;", 3) == 0) {
                tc = '<';
                charSize = 4;
            }
            else if (strncmp(idatap+1, "gt;", 3) == 0) {
                tc = '>';
                charSize = 4;
            }
            else if (strncmp(idatap+1, "amp;", 4) == 0) {
                tc = '&';
                charSize = 5;
            }
            else if (strncmp(idatap+1, "apos;", 5) == 0) {
                tc = '\'';
                charSize = 6;
            }
            else if (strncmp(idatap+1, "quot;", 5) == 0) {
                tc = '"';
                charSize = 6;
            }
            else if (strncmp(idatap+1, "nbsp;", 5) == 0) {
                tc = ' ';
                charSize = 6;
            }

            /* we matched something */
            stringp->append(1, tc);
            idatap += charSize;
            foundAny = 1;
            continue;
        }

        /* check this after advancing idatap past the single character token */
        if (inQuote) {
            if (tc == '"') {
                idatap++;
                foundAny = 1;
                break;
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
                foundAny = 1;
                sawQuote = 1;
            }
            else if (isSingleToken(tc)) {
                /* single character token; stop on it if we alread have
                 * been accumulating another token, or terminate immediately
                 * if this is the first character in the token.
                 */
                if (foundAny)
                    break;
                stringp->append(1, tc);
                isSingle = 1;
                foundAny = 1;
                idatap += charSize;
                break;
            }
            else {
                /* normal character */
                stringp->append(1, tc);
                foundAny = 1;
            }
        }

        idatap += charSize;
    }

    *idatapp = idatap;
    if (isSinglep)
        *isSinglep = isSingle;
    if (isQuotedp)
        *isQuotedp = sawQuote;
    return 0;
}

void
Xgml::parseFailed(const char *strp)
{
    printf("!Parse failed: %s\n", strp);
}

int32_t
Xgml::parse(char **inDatapp, Node **nodepp)
{
    std::string token;
    std::string nameToken;
    std::string endToken;
    char *inDatap;
    Node *nodep;
    Node *childNodep;
    char *prevDatap;
    int isSingle;
    int isQuoted;
    int32_t code;
    Attr *attrp;
    int tc;
    int nameTokenLen;

    /* the node we're parsing */
    nodep = NULL;
    inDatap = *inDatapp;

    prevDatap = inDatap;
    getToken(&inDatap, &token, &isSingle);

    if (isSingle && token == "<") {
        if (strncmp(inDatap, "?xml", 4) == 0) {
            /* skip <?xml ....> line */
            inDatap = strchr(inDatap, '\n');
            if (!inDatap) {
                parseFailed("no newline in xml line");
                return -1;
            }
            else {
                *inDatapp = inDatap+1;  /* skip past newline */
                return parse(inDatapp, nodepp);
            }
        }
        else if (strncmp(inDatap, "!--", 3) == 0) {
            inDatap += 3;       /* skip above */
            /* read until "-->" */
            while(1) {
                if (*inDatap == 0) {
                    parseFailed("no comment terminator");
                    return -1;
                }
                if (strncmp(inDatap, "-->", 3) == 0) {
                    inDatap += 3;
                    *inDatapp = inDatap;
                    return parse(inDatapp, nodepp);
                }
                inDatap++;
            }
        }
        else if (strncmp(inDatap, "![CDATA[", 8) == 0) {
            /* this is a '<![CDATA[' prefix, treat it as a leaf node with
             * the contents until we see ']]>'
             */
            inDatap += 8;
            nodep = new Node();
            token.clear();
            while(1) {
                tc = *inDatap;
                if (tc == 0 || strncmp(inDatap, "]]>", 3) == 0)
                    break;
#if 0
                /* spec says that cdata doesn't get entity replacements done */
                if (tc == '&') {
                    handleAmpString(&token, &inDatap);
                    continue;
                }
#endif
                token.append(1, tc);
                inDatap++;
            }
            if (tc == 0) {
                parseFailed("bad cdata in xgml parse");
                return -1;
            }
            inDatap += 3;       /* skip terminating substring ']]>' */
            nodep->_name = token;
            nodep->_isLeaf = 1;
            nodep->_needsEnd = 0;
            *inDatapp = inDatap;
            *nodepp = nodep;
            return 0;
        }

        /* parse the name of the node */
        nameToken.clear();

        while(1) {
            /* consume ":" and text tokens */
            prevDatap = inDatap;    /* in case we have to rewind */
            getToken(&inDatap, &token, &isSingle);
            if (!isSingle) {
                nameToken.append(token);
                if (isWhitespace(*inDatap))
                    break;
            }
            else if (token == ":") {
                nameToken.append(token);
                if (isWhitespace(*inDatap))
                    break;
            }
            else {
                /* some random token, including '>', that terminates node name */
                inDatap = prevDatap;
                break;
            }
        }

        /* nameToken now has the name */
        nodep = new Node();
        nodep->_name = nameToken;

        nameTokenLen = (int) nameToken.size();
        if (nameTokenLen >= 2 && nameToken[nameTokenLen-1] == '/') {
            /* we have something like <foo/> which is the equivalent of
             * <foo></foo>.  So create the node and return it.
             */
            getToken(&inDatap, &token, &isSingle);
            if (!isSingle || token != ">") {
                /* didn't find expected '>' */
                parseFailed("Expected '>' after '/' in '<foo/>'");
                return -1;
            }
            *nodepp = nodep;
            *inDatapp = inDatap;
            return 0;
        }

        while(1) {
            /* we've parsed '<foo' so far; now we either see a '>' or an 'attr="foo"' */
            getToken(&inDatap, &token, &isSingle);
            if (token == "/") {
                /* we may have '/>' which means terminate the node */
                getToken(&inDatap, &token, &isSingle);
                if (isSingle && token == ">") {
                    /* early termination of the parsing of this node, since we're
                     * all done.
                     */
                    *nodepp = nodep;
                    *inDatapp = inDatap;
                    return 0;
                }
                else {
                    /* we've got an unexpected character */
                    parseFailed("unexpected terminator");
                    return -1;
                }
            }
            else if (isSingle && token == ">")
                break;
            else if (isSingle) {
                /* something went wrong */
                parseFailed("bad char");
                *inDatapp = inDatap;
                return -1;
            }
            else {
                /* parse 'foo="bar"' */
                attrp = new Attr();
                attrp->_name = token;
                getToken(&inDatap, &token, &isSingle);
                if (!isSingle || (token != "=")) {
                    parseFailed("missing equal sign in attribute");
                    *inDatapp = inDatap;
                    return -1;
                }
                getToken(&inDatap, &token, &isSingle, &isQuoted);
                if (isSingle) {
                    parseFailed("RHS of attr is special char");
                    *inDatapp = inDatap;
                    return -1;
                }
                attrp->_value = token;
                if (isQuoted)
                    attrp->saveQuoted();

                /* append this attribute to the node */
                nodep->_attrs.append(attrp);
            }
        } /* loop over all attribute pairs */

        /* here, we've just read the closing ">" ending the '<foo bar=mode>' */
        if (nameToken[0] == '/') {
            /* this is a </foo> token, so we just return this alone */
            *nodepp = nodep;
            *inDatapp = inDatap;
            return 0;
        }
        if (!getTokenNeedsEnd(&nameToken)) {
            /* the token is followed by a number of words (tokens)
             * until we encounter a new '<foo>' token.
             */
            code = parse(&inDatap, &childNodep);
            if (code) {
                *inDatapp = inDatap;
                delete nodep;
                return code;
            }

            nodep->appendChild(childNodep);
            nodep->_needsEnd = 0;
            *nodepp = nodep;
            *inDatapp = inDatap;
            return 0;
        }
        else {
            /* we recursively call parse, until we read <\nameToken>,
             * adding each recursively read token to our child list.
             */
            endToken = "/" + nameToken;
            nodep->_needsEnd = 1;
            while(1) {
                code = parse(&inDatap, &childNodep);

                /* failed parse */
                if (code) {
                    *inDatapp = inDatap;
                    return code;
                }

                /* found our terminating node; delete it and terminate */
                if (childNodep->_name == endToken) {
                    delete childNodep;
                    *inDatapp = inDatap;
                    *nodepp = nodep;
                    return 0;
                }

                nodep->appendChild(childNodep);
            }
        } /* a normal token */
    } /* processed '<' character at the start of a record */
#ifdef notdef
    else if (isSingle) {
        parseFailed("single character unexpectedly encountered\n");
        *inDatapp = inDatap;
        return -1;
    }
#endif
    else {
        /* we have a character string, so parse until we find a '<' */
        inDatap = prevDatap;    /* rewind to start of text */
        nodep = new Node();
        copyToLt(&inDatap, &token);
        nodep->_name = token;
        nodep->_isLeaf = 1;
        nodep->_needsEnd = 0;
        *inDatapp = inDatap;
        *nodepp = nodep;
        return 0;
    }
}

/* static */ void
Xgml::utf8Encode(std::string *stringp, unsigned long tval)
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
Xgml::handleAmpString(std::string *stringp, char **inDatapp)
{
    char *inDatap = *inDatapp;
    char *endDatap;
    long tval;

    if (inDatap[1] == '#') {
        if (inDatap[2] == 'x') {
            tval = strtol(inDatap+3, &endDatap, 16);
            utf8Encode(stringp, tval);
            inDatap = endDatap+1;   /* skip terminating ';' */
        }
        else if (inDatap[2] >= '0' && inDatap[2] <= '9') {
            tval = strtol(inDatap+2, &endDatap, 10);
            utf8Encode(stringp, tval);
            inDatap = endDatap+1;   /* skip terminating ';' */
        }
    }
    else if (strncmp("lt;", inDatap+1, 3) == 0) {
        stringp->append(1, '<');
        inDatap += 4;
    }
    else if (strncmp("gt;", inDatap+1, 3) == 0) {
        stringp->append(1, '>');
        inDatap += 4;
    }
    else if (strncmp("amp;", inDatap+1, 4) == 0) {
        stringp->append(1, (char) '&');
        inDatap += 5;
    }
    else if (strncmp("apos;", inDatap+1, 5) == 0) {
        stringp->append(1, (char) '\'');
        inDatap += 6;
    }
    else if (strncmp("quot;", inDatap+1, 5) == 0) {
        stringp->append(1, (char) '"');
        inDatap += 6;
    }
    else if (strncmp("nbsp;", inDatap+1, 5) == 0) {
        stringp->append(1, (char) ' ');
        inDatap += 6;
    }
    else {
        stringp->append(1, '&');
        inDatap++;
    }

    *inDatapp = inDatap;
}

void
Xgml::copyToLt(char **datapp, std::string *stringp)
{
    char *inDatap = *datapp;
    int tc;

    stringp->clear();
    while (1) {
        tc = *inDatap;
        /* only stop on real '<' chars, not &lt; version */
        if (tc == 0 || tc == '<')
            break;
        if (tc == '&') {
            handleAmpString(stringp, &inDatap);
            continue;
        }

        stringp->append(1, (char ) tc);
        inDatap++;
    }
    *datapp = inDatap;
}

int32_t
Xgml::unparse(std::string *stringp, Xgml::Node *nodep)
{
    printf("OK, ready to print\n");
    return -1;
}

void
Xgml::setTokenDefault(std::string tokenName, int needsEnd)
{
    TokenState *tsp = new TokenState();
    tsp->_tokenName = tokenName;
    tsp->_needsEnd = needsEnd;
    _allTokenState.append(tsp);
}

/* set the default behavior for a token */
void
Xgml::setTokenDefault(int needsEnd)
{
    _defaultNeedsEnd = needsEnd;
}

void
Xgml::Node::print()
{
    std::string result;

    printInternal(&result, 1, 0);

    printf("%s\n", result.c_str());
}

void
Xgml::Node::unparse(std::string *resultp)
{
    printInternal(resultp, 0, 0);
    resultp->append("\n");
}

void
Xgml::Node::detach() {
    Node *parentp;

    parentp = _parentp;
    if (!parentp) return;

    _parentp = NULL;
    parentp->_children.remove(this);
}

Xgml::Node *
Xgml::Node::dive()
{
    Xgml::Node *childp;

    childp = _children.head();
    if (childp)
        return childp->dive();
    else
        return this;
}

Xgml::Node *
Xgml::Node::searchForChild(std::string cname, int checkData)
{
    Node *childp;
    Node *resultp;

    /* do a breadth first search for child with name 'cname', but
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
Xgml::printIndent(std::string *resultp, uint32_t level)
{
    uint32_t i;

    for(i=0;i<4*level;i++) {
        resultp->append(" ");
    }
}

/* static */ void
Xgml::appendStr(std::string *targetp, std::string newData)
{
    int tc;

    const char *tp = newData.c_str();
    while((tc = *tp++) != 0) {
        if (tc == '"') {
            targetp->append("&quot;");
        }
        else if (tc == '<') {
            targetp->append("&lt;");
        }
        else if (tc == '>') {
            targetp->append("&rt;");
        }
        else if (tc == '&') {
            targetp->append("&amp;");
        }
        else if (tc == '\'') {
            targetp->append("&apos;");
        }
        else {
            targetp->append(1, tc);
        }
    }
}

/* static */ void
Xgml::decodeStr(std::string *targetp, std::string inString)
{
    char *tp = const_cast<char *>(inString.c_str());
    int tc;

    while((tc = *tp) != 0) {
        if (tc == '&') {
            Xgml::handleAmpString(targetp, &tp);
        }
        else {
            targetp->append(1, tc);
            tp++;
        }
    }
}

void
Xgml::Node::printInternal(std::string *resultp, int pretty, uint32_t level)
{
    Node *childp;
    Attr *attrp;

    if (_isLeaf) {
        if (pretty)
            printIndent(resultp, level);
        Xgml::appendStr(resultp, _name);
        if (_attrs.head()) {
            for(attrp = _attrs.head(); attrp; attrp = attrp->_dqNextp) {
                resultp->append(" ");
                resultp->append(attrp->_name);
                resultp->append("=");
                if (attrp->_saveQuoted) {
                    resultp->append("\"");
                    Xgml::appendStr(resultp, attrp->_value);
                    resultp->append("\"");
                }
                else {
                    Xgml::appendStr(resultp, attrp->_value);
                }
            }
        }
        if (pretty)
            resultp->append("\n");
    }
    else {
        if (pretty)
            printIndent(resultp, level);
        resultp->append("<");
        resultp->append(_name);
        if (_attrs.head()) {
            for(attrp = _attrs.head(); attrp; attrp = attrp->_dqNextp) {
                resultp->append(" ");
                resultp->append(attrp->_name);
                resultp->append("=");
                if (attrp->_saveQuoted) {
                    resultp->append("\"");
                    Xgml::appendStr(resultp, attrp->_value);
                    resultp->append("\"");
                }
                else {
                    Xgml::appendStr(resultp, attrp->_value);
                }
            }
        }
        resultp->append(">");
        if (pretty)
            resultp->append("\n");
        for(childp = _children.head(); childp; childp=childp->_dqNextp) {
            childp->printInternal(resultp, pretty, level+1);
        }
        if (_needsEnd) {
            if (pretty)
                printIndent(resultp, level);
            resultp->append("</");
            resultp->append(_name);
            resultp->append(">");
            if (pretty)
                resultp->append("\n");
        }
        else {
            if (_children.count() != 1) {
                printf("*WRONG # OF CHILDREN\n");
            }
        }
    }
}

void
Xgml::Attr::init(const char *namep, const char *valuep)
{
    _parentp = NULL;
    _name = std::string(namep);
    _value = std::string(valuep);
}
