#include <string>

#include <string.h>
#include <time.h>

#include "utils.h"

namespace VanOfx {

// foo

// Date strings are yyyy-mm-dd
int
DateStrCmp(std::string s1, std::string s2)
{
    std::string p1;
    std::string p2;

    // Compare years
    p1 = s1.substr(0,4);
    p2 = s2.substr(0,4);
    if (p1<p2)
        return -1;
    else if (p1>p2)
        return 1;

    // compare months
    p1=s1.substr(5,2);
    p2=s2.substr(5,2);
    if (p1<p2)
        return -1;
    else if (p1>p2)
        return 1;
    
    // compare days
    p1=s1.substr(8,2);
    p2=s2.substr(8,2);
    if (p1<p2)
        return -1;
    else if (p1>p2)
        return 1;

    return 0;
}

std::string
CurrentDateStr(long seconds_adjust) {
    time_t now;

    now = time(0) + seconds_adjust;
    return TimeToDateStr(now);
}

std::string
TimeToDateStr(long secs) {
    struct tm local_tm;
    char tbuffer[16];
    localtime_r(&secs, &local_tm);
    snprintf(tbuffer, sizeof(tbuffer), "%4d-%02d-%02d",
            local_tm.tm_year+1900,
            local_tm.tm_mon+1,
            local_tm.tm_mday);
    return std::string(tbuffer);
}

long
DateStrToTime(std::string date_str) {
    struct tm local_tm;
    memset(&local_tm, 0, sizeof(local_tm));
    local_tm.tm_year = stoi(date_str.substr(0,4)) - 1900;
    local_tm.tm_mon = stoi(date_str.substr(5,2)) - 1;
    local_tm.tm_mday = stoi(date_str.substr(8,2));
    return mktime(&local_tm);
}

int
IsTranType(std::string id, std::string pattern) {
    char *result;

    result = strcasestr(id.c_str(), pattern.c_str());
    return (result != nullptr);
}

char
FirstNonblank(char *datap) {
    while(1) {
        char tc = *datap++;
        if (tc != ' ' && tc != '\t') {
            // This will return the terminating 0 when there is no
            // first non-blank character
            return tc;
        }
    }
}

std::string
RemoveLeadingZeroes(std::string input) {
    const char *datap;
    int tc;

    datap = input.c_str();
    while(1) {
        tc = *datap;
        if (tc == 0)
            break;
        if (tc != '0')
            break;
        datap++;
    }
    return std::string(datap);
}

int
CountDelim(char delim, char *datap) {
    int rval = 0;
    while (1) {
        char tc = *datap;
        if (tc == delim)
            rval++;
        if (tc == 0)
            break;
        datap++;
    }
    return rval;
}

int
IsWhiteSpace(char c) {
    return (c == ' ' || c == '\t');
}

int
ParseTokens(char delim, char *datap, std::vector<std::string> *vec) {
    // Parse "x,y,z," into three separate strings stored in a vector.
    // Always has a terminating ','.  Also we suppress any leading or
    // terminating spaces.
    int pending_space = 0;
    
    std::string current_token;
    while(1) {
        char tc = *datap++;
        if (tc == 0) {
            break;
        } else  if (tc == ',') {
            // Complete the token
            (*vec).push_back(current_token);
            current_token.erase();
            pending_space = 0;
        } else if (IsWhiteSpace(tc)) {
            if (pending_space) {
                current_token.append(1, tc);
            }
            pending_space = 1;
        } else {
            // regular character.
            if (pending_space) {
                current_token.append(1, ' ');
                pending_space = 0;
            }
            current_token.append(1, tc);
        }
    } // loop over all characters
    return 0;
}

int
ItemsContains(std::vector<std::string> *items, std::string key) {
    std::vector<std::string>::iterator it;
    for(it = items->begin(); it != items->end(); it++) {
        if (*it == key)
            return 1;
    }
    return 0;
}

int32_t
ItemsIndex(std::vector<std::string> *items, std::string key) {
    std::vector<std::string>::iterator it;
    int32_t ix;
    for(it = items->begin(), ix = 0; it != items->end(); it++, ix++) {
        if (*it == key)
            return ix;
    }
    return -1;
}

void
PrintGain(Gain *g) {
    printf("Gains:\n"
           "Qualified divs=%.2f\n"
           "Regular divs=%.2f\n"
           "Unrealized CG=%.2f\n"
           "Realized CG=%.2f\n"
           "Short-term dist=%.2f\n"
           "Long-term dist=%.2f\n"
           "Tax free divs=%.2f\n"
           "IRA divs=%.2f\n"
           "IRA gains=%.2f\n",
           g->_qualified_divs,
           g->_regular_divs,
           g->_unrealized_cg,
           g->_realized_cg,
           g->_short_term_dist,
           g->_long_term_dist,
           g->_tax_free_divs,
           g->_ira_divs,
           g->_ira_gains);
}

} // name space
