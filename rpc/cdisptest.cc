#include "cdisp.h"

#include <unistd.h>
#include <stdio.h>

class TestTask : public CDispTask {
    int _level;

public:
    int32_t start() {
        uint32_t i;
        TestTask *childp[2];
        CDisp *disp = getDisp();

        printf("Task %p at level %d starts\n", this, _level);

        if (_level > 0) {
            for(i=0;i<2;i++) {
                childp[i] = new TestTask(_level-1);
                disp->queueTask(childp[i]);
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

int
main(int argc, char **argv)
{
    CDisp *disp;
    TestTask *taskp;

    disp = new CDisp();
    disp->init(4);

    printf("Starting tests\n");
    taskp = new TestTask(6);
    disp->queueTask(taskp);

    while(1) {
        sleep(1);
    }
}