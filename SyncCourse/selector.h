#ifndef __SELECTOR_H_ENV__
#define __SELECTOR_H_ENV__ 1

#include <list>
#include <mutex>
#include <string>
#include <thread>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>


class Selector {
public:
    class FdInfo;

    typedef std::function<void(FdInfo *, int)> FdCallback;

    class FdInfo {
    public:
        int _fd;
        FdCallback _callback;

        // For 'flags' implemenetation only: this is the context that
        // has the reader's true protected state.
        //
        // the deleted flag is actually protected by the caller's
        // mutex, in this case, _readerLock in Reader class.  It tells
        // us that the context in the FdCallback structure may point
        // at freed storage, and nothing should be used in it.  In
        // this world, no references are held by the Selector on its
        // user (the Reader object).
        //
        // To make this work, we need the reader lock to either be
        // static, or in this case, since it protects the _deleted
        // flag, we put it in the same structure as the deleted flag.
        bool _flagsDeleted;

        FdInfo() {
            _fd = -1;
            _flagsDeleted = false;
        }
    };
    typedef std::shared_ptr<FdInfo> FdHandle;

    std::thread *_helperThreadp;
    std::list<std::shared_ptr<FdInfo>> _allFds;
    int _wakePair[2];      // 0 index for reader, 1 for writer
    std::mutex _selLock;

    Selector() {
        int32_t code;
        FdHandle fdp;

        pipe(_wakePair);
        code = fcntl(_wakePair[0], F_SETFD, O_NONBLOCK);
        assert(code == 0);
        fdp = std::make_shared<FdInfo>();
        fdp->_fd = _wakePair[0];        // read side of the pipe
        fdp->_callback = [this](FdInfo *fdip, int fd) {
            // drain the buffer; should typcially just hold a single character.
            char tbuffer[8];
            while(true) {
                int32_t code;
                code = read(_wakePair[0], tbuffer, sizeof(tbuffer));
                if (code <= 0)
                    return;
            }
        };

        _helperThreadp = new std::thread([this]() {
            helper();
        });
    }

    FdHandle monitor(int fd, FdCallback callback) {
        std::lock_guard<std::mutex> guard(_selLock);
        FdHandle fdp = std::make_shared<FdInfo>();
        fdp->_fd = fd;
        fdp->_callback = callback;
        _allFds.push_back(fdp);
        wakeHelper();
        return fdp;
    }

    void unmonitor(FdHandle fdHandlep) {
        std::lock_guard<std::mutex> guard(_selLock);
        _allFds.remove(fdHandlep);

        // This flag is only used by the 'flags' implementation of the
        // reader.  But no one else pays any attention to it, so we
        // can set it here independent of the locking scheme
        fdHandlep->_flagsDeleted = true;

        wakeHelper();
    }

    void
    wakeHelper() {
        write(_wakePair[1], "x", 1);
    }

    void helper() {
        std::list<FdHandle>::iterator it;
        pollfd pollFds[256];
        FdHandle contextsp[256];
        uint32_t ix;
        int32_t code;
        uint32_t activeCount;
        uint32_t i;

        _selLock.lock();
        while(true) {
            pollFds[0].fd = _wakePair[0];
            pollFds[0].events = POLLIN;
            pollFds[0].revents = 0;
            for(it = _allFds.begin(), ix=1; it != _allFds.end(); it++, ix++) {
                assert(ix < 256);
                pollFds[ix].fd = (*it)->_fd;
                pollFds[ix].events = POLLIN;
                pollFds[ix].revents = 0;
                contextsp[ix] = (*it);
            }
            activeCount = ix;

            _selLock.unlock();
            code = poll(pollFds, activeCount, 1000);
            if (code < 0) {
                perror("poll");
                exit(1);
            }
            _selLock.lock();

            if (code == 0) {
                // no FDs are ready
                continue;
            }

            // don't scan notification FD
            for(i=1;i<activeCount;i++) {
                assert(i<256);
                if ((pollFds[i].revents & POLLIN) != 0) {
                    // drop lock across callbacks so they can call
                    // back in to us.
                    
                    _selLock.unlock();
                    contextsp[i]->_callback(contextsp[i].get(), contextsp[i]->_fd);
                    _selLock.lock();
                }
            }
        }
    }
};
    
#endif // __SELECTOR_H_ENV__
