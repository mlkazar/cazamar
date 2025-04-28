#ifndef _VAN_OFX_H_ENV__
#define _VAN_OFX_H_ENV__ 1

#include <functional>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "yfdriver.h"

namespace VanOfx {

class Transaction;
class User;
class Fund;

typedef enum {
    TRAN_INVAL = 0,
    TRAN_BUY = 1,
    TRAN_SELL = 2,
    TRAN_DIV = 3,
    TRAN_LTGAIN = 4,
    TRAN_STGAIN = 5,
    TRAN_NOOP = 6
    
} TransType;

class Transaction {
 public:
    TransType _type;            // transaction type
    std::string _fund_name;
    std::string _date;          // in seconds to start of day GMT
    double _share_count;        // amount of transaction in USD
    double _share_price;        // share price
    double _net_amount;         // value of net amount field
    double _ex_div;             // gain beyond share price, or additional shares.
    double _ex_lt;              // LT capital gains
    double _ex_st;              // ST capital gains
    double _pre_share_count;    // share balance before transaction

    Transaction() {
        _type = TRAN_INVAL;
        _share_count = 0;
        _share_price = 0;
        _net_amount = 0;
        _ex_div = 0;
        _ex_lt = 0;
        _ex_st = 0;
        _pre_share_count = 0;
    }
};

class Gain {
public:
    double _qualified_divs;
    double _regular_divs;
    double _unrealized_cg;
    double _realized_cg;
    double _short_term_dist;
    double _long_term_dist;
    double _tax_free_divs;
    double _ira_divs;
    double _ira_gains;

    Gain() {
        _qualified_divs = 0;
        _regular_divs = 0;
        _unrealized_cg = 0;
        _realized_cg = 0;
        _short_term_dist = 0;
        _long_term_dist = 0;
        _tax_free_divs = 0;
        _ira_divs = 0;
        _ira_gains = 0;
    }

    const Gain operator +(const Gain &other) {
        Gain rval;
        rval._qualified_divs = this->_qualified_divs + other._qualified_divs;
        rval._regular_divs = this->_regular_divs + other._regular_divs;
        rval._unrealized_cg = this->_unrealized_cg + other._unrealized_cg;
        rval._realized_cg = this->_realized_cg + other._realized_cg;
        rval._short_term_dist = this->_short_term_dist + other._short_term_dist;
        rval._long_term_dist = this->_long_term_dist + other._long_term_dist;
        rval._tax_free_divs = this->_tax_free_divs + other._tax_free_divs;
        rval._ira_divs = this->_ira_divs + other._ira_divs;
        rval._ira_gains = this->_ira_gains + other._ira_gains;

        return rval;
    }

    Gain operator +=(const Gain &other) {
        Gain rval;
        this->_qualified_divs += other._qualified_divs;
        this->_regular_divs += other._regular_divs;
        this->_unrealized_cg += other._unrealized_cg;
        this->_realized_cg += other._realized_cg;
        this->_short_term_dist += other._short_term_dist;
        this->_long_term_dist += other._long_term_dist;
        this->_tax_free_divs += other._tax_free_divs;
        this->_ira_divs += other._ira_divs;
        this->_ira_gains += other._ira_gains;

        return *this;
    }

    double GetRegularIncome() {
        return _qualified_divs + _regular_divs + _tax_free_divs + _ira_divs;
    }

    double GetIrregularIncome() {
        return _realized_cg + _unrealized_cg + _short_term_dist + _long_term_dist + _ira_gains;
    }

    double GetAllIncome() {
        return GetRegularIncome() + GetIrregularIncome();
    }
};


class Fund {
public:
    // Basic information about a fund
    std::string _name;
    std::string _symbol;
    double _share_count;
    double _share_price;
    std::vector<Transaction *> _trans;
    User *_user;
    int _is_ira;        // a retirement account
    int _is_bond;       // not an equity account
    int _is_tax_free;   // bond fund with tax free divs

    Fund(User *user, std::string name) {
        _name = name;
        _user = user;
        _is_ira = 0;
        _is_bond = 0;
        if (strcasestr(name.c_str(), "bond") || strcasestr(name.c_str(), "money")) {
            _is_bond = 1;
        }
        _is_tax_free = 0;
        if (strcasestr(name.c_str(), "tax exempt")) {
            _is_tax_free = 1;
            _is_bond = 1;
        }
    }

    int32_t ApplyToTrans(std::function<int32_t(Transaction *)>func);

    int32_t GainDollars(std::string from_date, std::string to_date, int verbose, Gain *gain);
};

class Account {
public:
    typedef std::list<Fund *> FundList;
    std::string _number;
    FundList _funds;
    User *_user;

    Account(User *user) {
        _user = user;
    }

    Fund *GetFundByName(std::string fund_name);

    Fund *GetFundBySymbol(std::string symbol);

    int32_t ApplyToFunds(std::function<int32_t(Fund *)> func);
};

class User {
    typedef std::map<std::string, Account *> AccountMap;
    typedef std::map<std::string, std::string> NumberMap;

    // Parsing state
    //
    // Generally, there are section pairs.  One section describes a
    // number of accounts, each having a name, a number and a current
    // balance and share price.  Each account section is followed by a
    // transaction section, describing each transaction on one of the
    // accounts in the previous account sectiom.
    //
    // Each section starts off with a text line describing the fields
    // in the section.  We will parse this line to figure out where
    // each field we care about is located.
    //
    // For accounts, we care about the account #, account name, share
    // count and share price.
    int _model_funds; // or model statements
    int _account_number_ix;
    int _account_number_includes_fund;
    int _account_name_ix;
    int _account_symbol_ix;
    int _account_share_count_ix;
    int _account_share_price_ix;
    int _stmt_number_ix;
    int _stmt_name_ix;
    int _stmt_trade_date_ix;
    int _stmt_trans_type_ix;
    int _stmt_share_price_ix;
    int _stmt_share_count_ix;
    int _stmt_net_amount_ix;

    AccountMap _accounts;
    NumberMap _fund_map;
    YFDriver _yfdriver;

public:

    User();

    YFDriver *GetYF() {
        return &_yfdriver;
    }

    int32_t ParseOfx(std::string file_name);
    void ResetAccountState() {
        _account_number_ix = -1;
        _account_number_includes_fund = -1;
        _account_name_ix = -1;
        _account_symbol_ix = -1;
        _account_share_count_ix = -1;
        _account_share_price_ix = -1;
    }

    void ResetStmtState() {
        _stmt_number_ix = -1;
        _stmt_name_ix = -1;
        _stmt_trade_date_ix = -1;
        _stmt_trans_type_ix = -1;
        _stmt_share_price_ix = -1;
        _stmt_share_count_ix = -1;
    }

    int32_t BackfillBalances();

    Account *GetAccount(std::string account_number);

    int32_t PrintFundBalances();

    int32_t ApplyToAccounts(std::function<int32_t(Account *)>func);

    std::string LookupFundByNumber(std::string fund_number);
};

} // Namespace

#endif // _VAN_OFX_H_ENV__
