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

    // Check cache
    if (symbol == _cached_symbol) {
        PriceMap::iterator it;
        it = _cached_prices.find(date);
        if (it != _cached_prices.end()) {
            *price = it->second;
            return 0;
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
    pclose(tfile);

    if (did_any)
        return 0;
    else
        return -1;
}
