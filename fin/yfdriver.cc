#include <cstring>
#include <string>

#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>

#include "yfdriver.h"

YFDriver::CachedYear *YFDriver::GetCacheYear(std::string symbol, std::string date) {
    CacheList::iterator cit;
    std::string year = date.substr(0,4);
    CachedYear *cache;

    for(cit = _cache_list.begin(); cit != _cache_list.end(); ++cit) {
        if ((*cit)->_year == year && (*cit)->_symbol == symbol) {
            return *cit;
        }
    }

    // Create new one
    cache = new CachedYear();
    cache->_year = year;
    cache->_symbol = symbol;
    _cache_list.push_back(cache);
    return cache;
}

void
YFDriver::Reset() {
    CachedYear *cache;
    CacheList::iterator it;

    while(!_cache_list.empty()) {
        // get first element in list
        it = _cache_list.begin();
        cache = *it;

        // and remove it
        _cache_list.pop_front();
        delete cache;
    }

    InfoMap::iterator mit;
    CachedInfo *info;
    while(!_info_map.empty()) {
        // get first element in list
        mit = _info_map.begin();
        info = mit->second;

        // and remove it
        _info_map.erase(mit);
        delete info;
    }
}

int32_t
YFDriver::GetPriceFromCache(CachedYear *cache, std::string date, double *price) {
    PriceMap::iterator it;

    it = cache->_prices.lower_bound(date);
    if (it != cache->_prices.end()) {
        *price = it->second;
        if (_verbose)
            printf(" cache hit for date %s\n", it->first.c_str());
    } else {
        if (_verbose) {
            printf(" can't find date lower bound '%s' in year '%s'\n", date.c_str(),
                   cache->_year.c_str());
        }
        *price = cache->_last_price;
    }

    return 0;
}

int32_t
YFDriver::GetPrice(std::string date, std::string symbol, double *price) {
    FILE *tfile;
    char tbuffer[100];
    char command_buffer[100];
    char date_buffer[12];
    std::string tdate;
    int did_any=0;
    int32_t code;
    double tprice;
    CachedYear *cache;
    std::string year;

    if (symbol == "None" || symbol.size() == 0) {
        if (_verbose) {
            printf("YF: GetPrice returning 1 for symbol %s\n", symbol.c_str());
        }
        *price = 1.0;
        return 0;
    }

    year = date.substr(0,4);

    // Check cache
    cache = GetCacheYear(symbol, date);

    PriceMap::iterator it;
    if (_verbose) {
        printf("YF: symbol name match in cache %s date=%s\n",
               symbol.c_str(), date.c_str());
    }

    // Sometimes there are gaps in the dates of the returned data.
    // If that happens, lower bound will return first date after
    // the one we're searching for.
    if (cache->_loaded) {
        code = GetPriceFromCache(cache, date, price);
        return code;
    }

    snprintf(command_buffer, sizeof(command_buffer),
             "prices prices %s-01-01 %s 2>/dev/null", year.c_str(), symbol.c_str());
    if (_verbose)
        printf("Cache miss, loading data with cmd line '%s'\n", command_buffer);
    tfile = popen(command_buffer, "r");
    while(1) {
        fgets(tbuffer, sizeof(tbuffer), tfile);
        if (strncmp(tbuffer, "DONE", 4) == 0) {
            if (_verbose)
                printf(" cache load done\n");
            break;
        }
        code = sscanf(tbuffer, "%10s %lf", date_buffer, &tprice);
        if (code != 2) {
            printf("Syntax error on response from price command\n");
            printf("Line is '%s'\n", tbuffer);
            break;
        }
        tdate = std::string(date_buffer);
        if (_verbose)
            printf(" received date=%s price=%lf\n", date_buffer, tprice);
        cache->_prices[tdate] = tprice;
        if (!did_any) {
            did_any = 1;
        }
    }
    cache->_symbol = symbol;
    cache->_year = year;
    cache->_last_price = tprice;
    cache->_loaded = 1;

    if (did_any) {
        (void) GetPriceFromCache(cache, date, price);
    }

    pclose(tfile);

    if (did_any)
        return 0;
    else
        return -1;
}

int32_t
YFDriver::GetYield(std::string symbol, double *ayield) {
    InfoMap::iterator it;

    it = _info_map.find(symbol);
    if (it != _info_map.end()) {
        *ayield = it->second->_yield;
        return 0;
    }

    if (symbol == "" || symbol == "None") {
        *ayield = 0.0;
        return 0;
    }

    char command_buffer[128];
    char tbuffer[128];
    strcpy(command_buffer, "prices yield ");
    strcat(command_buffer, symbol.c_str());
    strcat(command_buffer, " 2>/dev/null");
    FILE *tfile = popen(command_buffer, "r");
    double tyield;

    fgets(tbuffer, sizeof(tbuffer), tfile);
    if (strncasecmp(tbuffer, "None", 4) == 0) {
        tyield = 0.0;
    } else {
        int32_t code = sscanf(tbuffer, "%lf", &tyield);
        if (code != 1) {
            printf("Syntax error on response from price command\n");
            printf("Line is '%s'\n", tbuffer);
            return -1;
        }
    }

    CachedInfo *info = new CachedInfo();
    info->_symbol = symbol;
    info->_yield = tyield;
    _info_map[symbol] = info;
    *ayield = tyield;
    return 0;
}
