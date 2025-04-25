#ifndef __VANCMD_H_ENV__
#define __VANCMD_H_ENV__ 1

#include "vancmd.h"
#include "vanofx.h"

class VanCmd {
public:
    int32_t Balance(VanOfx::User &user);

    int32_t Gain(VanOfx::User &user);

    int32_t InitProfile(VanOfx::User &user);

    int32_t SetProfs(VanOfx::User &user) {
        return -1;
    }

    std::string GetProfileDir();

    std::string GetProfilePath();

};

#endif //__VANCMD_H_ENV__
