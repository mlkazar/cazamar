#include "cdisp.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

class TestTask : public CDispTask {
    int _level;

public:
    int32_t start() {
        uint32_t i;
        TestTask *childp[2];
        CDispGroup *group = getGroup();

        printf("Task %p at level %d starts\n", this, _level);

        if (_level > 0) {
            for(i=0;i<2;i++) {
                childp[i] = new TestTask(_level-1);
                group->queueTask(childp[i]);
            }
        }

        sleep (1);

        printf(" Task %p at level %d complete\n", this, _level);
        return 0;
    }

    TestTask(int level) {
        _level = level;
    }
};

void
mainCallback(CDisp *disp, void *contextp)
{
    printf("All done callback performed\n");
    exit(0);
}

int
main(int argc, char **argv)
{
    CDisp *disp;
    CDispGroup *group;
    TestTask *taskp;

    disp = new CDisp();
    printf("Created new cdisp at %p\n", disp);
    disp->init(4);

    group = new CDispGroup();
    group->init(disp);
    group->setCompletionProc(mainCallback, NULL);

    printf("Starting tests\n");
    taskp = new TestTask(6);
    group->queueTask(taskp);

    while(1) {
        sleep(1);
    }
}
