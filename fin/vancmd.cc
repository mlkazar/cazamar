#include <string>

#include <inttypes.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "profile.h"
#include "utils.h"
#include "vancmd.h"
#include "vanofx.h"

int32_t
VanCmd::Balance(VanOfx::User &user) {
    printf("\n\n\n**Compute balances**\n");
    double grand_total = 0.0;
    auto acct_lambda = [&grand_total](VanOfx::Account *acct) {
        printf("\nAccount %s:\n", acct->_number.c_str());
        double  acct_total = 0.0;
        auto fund_lambda = [&acct_total](VanOfx::Fund *fund) -> int32_t {
            double fund_total = 0.0;
            fund_total += fund->_share_count * fund->_share_price;
            printf(" Fund %s(%s) total $%'.2f\n",
                   fund->_name.c_str(), fund->_symbol.c_str(), fund_total);
            acct_total += fund_total;
            return 0;
        };
        acct->ApplyToFunds(fund_lambda);
        grand_total += acct_total;
        return 0;
    };
    user.ApplyToAccounts(acct_lambda);
    printf("Total balance is %'.2f\n", grand_total);

    return 0;
}

std::string
VanCmd::GetProfileDir() {
    char *tp = getenv("VANPATH");
    if (!tp) {
        tp = getenv("HOME");
        if (!tp) {
            printf("$HOME not set\n");
            return std::string("");
        }
        std::string profile_path = std::string(tp) + std::string("/.van");

        // try to create dir
        struct stat tstat;
        int code=stat(profile_path.c_str(), &tstat);
        if (code < 0)
            mkdir(profile_path.c_str(), 0777);

        return profile_path;
    } else {
        return std::string(tp);
    }
}

std::string
VanCmd::GetProfilePath() {
    std::string path = GetProfileDir();
    if (path.size() == 0)
        return path;
    return path + "/profile.json";
}

int32_t
VanCmd::InitProfile(VanOfx::User &user) {
    ProfileUser *prof_user = new ProfileUser();
    std::string profile_path;

    // Applied to every account we have.  Asks about which profiles we want
    // to include the account within.
    auto acct_lambda = [&prof_user](VanOfx::Account *acct) {
        char tbuffer[1024];

        printf("\nAccount %s:\n", acct->_number.c_str());
        auto fund_lambda = [](VanOfx::Fund *fund) -> int32_t {
            double fund_total;
            fund_total = fund->_share_count * fund->_share_price;
            printf(" Fund %s(%s) total $%'.2f\n",
                   fund->_name.c_str(), fund->_symbol.c_str(), fund_total);
            return 0;
        };
        printf("Account %s has funds:\n", acct->_number.c_str());
        acct->ApplyToFunds(fund_lambda);

        ProfileAccount *prof_acct;

        while(1) {
            printf("Name to give account: ");
            fgets(tbuffer, sizeof(tbuffer), stdin);
            printf("len is %ld\n", strlen(tbuffer));
            if (strlen(tbuffer) > 1) {
                break;
            }
        }
        std::string acct_name = std::string(tbuffer);
        acct_name.pop_back(); // remove terminating lf
        prof_acct = prof_user->AddAccount(acct->_number, acct_name);
        prof_acct->_is_ira = VanOfx::GetYN("Is this an IRA/401(k) account(Y/N): ");

        printf("Profile names (',' separated): ");
        fgets(tbuffer, sizeof(tbuffer), stdin);
        std::vector<std::string> prof_names;
        VanOfx::ParseTokens(',', tbuffer, &prof_names);
        for(std::string pname : prof_names) {
            Profile *profile = prof_user->GetProfile(pname);
            profile->AddAccount(prof_acct);
        }
        
        return 0;
    };
    user.ApplyToAccounts(acct_lambda);
    prof_user->Save(GetProfilePath());
    prof_user->Print();

    return 0;
}
    
int32_t
VanCmd::Gain(VanOfx::User &user) {
    printf("\n\n\n**Compute Gains**\n");
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
    printf("\nTotal gain for user is:\n");
    VanOfx::PrintGain(&grand_total);

    return 0;
}
