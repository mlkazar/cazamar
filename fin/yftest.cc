#include <fcntl.h>
#include <unistd.h>

#include "yfdriver.h"

int
main(int argc, char **argv) {
    int32_t code;
    double price;
    YFDriver yf;

    code = yf.GetPrice("2020-01-01", "aapl", &price);
    printf("Price for APPL is %f(%d)\n", price, code);

    code = yf.GetPrice("2020-02-04", "aapl", &price);
    printf("Price for APPL on another date is %f (%d)\n", price, code);

    code = yf.GetPrice("2023-12-01", "aapl", &price);
    printf("Price for APPL on another date is %f (%d)\n", price, code);

    if (argc >= 3) {
        code = yf.GetPrice(argv[1], argv[2], &price);
        printf("Price for %s on %s is %f (%d)\n",
               argv[1], argv[2], price, code);
    }

    printf("\nStarting yield tests\n");
    double yield;
    code = yf.GetYield("VWIUX", &yield);
    printf("code is %d, yield is %lf for VWIUX\n", code, yield);

    code = yf.GetYield("AAPL", &yield);
    printf("code is %d, yield is %lf for AAPL\n", code, yield);

    code = yf.GetYield("VEIRX", &yield);
    printf("code is %d, yield is %lf for VEIRX\n", code, yield);

    printf("\nNow a second run should find stuff in the info cache\n");
    code = yf.GetYield("VWIUX", &yield);
    printf("code is %d, yield is %lf for VWIUX\n", code, yield);

    code = yf.GetYield("AAPL", &yield);
    printf("code is %d, yield is %lf for AAPL\n", code, yield);

    code = yf.GetYield("VEIRX", &yield);
    printf("code is %d, yield is %lf for VEIRX\n", code, yield);
}
