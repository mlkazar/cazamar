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

    snprintf(command_buffer, sizeof(command_buffer),
             "prices %s %s 2>/dev/null", date.c_str(), symbol.c_str());
    tfile = popen(command_buffer, "r");
    fgets(tbuffer, sizeof(tbuffer), tfile);
    sscanf(tbuffer, "%lf", price);
    return 0;
}
