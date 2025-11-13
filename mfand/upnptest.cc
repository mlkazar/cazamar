#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <errno.h>
#include <string>
#include <stdio.h>

#include <dqueue.h>
#include "bufsocket.h"
#include "rst.h"
#include "json.h"
#include "xapi.h"
#include "oasha1.h"
#include <xgml.h>
#include "upnp.h"

static int32_t
mainPrintCallback(void *callbackContextp, void *arecordp) {
    UpnpDBase::Record *recordp = (UpnpDBase::Record *) arecordp;
    printf("Item title=%s artist=%s album=%s url=%s genre=%s artUrl=%s tag=%d\n",
           recordp->_title.c_str(),
           recordp->_artist.c_str(),
           recordp->_album.c_str(),
           recordp->_url.c_str(),
           recordp->_genre.c_str(),
           recordp->_artUrl.c_str(),
           recordp->_tag);
    return 0;
}

int
main(int argc, char **argv)
{
    UpnpProbe probe;
    UpnpAv av;
    UpnpDBase dbase;
    int32_t code;
    char cmdBuffer[1024];
    char cmd[128];
    char arg0[128];
    char arg1[128];
    const char *obIdp;
    UpnpDevice *devp;
    const char *hostp;
    const char *servicep;
    char *linep;
    int ix;
    int i;
    int scanItems;

    while(1) {
        printf("\n>> ");
        fflush(stdout);
        linep = fgets(cmdBuffer, sizeof(cmdBuffer)-1, stdin);
        if (!linep) {
            if (feof(stdin)) {
                printf("short read\n");
                return -1;
            }
            else
                continue;
        }

        scanItems = sscanf(cmdBuffer, "%127s %127s %127s", cmd, arg0, arg1);
        if (scanItems <= 0)
            continue;

        if (strcmp(cmd, "p") == 0) {
            code = probe.init();
            printf("All done with probe, code = %d\n", code);
            code = probe.contactAllDevices();
            printf("All done with probe/contact code=%d\n\n", code);
        }
        else if (strcmp(cmd, "r") == 0) {
            if (strcmp(arg0, "on") == 0) {
                av.setRecurse(1);
            }
            else {
                av.setRecurse(0);
            }
        }
        else if (strcmp(cmd, "save") == 0) {
            code = probe.saveToFile("foo.json");
            printf("Dev save done, code=%d\n", code);
            code = dbase.saveToFile("dbase.json");
            printf("DBase save done, code=%d\n", code);
        }
        else if (strcmp(cmd, "restore") == 0) {
            probe.clearAll();
            code = probe.restoreFromFile("foo.json");
            printf("Restore done, code=%d\n", code);
            code = dbase.restoreFromFile("dbase.json");
            printf("DBase restore done, code=%d\n", code);
        }
        else if (strcmp(cmd, "b") == 0) {
            if (scanItems < 2) {
                printf("usage: b <count>\n");
                continue;
            }

            ix = atoi(arg0);

            for(devp = probe.getFirstDevice(); devp; devp=devp->_dqNextp) {
                if (devp->_controlPathKnown) {
                    if (ix <= 0)
                        break;
                    ix--;
                }
            }

            if (!devp) {
                printf("index too large\n");
                continue;
            }

            hostp = devp->_host.c_str();
            servicep = devp->_controlRelativePath.c_str();
            printf("Using host=%s service=%s\n", hostp, servicep);

            code = av.init(devp);
            printf("Av init code=%d\n", code);

            if (scanItems > 2)
                obIdp = arg1;
            else
                obIdp = "0";

            code = av.browse(&dbase, obIdp);
            printf("Av browse code=%d\n", code);
        }
        else if (strcmp(cmd, "l") == 0) {
            for(i=0, devp = probe._allDevices.head(); devp; devp=devp->_dqNextp, i++) {
                printf("%d: host=%s servicePath=%s tag=%d\n",
                       i, devp->_host.c_str(), devp->_controlRelativePath.c_str(), devp->_tag);
            }
        }
        else if (strcmp(cmd, "ld") == 0) {
            dbase._titleTree.apply(mainPrintCallback, NULL);
        }
        else if (strcmp(cmd, "dl") == 0) {
            UpnpDevice *devp;
            if (scanItems < 2) {
                printf("out of range\n");
                continue;
            }
            ix = atoi(arg0);
            devp = probe.getNthDevice(ix);
            if (devp) {
                dbase.deleteByTag(devp->_tag);
                probe.deleteNthDevice(ix);
            }
        }
        else if (strcmp(cmd, "q") == 0)
            break;
        else {
            printf("Unknown command '%s'\n", cmd);
        }
    } /* while loop */

    return 0;
}
