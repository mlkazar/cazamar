#ifndef __JWT_H_ENV__
#define __JWT_H_ENV__ 1

#include <string>

class Jwt {
    static int doChar(int);

 public:
    static std::string decode64(std::string a);

    static int32_t decode(std::string token, std::string *resultp);
};

#endif /* __JWT_H_ENV__ */
