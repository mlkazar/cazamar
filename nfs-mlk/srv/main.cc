#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nfsserver.h"
#include "nfsv41.h"
#include "vntest.h"

int
main(int argc, char **argv) {
    NfsServer *serverp;
    VfsTest *vfsp;

    vfsp = new VfsTest();

    serverp = new NfsServer(vfsp);
    serverp->start();

    return 0;

}
