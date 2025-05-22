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
VanCmd::Balance(VanOfx::User &user, Selector &sel) {
    printf("\n**Compute balances**\n");
    double grand_total = 0.0;
    auto acct_lambda = [&sel, &grand_total](VanOfx::Account *acct) {
        double  acct_total = 0.0;
        ProfileAccount *prof_acct = nullptr;
        auto fund_lambda = [&acct_total](VanOfx::Fund *fund) -> int32_t {
            double fund_total = 0.0;
            fund_total += fund->_share_count * fund->_share_price;
            printf(" Fund %s(%s) total $%'.2f\n",
                   fund->_name.c_str(), fund->_symbol.c_str(), fund_total);
            acct_total += fund_total;
            return 0;
        };

        // Apply an account filter
        Profile *prof = sel._profile;
        if (prof != nullptr) {
            prof_acct = prof->GetUser()->FindAccount(acct->_number);
        }
            
        if (prof == nullptr || prof->ContainsAccount(prof_acct)) {
            printf("\nAccount %s:\n", acct->_number.c_str());
            acct->ApplyToFunds(fund_lambda);
            grand_total += acct_total;
        }
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

std::string
VanCmd::GetOfxPath() {
    std::string path = GetProfileDir();
    if (path.size() == 0)
        return path;
    return path + "/OfxDownload.csv";
}

int32_t
VanCmd::SetupProfile(VanOfx::User &user, Selector &sel) {
    ProfileUser *prof_user = new ProfileUser();
    std::string profile_path;

    // Applied to every account we have.  Asks about which profiles we want
    // to include the account within.
    auto acct_lambda = [this, &prof_user](VanOfx::Account *acct) {
        char tbuffer[1024];

        printf("\nAccount %s:\n", acct->_number.c_str());

        ProfileAccount *prof_acct;

        prof_acct = prof_user->FindAccount(acct->_number);
        if (prof_acct != nullptr) {
            printf("Account %s (%s) already setup\n",
                   prof_acct->_account_number.c_str(),
                   prof_acct->_account_name.c_str());
            return 0;
        }

        auto fund_lambda = [](VanOfx::Fund *fund) -> int32_t {
            double fund_total;
            fund_total = fund->_share_count * fund->_share_price;
            printf(" Fund %s(%s) total $%'.2f\n",
                   fund->_name.c_str(), fund->_symbol.c_str(), fund_total);
            return 0;
        };
        printf("Account %s has funds:\n", acct->_number.c_str());
        acct->ApplyToFunds(fund_lambda);

        while(1) {
            printf("Name to give account: ");
            fgets(tbuffer, sizeof(tbuffer), stdin);
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
        
        // save incremental updates
        prof_user->Save(GetProfilePath());

        return 0;
    };
    user.ApplyToAccounts(acct_lambda);
    prof_user->Save(GetProfilePath());
    prof_user->Print();

    return 0;
}
    
int32_t
VanCmd::FutureDivs(VanOfx::User &user, Selector &sel) {
    printf("\n**Project future divdends\n");
    // Now compute the gain
    VanOfx::Gain grand_total;

    auto acct_lambda = [&sel, &grand_total](VanOfx::Account *acct) {
        VanOfx::Gain acct_total;
        ProfileAccount *prof_acct = nullptr;

        auto fund_lambda = [&sel, &acct_total](VanOfx::Fund *fund) -> int32_t {
            VanOfx::Gain fund_total;
            printf("\nFund %s(%s) with balance %lf:\n",
                   fund->_name.c_str(), fund->_symbol.c_str(),
                   fund->_share_count * fund->_share_price);
            fund->FutureDivs(sel._verbose, &fund_total);
            VanOfx::PrintGain(&fund_total);
            acct_total += fund_total;
            return 0;
        };

        // Apply an account filter
        Profile *prof = sel._profile;
        if (prof != nullptr) {
            prof_acct = prof->GetUser()->FindAccount(acct->_number);
        }
            
        if (prof == nullptr || prof->ContainsAccount(prof_acct)) {
            if (prof_acct != nullptr) {
                printf("\n----------------Account %s (%s)----------------\n",
                       prof_acct->_account_name.c_str(),
                       acct->_number.c_str());
            } else {
                printf("\n----------------Account number %s----------------\n",
                       acct->_number.c_str());
            }
            acct->ApplyToFunds(fund_lambda);
            grand_total += acct_total;
        }
        return 0;
    };

    user.ApplyToAccounts(acct_lambda);

    printf("\n***Grand Total***\n");
    VanOfx::PrintGain(&grand_total);
    // Qualified divs are 20% + 3.8% NIIT worst case
    // Regular divs are 37% + 3.8% NIIT
    // Tax free are federally taxed at 0%
    printf("\nPre-tax dividends %.2f\n",
           (grand_total._qualified_divs + grand_total._regular_divs +
            grand_total._tax_free_divs));
    printf("After tax dividend estimate %.2f\n",
           grand_total._qualified_divs * (1.0 - .238) +
           grand_total._regular_divs * (1.0 - .408) +
           grand_total._tax_free_divs);

    VanOfx::Gain annualized_total;
    annualized_total = grand_total;

    // factor is percent of a year that the duration represents.  Divide by this
    // to get annual expected dividends.
    double factor = ((double)(VanOfx::DateStrToTime(sel._to_date) -
                              VanOfx::DateStrToTime(sel._from_date)) /
                     (365.25 * 24.0 * 3600.0));
    annualized_total._qualified_divs /= factor;
    annualized_total._regular_divs /= factor;
    annualized_total._tax_free_divs /= factor;
    printf("\nAnnualized pre-tax dividends %.2f\n",
           (annualized_total._qualified_divs + annualized_total._regular_divs +
            annualized_total._tax_free_divs));
    printf("Annualized after tax dividend estimate %.2f\n",
           annualized_total._qualified_divs * (1.0 - .238) +
           annualized_total._regular_divs * (1.0 - .408) +
           annualized_total._tax_free_divs);

    return 0;
}

int32_t
VanCmd::Gain(VanOfx::User &user, Selector &sel) {
    printf("\n**Compute Gains from %s to %s**\n",
           sel._from_date.c_str(), sel._to_date.c_str());
    // Now compute the gain
    VanOfx::Gain grand_total;

    auto acct_lambda = [&sel, &grand_total](VanOfx::Account *acct) {
        VanOfx::Gain acct_total;
        ProfileAccount *prof_acct = nullptr;

        auto fund_lambda = [&sel, &acct_total](VanOfx::Fund *fund) -> int32_t {
            VanOfx::Gain fund_total;
            printf("\nFund %s(%s):\n",
                   fund->_name.c_str(), fund->_symbol.c_str());
            fund->GainDollars(sel._from_date.c_str(),
                              sel._to_date.c_str(),
                              sel._verbose,
                              &fund_total);
            VanOfx::PrintGain(&fund_total);
            double weighted_balance = 0.0;
            fund->AvgBalance(sel._from_date.c_str(),
                             sel._to_date.c_str(),
                             sel._verbose,
                             &weighted_balance);
            printf("Weighted balance %.2f (Final balance %.2f)\n",
                   weighted_balance, fund->_share_count * fund->_share_price);
            acct_total += fund_total;
            return 0;
        };

        // Apply an account filter
        Profile *prof = sel._profile;
        if (prof != nullptr) {
            prof_acct = prof->GetUser()->FindAccount(acct->_number);
        }
            
        if (prof == nullptr || prof->ContainsAccount(prof_acct)) {
            if (prof_acct != nullptr) {
                printf("\n----------------Account %s (%s)----------------\n",
                       prof_acct->_account_name.c_str(),
                       acct->_number.c_str());
            } else {
                printf("\n----------------Account number %s----------------\n",
                       acct->_number.c_str());
            }
            acct->ApplyToFunds(fund_lambda);
            grand_total += acct_total;
        }
        return 0;
    };

    user.ApplyToAccounts(acct_lambda);

    printf("\n***Grand Total***\n");
    VanOfx::PrintGain(&grand_total);
    // Qualified divs are 20% + 3.8% NIIT worst case
    // Regular divs are 37% + 3.8% NIIT
    // Tax free are federally taxed at 0%
    printf("\nPre-tax dividends %.2f\n",
           (grand_total._qualified_divs + grand_total._regular_divs +
            grand_total._tax_free_divs));
    printf("After tax dividend estimate %.2f\n",
           grand_total._qualified_divs * (1.0 - .238) +
           grand_total._regular_divs * (1.0 - .408) +
           grand_total._tax_free_divs);

    VanOfx::Gain annualized_total;
    annualized_total = grand_total;

    // factor is percent of a year that the duration represents.  Divide by this
    // to get annual expected dividends.
    double factor = ((double)(VanOfx::DateStrToTime(sel._to_date) -
                              VanOfx::DateStrToTime(sel._from_date)) /
                     (365.25 * 24.0 * 3600.0));
    annualized_total._qualified_divs /= factor;
    annualized_total._regular_divs /= factor;
    annualized_total._tax_free_divs /= factor;
    printf("\nAnnualized pre-tax dividends %.2f\n",
           (annualized_total._qualified_divs + annualized_total._regular_divs +
            annualized_total._tax_free_divs));
    printf("Annualized after tax dividend estimate %.2f\n",
           annualized_total._qualified_divs * (1.0 - .238) +
           annualized_total._regular_divs * (1.0 - .408) +
           annualized_total._tax_free_divs);

    return 0;
}
