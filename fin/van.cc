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
    VanCmd::Selector sel;
    uint32_t i;

    if (argc < 2) {
        printf("usage: van <command> [args]\n");
        printf("       commands are 'balance', 'gains', 'future' or  'setup'\n");
        printf("       --prof <profile name>\n");
        printf("       --from <from date>\n");
        printf("       --to <to date>\n");
        printf("       -v or --verbose for verbose\n");
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

    for(i=2; i < argc; i++) {
        if (strcmp(argv[i], "--prof") == 0) {
            std::string profile_name = std::string(argv[i+1]);
            sel._profile = prof_user.FindProfile(profile_name);
            ++i;
        } else if (strcmp(argv[i], "--from") == 0) {
            sel._from_date = std::string(argv[i+1]);
            ++i;
        } else if (strcmp(argv[i], "--to") == 0) {
            sel._to_date = std::string(argv[i+1]);
            ++i;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            sel._verbose = 1;
        } else {
            printf("unknown switch/operand '%s'\n", argv[i]);
            return -1;
        }
   }

    if (sel._to_date.size() == 0) {
        // If we pick a date after which the market hasn't opened, we won't
        // be able to get the price of shares, and we'll get a value of $0
        // for all funds.  We'll assume there's no more than a 4 day period
        // where the markets aren't open.
        sel._to_date = VanOfx::CurrentDateStr(-4LL * 60*60*24);
    }
    if (sel._from_date.size() == 0) {
        long start_time;

        start_time = VanOfx::DateStrToTime(sel._to_date);
        start_time -= 365LL * 60 * 60 * 24;
        sel._from_date = VanOfx::TimeToDateStr(start_time);
    }

    if (strcasecmp(argv[1], "balance") == 0) {
        code = cmd.Balance(user, sel);
    } else if (strcasecmp(argv[1], "gains") == 0) {
        code = cmd.Gain(user, sel);
    } else if (strcasecmp(argv[1], "setup") == 0) {
        code = cmd.SetupProfile(user, sel);
    } else if (strcasecmp(argv[1], "future") == 0) {
        code = cmd.FutureDivs(user, sel);
    } else {
        printf("unrecognized command, try 'van' alone for help\n");
        return -1;
    }

    return 0;
}
