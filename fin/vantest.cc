#include <string>

#include <inttypes.h>
#include <locale.h>
#include <string.h>

#include "utils.h"
#include "vanofx.h"

int
main(int argc, char **argv) {
    VanOfx::User user;

    if (argc < 2) {
        printf("usage: vantest <ofx file name>\n");
        return -1;
    }

    int32_t code = user.ParseOfx(argv[1]);

    if (code != 0) {
        printf("Parse failed with code=%d\n", code);
        return -1;
    } else {
        printf("Success from parsing!\n");
    }

    setlocale(LC_NUMERIC, "");

    printf("**Compute balances**\n");
    {
        VanOfx::Gain grand_total;
        auto acct_lambda = [&grand_total](VanOfx::Account *acct) {
            printf("\nAccount %s:\n", acct->_number.c_str());
            VanOfx::Gain  acct_total;
            auto fund_lambda = [&acct_total](VanOfx::Fund *fund) -> int32_t {
                VanOfx::Gain fund_total;
                fund_total._unrealized_cg = fund->_share_count * fund->_share_price;
                printf(" Fund %s(%s) total $%'.2f\n",
                       fund->_name.c_str(), fund->_symbol.c_str(), fund_total._unrealized_cg);
                acct_total += fund_total;
                return 0;
            };
            acct->ApplyToFunds(fund_lambda);
            grand_total += acct_total;
            return 0;
        };
        user.ApplyToAccounts(acct_lambda);
        printf("Total balance:\n");
        VanOfx::PrintGain(&grand_total);
    }

    
    printf("**Compute Gains**\n");
    {
        // Now compute the gain
        VanOfx::Gain grand_total;

        auto acct_lambda = [&grand_total](VanOfx::Account *acct) {
            printf("\nAccount %s:\n", acct->_number.c_str());
            VanOfx::Gain acct_total;
            auto fund_lambda = [&acct_total](VanOfx::Fund *fund) -> int32_t {
                VanOfx::Gain fund_total;
                fund->GainDollars("2024-01-01", "2024-12-31", &fund_total);
                printf(" Fund %s(%s):\n",
                       fund->_name.c_str(), fund->_symbol.c_str());
                VanOfx::PrintGain(&fund_total);
                acct_total += fund_total;
                return 0;
            };
            acct->ApplyToFunds(fund_lambda);
            grand_total += acct_total;
            return 0;
        };
        user.ApplyToAccounts(acct_lambda);
        printf("Total gain for account is:\n");
        VanOfx::PrintGain(&grand_total);
    }

    return 0;
}
