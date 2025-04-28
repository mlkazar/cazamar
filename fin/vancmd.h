#ifndef __VANCMD_H_ENV__
#define __VANCMD_H_ENV__ 1

#include "profile.h"
#include "vancmd.h"
#include "vanofx.h"

class VanCmd {
public:
    class Selector {
    public:
        Profile *_profile;
        std::string _from_date; // dates are in YYY-MM-DD format
        std::string _to_date;
        int _verbose;
        Selector() {
            _profile = nullptr;
            _verbose = 0;
        };
    };

    int32_t Balance(VanOfx::User &user, Selector &sel);

    int32_t Gain(VanOfx::User &user, Selector &sel);

    int32_t SetupProfile(VanOfx::User &user, Selector &sel);

    int32_t SetProfs(VanOfx::User &user, Selector &sel) {
        return -1;
    }

    std::string GetProfileDir();

    std::string GetProfilePath();

    std::string GetOfxPath();
};

#endif //__VANCMD_H_ENV__
