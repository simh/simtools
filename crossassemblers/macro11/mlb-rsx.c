/*
 * Routines for reading from an RSX-11 M+ macro library (like RSXMAC.SML)
 */
/*
Copyright (c) 2001, Richard Krehbiel
Copyright (c) 2015, 2017, Olaf Seibert
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

o Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

o Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

o Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.
*/

/*
 * OLB: Object LiBrary
 * MLB, SML: MacroLiBrary, System MacroLibrary.
 */
/*
 * The MLB file format is (mostly) documented in chapter 10 of
 * 3a/AA-JS15A-TC_RSX-11M-PLUS_4.0_Utilities_Manual_Aug87.pdf .
 *
 *                       Figure 10-3 (page 10-6):
 *
 *                       Library file header
 *
 *                                                     Byte offset (octal!)
 *                  +1                          +0
 *  +------------------------------+------------------------------+
 *  |Nonzero ID (2)                | Library type 0=OLB 1=MLB     |  0
 *  +------------------------------+------------------------------+
 *  |                  LBR (Librarian) version                    |  2
 *  +-------------                                   -------------+
 *  |                  IDENT format                               |  4
 *  +------------------------------+------------------------------+
 *  |                                             Year            |  6
 *  +-------------                                   -------------+
 *  |      Date and                               Month           | 10
 *  +-------------                                   -------------+
 *  |      Time of Last                           Day             | 12
 *  +-------------                                   -------------+
 *  |      Insert                                 Hour            | 14
 *  +-------------                                   -------------+
 *  |                                             Minute          | 16
 *  +-------------                                   -------------+
 *  |                                             Second          | 20
 *  +------------------------------+------------------------------+
 *  |      Reserved                | Size EPT entries             | 22
 *  +------------------------------+------------------------------+
 *  |      EPT starting relative block (Entry Point Table)        | 24
 *  +------------------------------+------------------------------+
 *  |      Nr of EPT entries allocated                            | 26
 *  +------------------------------+------------------------------+
 *  |      Nr of EPT entries available                            | 30
 *  +------------------------------+------------------------------+
 *  |      Reserved                | Size MNT entries             | 32
 *  +------------------------------+------------------------------+
 *  |      MNT starting relative block (Module Name Table)        | 34
 *  +------------------------------+------------------------------+
 *  |      Nr of MNT entries allocated                            | 36
 *  +------------------------------+------------------------------+
 *  |      Nr of MNT entries available                            | 40
 *  +------------------------------+------------------------------+
 *  |      Logically Deleted                                      | 42
 *  +-------------                                   -------------+
 *  |      Available (bytes)                                      | 44
 *  +------------------------------+------------------------------+
 *  |      Contiguous Space                                       | 46
 *  +-------------                                   -------------+
 *  |      Available (bytes)                                      | 50
 *  +------------------------------+------------------------------+
 *  |      Next Insert Relative Block                             | 52
 *  +-------------                                   -------------+
 *  |      Start Byte Within Block                                | 54
 *  +------------------------------+------------------------------+
 *  |   Default Insert Type for Universal Libraries (not for MLB) | 56
 *  +------------------------------+------------------------------+
 *
 *                       Figure 10-5 (page 10-7):
 *
 *                 Format of Module Name Table (MNT) element:
 *                                                                 Byte
 *  +------------------------------+------------------------------+
 *  |      Module Name                                            |  0
 *  +-------------                                   -------------+
 *  |      RAD50                                                  |  2
 *  +------------------------------+------------------------------+
 *  |      Address of module                      Relative Block  |  4
 *  +-------------                                   -------------+
 *  |      header                                 Byte in Block   |  6
 *  +------------------------------+------------------------------+
 *
 *                       Figure 10-6 (page 10-8):
 *
 *                       Module header format for
 *                      Object And Macro Libraries
 *                                                                 Byte
 *                                                 1=deleted module
 *  +------------------------------+------------------------------+
 *  |   Attributes                 |   Status   0=normal module   |  0
 *  +------------------------------+------------------------------+
 *      ....
 *
 *  The above layout is from the book but this seems to match my files:
 *  +------------------------------+------------------------------+
 *  |   Attributes                                                |  0
 *  +------------------------------+------------------------------+
 *  |   Status 0=normal module 1=deleted module                   |  2
 *  +------------------------------+------------------------------+
 *  |                  Size Of Module                high word    |  4
 *  +-------------                                   -------------+
 *  |                  (bytes)                       low word     |  6
 *  +------------------------------+------------------------------+
 *  |      Date                                   Year            | 10
 *  +-------------                                   -------------+
 *  |      Module                                 Month           | 12
 *  +-------------                                   -------------+
 *  |      Inserted                               Day             | 14
 *  +------------------------------+------------------------------+
 *  |      Type dependent Information                             | 16
 *  +-------------                                   -------------+
 *  |      (undefined for MLB)                                    | 20
 *  +------------------------------+------------------------------+
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "rad50.h"
#include "stream2.h"
#include "mlb.h"
#include "util.h"

static MLB     *mlb_rsx_open(
    char *name,
    int allow_object_library);
static BUFFER  *mlb_rsx_entry(
    MLB *mlb,
    char *name);
static void     mlb_rsx_close(
    MLB *mlb);
static void     mlb_rsx_extract(
    MLB *mlb);

struct mlb_vtbl mlb_rsx_vtbl = {
    mlb_rsx_open,
    mlb_rsx_entry,
    mlb_rsx_extract,
    mlb_rsx_close,
};

#define BLOCKSIZE               512

#define WORD(cp) ((*(cp) & 0xff) + ((*((cp)+1) & 0xff) << 8))

/* BYTEPOS calculates the byte position within the macro libray file. */

#define BYTEPOS(rec) (((WORD((rec)+4) & 32767) - 1) * BLOCKSIZE + \
                       (WORD((rec)+6) & 511))

/* trim removes trailing blanks from a string. */
static void trim(
    char *buf)
{
    char           *cp = buf + strlen(buf);

    while (--cp >= buf && *cp == ' ')
        *cp = 0;
}

/* mlb_open opens a file which is given to be a macro library. */
/* Returns NULL on failure. */

MLB            *mlb_rsx_open(
    char *name,
    int allow_objlib)
{
    MLB            *mlb = memcheck(malloc(sizeof(MLB)));
    char           *buff;
    unsigned        entsize;
    unsigned        nr_entries;
    unsigned        start_block;
    int             i;

    mlb->vtbl = &mlb_rsx_vtbl;
    mlb->directory = NULL;

    mlb->fp = fopen(name, "rb");
    if (mlb->fp == NULL) {
        mlb_rsx_close(mlb);
        return NULL;
    }

    buff = memcheck(malloc(060));      /* Size of MLB library header */

    if (fread(buff, 1, 060, mlb->fp) < 060) {
        fprintf(stderr, "error: can't read full header\n");
        mlb_rsx_close(mlb);
        free(buff);
        return NULL;
    }

    mlb->is_objlib = 0;
    if (allow_objlib && WORD(buff) == 01000) { /* Is it an object library? */
        mlb->is_objlib = 1;
    } else if (WORD(buff) != 01001) {  /* Is this really a macro library? */
        /* fprintf(stderr, "error: first word not correct value\n"); */
        mlb_rsx_close(mlb);            /* Nope. */
        free(buff);
        return NULL;
    }

    entsize = buff[032];               /* The size of each macro directory
                                          entry */
    nr_entries = WORD(buff + 036);     /* The number of directory entries */
    start_block = WORD(buff + 034) - 1;/* The start RT-11 block of the
                                          directory */

    if (entsize < 8) {                 /* Is this really a macro library? */
        mlb_rsx_close(mlb);            /* Nope. */
        fprintf(stderr, "error: entsize too small: %d\n", entsize);
        return NULL;
    }

//    fprintf(stderr, "entsize=%d, nr_entries=%d, start_block=%d\n",
//            entsize, nr_entries, start_block);
    free(buff);                        /* Done with that header. */

    /* Allocate a buffer for the disk directory */
    buff = memcheck(malloc(nr_entries * entsize));
    fseek(mlb->fp, start_block * BLOCKSIZE, SEEK_SET);        /* Go to the directory */

    /* Read the disk directory */
    if (fread(buff, entsize, nr_entries, mlb->fp) < nr_entries) {
        mlb_rsx_close(mlb);            /* Sorry, read error. */
        free(buff);
        return NULL;
    }

    /* Shift occupied directory entries to the front of the array */
    {
        int             j;

        for (i = 0, j = nr_entries; i < j; i++) {
            char           *ent1,
                           *ent2;

            ent1 = buff + (i * entsize);
            /* Unused entries have 0177777 0177777 for the RAD50 name,
               which is not legal RAD50. */
            if (WORD(ent1) == 0177777 && WORD(ent1 + 2) == 0177777) {
                while (--j > i
                       && (ent2 = buff + (j * entsize), WORD(ent2) == 0177777 && WORD(ent2 + 2) == 0177777)) ;
                if (j <= i)
                    break;             /* All done. */
                memcpy(ent1, ent2, entsize);    /* Move used entry
                                                   into unused entry's
                                                   space */
                memset(ent2, 0377, entsize);    /* Mark entry unused */
            } else {
//            fprintf(stderr, "entry %d:  %02x%02x.%02x%02x\n",
//                    i, ent1[5] & 0xFF, ent1[4] & 0xFF, ent1[7] & 0xFF, ent1[6] & 0xFF);
            }
        }

        /* Now i contains the actual number of entries. */

        mlb->nentries = i;
//        fprintf(stderr, " mlb->nentries=%d\n",  mlb->nentries);

        /* Now, allocate my in-memory directory */
        mlb->directory = memcheck(malloc(sizeof(MLBENT) * mlb->nentries));
        memset(mlb->directory, 0, sizeof(MLBENT) * mlb->nentries);

        /* Build in-memory directory */
        for (j = 0; j < i; j++) {
            char            radname[16];
            char           *ent;

            ent = buff + (j * entsize);

            unrad50(WORD(ent), radname);
            unrad50(WORD(ent + 2), radname + 3);
            radname[6] = 0;

//            fprintf(stderr, "entry %d: \"%s\" %02x%02x.%02x%02x\n",
//                    j, radname,
//                    ent[5] & 0xFF, ent[4] & 0xFF, ent[7] & 0xFF, ent[6] & 0xFF);
            trim(radname);

            mlb->directory[j].label = memcheck(strdup(radname));
            mlb->directory[j].position = BYTEPOS(ent);
//            fprintf(stderr, "entry %d: \"%s\" bytepos=%d\n", j, mlb->directory[j].label, mlb->directory[j].position);
            mlb->directory[j].length = -1;
        }

        free(buff);
    }

    /* Done.  Return the struct that represents the opened MLB. */
    return mlb;
}

/* mlb_rsx_close discards MLB and closes the file. */
void mlb_rsx_close(
    MLB *mlb)
{
    if (mlb) {
        int             i;

        if (mlb->directory) {
            for (i = 0; i < mlb->nentries; i++)
                if (mlb->directory[i].label)
                    free(mlb->directory[i].label);
            free(mlb->directory);
        }
        if (mlb->fp)
            fclose(mlb->fp);

        free(mlb);
    }
}

/* mlb_rsx_entry returns a BUFFER containing the specified entry from the
   macro library, or NULL if not found. */

BUFFER         *mlb_rsx_entry(
    MLB *mlb,
    char *name)
{
    int             i;
    MLBENT         *ent = NULL;
    BUFFER         *buf;
    char           *bp;
    int             c;
    unsigned char   module_header[022];

    for (i = 0; i < mlb->nentries; i++) {
        ent = &mlb->directory[i];
        if (strcmp(mlb->directory[i].label, name) == 0)
            break;
    }

    if (i >= mlb->nentries) {
//        fprintf(stderr, "mlb_rsx_entry: %s not found\n", name);
        return NULL;
    }

    fseek(mlb->fp, ent->position, SEEK_SET);
//    fprintf(stderr, "mlb_rsx_entry: %s at position %ld\n", name, (long)ent->position);

#define MODULE_HEADER_SIZE      022

    if (fread(module_header, MODULE_HEADER_SIZE, 1, mlb->fp) < 1) {
//        fprintf(stderr, "mlb_rsx_entry: %s at position %lx can't read 022 bytes\n", name, (long)ent->position);
        return NULL;
    }

//    for (i = 0; i < MODULE_HEADER_SIZE; i++) {
//        fprintf(stderr, "%02x ", module_header[i]);
//    }
//    fprintf(stderr, "\n");
    ent->length = (WORD(module_header + 04) << 16) +
                   WORD(module_header + 06);
    ent->length -= MODULE_HEADER_SIZE; /* length is including this header */
//    fprintf(stderr, "mlb_rsx_entry: %s at position %lx length = %d\n", name, (long)ent->position, ent->length);

    if (module_header[02] == 1) {
        fprintf(stderr, "mlb_rsx_entry: %s at position %lx deleted entry\n", name, (long)ent->position);
        /* Deleted Entry */
        return NULL;
    }

    /*
     * Allocate a buffer to hold the text.
     * The text is always shorter than the on-disk size.
     */
    buf = new_buffer();
    buffer_resize(buf, ent->length);       /* Make it large enough */
    bp = buf->buffer;

    /*
     * Check the file format: variable length records,
     * or stream of bytes.
     * Not sure if this check is the correct one; I've only
     * seen MLB and OLB files with var length records.
     */

    if (mlb->is_objlib) {
        /* In object libraries, copy the internal structure, since we
         * can consider them to be binary.
         */
        i = fread(bp, 1, ent->length, mlb->fp);
        bp += i;
    } else if (module_header[0] & 0x10) {
//        fprintf(stderr, "mlb_rsx_entry: %s at position %lx variable length records\n", name, (long)ent->position);
        /* Variable length records with size before them */
        i = ent->length;
        while (i > 0) {
            int length;

//            fprintf(stderr, "file offset:$%lx\n", (long)ftell(mlb->fp));
            c = fgetc(mlb->fp);            /* Get low byte of length */
            length = c & 0xFF;
            c = fgetc(mlb->fp);            /* Get high byte */
            length += (c & 0xFF) << 8;
            i -= 2;
//            fprintf(stderr, "line length: %d $%x\n", length, length);

            /* Odd lengths are padded with an extra 0 byte */
            int padded = length & 1;
            if (length > i) {
                fprintf(stderr, "line length %d > remaining archive member %d\n", length, i);
                length = i;
            }

            while (length > 0) {
                c = fgetc(mlb->fp);        /* Get macro byte */
                //fprintf(stderr, "%02x %c length=%d\n", c, c, length);
                i--;
                length--;
                if (c == '\r' || c == 0)   /* If it's a carriage return or 0,
                                              discard it. */
                    continue;
                *bp++ = c;
            }
            *bp++ = '\n';
            if (padded) {
                c = fgetc(mlb->fp);        /* Get pad byte; need not be 0. */
                //fprintf(stderr, "pad byte %02x %c length=%d\n", c, c, length);
                i--;
            }
        }
    } else {
//        fprintf(stderr, "mlb_rsx_entry: %s at position %lx byte stream records\n", name, (long)ent->position);
        for (i = 0; i < ent->length; i++) {
            c = fgetc(mlb->fp);            /* Get macro byte */
            if (c == '\r' || c == 0)       /* If it's a carriage return or 0,
                                              discard it. */
                continue;
            *bp++ = c;
        }
    }

    /* Now resize that buffer to the length actually read. */
    buffer_resize(buf, (int) (bp - buf->buffer));

    return buf;
}

/* mlb_rsx_extract - walk thru a macro library and store its contents
   into files in the current directory.

   See, I had decided not to bother writing macro library maintenance
   tools, since the user can call macros directly from the file
   system.  But if you've already got a macro library without the
   sources, you can use this to extract the entries and maintain them
   in the file system from thence forward.
*/

void mlb_rsx_extract(
    MLB *mlb)
{
    int             i;
    FILE           *fp;
    BUFFER         *buf;

    for (i = 0; i < mlb->nentries; i++) {
        char            name[32];

        buf = mlb_entry(mlb, mlb->directory[i].label);
        if (buf != NULL) {
            char *suffix = mlb->is_objlib ? "OBJ" : "MAC";
            sprintf(name, "%s.%s", mlb->directory[i].label, suffix);
            fp = fopen(name, "w");
            int length = buf->length;
            fwrite(buf->buffer, 1, length, fp);
            fclose(fp);
            buffer_free(buf);
        }
    }
}
