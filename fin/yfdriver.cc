#include <string>

#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>

#include "yfdriver.h"

int32_t
YFDriver::GetPrice(std::string date, std::string symbol, double *price) {
    FILE *tfile;
    char tbuffer[100];
    char command_buffer[100];
    char date_buffer[12];
    int did_any=0;
    int32_t code;
    double tprice;

    if (symbol == "None" || symbol.size() == 0) {
        *price = 1.0;
        return 0;
    }

    // Check cache
    if (symbol == _cached_symbol) {
        PriceMap::iterator it;
        // Sometimes there are gaps in the dates of the returned data.
        // If that happens, lower bound will return first date after
        // the one we're searching for.  But only use that if the date
        // that provided the data is <= our search date.
        it = _cached_prices.lower_bound(date);
        if (it != _cached_prices.end()) {
            if (date >= _cached_start_date) {
                *price = it->second;
                return 0;
            }
        }
    }

    _cached_prices.clear();
    snprintf(command_buffer, sizeof(command_buffer),
             "prices %s %s 2>/dev/null", date.c_str(), symbol.c_str());
    tfile = popen(command_buffer, "r");
    while(1) {
        fgets(tbuffer, sizeof(tbuffer), tfile);
        if (strncmp(tbuffer, "DONE", 4) == 0)
            break;
        code = sscanf(tbuffer, "%10s %lf", date_buffer, &tprice);
        if (code != 2)
            break;
        _cached_prices[std::string(date_buffer)] = tprice;
        if (!did_any) {
            did_any = 1;
            *price = tprice;
        }
    }
    _cached_symbol = symbol;
    _cached_start_date = date;
    pclose(tfile);

    if (did_any)
        return 0;
    else
        return -1;
}
