#include <string>

#include <inttypes.h>
#include <locale.h>
#include <string.h>

#include "profile.h"
#include "utils.h"
#include "vancmd.h"
#include "vanofx.h"

int
main(int argc, char **argv) {
    VanOfx::User user;
    VanCmd cmd;
    ProfileUser prof_user;
    Profile *profile = nullptr;

    if (argc < 2) {
        printf("usage: van <command> [args]\n");
        printf("       commands are 'balance', 'gains', or 'setup'\n");
        return -1;
    }

    prof_user.Init(cmd.GetProfilePath());

    setlocale(LC_NUMERIC, "");

    int32_t code = user.ParseOfx(cmd.GetOfxPath());

    if (code != 0) {
        printf("Parse failed with code=%d\n", code);
        return -1;
    } else {
        printf("Success loading profile.\n");
    }

    if (argc >= 3) {
        // profile name
        std::string profile_name = std::string(argv[2]);
        profile = prof_user.FindProfile(profile_name);
    }

    if (strcasecmp(argv[1], "balance") == 0) {
        code = cmd.Balance(user, profile);
    } else if (strcasecmp(argv[1], "gains") == 0) {
        code = cmd.Gain(user, profile);
    } else if (strcasecmp(argv[1], "setup") == 0) {
        code = cmd.SetupProfile(user, profile);
    } else {
        printf("unrecognized command, try 'van' alone for help\n");
    }

    return 0;
}
