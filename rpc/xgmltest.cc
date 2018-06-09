#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include "xgml.h"

void
check(const char *testStringp)
{
    int code;
    Xgml::Node *nodep = 0;
    char *strp;
    Xgml xgmlSys;

    strp = const_cast<char *>(testStringp);
    code = xgmlSys.parse(&strp, &nodep);
    printf("xgml parse code is %d\n", code);
    if (code == 0) {
        nodep->print();
    }
}

int
main(int argc, char **argv)
{
    int fd;
    int code;
    static const int maxLen=100*1024;
    char *namep;

    char *bufferp = (char *) malloc(maxLen);

    if (argc < 2)
        namep = (char *) "xgmltest1.xml";
    else
        namep = argv[1];
    
    fd = open(namep, O_RDONLY, 0666);
    if (fd<0) {
        perror("open 1");
    }
    code = read(fd, bufferp, maxLen);
    if (code < 0 || code >= maxLen) {
        printf("read failure code=%d\n", code);
        return -1;
    }
    bufferp[code] = 0;

    check(bufferp);

    close(fd);

    return 0;
}
