#include <string>

#include <inttypes.h>
#include <locale.h>
#include <string.h>

#include "utils.h"
#include "vancmd.h"
#include "vanofx.h"

int
main(int argc, char **argv) {
    VanOfx::User user;
    VanCmd cmd;

    if (argc < 3) {
        printf("usage: vantest <ofx file name> operation [args]\n");
        return -1;
    }

    setlocale(LC_NUMERIC, "");

    int32_t code = user.ParseOfx(argv[1]);

    if (code != 0) {
        printf("Parse failed with code=%d\n", code);
        return -1;
    } else {
        printf("Success from parsing!\n");
    }

    if (strcasecmp(argv[2], "balance") == 0) {
        code = cmd.Balance(user);
    } else if (strcasecmp(argv[2], "gain") == 0) {
        code = cmd.Gain(user);
    } else if (strcasecmp(argv[2], "profs") == 0) {
        code = cmd.InitProfile(user);
    } else {
        printf("unrecognized command, try 'van' alone for help\n");
    }

    return 0;
}
