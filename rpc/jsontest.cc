#include "json.h"

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
    printf("test1:\n");
    check(test1);
    printf("test2:\n");
    check(test2);
    printf("test3:\n");
    check(test3);
    printf("test4:\n");
    check(test4);
    printf("test5:\n");
    check(test5);

    return 0;
}
