#include <string>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "utils.h"

namespace VanOfx {

// Flip date from MM/DD/YYYY to YYYY-MM-DD
std::string
FlipDate(std::string us_date) {
    const char *input;
    std::string eu_date;
    input = us_date.c_str();
    eu_date = (std::string(input+6, 4) +
               "-" + std::string(input, 2) +
               "-" + std::string(input+3,2));
    return eu_date;
}


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

int
GetYN(std::string prompt) {
    char tbuffer[128];
    while(1) {
        printf("%s", prompt.c_str());
        char *tp = fgets(tbuffer, sizeof(tbuffer), stdin);
        if (!tp) {
            return 0;
        }
        if (strncasecmp(tbuffer, "y", 1) == 0 || strncasecmp(tbuffer,"yes", 3) == 0)
            return 1;
        else if (strncasecmp(tbuffer, "n", 1) == 0 || strncasecmp(tbuffer,"no", 2) == 0)
            return 0;
        printf("Please answer yes/y or no/n\n");
    }
}

std::string
TimeToDateStr(long secs) {
    struct tm local_tm;
    char tbuffer[64];
    localtime_r(&secs, &local_tm);
    snprintf(tbuffer, sizeof(tbuffer), "%4d-%02d-%02d",
            local_tm.tm_year+1900,
            local_tm.tm_mon+1,
            local_tm.tm_mday);
    return std::string(tbuffer);
}

std::string
RemoveEdgeSpaces(std::string input) {
    int head_count = 0;
    int tail_count = 0;
    int saw_nonspace = 0;
    const char *tp;
    int tc;
    tp = input.c_str();
    while(1) {
        tc = *tp++;
        if (tc == 0) {
            break;
        } else if (tc != ' ') {
            saw_nonspace = 1;
            tail_count = 0;
        } else {
            if (saw_nonspace) {
                tail_count++;
            } else {
                head_count++;
            }
        }
    }
    if (!saw_nonspace)
        return std::string("");
    int tlen = strlen(input.c_str());
    return input.substr(head_count, tlen - head_count - tail_count);
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

    result = strcasestr(const_cast<char *>(id.c_str()),
                        const_cast<char *>(pattern.c_str()));
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
    return (c == ' ' || c == '\t' || c == '\n');
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
            if (current_token.size() > 0) {
                (*vec).push_back(current_token);
                current_token.erase();
            }
            break;
        } else  if (tc == ',') {
            // Complete the token
            (*vec).push_back(current_token);
            current_token.erase();
            pending_space = 0;
        } else if (IsWhiteSpace(tc)) {
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
    printf("Qualified divs=%'.2f\n"
           "Regular divs=%'.2f\n"
           "Unrealized CG=%'.2f\n"
           "Realized CG=%'.2f\n"
           "Short-term dist=%'.2f\n"
           "Long-term dist=%'.2f\n"
           "Tax free divs=%'.2f\n"
           "IRA divs=%'.2f\n"
           "IRA gains=%'.2f\n",
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
