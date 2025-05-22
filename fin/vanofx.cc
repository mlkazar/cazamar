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

User::User()
{
    _fund_map["0030"] = "VMMXX";
    _fund_map["0066"] = "VMRXX";
    _fund_map["0509"] = "VIGAX";
    _fund_map["0540"] = "VFIAX";
    _fund_map["1945"] = "VSCSX";
    _fund_map["5314"] = "VWIUX";
    // TODO: get the rest of these

    _model_funds = 1;
}

std::string
User::LookupFundByNumber(std::string fund_number) {
    NumberMap::iterator it;
    it = _fund_map.find(fund_number);
    if (it == _fund_map.end())
        return std::string("None");
    else
        return it->second;
}

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
        account = new Account(this);
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
Account::GetFundByName(std::string fund_name) {
    FundList::iterator it;
    Fund *fund;
    for(it = _funds.begin(); it != _funds.end(); it++) {
        fund = *it;
        if (fund->_name == fund_name)
            return fund;
    }

    fund = new Fund(_user, fund_name);
    _funds.push_back(fund);
    return fund;
}

Fund *
Account::GetFundBySymbol(std::string symbol) {
    FundList::iterator it;
    Fund *fund;
    for(it = _funds.begin(); it != _funds.end(); it++) {
        fund = *it;
        if (fund->_symbol == symbol)
            return fund;
    }

    return nullptr;
}

int32_t
User::BackfillBalances() {
    std::vector<Transaction *>::reverse_iterator rit;
    for(auto &pair : _accounts) {
        Account *acct = pair.second;
        for(auto fund : acct->_funds) {
            double prev_count = fund->_share_count;
            for(rit = fund->_trans.rbegin(); rit != fund->_trans.rend(); rit++) {
                Transaction *trans = *rit;
                trans->_pre_share_count = prev_count - trans->_share_count;
                prev_count = trans->_pre_share_count;
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

int32_t
Fund::FutureDivs(int verbose, Gain *gains) {
    double yield;
    int32_t code;

    double balance = _share_price * _share_count;

    code = _user->GetYF()->GetYield(_symbol, &yield);
    if (code)
        return code;

    double div = balance * yield;
    if (_is_tax_free)
        gains->_tax_free_divs += div;
    else if (_is_bond)
        gains->_regular_divs += div;
    else
        gains->_qualified_divs += div;

    return 0;
}

int32_t
Fund::AvgBalance(std::string from_date, std::string to_date, int verbose, double *balance) {
    double from_price = 0.0;
    double to_price = 0.0;
    int can_get_prices = (_symbol.length() > 0);

    _user->GetYF()->SetVerbose(verbose);

    if (can_get_prices) {
        GetPrice(from_date, _symbol, &from_price);
        GetPrice(to_date, _symbol, &to_price);
    }

    double prev_price = from_price;
    long prev_date = DateStrToTime(from_date);

    int did_any = 0;
    double current_price = 0.0;
    long seconds_in_range = DateStrToTime(to_date) - DateStrToTime(from_date);
    double weighted_balance = 0.0;
    long current_time = DateStrToTime(from_date);

    for(Transaction *trans : _trans) {
        if ( DateStrCmp(from_date, trans->_date) <= 0 &&
             DateStrCmp(trans->_date, to_date) <= 0) {
            // date is in range
            GetPrice(trans->_date, _symbol, &current_price);
            if (!can_get_prices) {
                // Assume price was level across first range.
                prev_price = current_price;
            }

            // Weighted balance for this section of time is the price
            // up to this point times the # of shares up to this
            // point, timed the percent of the total time range that
            // the segment ending at this transaction's time
            // represents.
            current_time = DateStrToTime(trans->_date);
            weighted_balance += ((trans->_pre_share_count * prev_price) *
                                 ((double)(current_time - prev_date) /
                                  seconds_in_range));

            if (verbose) {
                printf("    adding trans date=%s type=%d current_value=%f "
                       "range_pct=%3f cumulative-balance=%.4f\n",
                       trans->_date.c_str(), trans->_type,
                       trans->_pre_share_count * prev_price,
                       100 * ((double)(current_time - prev_date) /
                              seconds_in_range),
                       weighted_balance);
            }
            prev_price = current_price;
            prev_date = DateStrToTime(trans->_date);
            did_any = 1;
        }
    }

    // At this point we've covered from from_date to the first
    // transaction, but haven't covered from the last transaction to
    // the to_date.  And if did_any is false, then we just price the
    // shares from the from_price to the to_price.  Note that
    // current_price is the last price from a transaction.
    if (did_any) {
        current_time = DateStrToTime(to_date);
        weighted_balance += ((_share_count * prev_price) *
                             ((double)(current_time - prev_date) /
                              seconds_in_range));
        if (verbose)
            printf("    final section weighted balance=%.2f\n", weighted_balance);
    } else {
        // No transactions at all, just look at the shares * price
        weighted_balance = _share_count * _share_price;
    }

    *balance = weighted_balance;

    return 0;
}

int32_t
Fund::GetPrice(std::string date, std::string symbol, double *price) {
    int32_t code;

    if (_is_money_market) {
        *price = 1.0;
        code = 0;
    } else  {
        code = _user->GetYF()->GetPrice(date, symbol, price);
    }

    return code;
}

int32_t
Fund::GainDollars(std::string from_date, std::string to_date, int verbose, Gain *gain) {
    double from_price = 0.0;
    double to_price = 0.0;
    int can_get_prices = (_symbol.length() > 0);

    double total_gain = 0.0;

    _user->GetYF()->SetVerbose(verbose);

    if (can_get_prices) {
        GetPrice(from_date, _symbol, &from_price);
        GetPrice(to_date, _symbol, &to_price);
    }

    double prev_price = from_price;
    int did_any = 0;
    double current_price = 0.0;

    for(Transaction *trans : _trans) {
        if ( DateStrCmp(from_date, trans->_date) <= 0 &&
             DateStrCmp(trans->_date, to_date) <= 0) {
            // date is in range
            GetPrice(trans->_date, _symbol, &current_price);
            gain->_unrealized_cg += trans->_pre_share_count * (current_price - prev_price);
            if (_is_tax_free) {
                gain->_tax_free_divs += trans->_ex_div;
            } else if (!_is_bond) {
                // stock fund divs are qualified
                gain->_qualified_divs += trans->_ex_div;
            } else {
                gain->_regular_divs += trans->_ex_div;
            }
            gain->_short_term_dist += trans->_ex_st;
            gain->_long_term_dist += trans->_ex_lt;
            if (verbose) {
                printf("    adding trans date=%s type=%d pre_shares=%f "
                       "trans_shares=%f price=%f-%f ex_div=%f total_gain=%f\n",
                       trans->_date.c_str(), trans->_type, trans->_pre_share_count,
                       trans->_share_count,
                       prev_price, current_price, trans->_ex_div, total_gain);
            }
            prev_price = current_price;
            did_any = 1;
        }
    }

    // At this point we've covered from from_date to the first
    // transaction, but haven't covered from the last transaction to
    // the to_date.  And if did_any is false, then we just price the
    // shares from the from_price to the to_price.  Note that
    // current_price is the last price from a transaction.
    if (did_any) {
        gain->_unrealized_cg += _share_count * (to_price - current_price);
    } else {
        // No transactions at all, just look at unrealized gain across
        // the date range.
        gain->_unrealized_cg = (to_price - from_price) * _share_count;
    }

    return 0;
}

// Parsing an OFX file is pretty hard.  The file is a set of fund
// definitions followed by a set of transactions.  Each set is
// preceded by a set of keywords specifying the order of the items in
// the CSV lines.  Because these vary, we're going to have to build a
// mapping table for the items we're interested in that describes
// which field index contains a particular item like share price.
//
// The gain fields (_ex_div, _ex_lt and _ex_st) in the transaction
// requires some explanation. Consider trying to track the total gain
// of a fund.  You might think that you can just compare the USD value
// of the fund at two different times and estimate the gain, but that
// doesn't take into account purchases of new shares or sales of
// existing ones.  It also doesn't differentiate shares appearing due
// to purchases vs. shares appearing vs reinvested dividends.
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
                std::string fund_number;
                // If the account # contains a -, the number is really the part
                // after that point.
                size_t pos = items[_account_number_ix].find_first_of('-');
                if (pos != std::string::npos) {
                    account_number = RemoveLeadingZeroes(items[_account_number_ix].substr(pos+1));
                    fund_number = items[_account_number_ix].substr(0,4);
                } else {
                    account_number = RemoveLeadingZeroes(items[_account_number_ix]);
                }
                // These records represent individual funds within a specfic
                // account.
                account = GetAccount(account_number);
                fund = new Fund(this, items[_account_name_ix]);
                fund->_share_price  = stof(items[_account_share_price_ix]);
                fund->_share_count  = stof(items[_account_share_count_ix]);
                if (_account_symbol_ix >= 0)
                    fund->_symbol = items[_account_symbol_ix];
                else if (fund_number.size() > 0) {
                    fund->_symbol = LookupFundByNumber(fund_number);
                }
                else
                    fund->_symbol = "None";
                account->_funds.push_back(fund);
            } else {
                // these lines describe statements within one or more
                // funds.  Find the account and then the fund and add
                // a statement to it.  Note the variety of ways of
                // expressing the same type of transaction.  Boo!
                account = GetAccount(RemoveLeadingZeroes(items[_stmt_number_ix]));
                fund = account->GetFundByName(items[_stmt_name_ix]);
                trans = new Transaction();
                std::string ttype;

                trans->_date = items[_stmt_trade_date_ix];
                trans->_fund_name = items[_stmt_name_ix];
                trans->_share_price = stof(items[_stmt_share_price_ix]);
                trans->_share_count = stof(items[_stmt_share_count_ix]);
                if (_stmt_net_amount_ix >= 0)
                    trans->_net_amount = stof(items[_stmt_net_amount_ix]);
                else
                    trans->_net_amount = 0.0;
                trans->_ex_div = 0.0;
                trans->_ex_st = 0.0;
                trans->_ex_lt = 0.0;
                trans->_pre_share_count = 0.0;

                ttype = items[_stmt_trans_type_ix];
                if (IsTranType(ttype,"Plan Contribution")) {
                    trans->_type = TRAN_BUY;
                    trans->_date = FlipDate(trans->_date);
                } else if (IsTranType(ttype, "Dividend")) {
                    trans->_type = TRAN_DIV;
                    trans->_ex_div = trans->_net_amount;
                    if (trans->_share_count > 0.0) {
                        // in this case, a dividend is also a purchase, but the
                        // purchase is not recorded as a separate line in the file.
                        // So we create one.
                        Transaction *alt_trans = new Transaction();
                        *alt_trans = *trans;
                        alt_trans->_ex_div = 0.0;
                        alt_trans->_type = TRAN_BUY;
                        fund->_trans.push_back(alt_trans);

                        // and zero out the share count in the DIV transaction
                        trans->_share_count = 0.0;
                    }
                } else if (IsTranType(ttype, "Short-term") ||
                           IsTranType(ttype, "(ST)")) {
                    trans->_type = TRAN_STGAIN;
                    // on a cap gain, the share price is adjusted
                    // down, but this distribution is really a profit,
                    // so it needs to show up somewhere.
                    trans->_ex_st = trans->_net_amount;
                } else if (IsTranType(ttype, "Long-term") ||
                           IsTranType(ttype, "(LT)")) {
                    trans->_type = TRAN_LTGAIN;
                    // on a cap gain, the share price is adjusted
                    // down, but this distribution is really a profit,
                    // so it needs to show up somewhere.
                    trans->_ex_lt = trans->_net_amount;
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
                        // trans->_ex_div = trans->_share_price * trans->_share_count;
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
                } else if ((_account_number_ix = ItemsIndex(&items, "Plan Number")) >= 0) {
                    // retirement funds
                    _account_name_ix = ItemsIndex(&items, "Fund Name");
                } else {
                    // Failure
                }

                _account_symbol_ix = ItemsIndex(&items, "Symbol");
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
                _stmt_net_amount_ix = ItemsIndex(&items, "Net Amount");
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

    BackfillBalances();

    return 0;
}

}
