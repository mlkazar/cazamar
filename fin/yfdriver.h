#ifndef _YFDRIVER_H_ENV__
#define _YFDRIVER_H_ENV__ 1

#include <map>
#include <string>

#include <inttypes.h>

class YFDriver {
 public:
    typedef std::map<std::string,double> PriceMap;
    int32_t GetPrice(std::string date, std::string symbol, double *price);

    std::string _cached_symbol;
    PriceMap _cached_prices;
};

#endif // _YFDRIVER_H_ENV__
