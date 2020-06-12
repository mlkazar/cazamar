#include "osptypes.h"
#include "rbufstr.h"

/* print out the current buffer contents start at the read position, and when done,
 * reset the read position back to its original value.
 */
void 
tprint(Rbuf *bufp, char *commentp)
{
    int32_t code;
    char *datap;
    char tbuffer[1024];
    uint32_t originalPos;

    originalPos = bufp->getReadPosition();
    printf("%s: '", commentp);
    while(1) {
        code = bufp->scan(&datap);
        if (code == 0) {
            printf("'\n");
            break;
        }
        if (code >= sizeof(tbuffer)) {
            printf("%d too large to print\n", code);
            continue;
        }
        memcpy(tbuffer, datap, code);
        tbuffer[code] = 0;
        printf("%s", tbuffer);
    }

    bufp->setReadPosition(originalPos);
}

int
main(int argc, char **argv)
{
    RbufStr a;
    RbufStr b;
    int32_t code;
    char tbuffer[1024];
    
    a.append((char *) "Jello", 5);
    a.append((char *)" ", 1);
    a.append((char *) "Biafra", 6);

    tprint(&a, (char *) "after adding full name");

    code = a.read(tbuffer, 5);
    tbuffer[5] = 0;
    printf("Read 5 bytes - '%s'\n", tbuffer);

    tprint(&a, (char *) "read missing the first 5 bytes");

    a.setReadPosition(0);

    tprint(&a, (char * ) "all data back");

    a.pop(5);
    a.prepend((char *) "Wedge Salad", 11);
    a.setReadPosition(0);
    
    tprint(&a, (char *) "another 50s food");

    a.erase();
    printf("Read position after erase %d\n", a.getReadPosition());
    tprint(&a, (char *) "should be empty");

    return 0;
}
