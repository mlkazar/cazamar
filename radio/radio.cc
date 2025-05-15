#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "../rpc/xgml.h"
#include "../mfand/radiostream.h"
#include "../mfand/bufsocket.h"

/* general advice:
 *
 *http://opml.radiotime.com/Tune.ashx?id=s24147
 *      gives real URLs stations for tunein/radiotime station ID
 *
 *http://opml.radiotime.com/Search.ashx?&query=wyep&types=station&format=mp3,aac
 *	gets radio info for query -- includes current show
 *
 *http://opml.radiotime.com/Browse.ashx?id=s24147
 *	describe station <id>
 *      returns XML, including other recommendations
 *
 *http://opml.radiotime.com/Browse.ashx?c=playlist&id=s32500
 *	download playlist
 *
 *partnerId: add '&parterId=k2YHnXyS' to above
 *docs: checkout
 *      https://raw.githubusercontent.com/diegofn/Tunein-Radio-VLC/master/tunein.lua
 *      https://github.com/brianhornsby/plugin.audio.tuneinradio/wiki/RadioTime-API-Methods:-Browse

 */

/* statics */

class Radio {
    std::string _responseBuffer;
    Xgml *_xgmlp;
    int _sawData;
    RadioStream *_radioStreamp;
    std::string *_urlp;

public:
    Xgml *initXgml();

    int32_t setupCurl();

    int32_t testUrl( std::string *urlp, int *hasDatap);

    void cleanupCall();

    static int dataProc(void *contextp, RadioStream *radiop, char *bufferp, int nbytes);

    static int32_t controlProc( void *contextp,
                                RadioStream *radiop,
                                RadioStream::EvType event,
                                void *evDatap);

    Radio();
};

Radio::Radio()
{
    initXgml();
}

/* get an OFX XGML object with the right keywords for OFX 1.0.2 preloaded; this can
 * be used to parse the XML / SGML parts of a response, or to generate the output
 * from an Xgml (== SGML / XML) node.
 */
Xgml *
Radio::initXgml()
{
    Xgml *xgmlp;

    /* now parse the results */
    xgmlp = new Xgml();

    _xgmlp = xgmlp;

    return xgmlp;
}

int32_t
Radio::dataProc(void *contextp, RadioStream *radioStreamp, char *bufferp, int32_t nbytes)
{
    Radio *radiop = (Radio *)contextp;

    radiop->_sawData = 1;
    printf(" radio: data proc for pid %d saw data\n", getpid());
    if (radiop->_radioStreamp) {
        radiop->_radioStreamp->close();
        radiop->_radioStreamp = NULL;
    }
    return 0;
}

int32_t
Radio::controlProc(void *contextp, RadioStream *radiop, RadioStream::EvType event, void *evDatap)
{
    printf(" radio: control proc called\n");
    return 0;
}

/* return negative error code, with option of setting *hasDatap to 0 */
int32_t
Radio::testUrl( std::string *urlp, int *hasDatap)
{
    int32_t code;
    BufSocketFactory socketFactory;
    
    _sawData = 0;
    _urlp = urlp;

    _radioStreamp = new RadioStream();
    _radioStreamp->setTimeout(5000);
    printf("Checking %s\n", urlp->c_str());
    code = _radioStreamp->init( &socketFactory,
                                (char *) urlp->c_str(),
                                &Radio::dataProc,
                                &Radio::controlProc,
                                this);

    printf(" checked code=%d sawData=%d\n", code, _sawData);
    if (_radioStreamp) {
        _radioStreamp->close();
        _radioStreamp = NULL;
    }
    _urlp = NULL;

    if (_sawData) {
        *hasDatap = 1;
        return 0;
    }
    else {
        return -1;
    }
}

void
validateFile(Radio *radiop, char *fileNamep, long skipToLine)
{
    FILE *filep;
    FILE *ofilep;
    char tbuffer[10240];
    char oname[256];
    char *tp;
    int i;
    std::string intros[5];
    std::string urls[6];
    int goodUrls[6];
    long code;
    int goodCount;
    int initCount;
    int hasData;
    int first;
    int tc;
    char *getDatap;
    long currentLine;

    filep = fopen(fileNamep, "r");
    if (!filep) {
        printf("can't open '%s'\n", fileNamep);
        return;
    }

    strcpy(oname, fileNamep);
    strcat(oname, ".checked");
    ofilep = fopen(oname, "a");
    if (!ofilep) {
        printf("can't open for output\n");
        return;
    }

    currentLine = 0;
    while(1) {
        getDatap = fgets(tbuffer, sizeof(tbuffer), filep);
        currentLine++;
        if (getDatap == NULL) {
            printf("EOF encountered\n");
            break;
        }
        if (skipToLine > currentLine)
            continue;

        goodCount = 0;
        initCount = 0;
        tp = tbuffer;

        for(i=0;i<5;i++) {
            intros[i].erase();
            while (1) {
                tc = *tp++;
                if (tc == 0 || tc == '\t' || tc == '\n')
                    break;
                intros[i].append(1, tc);
            }
        }

        /* now parse URLs */
        for(i=0;i<6;i++) {
            urls[i].erase();
            while (1) {
                tc = *tp++;
                if (tc == 0 || tc == '\t' || tc == '\n')
                    break;
                urls[i].append(1, tc);
            }
        }

        for(i=0;i<6;i++) {
            goodUrls[i] = 0;
        }
        for(i=0;i<6;i++) {
            if (urls[i] != "-") {
                initCount++;
                code = radiop->testUrl(urls + i, &hasData);
                if (code == 0) {
                    if (!hasData)
                        printf("URL %s has no data\n", urls[i].c_str());
                    goodUrls[i] = 1;
                    goodCount++;
                }
                else {
                    printf("URL failed '%s'\n", urls[i].c_str());
                }
            }
        }

        if (goodCount <= 0) {
            printf("L%ld: No good entries for '%s'\n", currentLine, intros[0].c_str());
        }
        else {
            printf("L%ld: %d of %d good entries for '%s'\n",
                   currentLine, goodCount, initCount, intros[0].c_str());
            for(i=0;i<5;i++)
                fprintf(ofilep, "%s\t", intros[i].c_str());
            first = 1;
            /* note that goodCount > 0, so we're guaranteed to go through the code
             * below at least once, and thus process the 'first' entry.
             */
            for(i=0;i<6;i++) {
                if (goodUrls[i]) {
                    if (!first)
                        fprintf(ofilep, "\t");
                    first = 0;
                    fprintf(ofilep, "%s", urls[i].c_str());
                }
            }

            /* now print any leftover '-' items */
            for(i=0;i<6 - goodCount;i++)
                fprintf(ofilep, "\t-");
            fprintf(ofilep, "\n");
            fflush(ofilep);
        }
    }

    fclose(filep);
}

int
main(int argc, char **argv)
{
    Radio *radiop;
    std::string urlStr;
    long skipToLine;

    radiop = new Radio();

    if (argc > 2) {
        /* options:
         * validate <filename> <skip to line>
         */
        if (strcmp(argv[1], "validate") == 0 || strcmp(argv[1], "v") == 0) {
            if (argc >= 4)
                skipToLine = atoi(argv[3]);
            else
                skipToLine = 1;
            validateFile(radiop, argv[2], skipToLine);
        }
        else {
            printf("unknown operation '%s'\n", argv[1]);
            return -1;
        }
    }

    return 0;
}
