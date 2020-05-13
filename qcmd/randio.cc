#include <fcntl.h>
#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include "dqueue.h"
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>

int
main(int argc, char **argv)
{
    static const uint32_t blockSize = 4096;
    uint64_t blockOffset;
    int fd;
    char tbuffer[blockSize];
    uint64_t blockCount;
    uint64_t blockIx;
    char *fileNamep;
    long code;
    char mode;

    if (argc < 4) {
        printf("usage: randio r/w <filename> <size-in-4K blocks>\n");
        exit(1);
    }

    mode = argv[1][0];
    fileNamep = argv[2];
    blockCount = atoi(argv[3]);

    fd = open(fileNamep, (mode == 'w'? O_CREAT | O_RDWR | O_SYNC : O_RDONLY), 0666);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    srandom(4+getpid());

    while(1) {
        blockOffset = (long) (random() % blockCount) * blockSize;
        lseek(fd, blockOffset, SEEK_SET);
        if (mode == 'w')
            code = write(fd, tbuffer, sizeof(tbuffer));
        else
            code = read(fd, tbuffer, sizeof(tbuffer));
    }
}
