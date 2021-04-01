/*

Copyright 2016-2020 Cazamar Systems

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>

#include "xapi.h"
#include "bufsocket.h"
#include "buftls.h"

void client(uint32_t zipcode);

void
parseStoreNumbers(const char *resp)
{
    const char *tp;
    char storeId[10];

    tp = resp;
    while(1) {
        if (*tp == 0)
            break;
        if (strncmp("-->Rite Aid #", tp, 13) == 0) {
            strncpy(storeId, tp+13, 5);
            storeId[5] = 0;
            printf("%s\n", storeId);
            tp += 18;
        }
        else {
            tp++;
        }
    }
}

int
main(int argc, char **argv)
{
    uint32_t zipCode;

    if (argc <= 1) {
        printf("usage: ritestores -i <indiv. zipcode>\n");
        printf("usage: ritestores -f <file of zipcodes>\n");
        return 1;
    }

    if (strcmp(argv[1], "-i") == 0) {
        zipCode = atoi(argv[2]);
        client(zipCode);
    }
    else {
        FILE *filep;
        char tbuffer[128];
        char *tp;
        
        filep = fopen(argv[2], "r");
        if (filep == NULL) {
            printf("File %s not found\n", argv[2]);
            return -1;
        }
        while(1) {
            tp = fgets(tbuffer, sizeof(tbuffer), filep);
            if (!tp)
                break;
            zipCode = atoi(tbuffer);
            if (zipCode != 0) {
                printf("ZIP %05d\n", zipCode);
                client(zipCode);
            }
            
        }
        fclose(filep);
    }

    return 0;
}

void
client(uint32_t zipCode)
{
    XApi *xapip;
    XApi::ClientConn *connp;
    BufGen *socketp;
    XApi::ClientReq *reqp;
    char tbuffer[8192];
    CThreadPipe *inPipep;
    CThreadPipe *outPipep;
    int32_t code;
    int useSecure = 1;
    std::string path;
    int port = 443;
    std::string response;

    xapip = new XApi();

    const char *hostNamep = "www.riteaid.com";
    sprintf(tbuffer, "/locations/search.html?q=%05d", zipCode);
    path = std::string(tbuffer);

    if (!useSecure) {
        socketp = new BufSocket();
        socketp->init(const_cast<char *>(hostNamep), port);
    }
    else {
        socketp = new BufTls("");
        socketp->init(const_cast<char *>(hostNamep), port);
    }

    connp = xapip->addClientConn(socketp);

    /* now prepare a call */
    strcpy(tbuffer, "Client call data\n");
    reqp = new XApi::ClientReq();

    reqp->setSendContentLength(0x7FFFFFFF);

    reqp->addHeader("User-Agent", "curl/7.66.0");
    reqp->addHeader("Accept", "*/*");

    reqp->startCall(connp, path.c_str(), /* isPost */ XApi::reqGet);

    inPipep = reqp->getIncomingPipe();
    outPipep = reqp->getOutgoingPipe();

    code = reqp->waitForHeadersDone();

    if (code == 0) {
        while(1) {
            code = inPipep->read(tbuffer, sizeof(tbuffer));
            if (code <= 0) {
                break;
            }

            response.append(tbuffer, code);
        }

        parseStoreNumbers(response.c_str());
    }

    reqp->resetConn();

    delete reqp;
    reqp = NULL;
}
