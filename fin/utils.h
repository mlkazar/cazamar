#ifndef _UTILS_H_ENV__
#define _UTILS_H_ENV__ 1

#include <vector>
#include <string>

#include <inttypes.h>

#include "vanofx.h"

namespace VanOfx {
    int32_t CountDelim(char delim, char *datap);

    char FirstNonblank(char *datap);

    std::string RemoveLeadingZeroes(std::string input);

    int ParseTokens(char delim, char *datap, std::vector<std::string> *vec);

    int IsTranType(std::string identifier, std::string pattern);

    int ItemsContains(std::vector<std::string> *items, std::string key);

    int32_t ItemsIndex(std::vector<std::string> *items, std::string key);

    std::string CurrentDateStr(long seconds_adjust);

    std::string TimeToDateStr(long secs);

    long DateStrToTime(std::string date_str);

    int DateStrCmp(std::string s1, std::string s2);

    void PrintGain(Gain *g);
};

#endif // _UTILS_H_ENV__
