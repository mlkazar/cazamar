#include <string>

#include <stdio.h>

#include "profile.h"

int32_t
ProfileUser::Init(std::string profile_path) {
    Json json;
    Json::Node *root_node;
    FILE *input;
    int32_t code;

    input = fopen(profile_path.c_str(), "r");
    if (!input)
        return -1;

    code = json.parseJsonFile(input, &root_node);
    if (code) {
        fclose(input);
        return -2;
    }
    fclose(input);

    // Create appropriate structures
    Json::Node *accounts_node = root_node->searchForChild("accounts");
    if (!accounts_node)
        return 0;

    Json::Node *anode;
    for(anode = accounts_node->_children.head(); anode; anode=anode->_dqNextp) {
        std::string acct_number;
        std::string acct_name;
        Json::Node *tnode;

        // anode is an array, so we have to walk through all the
        // elemets in the array.
        Json::Node *elt_node;
        for(elt_node = anode->_children.head(); elt_node; elt_node=elt_node->_dqNextp) {
            tnode = elt_node->searchForChild("account_number");
            if (tnode == nullptr) {
                printf("bad internal account node missing account_number field\n");
                continue;
           }
            acct_number = tnode->getString();

            tnode = elt_node->searchForChild("account_name");
            if (tnode == nullptr) {
                printf("bad internal account node missing account_name field\n");
                continue;
            }
            acct_name = tnode->getString();
            (void) AddAccount(acct_number, acct_name);
        }
    }

    Json::Node *profiles_node = root_node->searchForChild("profiles");
    if (!profiles_node) {
        printf("Saw accounts, but no profile definitions\n");
        return 0;
    }
    // The profiles node is an array of structs, each with a name field
    // giving the profile name, and an array of account number strings.
    Json::Node *dnode;
    for ( dnode = profiles_node->_children.head();
          dnode;
          dnode=dnode->_dqNextp) {
        Json::Node *elt_node;
        for(elt_node = dnode->_children.head(); elt_node; elt_node=elt_node->_dqNextp) {
            Json::Node *tnode = elt_node->searchForChild("name");
            if (!tnode) {
                printf("internal error -- no name node in profile definition\n");
                continue;
            }
            std::string profile_name = tnode->getString();
            accounts_node = elt_node->searchForChild("accounts");
            if (accounts_node) {
                printf("internal error -- no accounts node in profile definition\n");
                continue;
            }
            // this should be an array of string leaf nodes, each giving an account
            // number that's part of the profile.
            Profile *profile = GetProfile(profile_name);
            for( Json::Node *tnode = accounts_node->_children.head();
                 tnode;
                 tnode=tnode->_dqNextp) {
                ProfileAccount *prof_account = FindAccount(tnode->_name);
                if (prof_account == nullptr) {
                    printf("internal error -- can't find profile's account %s\n",
                           tnode->_name.c_str());
                    continue;
                }
                profile->AddAccount(prof_account);
            }
        }
    }

    return 0;
}

int32_t
ProfileUser::Save(std::string profile_path) {
    Json::Node *root_node = new Json::Node();
    int32_t code;
    int32_t tcode;
    Json::Node *struct_node;
    Json::Node *profiles_array_node;
    Json::Node *array_node;
    Json::Node *string_node;

    root_node->initStruct();

    // The account element in the root structure is an array of struct
    // nodes, each containing an account_name, account_number and is_ira
    // field.

    Json::Node *account_array_node = new Json::Node();
    account_array_node->initArray();
    Json::Node *named_node = new Json::Node();
    named_node->initNamed("accounts", account_array_node);
    root_node->appendChild(named_node);

    ProfileAccountMap::iterator ait;
    for(ait = _account_map.begin(); ait != _account_map.end(); ++ait) {
        ProfileAccount *prof_account = ait->second;
        struct_node = new Json::Node();
        struct_node->initStruct();

        Json::Node *child_node = new Json::Node();
        named_node = child_node->initStringPair("account_number",
                                                prof_account->_account_number.c_str(),
                                                1);
        struct_node->appendChild(named_node);

        child_node = new Json::Node();
        named_node = child_node->initStringPair("account_name",
                                                prof_account->_account_name.c_str(),
                                                1);
        struct_node->appendChild(named_node);

        child_node = new Json::Node();
        named_node = child_node->initIntPair("is_ira", prof_account->_is_ira);
        struct_node->appendChild(named_node);

        // Now add the structure to the array
        account_array_node->appendChild(struct_node);
    }

    // Create nodes for profile definitions.  The 'profiles' node
    // contains an array of structs, each of which contains a
    // string node named 'name' with the profile name, and a named
    // node named 'accounts' which is an array of string value nodes
    // each giving an account number.
    profiles_array_node = new Json::Node();
    profiles_array_node->initArray();
    named_node = new Json::Node();
    named_node->initNamed("profiles", profiles_array_node);

    root_node->appendChild(named_node);

    ProfileMap::iterator pit;
    for(pit = _profile_map.begin(); pit != _profile_map.end(); ++pit){
        Profile *profile = pit->second;

        struct_node = new Json::Node();
        struct_node->initStruct();

        // put in name
        Json::Node *child_node = new Json::Node();
        named_node = child_node->initStringPair("name", profile->_name.c_str(), 1);
        struct_node->appendChild(named_node);

        // create array of strings
        array_node = new Json::Node();
        array_node->initArray();
        named_node = new Json::Node();
        named_node->initNamed("accounts", array_node);

        std::list<ProfileAccount *>::iterator nit;
        for(nit = profile->_accounts.begin(); nit != profile->_accounts.end(); ++nit){
            ProfileAccount *prof_account = *nit;
            string_node = new Json::Node();
            string_node->initString(prof_account->_account_number.c_str(), 1);
            array_node->appendChild(string_node);
        }
        struct_node->appendChild(named_node);

        // and append the newly formatted structure to the array.
        profiles_array_node->appendChild(struct_node);
    }

    // Write out structure
    std::string result;
    FILE *output_file;
    output_file = fopen(profile_path.c_str(), "w");
    if (output_file == nullptr) {
        printf("Failed to open %s\n", profile_path.c_str());
        return -1;
    }
    root_node->unparse(&result, 1);
    code = fwrite(result.c_str(), result.length(), 1, output_file);
    if (code == 1)
        code = 0;
    else
        code = -1;

    tcode = fclose(output_file);
    if (code == 0 && tcode != 0)
        code = tcode;

    // Free Json structure
    delete root_node;

    return code;
}

void
ProfileUser::Print() {
    ProfileMap::iterator prof_it;
    ProfileAccountMap::iterator acct_it;
    Profile *profile;
    ProfileAccount *acct;

    for(prof_it = _profile_map.begin(); prof_it != _profile_map.end(); ++prof_it) {
        profile = prof_it->second;
        printf("Profile name=%s with accounts:\n", profile->_name.c_str());
        std::list<ProfileAccount *>::iterator it;
        for(it = profile->_accounts.begin(); it != profile->_accounts.end(); ++it) {
            printf("  account name %s\n", (*it)->_account_name.c_str());
        }
    }

    for(acct_it = _account_map.begin(); acct_it != _account_map.end(); ++acct_it) {
        acct = acct_it->second;

        printf("Acct name=%s number=%s is_ira=%d\n",
               acct->_account_name.c_str(),
               acct->_account_number.c_str(),
               acct->_is_ira);
    }
}

ProfileAccount *
ProfileUser::AddAccount(std::string account_number, std::string account_name) {
    ProfileAccount *account = new ProfileAccount(account_number);
    account->_account_name = account_name;
    _account_map[account_number] = account;

    return account;
}

Profile *ProfileUser::GetProfile(std::string profile_name) {
    ProfileMap::iterator it;
    it = _profile_map.find(profile_name);
    if (it == _profile_map.end()) {
        Profile *profile = new Profile(profile_name);
        _profile_map[profile_name] = profile;
        return profile;
    } else {
        return it->second;
    }
}

ProfileAccount *
ProfileUser::FindAccount(std::string account_number) {
    ProfileAccountMap::iterator it;

    it = _account_map.find(account_number);
    if (it == _account_map.end())
        return nullptr;
    return it->second;
}

void
Profile::AddAccount(ProfileAccount *account) {
    _accounts.push_back(account);
}
