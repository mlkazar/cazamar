#include "jwt.h"
#include "osptypes.h"

int
Jwt::doChar(int tc)
{
    if (tc == '=')
        return 0;
    else if (tc >= 'A' && tc <= 'Z')
        return tc - 'A';
    else if (tc >= 'a' && tc <= 'z')
        return tc - 'a' + 26;
    else if (tc >= '0' && tc <= '9')
        return tc - '0' + 52;
    else if (tc == '+')
        return 62;
    else if (tc == '/')
        return 63;
    else 
        return 0;       /* should return failure */
}

std::string
Jwt::decode64(std::string ins)
{
    char *tp;
    char localBuffer[3];
    char tc;
    char inc;
    uint32_t inLen;
    std::string rval;

    /* consume groups of 4 bytes (24 bits), generating 3 characters from each.  If we see
     * a '=' character, it is a zero padding that doesn't turn into new characters.
     * If we have one equal sign, we have two bytes and 2 bits of zero.  If we have two equal
     * signs, we have 8 bits with 4 bits of 0.  We shouldn't have 3 equal signs in a block.
     */
    inLen = ins.length();
    tp = const_cast<char *>(ins.c_str());

    while(inLen >= 4) {
        /* first byte: all 6 bits go to leftmost 6 bits of byte 0 */
        inc = *tp++;
        tc = doChar(inc);
        localBuffer[0] = tc<<2;

        /* second byte: first 2 bits go into rightmost 2 bits of byte 0,
         * next 4 bits go into leftmost 4 bits of byte 1.
         */
        inc = *tp++;
        tc = doChar(inc);
        localBuffer[0] |= (tc >> 4);
        localBuffer[1] = (tc << 4);

        /* third byte: first 4 bits go into rightmost 4 bits of byte 1, last 2 bits
         * go into leftmost 2 bits of byte 2.  But if an equal sign, then byte 0
         * is the only byte this chunk decodes.
         */
        inc = *tp++;
        tc = doChar(inc);
        if (inc == '=') {
            /* one character is valid */
            rval.append(1, localBuffer[0]);
            return rval;
        }
        localBuffer[1] |= (tc >> 2);
        localBuffer[2] = (tc << 6);

        /* fourth byte: all 6 bits go into rightmost 6 bits of byte 2, but if an equal
         * sign, bytes 0 and 1 are the return value.
         */
        inc = *tp++;
        tc = doChar(inc);
        if (inc == '=') {
            rval.append(localBuffer, 2);
            return rval;
        }
        localBuffer[2] |= tc;

        /* add the 3 decoded bytes */
        rval.append(localBuffer, 3);
        inLen -= 4;
    }
    return rval;
}

int32_t
Jwt::decode(std::string token, std::string *resultp)
{
    int dotsSeen;
    int dotPos1;
    int dotPos2;
    const char *tp;
    const char *firstp;
    int tc;
    int ix;
    std::string middle;
        
    dotsSeen = 0;
    /* if a real JWT token, we should have two '.' characters, dividing the
     * token into three parts.  The middle one decodes into json text as 
     * a base 64 string.  But if the dots aren't present, this is a bogus
     * token.
     */
    firstp = tp = token.c_str();
    ix = 0;
    while(1) {
        tc = *tp++;
        if (tc == 0)
            break;
        if (tc == '.') {
            dotsSeen++;
            if (dotsSeen == 1) 
                dotPos1 = ix;
            else if (dotsSeen == 2)
                dotPos2 = ix;
        }
        ix++;
    }
    if (dotsSeen != 2)
        return -1;

    /* a.b.c would have dotPos1==1, dotPos2=3 */
    middle = std::string(firstp+dotPos1+1, dotPos2-dotPos1-1);
    *resultp = decode64(middle);
    return 0;
}
