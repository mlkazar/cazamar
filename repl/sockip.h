#ifndef __SOCK_LOCAL_H_ENV__
#define __SOCK_LOCAL_H_ENV__ 1

#include <pthread.h>
#include "sock.h"

typedef SockNode SockLocalNode;

typedef std::map<std::string,std::shared_ptr<SockLocalConn>> SockLocalOutConnMap;
typedef std::map<int,std::shared_ptr<SockLocalConn>> SockLocalInConnMap;

class SockLocalSys : public SockSys {
 private:
    SockLocalOutConnMap _outConnMap;
    SockLocalInConnMap _inConnMap;

 public:
    std::shared_ptr<SockLocalConn> getLocalOutConn(std::string nodeName) {
        std::shared<SockLocalConn> connp;
        SockLocalOutConnMap::iterator it;

        it = _outConnMap.find(nodeName);
        if (it != _nodeMap.end()) {
            /* we already have an entry */
            return it->second();
        }

        /* otherwise, we create a new node */
        nodep = SockLocalConn::getLocalConn(-1, nodeName);
        _outConnMap[nodeName] = nodep;

        return nodep;
    }

    std::shared_ptr<SockLocalConn> getLocalOutConn( int sfd) {
        std::shared<SockLocalConn> connp;
        SockLocalInConnMap::iterator it;

        it = _inConnMap.find(sfd);
        if (it != _nodeMap.end()) {
            /* we already have an entry */
            return it->second();
        }

        /* otherwise, we create a new node */
        nodep = SockLocalConn::getLocalConn(sfd, "Incoming");
        _inConnMap[nodeName] = nodep;

        return nodep;
    }
};

#endif /* __SOCK_LOCAL_H_ENV__ */
