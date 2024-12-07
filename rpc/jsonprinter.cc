#include <stdio.h>
#include <sys/types.h>
#include <inttypes.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "jsonprint.h"

const char *test1 =
"\
 {\"foo\" : \"fooValue\", \
  \"bar\" : \"barValue\"}     \
";

const char *test2 =
"\
[\
 {\"foo\" : \"fooValue\", \
  \"bar\" : \"barValue\"},     \
 {\"modes\" : \"modesvalue\", \
  \"barf\" : \"barfalue\"}     \
]\
";

const char *test3 = "\
\"foo\" \
";

const char *test4 = "\
7 \
";

const char *test5 = "\
{ \"foo\" : [ \
    { \"key1\" : 7, \
            \"key2\" : \"data2\"},              \
    { \"key3\" : 8, \
            \"key4\" : \"data4\"}               \
    ] \
        } \
";

Json jsonSys;

void
check(const char *testStringp)
{
    int code;
    Json::Node *nodep = 0;
    char *strp;

    strp = const_cast<char *>(testStringp);
    code = jsonSys.parseJsonChars(&strp, &nodep);
    printf("code is %d\n", code);
    if (code == 0) {
        nodep->print();
    }
}

int
main(int argc, char **argv)
{
    if (argc >= 2) {
        Json::Node *rootp;
        int32_t code;
        FILE *filep = fopen(argv[1], "r");
        if (!filep) {
            printf("Boo, file not found\n");
            return -1;
        }

        code = jsonSys.parseJsonFile(filep, &rootp);
        printf("code is %d\n", code);
    }

    return 0;
}
