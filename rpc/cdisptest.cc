#include "cdisp.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

CDisp *main_disp;

void
main_test2a(CDisp *disp, void *contextp)
{
    printf("main_test2a done\n");
    exit(0);
}

class TestTask2a : public CDispTask {
public:
    int32_t start() {
        TestTask2a * taskp = new TestTask2a();
        CDispGroup *group;

        printf("TestTask2a runs\n");
        sleep(1);
        group = getGroup();
        group->queueTask(taskp);
        return 0;
    }

    ~TestTask2a() {}
};

class TestTask2 : public CDispTask {
public:
    int32_t start() {
        uint32_t i;
        CDispGroup *group;
        TestTask2a *taskp;

        group = getGroup();
        group->setCompletionProc(main_test2a, NULL);

        taskp = new TestTask2a();
        group->queueTask(taskp);

        for(i=0;i<3;i++) {
            sleep(4);
            printf("Pausing 2a for 5 seconds\n");
            group->pause(1);
            sleep(4);
            printf("Resuming 2a\n");
            group->resume();
        }

        sleep(4);

        printf("Stopping 2a\n");
        group->stop(1);
        sleep(4);
        return 0;
    }
};

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
                group->queueTask(childp[i], (i&1));
            }
        }

        //sleep (1);

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
    CDispGroup *group;
    TestTask2 *taskp;

    printf("All done test1 callback performed\n");

    group = new CDispGroup();
    group->init(disp);

    taskp = new TestTask2();
    group->queueTask(taskp);
}

int
main(int argc, char **argv)
{
    CDisp *disp;
    CDispGroup *group;
    TestTask *taskp;
    
    main_disp = disp = new CDisp();
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
        
