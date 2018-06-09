#include <stdio.h>
#include "id.h"
#include "streams.h"

int
main(int arc, char **argv)
{
    FILE *inFilep;
    FILE *outFilep;
    Id id;
    Id::Id3 id3;
    int32_t code;
    std::string genre;

    inFilep = fopen(argv[1], "r");
    outFilep = fopen("image.jpg", "w");

    FileInStream inStr(inFilep);

    code = id.init(&inStr);
    printf("id init code=%d\n", code);

    printf("%s:\n", argv[1]);
    code = id3.parse(&id, outFilep, &genre);
    printf("id3 parse code=%d, genre=%s, write image.jpg\n", code, genre.c_str());
    fclose(outFilep);

    return 0;
}
