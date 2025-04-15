#include <fcntl.h>
#include <unistd.h>

#include "yfdriver.h"

int
main(int argc, char **argv) {
    int32_t code;
    double price;

    code = YFDriver::GetPrice("2020-01-01", "aapl", &price);
    printf("Price for APPL is %f\n", price);

    code = YFDriver::GetPrice("2020-02-01", "aapl", &price);
    printf("Price for APPL on another date is %f\n", price);
    return code;
}
