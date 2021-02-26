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
#include "json.h"

int main_verbose = 0;

int32_t client(XApi::ClientConn *connp, uint32_t zipcode);

/* return 0 for success, -1 for mystery errors and 1 for non-false value for slots */
int
parseSlots(const char *aresp)
{
    Json json;
    Json::Node *rootNodep;
    Json::Node *tnodep;
    Json::Node *arrayNodep;
    char *resp = const_cast<char *>(aresp);
    int32_t code;
    const char *slotValuep;
    int32_t rval = 1;

    if (main_verbose)
        printf("resp=%s\n", resp);
    code = json.parseJsonChars(&resp, &rootNodep);
    if (code == 0) {
        arrayNodep = rootNodep->searchForChild("slots", 0);
        if (!arrayNodep) {
            printf("store has no slots array\n");
            printf("resp=%s\n", aresp);
            return -1;
        }

        /* get the value of the named array, which is the real array */
        arrayNodep = arrayNodep->_children.head();
        if (!arrayNodep) {
            printf("slots doesn't appear to be an array\n");
            printf("resp=%s\n", aresp);
            return -1;
        }

        rval = 2;
        for(tnodep = arrayNodep->_children.head(); tnodep; tnodep=tnodep->_dqNextp) {
            slotValuep = tnodep->_children.head()->_name.c_str();
            if (strcmp(slotValuep, "false") != 0) {
                /* non false value! */
                rval = 0;
                break;
            }
        }

        if (rval < 0) {
            rootNodep->print();
        }

        delete rootNodep;
        return rval;
    }

    printf("can't parse json -- resp=%s\n", aresp);
    return -1;
}

int
main(int argc, char **argv)
{
    uint32_t storeId=0;
    const char *fileNamep = 0;
    uint32_t counter;
    int32_t code;
    XApi *xapip;
    XApi::ClientConn *connp;
    BufGen *socketp;
    int useSecure = 1;
    const char *hostNamep = "www.riteaid.com";
    uint32_t i;
    uint32_t sleepTime = 1;

    if (argc <= 1) {
        printf("usage: ritestores -i storeId\n");
        printf("usage: ritestores -f <file of storeIds>\n");
        printf(" -s <sleep time in seconds = 2>\n");
        printf(" -v (verbose)\n");
        return 1;
    }

    xapip = new XApi();

    if (!useSecure) {
        socketp = new BufSocket();
        socketp->init(const_cast<char *>(hostNamep), 80);
    }
    else {
        socketp = new BufTls("");
        socketp->init(const_cast<char *>(hostNamep), 443);
    }

    connp = xapip->addClientConn(socketp);

    for(i=1;i<argc;i++) {
        if (strcmp(argv[i], "-v") == 0)
            main_verbose = 1;
        else if (strcmp(argv[i], "-i") == 0) {
            storeId = atoi(argv[i+1]);
            i++;
        }
        else if (strcmp(argv[i], "-f") == 0) {
            fileNamep = argv[i+1];
            i++;
        }
        else if (strcmp(argv[i], "-s") == 0) {
            sleepTime = atoi(argv[i+1]);
            i++;
        }
    }

    if (storeId != 0) {
        client(connp, storeId);
    }
    else if (fileNamep != NULL) {
        FILE *filep;
        char tbuffer[128];
        char *tp;
        uint32_t i;
        
        filep = fopen(fileNamep, "r");
        if (filep == NULL) {
            printf("File %s not found\n", argv[2]);
            return -1;
        }

        counter = 0;
        while(1) {
            tp = fgets(tbuffer, sizeof(tbuffer), filep);
            if (!tp)
                break;
            counter++;
            storeId = atoi(tbuffer);
            if (storeId != 0) {
                for(i=0;i<4;i++) {
                    printf("Checking store %d\n", storeId);
                    code = client(connp, storeId);
                    sleep(sleepTime);
                    if (code == 0) {
                        printf("***Store %05d has appointments\n", storeId);
                    }
                    else if (code < 0) {
                        printf("resetting conn\n");
                        connp->reset();
                        continue;
                    }
                    break;
                }
            }

            // printf("Processed %d lines\n", counter);
        }
        fclose(filep);
    }
    else {
        printf("No -f or -i switch found\n");
    }

    return 0;
}

int32_t
client(XApi::ClientConn *connp, uint32_t storeId)
{
    XApi::ClientReq *reqp;
    char tbuffer[8192];
    CThreadPipe *inPipep;
    CThreadPipe *outPipep;
    int32_t code;
    std::string path;
    std::string response;

    sprintf(tbuffer, "/services/ext/v2/vaccine/checkSlots?storeNumber=%05d", storeId);
    path = std::string(tbuffer);

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

        code = parseSlots(response.c_str());
    }
    else {
        code = -2;
    }

    delete reqp;
    reqp = NULL;

    return code;
}
