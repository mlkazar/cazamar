#ifndef _YFDRIVER_H_ENV__
#define _YFDRIVER_H_ENV__ 1

#include <string>

#include <inttypes.h>

class YFDriver {
 public:
    static int32_t GetPrice(std::string date, std::string symbol, double *price);
};

#endif // _YFDRIVER_H_ENV__
