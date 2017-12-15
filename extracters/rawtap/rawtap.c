/* rawtap.c: process tape files within a SIMH .tap container

   Copyright (c) 2017, John Forecast

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   JOHN FORECAST BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of John Forecast shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from John Forecast.

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "tapeio.h"

#define MINRECLEN           1
#define MAXRECLEN       65536
#define DEFRECLEN       10240

int reclen = DEFRECLEN, seqno = 1;
int create = 0, append = 0, extract = 0;

char record[MAXRCLNT];

/*++
 *      usage
 *
 *  Display a usage message on stderr and exit.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
void usage(void)
{
  fprintf(stderr, "Usage: rawtap -cae container [-r len] [...]\n\n");
  fprintf(stderr,
          "   Create new container and append file(s):\n");
  fprintf(stderr,
          "     rawtap -c container [-r len] file ...\n\n");
  fprintf(stderr,
          "   Append file(s) to an existing container:\n");
  fprintf(stderr,
          "     rawtap -a container [-r len] file ...\n\n");
  fprintf(stderr,
          "   Extract files from container(s):\n");
  fprintf(stderr,
          "     rawtap -e container ...\n");
  fprintf(stderr,
          "       - Extracted files will be name 00001.dat, 00002.dat etc\n\n");
  fprintf(stderr,
          "\nSwitches:\n\n");
  fprintf(stderr,
          "-r len     - Specify max tape record size (%u <= len <= %u)\n",
          MINRECLEN, MAXRECLEN);
  fprintf(stderr,
          "             The record size applies to all following files and\n");
  fprintf(stderr,
          "             there may be several \"-r nnn\" present on the command line.\n");
  fprintf(stderr,
          "             The default record size is 10240 bytes.\n\n");
  exit(1);
}

/*++
 *      main
 *
 *  Entry point for the rawtap program.
 *
 * Inputs:
 *
 *      argc            - # of supplied arguments
 *      argv            - array of argument strings
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Exit status for rawtap
 *
 --*/
int main(
  int argc,
  char *argv[]
)
{
  int readError = 0, writeError = 0;
  char *s, filename[16], bot;
  FILE *file;
  unsigned int status;

  if ((argc < 2) || (argv[0] == NULL))
    usage();

  argc--, argv++;

  s = argv[0];
  if ((s != NULL) && (*s++ == '-')) {
    ++argv, --argc;

    switch (*s) {
      case 'c':
        create = 1;
        break;

      case 'a':
        append = 1;
        break;

      case 'e':
        extract = 1;
        break;

      default:
        fprintf(stderr, "Bad option: %c\n", *s);
        usage();
    }
  }

  if (create != 0) {
    /*
     * Create a new container file and append 0 or more files to the tape.
     */
    if (argc <= 1)
      usage();

    switch (OpenTapeForWrite(argv[0])) {
      case TIO_SUCCESS:
    appendFiles:
        argc--, argv++;

        while (argc >= 1) {
          if (strcmp(argv[0], "-r") == 0) {
            argc--, argv++;

            if (argc >= 1) {
              reclen = strtol(argv[0], NULL, 10);
              if (reclen < MINRECLEN)
                reclen = MINRECLEN;
              if (reclen > MAXRECLEN)
                reclen = MAXRECLEN;
              argc--, argv++;
            }
            continue;
          }
          if ((file = fopen(argv[0], "r")) != NULL) {
            size_t datalen;
            
            while ((datalen = fread(record, sizeof(char), reclen, file)) != 0) {
              if (ferror(file)) {
                readError++;
                break;
              }
              if (WriteTapeRecord(record, datalen) != 0) {
                writeError++;
                break;
              }
            }
            fclose(file);
            if ((WriteTapeMark(0) != 0) || (WriteTapeMark(1) != 0))
              writeError++;
          } else {
            writeError++;
            break;
          }
          argc--, argv++;
        }
        CloseTape();
        break;

      case TIO_IOERROR:
        fprintf(stderr, "Error writing to container file %s\n", argv[0]);
        exit(5);

      case TIO_CREATEFAIL:
        fprintf(stderr, "Failed to create container file - %s\n", argv[0]);
        exit(3);
    }
  } else if (append != 0) {
    /*
     * Open an existing container file and append 0 or more files to the tape.
     */
    if (argc <= 1)
      usage();

    switch (OpenTapeForAppend(argv[0])) {
      case TIO_SUCCESS:
      case TIO_ERROR:
        goto appendFiles;

      case TIO_CORRUPT:
        fprintf(stderr, "%s is not a SIMH .tap container file\n", argv[0]);
        exit(2);

      case TIO_OPENFAIL:
        fprintf(stderr, "%s open failed\n", argv[0]);
        exit(3);
    }
  } else if (extract != 0) {
    /*
     * Extract files from one or more container files.
     */
    if (argc == 0)
      usage();

    while (argc >= 1) {
      bot = 1;

      switch (OpenTapeForRead(argv[0])) {
        case TIO_SUCCESS:
        case TIO_ERROR:
          /*
           * Extract files from the container file.
           */
          do {
            switch (status = ReadTapeRecord(record, sizeof(record))) {
              case ST_EOM:
                break;

              case ST_TM:
                /* Ignore tape mark at the beginning pf a container */
                if (!bot) {
                  /* Second tape mark in a row - treat as end of medium */
                  status = ST_EOM;
                }
                break;

              default:
                if ((status & ST_ERROR) == 0) {
                  sprintf(filename, "%05u.dat", seqno++);
                  if ((file = fopen(filename, "w")) != NULL) {
                    while ((status != ST_EOM) && (status != ST_TM)) {
                      size_t length = status & ST_LENGTH;

                      if ((status & ST_ERROR) != 0)
                        readError++;

                      if (fwrite(record, sizeof(char), length, file) != length)
                        writeError++;

                      status = ReadTapeRecord(record, sizeof(record));
                    }
                  } else writeError++;
                }
                break;
            }
            bot = 0;
          } while (status != ST_EOM);
          CloseTape();
          break;

        case TIO_CORRUPT:
          fprintf(stderr, "%s is not a SIMH .tap container file\n", argv[0]);
          exit(2);

        case TIO_OPENFAIL:
          fprintf(stderr, "%s open failed\n", argv[0]);
          exit(3);
      }
      argc--, argv++;
    }
  }

  if ((readError != 0) || (writeError != 0)) {
    fprintf(stderr, "Read Errors: %d, Write Errors: %d\n",
            readError, writeError);
    return 4;
  }
  return 0;
}
