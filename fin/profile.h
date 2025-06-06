#ifndef __PROFILE_H_ENV__
#define __PROFILE_H_ENV__ 1

#include <string>
#include <vector>
#include <map>

#include <inttypes.h>

#include "json.h"
#include "vanofx.h"

// The profile file contains a dictionary with two objects.  One is
// labelled profiles with a corresponding object consisting of an
// array of profiles.
//
// Each profile in the list is a dictionary with a name field giving
// the profile name as a string, and an accounts field whose value is
// an array of account numbers, each represented as a string.
//
// The second field in the file's dictionary is labelled
// accounts, whose value is an array of ProfileAccount
// objects.  Each ProfileAccount object is represented as a dictionary
// with an account_number field (string) and an account_name.
//
// Once the profile file is loaded, we post process it by finding the
// account names in the VanOfx::User structure, and putting in
// references to the Account structures in the ProfileAccount
// structure.

class Profile;
class ProfileAccount;

// One of these per user, points to the User structure containing accounts.  Contains
// multiple Profile objects, each with a name and a set of accounts that are included
// in the profile.
class ProfileUser {
    // Map profile names to Profile objects.
    typedef std::map<std::string, Profile *> ProfileMap;
    typedef std::map<std::string, ProfileAccount *> ProfileAccountMap;

    ProfileMap _profile_map;
    ProfileAccountMap _account_map;

    // Save this in case we make updates to parts
    Json::Node *_root_node;

 public:
    ProfileUser() {
        _root_node = nullptr;
    }

    int32_t Init(std::string profile_path);

    int32_t Save(std::string profile_path);

    void Print();

    ProfileAccount *AddAccount(std::string account_number,
                               std::string account_name);

    ProfileAccount *FindAccount(std::string account_number);

    Profile *GetProfile(std::string profile_name);

    Profile *FindProfile(std::string profile_name);
};

// One of these for each account referenced from a profile.
class ProfileAccount {
public:
    std::string _account_number;
    std::string _account_name;
    VanOfx::Account *_account;
    uint8_t _is_ira;
    ProfileUser *_prof_user;

    ProfileAccount(std::string account_number, ProfileUser *prof_user) {
        _prof_user = prof_user;
        _account_number = account_number;
        _is_ira = 0;
    }

    int isIra() {
        return _is_ira;
    }

    ProfileUser *GetUser() {
        return _prof_user;
    }
};

class Profile {
public:
    std::string _name;          // Profile name
    std::list<ProfileAccount *> _accounts;
    ProfileUser *_prof_user;

    Profile(std::string name, ProfileUser *prof_user) {
        _prof_user = prof_user;
        _name = name;
    }

    ProfileUser *GetUser() {
        return _prof_user;
    }

    void AddAccount(ProfileAccount *account);

    int ContainsAccount(ProfileAccount *account);
};

#endif // __PROFILE_H_ENV__
