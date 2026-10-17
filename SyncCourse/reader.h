#ifndef _READER_H_ENV__
#define _READER_H_ENV__ 1

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

// Stuff shared by all readers.

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

class Reader : public std::enable_shared_from_this<Reader> {
public:
    int junk;

    Reader() {};

    virtual void stop() = 0;

    virtual ~Reader() {};
};

#endif // _READER_H_ENV__
