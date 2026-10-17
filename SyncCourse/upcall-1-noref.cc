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

// This version has internal locking, and tries to stop upcalls from
// the Selector class, but still has races where a pending event gets
// delivered to a deleted object.  It doesn't make use of reference
// counts anywhere.

typedef std::function<void(int)> FdCallback;

class SharedState {
public:
    std::mutex _demo;
    uint32_t _counter;
    uint32_t _races;

    SharedState() {
        _counter = 0;
        _races = 0;
    }
};

class Selector {
public:
    class FdInfo {
    public:
        int _fd;
        FdCallback _callback;
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
        fdp->_callback = [this](int fd) {
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
                    contextsp[i]->_callback(contextsp[i]->_fd);
                    _selLock.lock();
                }
            }
        }
    }
};
    
class Reader : public std::enable_shared_from_this<Reader>{
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

    Reader(int fd, Selector *selp, SharedState *sharedp) {
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
        
        _monitorHandle = _selp->monitor(_fd, [this](int fd) {
            char tbuffer[64];
            int32_t code;
            uint32_t spins;

            // turn into a strong reference, and detect if the object
            // (us) was actually already deleted.
            // std::shared_ptr<Reader> moi = strong_this;
            Reader *moi = this;
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
        std::shared_ptr<Reader> p = std::make_shared<Reader>(fd, selp, sharedp);
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

    ~Reader() {
        _sharedp->_demo.lock();
        (_sharedp->_counter)--;
        _sharedp->_demo.unlock();
        stop();
        _magic = kMagic+1;
    }
};

// This runs in a separate thread, writes some records 
class Writer {
public:
    int _fd;
    bool _running;
    bool _stop;

    Writer(int fd) {
        _fd = fd;
        _running = true;
        _stop = false;
    }

    void stop() {
        _stop = true;
    }

    void start() {
        char tbuffer[128];
        char hbuffer[1];
        int32_t code;

        memset(tbuffer, 'a', sizeof(tbuffer));
        hbuffer[0] = 'b';
        while(!_stop) {
            code = write(_fd, hbuffer, sizeof(hbuffer));
            if (code != 1) {
                break;
            }
            code = write(_fd, tbuffer, sizeof(tbuffer));
            if (code != sizeof(tbuffer))
                break;
        }
        _running = false;
        // exit thread by returning to calling block.
    }

    ~Writer() {
        close(_fd);
    }
};

class Tester {
public:
    Selector *_selp;
    static const uint32_t _maxIOs = 8;
    std::shared_ptr<Reader> _readers[_maxIOs];
    Writer *_writers[_maxIOs];
    std::thread *_writerThreadp[_maxIOs];
    SharedState _shared;

    Tester(Selector *selp) {
        uint32_t i;
        _selp = selp;
    }

    void start(bool doDeletes) {
        uint32_t i;
        int pipePair[2];
        Writer *writerp;
        
        for(i=0;i<_maxIOs;i++) {
            pipe(pipePair);
            _readers[i] = Reader::create(pipePair[0], _selp, &_shared);

            _writers[i] = writerp = new Writer(pipePair[1]);
            _writerThreadp[i]= new std::thread([this, writerp]() {
                writerp->start();
                // thread exits
            });
        }

        if (!doDeletes)
            return;

        while(true) {
            uint32_t ix = random() % _maxIOs;

            std::shared_ptr<Reader> readerp = _readers[ix];

            readerp->stop();
            _readers[ix] = nullptr;

            _writerThreadp[ix]->join();
            _writerThreadp[ix] = nullptr;
            delete _writers[ix];
            _writers[ix] = nullptr;

            // and recreate
            pipe(pipePair);
            _readers[ix] = Reader::create(pipePair[0], _selp, &_shared);
            writerp = new Writer(pipePair[1]);
            _writers[ix] = writerp;
            _writerThreadp[ix] = new std::thread([this, writerp]() {
                writerp->start();
                // thread exits
            });
            usleep(20000);
        }
    }
};

int32_t
main(int argc, char **argv) {
    Selector sel;
    Tester *testerp = new Tester(&sel);

    bool doDeletes = true;
    if (argc > 1 && strcmp(argv[1], "-n") == 0)
        doDeletes = false;
        

    signal(SIGPIPE, SIG_IGN);
    testerp->start(doDeletes);
    while(true)
        sleep(1);
}
