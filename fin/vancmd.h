#ifndef __VANCMD_H_ENV__
#define __VANCMD_H_ENV__ 1

#include "profile.h"
#include "vancmd.h"
#include "vanofx.h"

class VanCmd {
public:
    int32_t Balance(VanOfx::User &user, Profile *prof);

    int32_t Gain(VanOfx::User &user, Profile *prof);

    int32_t SetupProfile(VanOfx::User &user, Profile *prof);

    int32_t SetProfs(VanOfx::User &user) {
        return -1;
    }

    std::string GetProfileDir();

    std::string GetProfilePath();

    std::string GetOfxPath();
};

#endif //__VANCMD_H_ENV__
