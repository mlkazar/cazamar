#include <string>
#include <stdlib.h>
#include "id.h"

char * id3Names[] =
    { (char *) "Blues",          /* 0 */
      (char *) "Classic Rock",   /* 1 */
      (char *) "Country",        /* 2 */
      (char *) "Dance",  /* 3 */
      (char *) "Disco",  /* 4 */
      (char *) "Funk",   /* 5 */
      (char *) "Grunge", /* 6 */
      (char *) "Hip-Hop",        /* 7 */
      (char *) "Jazz",   /* 8 */
      (char *) "Metal",  /* 9 */
      (char *) "New Age",        /* 10 */
      (char *) "Oldies", /* 11 */
      (char *) "Other",  /* 12 */
      (char *) "Pop",    /* 13 */
      (char *) "R&B",    /* 14 */
      (char *) "Rap",    /* 15 */
      (char *) "Reggae", /* 16 */
      (char *) "Rock",   /* 17 */
      (char *) "Techno", /* 18 */
      (char *) "Industrial",     /* 19 */
      (char *) "Alternative",    /* 20 */
      (char *) "Ska",    /* 21 */
      (char *) "Death Metal",    /* 22 */
      (char *) "Pranks", /* 23 */
      (char *) "Soundtrack",     /* 24 */
      (char *) "Euro-Techno",    /* 25 */
      (char *) "Ambient",        /* 26 */
      (char *) "Trip-Hop",       /* 27 */
      (char *) "Vocal",  /* 28 */
      (char *) "Jazz+Funk",      /* 29 */
      (char *) "Fusion", /* 30 */
      (char *) "Trance", /* 31 */
      (char *) "Classical",      /* 32 */
      (char *) "Instrumental",   /* 33 */
      (char *) "Acid",   /* 34 */
      (char *) "House",  /* 35 */
      (char *) "Game",   /* 36 */
      (char *) "Sound Clip",     /* 37 */
      (char *) "Gospel", /* 38 */
      (char *) "Noise",  /* 39 */
      (char *) "AlternRock",     /* 40 */
      (char *) "Bass",   /* 41 */
      (char *) "Soul",   /* 42 */
      (char *) "Punk",   /* 43 */
      (char *) "Space",  /* 44 */
      (char *) "Meditative",     /* 45 */
      (char *) "Instrumental Pop",       /* 46 */
      (char *) "Instrumental Rock",      /* 47 */
      (char *) "Ethnic", /* 48 */
      (char *) "Gothic", /* 49 */
      (char *) "Darkwave",       /* 50 */
      (char *) "Techno-Industrial",      /* 51 */
      (char *) "Electronic",     /* 52 */
      (char *) "Pop-Folk",       /* 53 */
      (char *) "Eurodance",      /* 54 */
      (char *) "Dream",  /* 55 */
      (char *) "Southern Rock",  /* 56 */
      (char *) "Comedy", /* 57 */
      (char *) "Cult",   /* 58 */
      (char *) "Gangsta",        /* 59 */
      (char *) "Top 40", /* 60 */
      (char *) "Christian Rap",  /* 61 */
      (char *) "Pop/Funk",       /* 62 */
      (char *) "Jungle", /* 63 */
      (char *) "Native American",        /* 64 */
      (char *) "Cabaret",        /* 65 */
      (char *) "New Wave",       /* 66 */
      (char *) "Psychedelic",    /* 67 */
      (char *) "Rave",   /* 68 */
      (char *) "Showtunes",      /* 69 */
      (char *) "Trailer",        /* 70 */
      (char *) "Lo-Fi",  /* 71 */
      (char *) "Tribal", /* 72 */
      (char *) "Acid Punk",      /* 73 */
      (char *) "Acid Jazz",      /* 74 */
      (char *) "Polka",  /* 75 */
      (char *) "Retro",  /* 76 */
      (char *) "Musical",        /* 77 */
      (char *) "Rock & Roll",    /* 78 */
      (char *) "Hard Rock",      /* 79 */
};

int32_t
Id::init(InStream *inp)
{
    _inStreamp = inp;

    return 0;
}

/* copy up to *asizep bytes, terminating on a null if we see one, otherwise
 * stopping after *asizep bytes in the record.
 */
int32_t
Id::Id3::decodeString(InStream *inStreamp, std::string *stringp, uint32_t *asizep, int skipType)
{
    int tc;
    uint32_t i;
    uint32_t asize = *asizep;
    uint32_t bytesLeft;

    bytesLeft = asize;

    if (!skipType) {
        tc = inStreamp->top();
        inStreamp->next();
        if (tc != 0) {
            printf("unknown string type\n");
            return -1;
        }
        asize--;
        bytesLeft--;
    }

    for(i=0;i<asize;i++) {
        tc = inStreamp->top();
        if (tc != 0) {
            stringp->append(1, tc);
            inStreamp->next();
            bytesLeft--;
        }
        else {
            /* we consumed the terminating null, but don't have to append it */
            inStreamp->next();
            bytesLeft--;
            break;
        }
    }

    *asizep = bytesLeft;

    return 0;
}

int32_t
Id::Id3::parse(Id *idp, FILE *outFilep, std::string *genreStrp)
{
    int32_t code;
    uint8_t tbuffer[6];
    uint8_t opcode[5];
    uint32_t asize;
    uint32_t tsize;
    uint32_t idVersion;
    uint32_t i;
    std::string tstring;

    _idp = idp;

    code = idp->_inStreamp->read((char *) _header, 10);
    if (code)
        return code;

    if (_header[0] != 'I' || _header[1] != 'D' || _header[2] != '3')
        return -1;

    idVersion = _header[3];

    _flagUnsync = _header[5] & 0x80;
    _flagExtendedHeader = _header[5] & 0x40;
    _bytesLeft = 4 * ((_header[6]<<21) + (_header[7]<<14) + (_header[8]<<7) + _header[9]);

    if (_flagExtendedHeader) {
        /* skip the extended header */
        if (idVersion <= 3) {
            /* in 2.3.0, this is a 4 byte size encoded normally,
             * followed by size bytes.  The size will be 6
             * (flags+paddingCount) or 10 (same + CRC).  Size doesn't count
             * the size of the extended header.
             */
            code = idp->_inStreamp->read((char *) tbuffer, 4);
            if (code < 0)
                return code;
            asize = ((tbuffer[0]<<24) + (tbuffer[1]<<16) + (tbuffer[2]<<8) + tbuffer[3]);
            idp->_inStreamp->skip(asize);

            _bytesLeft -= asize + 4;
        }
        else if (idVersion >= 4) {
            code = idp->_inStreamp->read((char *) tbuffer, 4);
            asize = 4 * ((tbuffer[0]<<21) + (tbuffer[1]<<14) + (tbuffer[2]<<7) + tbuffer[3]);
            idp->_inStreamp->skip(asize - 4);        /* count includes 4 we've read already */

            _bytesLeft -= asize;
        }
    }

    while(_bytesLeft > 0) {
        code = idp->_inStreamp->read((char *)opcode, 4);
        if (code < 0)
            return code;
        opcode[4] = 0;
        if (opcode[0] == 0) {
            printf("Done\n");
            break;
        }

        code = idp->_inStreamp->read((char *) tbuffer, 4);
        if (code < 0)
            return code;
        if (idVersion >= 4)
            asize = 4 * ((tbuffer[0]<<21) + (tbuffer[1]<<14) + (tbuffer[2]<<7) + tbuffer[3]);
        else
            asize = (tbuffer[0]<<24) + (tbuffer[1]<<16) + (tbuffer[2]<<8) + tbuffer[3];
        code = idp->_inStreamp->read((char *) tbuffer, 2);
        if (code < 0)
            return code;
        if (strcmp((char *) opcode, "TRCK") == 0) {
            tsize = asize;
            tstring.erase();
            code = decodeString(idp->_inStreamp, &tstring, &tsize);
            if (code < 0)
                return code;
            printf("TRCK info '%s'\n", tstring.c_str());
        }
        else if (strcmp((char *) opcode, "TIT2") == 0) {
            tsize = asize;
            tstring.erase();
            code = decodeString(idp->_inStreamp, &tstring, &tsize);
            if (code < 0)
                return code;
            printf("TIT2 (title) info '%s'\n", tstring.c_str());
        }
        else if (strcmp((char *) opcode, "TPE1") == 0) {
            tsize = asize;
            tstring.erase();
            code = decodeString(idp->_inStreamp, &tstring, &tsize);
            if (code < 0)
                return code;
            printf("TPE1 (artist) info '%s'\n", tstring.c_str());
        }
        else if (strcmp((char *) opcode, "TPE2") == 0) {
            tsize = asize;
            tstring.erase();
            code = decodeString(idp->_inStreamp, &tstring, &tsize);
            if (code < 0)
                return code;
            printf("TPE2 (band) info '%s'\n", tstring.c_str());
        }
        else if (strcmp((char *) opcode, "WCOM") == 0) {
            tsize = asize;
            tstring.erase();
            code = decodeString(idp->_inStreamp, &tstring, &tsize, 1);
            if (code < 0)
                return code;
            printf("WCOM (purchase web address) '%s'\n", tstring.c_str());
        }
        else if (strcmp((char *) opcode, "WXXX") == 0) {
            tsize = asize;
            tstring.erase();
            code = decodeString(idp->_inStreamp, &tstring, &tsize);
            if (code < 0)
                return code;
            printf("WXXX descr '%s'\n", tstring.c_str());
            tstring.erase();
            code = decodeString(idp->_inStreamp, &tstring, &tsize, 1);
            printf("WXXX (user web address) '%s'\n", tstring.c_str());
        }
        else if (strcmp((char *) opcode, "TALB") == 0) {
            tsize = asize;
            tstring.erase();
            code = decodeString(idp->_inStreamp, &tstring, &tsize);
            if (code < 0)
                return code;
            printf("TALB (album) info '%s'\n", tstring.c_str());
        }
        else if (strcmp((char *) opcode, "TYER") == 0) {
            tsize = asize;
            tstring.erase();
            code = decodeString(idp->_inStreamp, &tstring, &tsize);
            if (code < 0)
                return code;
            printf("TYER (year) info '%s'\n", tstring.c_str());
        }
        else if (strcmp((char *) opcode, "TCON") == 0) {
            const char *genrep;
            uint32_t ix;

            tsize = asize;
            tstring.erase();
            code = decodeString(idp->_inStreamp, &tstring, &tsize);
            if (code < 0)
                return code;
            genrep = tstring.c_str();
            if (strlen(genrep) > 1 && genrep[0] == '(') {
                ix = atoi(genrep+1);
                if (ix < sizeof(id3Names) / sizeof(char *)) {
                    tstring = std::string(id3Names[ix]);
                }
            }
            *genreStrp = tstring;
            printf("TCON (Genre) info '%s'\n", tstring.c_str());
        }
        else if (strcmp((char *)opcode, "APIC") == 0) {
            tsize = asize;
            tstring.erase();

            code = decodeString(idp->_inStreamp, &tstring, &tsize);
            printf("mime type '%s'\n", tstring.c_str());

            idp->_inStreamp->skip(1);   /* skip picture type (cover, artist, etc) */
            tsize--;

            tstring.erase();
            code = decodeString(idp->_inStreamp, &tstring, &tsize, 1);
            printf("description: '%s'\n", tstring.c_str());

            /* write out the jpg */
            for(i=0;i<tsize;i++) {
                code = idp->_inStreamp->read((char *)tbuffer, 1);
                if (code < 0) {
                    return code;
                }
                if (outFilep)
                    fwrite(tbuffer, 1, 1, outFilep);
            }
        }
        else
            code = idp->_inStreamp->skip(asize);
        printf("Read %s (%d bytes) flags=0x%x\n",
               (char *) opcode, asize+10, (tbuffer[0]<<8) + tbuffer[1]);
        _bytesLeft -= asize+10;
    }

    printf("idversion=%d unsync=%d extended=%d bytesLeft=%d\n",
           idVersion, _flagUnsync, _flagExtendedHeader, _bytesLeft);
    return 0;
}
