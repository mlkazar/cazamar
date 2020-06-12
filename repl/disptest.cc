#include <stdio.h>
#include "disp.h"
#include "task.h"

class Test : public Task {
public:
    int _value;

    Test() {
        _value = 0;
        setRunMethod(&Test::bounce);
    }

    void bounce() {
        _value++;
        if ((_value % 1000000) == 0) {
            printf("Test: value is %d\n", _value);
        }
        setRunMethod(&Test::bounce);
        queue();
    }
};

int
main(int argc, char **argv)
{
    Test *testp;
    int i;

    printf("Starting\n");
    
    for(i=0;i<2;i++)
        new Disp();

    testp = new Test();
    testp->queue();

    while(1) {
        sleep(1);
    }
}
