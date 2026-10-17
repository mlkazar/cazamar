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

#include "selector.h"

#include "reader1-norefs.h"
#include "reader2-strong.h"
#include "reader3-weak.h"
#include "reader4-flags.h"

typedef std::shared_ptr<Reader> CreateProc(int fd, Selector *selp, SharedState *sharedp);

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

    // createProcp is typically one of Reader<n>::create, a static
    // factory for reader instances.
    void start(CreateProc *createProcp, bool doDeletes) {
        uint32_t i;
        int pipePair[2];
        Writer *writerp;
        
        for(i=0;i<_maxIOs;i++) {
            pipe(pipePair);
            _readers[i] = createProcp(pipePair[0], _selp, &_shared);

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
            _readers[ix] = createProcp(pipePair[0], _selp, &_shared);
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
    uint32_t i;

    bool doDeletes = true;
    int32_t readerType;
    CreateProc *createProcp = nullptr;

    for(i=1;i<argc;i++) {
        if (strcmp(argv[i], "-n") == 0)
            doDeletes = false;
        else if (strcmp(argv[i], "-1") == 0)
            createProcp = Reader1::create;
        else if (strcmp(argv[i], "-2") == 0)
            createProcp = Reader2::create;
        else if (strcmp(argv[i], "-3") == 0)
            createProcp = Reader3::create;
        else if (strcmp(argv[i], "-4") == 0) 
            createProcp = Reader4::create;
    }

    signal(SIGPIPE, SIG_IGN);
    testerp->start(createProcp, doDeletes);
    while(true)
        sleep(1);
}
