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
    double _ex_gain;            // gain beyond share price, or additional shares.
    double _pre_share_count;    // share balance before transaction
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

    Fund(User *user, std::string name) {
        _name = name;
        _user = user;
    }

    int32_t ApplyToTrans(std::function<int32_t(Transaction *)>func);

    double GainDollars(std::string from_date, std::string to_date);
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
