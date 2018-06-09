#ifndef __STREAMS_H_ENV__
#define __STREAMS_H_ENV__ 1

#include "json.h"

class FileInStream : public InStream {
    FILE *_inFilep;
    int _buffer;

 public:
    int top();

    void next();

    void fillBuffer();

    FileInStream(FILE *filep);

    virtual ~FileInStream() {
        if (_inFilep)
            fclose(_inFilep);
    }
};

#endif /* __STREAMS_H_ENV__ */
