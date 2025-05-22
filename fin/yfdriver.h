#ifndef _YFDRIVER_H_ENV__
#define _YFDRIVER_H_ENV__ 1

#include <list>
#include <map>
#include <string>

#include <inttypes.h>

class YFDriver {
 public:
    typedef std::map<std::string,double> PriceMap;

    class CachedInfo {
    public:
        std::string _symbol;
        double _yield;
    };

    class CachedYear {
    public:
        PriceMap _prices;
        std::string _symbol;
        std::string _year;

        // Have we loaded the data?
        int _loaded;

        // Keep track of the last price of the year, so if we search
        // for 2025-12-31 and the last value we have is 2025-12-29, we
        // have a price to return that's near in time.  Note that our
        // search function returns the first entry whose key is >= our
        // search value, so we wouldn't return 12-28 if searching for
        // 12-31.  Valid only if _loaded is true.
        double _last_price;

        CachedYear() {
            _loaded = 0;
            _last_price = 0.0;
        }
    };

    typedef std::list<CachedYear *> CacheList;

    typedef std::map<std::string, CachedInfo *> InfoMap;

    int32_t GetYield(std::string symbol, double *yfraction);

    int32_t GetPrice(std::string date, std::string symbol, double *price);

    int32_t GetPriceFromCache(CachedYear *cache, std::string date, double *price);

    void SetVerbose(int verbose) {
        _verbose = verbose;
    }

    CachedYear *GetCacheYear(std::string symbol, std::string date);

    int _verbose;

    CacheList _cache_list;
    InfoMap _info_map;

    YFDriver() {
        _verbose = 0;
    }

    void Reset();

    ~YFDriver() {
        Reset();
    }
};

#endif // _YFDRIVER_H_ENV__
