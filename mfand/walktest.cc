#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "walkdisp.h"

int32_t
mainCallback(void *contextp, std::string *pathp, struct stat *statp)
{
    const char *typep;
    if ((statp->st_mode & S_IFMT) == S_IFDIR)
        typep = "dir";
    else if ((statp->st_mode & S_IFMT) == S_IFREG)
        typep = "file";
    else if ((statp->st_mode & S_IFMT) == S_IFLNK)
        typep = "symlink";
    printf("%s at path=%s mode=%o size=%ld\n",
           typep, pathp->c_str(), statp->st_mode, (long) statp->st_size);
    if ((statp->st_mode & S_IFMT) != S_IFDIR && statp->st_nlink > 1)
        printf("  has %d links\n", statp->st_nlink);
    return 0;
}

int
main(int argc, char **argv)
{
    CDisp *disp;
    CDispGroup *group;
    WalkTask *taskp;
    int nhelpers = 4;

    if (argc < 2) {
        printf("usage: walktest <path> [nhelpers] \n");
        return -1;
    }

    if (argc > 2)
        nhelpers = atoi(argv[2]);
    if (nhelpers <= 0)
        nhelpers = 1;

    disp = new CDisp();
    printf("Created new cdisp at %p\n", disp);
    disp->init(nhelpers);
    group = new CDispGroup();
    group->init(disp);

    printf("Starting tests\n");
    taskp = new WalkTask();
    taskp->initWithPath(std::string(argv[1]));
    taskp->setCallback(&mainCallback, NULL);
    group->queueTask(taskp);

    while(!group->isAllDone())
        sleep(1);

    return 0;
}
