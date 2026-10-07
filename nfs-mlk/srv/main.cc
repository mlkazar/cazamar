#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nfsserver.h"
#include "nfsv41.h"

int
main(int argc, char **argv) {
    NfsServer *serverp;

    serverp = new NfsServer();
    serverp->start();

    return 0;

}
