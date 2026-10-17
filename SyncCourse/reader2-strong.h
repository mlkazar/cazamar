#ifndef __READER2_H_ENV__
#define __READER2_H_ENV__ 1

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

#include "selector.h"
#include "reader.h"

class Reader2 : public Reader {
public:
    static const uint32_t kMagic = 0x12345678;
    uint32_t _magic;
    int _fd;
    uint64_t _bytesRead;
    uint64_t _callbacks;
    uint64_t _lastPrinted;
    Selector *_selp;
    Selector::FdHandle _monitorHandle;
    SharedState *_sharedp;
    std::mutex _readerLock;

    Reader2(int fd, Selector *selp, SharedState *sharedp) {
        int32_t code;
        _magic = kMagic;
        _fd = fd;
        _selp = selp;
        _sharedp = sharedp;
        sharedp->_demo.lock();
        sharedp->_counter++;
        sharedp->_demo.unlock();
    }

    void init() {
        int32_t code;

        code = fcntl(_fd, F_SETFD, O_NONBLOCK);
        if (code < 0) {
            perror("fcntl reader");
            return;
        }
        
        _bytesRead = 0;
        _callbacks = 0;
        _lastPrinted = 0;
        
        std::shared_ptr<Reader2> strong_this(std::static_pointer_cast<Reader2>(shared_from_this()));
        _monitorHandle = _selp->monitor(_fd, [strong_this](Selector::FdInfo *fdip, int fd) {
            char tbuffer[64];
            int32_t code;
            uint32_t spins;

            // turn into a strong reference, and detect if the object
            // (us) was actually already deleted.
            std::shared_ptr<Reader2> moi = strong_this;
            if (moi == nullptr)
                return;

            assert(moi->_magic == kMagic);

            std::lock_guard<std::mutex> guard(moi->_readerLock);
            if (moi->_fd < 0) {
                moi->_sharedp->_races++;
                return;
            }
            spins = 0;
            while (true) {
                code = read(moi->_fd, tbuffer, sizeof(tbuffer));
                if (code <= 0)
                    break;
                moi->_bytesRead += code;
                moi->_callbacks++;

                // sometimes we can't keep up with the writer, so we spin
                // here forever.  But we want to print out.
                if (++spins > 1000000)
                    break;
            }

            moi->_sharedp->_demo.lock();
            uint32_t stableCounter = moi->_sharedp->_counter;
            moi->_sharedp->_demo.unlock();

            uint64_t now = time(0);;
            if (now > moi->_lastPrinted + 4) {
                printf("fd=%d: %lld bytes in %lld calls (objs=%d, races=%d)\n",
                       moi->_fd, moi->_bytesRead, moi->_callbacks, stableCounter,
                       moi->_sharedp->_races);
                moi->_lastPrinted = now;
            }});
    }

    static std::shared_ptr<Reader> create(int fd, Selector *selp, SharedState *sharedp) {
        // we have to split creation into two parts so that we have a good shared
        // pointer before we try (in init) to make a weak pointer from it.
        std::shared_ptr<Reader2> p = std::make_shared<Reader2>(fd, selp, sharedp);
        p->init();
        return p;
    }

    void stop() {
        std::lock_guard<std::mutex> guard(_readerLock);

        if (_fd >= 0) {
            close(_fd);
            _fd = -1;
        }

        if (_monitorHandle != nullptr) {
            _selp->unmonitor(_monitorHandle);
            _monitorHandle.reset();
        }
    }

    ~Reader2() {
        _sharedp->_demo.lock();
        (_sharedp->_counter)--;
        _sharedp->_demo.unlock();
        stop();
        _magic = kMagic+1;
    }
};

#endif //reader2
