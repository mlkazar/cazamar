#if HAVE_CONFIG_H
# include <config.h>
#endif

#define WIPE_MEMORY ///< overwrite sensitve data before free()ing it.

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <ctype.h> // isxdigit

#include "oaxmalloc.h"
#include "oauth.h"

#ifndef WIN32 // getpid() on POSIX systems
#include <sys/types.h>
#include <unistd.h>
#else
#define snprintf _snprintf
#define strncasecmp strnicmp
#endif

/* returns buffer that must be free'd */
char *
oauth_encode_pass(const char *passp, const char *userNamep)
{
    char tbuffer[1024];
    int32_t maxPassLen;
    char *tp;

    /* each strcat adds 32 bytes */
    strcpy(tbuffer, "lfdsajfldsjaor84hdjalfdjslafjda5");

    /* compute a max password len that's small enough to ensure that two of them
     * (a user name and a password) can't get near the end of tbuffer.
     */
    maxPassLen = (sizeof(tbuffer) - 64 - 8) / 2; /* 8 is slop for null termination */
    strncat(tbuffer, userNamep, maxPassLen);    /* always terminates, even at max len */
    strncat(tbuffer, passp, maxPassLen);        /* strncat always null terminates */
    strcat(tbuffer, "549823qr9esdhvnkrqyt9ofdzvhorequ");

    tp = oauth_sign_hmac_sha1 (tbuffer, passp);
    return tp;
}
