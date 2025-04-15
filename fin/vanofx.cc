#include <functional>
#include <string>
#include <vector>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"
#include "vanofx.h"
#include "yfdriver.h"

namespace VanOfx {

int32_t
Fund::ApplyToTrans(std::function<int32_t(Transaction *)>func) {
    int32_t rval;
    for(Transaction *tran : _trans) {
        rval = func(tran);
        if (rval != 0)
            return rval;
    }
    return 0;
}

Account *
User::GetAccount(std::string number) {
    AccountMap::iterator it;
    Account *account;

    // Create an account object if none found.
    it = _accounts.find(number);
    if (it == _accounts.end()) {
        account = new Account();
        account->_number = number;
        _accounts.insert(make_pair(number, account));
    } else {
        account = it->second;
    }

    return account;
}

int32_t
Account::ApplyToFunds(std::function<int32_t(Fund *)> func) {
    int32_t rval;

    for(Fund *fund : _funds) {
        rval = func(fund);
        if (rval != 0) {
            return rval;
        }
    }
    return 0;
}

Fund *
Account::GetFund(std::string fund_name) {
    FundList::iterator it;
    Fund *fund;
    for(it = _funds.begin(); it != _funds.end(); it++) {
        fund = *it;
        if (fund->_name == fund_name)
            return fund;
    }

    fund = new Fund(fund_name);
    _funds.push_back(fund);
    return fund;
}

int32_t
User::AssignSalesGains() {
    for(auto &pair : _accounts) {
        Account *acct = pair.second;
        for(auto fund : acct->_funds) {
            for(auto trans : fund->_trans) {
                // This is just a placeholder.  We should really check
                // for a LOT file, figure out which lots still apply
                // and use mintax or whatever to figure out the
                // specific gain.  Or if we don't have lots, use Yahoo
                // finance to get an average purchase price, augmented
                // by actual Buy transactions we have for the shares.
                trans->_ex_gain = (trans->_share_price / 2)  * trans->_share_count;
            }
        }
    }

    return 0;
}

int32_t
User::ApplyToAccounts(std::function<int32_t(Account *)> func) {
    int32_t rval;
    for(auto &pair : _accounts) {
        Account *acct = pair.second;
        rval = func(acct);
        if (rval != 0)
            return rval;
    }
    return 0;
}

double
Fund::GainDollars(std::string from_date, std::string to_date) {
    double from_price;
    double to_price;

    YFDriver::GetPrice(from_date, _symbol, &from_price);
    YFDriver::GetPrice(to_date, _symbol, &to_price);
    return 0.0;
}

// Parsing an OFX file is pretty hard.  The file is a set of fund
// definitions followed by a set of transactions.  Each set is
// preceded by a set of keywords specifying the order of the items in
// the CSV lines.  Because these vary, we're going to have to build a
// mapping table for the items we're interested in that describes
// which field index contains a particular item like share price.
//
// The _ex_gain field in the transaction requires some
// explanation. Consider trying to track the total gain of a fund.
// You might think that you can just compare the USD value of the fund
// at two different times and estimate the gain, but that doesn't take
// into account purchases of new shares or sales of existing ones.
// It also doesn't differentiate shares appearing due to purchases
// vs. shares appearing vs reinvested dividends.
//
// Our approach is to look at inflows and outflows, including sales
// and purchases, across the window we're examining, in a segment-wise
// basis.  If a segment ends with a Buy, we compare the starting value
// and ending value, and treat it as a gain over that period and with
// a base value of the starting value.  Same thing for a segment that
// ends with a Sell.  If it ends with a Dividend, we add the dividend
// into the ending price, and otherwise do the same computation; note
// that if the dividend is reinvested in the same fund, it will also
// appear as a Buy that changes the starting value for the next
// segment.  Capital gains distributions are modeled as a sale, but
// one where the share price is reduced instead of the share count?
// 
int32_t
User::ParseOfx(std::string file_name) {
    FILE *ifp = nullptr;
    Account *account;
    Fund *fund;
    Transaction *trans;

    ifp = fopen(file_name.c_str(), "r");
    if (!ifp) {
        return -1;
    }

    while(1) {
        char tbuffer[2048];
        char tc;
        char *tp = fgets(tbuffer, sizeof(tbuffer), ifp);
        if (!tp) {
            break;
        }

        tc = FirstNonblank(tbuffer);
        int32_t delims = CountDelim(',', tbuffer);
        if (delims == 0) {
            // blank line
            continue;
        }

        // Split into a vector of strings
        std::vector<std::string> items;
        items.clear();
        ParseTokens(',', tbuffer, &items);

        tc = tbuffer[0];
        if (tc >= '0' && tc <= '9') {
            if (_model_funds) {
                std::string account_number;
                // If the account # contains a -, the number is really the part
                // after that point.
                size_t pos = items[_account_number_ix].find_first_of('-');
                if (pos != std::string::npos) {
                    account_number = items[_account_number_ix].substr(pos+1);
                } else {
                    account_number = items[_account_number_ix];
                }
                // These records represent individual funds within a specfic
                // account.
                account = GetAccount(account_number);
                fund = new Fund(items[_account_name_ix]);
                fund->_share_price  = stof(items[_account_share_price_ix]);
                fund->_share_count  = stof(items[_account_share_count_ix]);
                account->_funds.push_back(fund);
            } else {
                // these lines describe statements within one or more
                // funds.  Find the account and then the fund and add
                // a statement to it.  Note the variety of ways of
                // expressing the same type of transaction.  Boo!
                account = GetAccount(items[_stmt_number_ix]);
                fund = account->GetFund(items[_stmt_name_ix]);
                trans = new Transaction();
                std::string ttype;

                trans->_date = items[_stmt_trade_date_ix];
                trans->_fund_name = items[_stmt_name_ix];
                trans->_share_price = stof(items[_stmt_share_price_ix]);
                trans->_share_count = stof(items[_stmt_share_count_ix]);
                trans->_ex_gain = 0.0;
                trans->_post_balance = 0.0;

                ttype = items[_stmt_trans_type_ix];
                if (IsTranType(ttype,"Plan Contribution")) {
                    trans->_type = TRAN_BUY;
                } else if (IsTranType(ttype, "Dividend")) {
                    trans->_type = TRAN_DIV;
                    trans->_ex_gain = trans->_share_price * trans->_share_count;
                } else if (IsTranType(ttype, "Short-term") ||
                           IsTranType(ttype, "(ST)")) {
                    trans->_type = TRAN_STGAIN;
                } else if (IsTranType(ttype, "Long-term") ||
                           IsTranType(ttype, "(LT)")) {
                    trans->_type = TRAN_LTGAIN;
                } else if (IsTranType(ttype, "Sell")) {
                    trans->_type = TRAN_SELL;
                } else if (IsTranType(ttype, "Sweep out")) {
                    trans->_type = TRAN_SELL;
                    // the value is positive in the record even though
                    // this is a sale.
                    trans->_share_count = - trans->_share_count;
                } else if (IsTranType(ttype, "Buy")) {
                    trans->_type = TRAN_BUY;
                } else if (IsTranType(ttype, "Exchange To")) {
                    trans->_type = TRAN_BUY;
                } else if (IsTranType(ttype, "Exchange From")) {
                    trans->_type = TRAN_SELL;
                } else if (IsTranType(ttype, "Transfer") ||
                           IsTranType(ttype, "Stock split") ||
                           IsTranType(ttype, "Withdrawal") ||
                           IsTranType(ttype, "Distribution") ||
                           IsTranType(ttype, "Fee") ||
                           IsTranType(ttype, "Funds Received") ||
                           IsTranType(ttype, "Rollover") ||
                           IsTranType(ttype, "Corp Action")) {
                    // This is just an advisory; we generally see a
                    // corresponding sweep-in record that describes
                    // the actual monetary movement.
                    //
                    // Todo: Stock split shouldn't be in this list,
                    // but we'll handle it later.  Also, figure out
                    // how "Fee" records work.
                    delete trans;
                    trans = nullptr;
                } else if (IsTranType(ttype, "Sweep in")) {
                    // Sweep in amount is reported as a negative share count, but
                    // it represents a purchase of the shares.
                    trans->_type = TRAN_BUY;
                    trans->_share_count = - trans->_share_count;
                    
                } else if (IsTranType(ttype, "Reinvestment")) {
                    if (trans->_share_count > 0) {
                        trans->_type = TRAN_BUY;
                        trans->_ex_gain = trans->_share_price * trans->_share_count;
                    } else {
                        // this is an extra record that reiterates that the money
                        // is going into cash.
                        delete trans;
                        trans = nullptr;
                    }
                } else {
                    printf("bad transaction type %s\n", ttype.c_str());
                    delete trans;
                    return -1;
                }

                if (trans) {
                    fund->_trans.push_back(trans);
                }
            }
        } else {
            // This describes the fields in a section.
            if (ItemsContains(&items, "Total Value")) {
                // We're in a section defining a set of funds for one or more
                // Accounts.
                ResetAccountState();
                _account_number_includes_fund = ItemsContains(&items, "Fund Account Number");

                if ((_account_number_ix = ItemsIndex(&items, "Fund Account Number")) >= 0) {
                    _account_name_ix = ItemsIndex(&items, "Fund Name");
                } else if ((_account_number_ix = ItemsIndex(&items,"Account Number")) >= 0) {
                    _account_name_ix = ItemsIndex(&items, "Investment Name");
                    _account_symbol_ix = ItemsIndex(&items, "Symbol");
                } else if ((_account_number_ix = ItemsIndex(&items, "Plan Number")) >= 0) {
                    // retirement funds
                    _account_name_ix = ItemsIndex(&items, "Fund Name");
                } else {
                    // Failure
                }

                _account_share_count_ix = ItemsIndex(&items, "Shares");
                _account_share_price_ix = ItemsIndex(&items, "Share Price");
                if (_account_share_price_ix < 0)
                    _account_share_price_ix = ItemsIndex(&items, "Price");

                // The next fields describe funds
                _model_funds = 1;
            } else {
                // We're in a section containing multiple transations.  There are a
                // number of fields we need:
                // transaction type, fund_name, transaction date as a string,
                // number of shares, share price, external gain and fund post-transaction
                // balance.
                ResetStmtState();
                _stmt_number_ix = ItemsIndex(&items, "Account Number");
                _stmt_trade_date_ix = ItemsIndex(&items, "Trade Date");
                _stmt_name_ix = ItemsIndex(&items, "Investment Name");
                _stmt_trans_type_ix = ItemsIndex(&items, "Transaction Type");
                if (_stmt_trans_type_ix < 0) {
                    // Use transaction descrption; in this case we're probably looking at
                    // a 401(k) transaction.
                    _stmt_trans_type_ix = ItemsIndex(&items, "Transaction Description");
                }
                _stmt_share_count_ix = ItemsIndex(&items, "Shares");
                if (_stmt_share_count_ix < 0) {
                    _stmt_share_count_ix = ItemsIndex(&items, "Transaction Shares");
                }
                _stmt_share_price_ix = ItemsIndex(&items, "Share Price");

                // The data models transactions, not funds.
                _model_funds = 0;
            }
        } // else not a digit
    } // loop over all lines

    AssignSalesGains();

    return 0;
}



}
