#include "nfsv41.h"
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <rpc/pmap_clnt.h>

int svc_sendreply_mlk(SVCXPRT *svcp, xdrproc_t proc, char *parm) {
    svc_sendreply(svcp, proc, parm);
    xdr_free(proc, parm);
    return 0;
}
