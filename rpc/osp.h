#ifndef _OSP_H_ENV__
#define _OSP_H_ENV__ 1

#include <sys/types.h>
#include "osptypes.h"

#define osp_assert(x) do { \
    if (!(x)) {                            \
        osp_panic((char *) #x, (char *) __FILE__, __LINE__);     \
    }                                      \
 } while(0)

extern void osp_panic(char *why, char *filep, int line);

extern uint32_t osp_time_ms();

extern uint32_t osp_time_sec(); /* time since 1970 */

#include "ospmbuf.h"

#endif /* _OSP_H_ENV__ */
