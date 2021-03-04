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
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "xapi.h"
#include "bufsocket.h"
#include "buftls.h"
#include "json.h"

int main_verbose = 0;
int main_fd;
int main_fd_used = 0;

char *cookiep=0;

char *xtokenp=0;


int32_t client(XApi::ClientConn *connp, double latitude, double longitude);
void printCookieRecipe();

void *
monitor(void *ctxp)
{
    time_t now;

    while(1) {
        now = time(0);
        if (main_fd_used != 0) {
            if (now - main_fd_used > 15) {
                printf("closing hung fd\n");
                close(main_fd);
                sleep(4);
            }
        }
        sleep(5);
    }
}

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
    int32_t rval = 1;

    if (main_verbose)
        printf("resp=%s\n", resp);
    code = json.parseJsonChars(&resp, &rootNodep);
    if (code == 0) {
        arrayNodep = rootNodep->searchForChild("appointmentsAvailable", 0);
        if (!arrayNodep) {
            printf("store has no slots array\n");
            printf("resp=%s\n", aresp);
            return -1;
        }

        /* get the value of this field */
        tnodep = arrayNodep->_children.head();
        if (!tnodep) {
            printf("no associated value\n");
            printf("resp=%s\n", aresp);
            return -1;
        }

        rval = 2;
        if (strcmp(tnodep->_name.c_str(), "false") != 0) {
            rval = 0;
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
readCookies()
{
    struct stat tstat;
    char *datap;
    int fd;
    long tlen;
    int code;

    fd = open("walcookie.txt", O_RDONLY);
    if (fd < 0) {
        printCookieRecipe();
        return -1;
    }

    code = fstat(fd, &tstat);
    if (code) {
        perror("fstat");
        return -1;
    }
               
    tlen = tstat.st_size;
    datap = (char *) malloc(tlen+1);
    memset(datap, 0, tlen+1);
    code = read(fd, datap, tlen);
    if (code > 0) {
        code--; /* last char read */
        while(datap[code] == '\n' || datap[code] == '\r' || datap[code] == ' ') {
            datap[code] = 0;
            code--;
        }
    }
    close(fd);
    cookiep = datap;

    fd = open("waltoken.txt", O_RDONLY);
    if (fd < 0) {
        printCookieRecipe();
        return -1;
    }

    code = fstat(fd, &tstat);
    if (code) {
        perror("fstat");
        return -1;
    }
               
    tlen = tstat.st_size;
    datap = (char *) malloc(tlen+1);
    code = read(fd, datap, tlen);
    if (code > 0) {
        code--; /* last char read */
        while(datap[code] == '\n' || datap[code] == '\r' || datap[code] == ' ') {
            datap[code] = 0;
            code--;
        }
    }
    close(fd);
    xtokenp = datap;
    return 0;
}

void
printCookieRecipe()
{
    printf("  NOTE: must turn on Safari web inspector, \n");
    printf("   go to 'https://www.walgreens.com/findcare/vaccination/covid-19/location-screening'\n");
    printf("   check 'availability' headers\n");
    printf("   and copy cookie to walcookie.txt and X-XSRF-TOKEN to waltoken.txt\n");
}

int
main(int argc, char **argv)
{
    uint32_t counter;
    int32_t code;
    XApi *xapip;
    XApi::ClientConn *connp;
    BufGen *socketp;
    const char *hostNamep = "www.walgreens.com";
    uint32_t i;
    uint32_t sleepTime = 1;
    pthread_t junkId;

    double latSkip = 0.246;     /* degrees */
    double longSkip = 0.320;    /* degrees longitude */
    double latStart = 39.739;
    double latEnd = 42.0;
    double longStart = -80.55;
    double longEnd = -75.193;
    double latitude;
    double longitude;

    if (argc <= 1) {
        printf("usage: walstores -x\n");
        printf(" -s <sleep time in seconds = 2>\n");
        printf(" -v (verbose)\n");
        printf(" -x (actually do the work, instead of printing this message)\n");
        printCookieRecipe();
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    pthread_create(&junkId, NULL, monitor, NULL);

    xapip = new XApi();

    socketp = new BufTls("");
    socketp->init(const_cast<char *>(hostNamep), 443);

    connp = xapip->addClientConn(socketp);

    for(i=1;i<argc;i++) {
        if (strcmp(argv[i], "-v") == 0)
            main_verbose = 1;
        else if (strcmp(argv[i], "-x") == 0) {
            /* nothing to do here; -x just avoids the help message */
        }
        else if (strcmp(argv[i], "-s") == 0) {
            sleepTime = atoi(argv[i+1]);
            i++;
        }
    }

    readCookies();

    {
        counter = 0;
        for(latitude = latStart; latitude <= latEnd; latitude += latSkip) {
            for(longitude = longStart; longitude <= longEnd; longitude += longSkip) {
                counter++;
                for(i=0;i<4;i++) {
                    if (main_verbose)
                        printf("Checking store at (%f,%f)\n", latitude, longitude);
                    code = client(connp, latitude, longitude);
                    sleep(sleepTime);
                    if (code < 0) {
                        printf("resetting conn\n");
                        connp->reset();
                        sleep(1);
                        continue;
                    }
                    break;
                }

                if ((counter % 16) == 1) {
                    printf("Switching to new connection\n");
                    connp->reset();
                    sleep(1);
                }
                printf("Counter=%d\n", counter);
            }
        }
    }

    printf("All done\n");
    return 0;
}

std::string
unzip(std::string adata)
{
    int fd;
    char tbuffer[10240];
    int code;

    fd = open("/tmp/unzipdata.gz", O_CREAT | O_RDWR, 0666);
    if (fd<0) {
        perror("open unzipdata");
    }
    write(fd, adata.data(), adata.length());
    close(fd);

    printf("Done with data prep\n");
    system("rm /tmp/unzipdata");
    system("gunzip /tmp/unzipdata.gz");
    fd = open("/tmp/unzipdata", O_RDONLY);
    code = read(fd, tbuffer, sizeof(tbuffer));
    if (code == sizeof(tbuffer)) {
        printf("tbuffer too small\n");
    }
    return std::string(tbuffer, code);
}

int32_t
client(XApi::ClientConn *connp, double latitude, double longitude)
{
    XApi::ClientReq *reqp;
    char tbuffer[8192];
    char postBuffer[1000];
    CThreadPipe *inPipep;
    CThreadPipe *outPipep;
    int32_t code;
    std::string path;
    std::string response;
    time_t ctimeValue;
    struct tm *tmValuep;
    uint32_t dateYear;
    uint32_t dateMonth;
    uint32_t dateDay;
    uint32_t nbytes;
    std::string hdrValue;

    ctimeValue = time(0);
    tmValuep = localtime(&ctimeValue);
    dateYear = tmValuep->tm_year + 1900;
    dateMonth = tmValuep->tm_mon+1;
    dateDay = tmValuep->tm_mday;

    path = std::string("/hcschedulersvc/svc/v1/immunizationLocations/availability");

    sprintf(postBuffer, "{\"serviceId\":\"99\",\"position\":{\"latitude\":%.7f,\"longitude\":%.7f},\"appointmentAvailability\":{\"startDateTime\":\"%4d-%02d-%02d\"},\"radius\":25}",
            latitude, longitude, dateYear, dateMonth, dateDay);
    nbytes = strlen(postBuffer);

    /* now prepare a call */
    reqp = new XApi::ClientReq();

    reqp->setSendContentLength(nbytes);

    reqp->addHeader("User-Agent", "curl/7.66.0");
    reqp->addHeader("Accept", "*/*");
    reqp->addHeader("Content-Type", "application/json");
    reqp->addHeader("Cookie", cookiep);
    reqp->addHeader("X-XSRF-TOKEN", xtokenp);
    main_fd = static_cast<BufTls *>(connp->_bufGenp)->getSocket();
    main_fd_used = time(0);
    reqp->startCall(connp, path.c_str(), /* isPost */ XApi::reqPost);

    inPipep = reqp->getIncomingPipe();
    outPipep = reqp->getOutgoingPipe();

    outPipep->write(postBuffer, nbytes);
    outPipep->eof();

    code = reqp->waitForHeadersDone();

    if (code == 0) {
        while(1) {
            code = inPipep->read(tbuffer, sizeof(tbuffer));
            if (code <= 0) {
                break;
            }

            response.append(tbuffer, code);
        }

        code = reqp->findIncomingHeader("content-encoding", &hdrValue);
        if (code == 0 && hdrValue.find("gzip", 0) != std::string::npos) {
            response = unzip(response);
        }

        code = parseSlots(response.c_str());
        if (code == 0) {
            printf("********\nYAY! '%s'\n**********\n", response.c_str());
        }
    }
    else {
        code = -2;
    }

    delete reqp;
    reqp = NULL;

    return code;
}
