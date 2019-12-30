#ifndef __BUFFACTORY_H_ENV_
#define __BUFFACTORY_H_ENV_ 1

#include "bufsocket.h"
#include "buftls.h"

class BufTlsFactory : public BufGenFactory {
 public:
    BufGen *allocate(int secure) {
        if (secure)
            return new BufTls("");
        else
            return new BufSocket();
    }
};

#endif /* __BUFFACTORY_H_ENV_ */
