/* cpytap.c: copy SIMH .tap container with file changes

   Copyright (c) 2015-2017, John Forecast

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

int reclen = DEFRECLEN;

/*
 * We maintain an array of requests indexed by the file # on the source tape.
 */
#define MAXFILES        100             /* Max files on tape */
#define APPFILES         20             /* Max append files */

struct info {
  int           reclen;                 /* Record length to be used */
  char          *filename;              /* Filename */
};

struct fileop {
  int           used;                   /* Use count */
  int           skipfile;               /* Skip this file */
  struct info   repfile;                /* Replace with this file */
  struct info   insfiles[APPFILES];     /* Insert these files (in order) */
} fileops[MAXFILES + 1];

struct fileop appendops;

char *srcfile = NULL, *dstfile = NULL;

FILE *src = NULL, *dst = NULL;

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
 *      Never returns
 *
 --*/
void usage(void)
{
  fprintf(stderr,
          "Usage: cpytap src dst [-r len] [-S n] [-I n,file] [-R n,file] [-A file]\n");
  fprintf(stderr,
          "  Copy src .tap file to destination maintaining the internal\n"
          "  record structure. The switches control file level changes to\n"
          "  destination tape.\n");
  fprintf(stderr, "\nSwitches:\n\n");
  fprintf(stderr,
          "-r len        - Specify max record size when writing new files.\n"
          "                There may be multiple \"-r\" switches on the\n"
          "                command line if the new files need different\n"
          "                record sizes. (%u <= len <= %u, default %u)\n"
          "                If len is outside the supported range, it will be\n"
          "                quietly modified to the minimum or maximum value.\n",
          MINRECLEN, MAXRECLEN, DEFRECLEN);
  fprintf(stderr,
          "-I n,file     - Insert specified file before file n of the source tape.\n");
  fprintf(stderr,
          "-R n,file     - Replace file n of the source tape with the specified file.\n");
  fprintf(stderr,
          "-S n          - Skip file n from the source tape.\n");
  fprintf(stderr,
          "-A file       - Append the specified file after all the files on\n"
          "                the source tape have been copied to the destination.\n");
  fprintf(stderr,
          "\nFiles are number 1 - N according to their position on the\n"
          "source tape.\n");
  exit(1);
}

/*++
 *      copyFile
 *
 *  Copy the next file from the source tape to the destination tape
 *  replicating the record structure up to and including the tape mark.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      ...
 *
 * Returns:
 *
 *      ST_EOM          - End of medium detected
 *      ST_TM           - Tape mark detected
 *
 --*/
static unsigned int copyFile(void)
{
  char record[MAXRCLNT];
  uint32 len;

  for (;;) {
    switch (len = ReadTapeRecord(src, record, sizeof(record))) {
        case ST_EOM:
          return ST_EOM;

        case ST_TM:
          if (WriteTapeMark(dst, 0) == 0)
            return ST_TM;
          fprintf(stderr, "Error writing tape mark to destination tape\n");
          exit(6);

        default:
          if (WriteTapeRecord(dst, record, len) == 0)
            continue;
          fprintf(stderr, "Error writing record to destination tape\n");
          exit(6);
      }
  }
}

/*++
 *      writeFile
 *
 *  Write a file at the current position on the destination tape.
 *
 * Inputs:
 *
 *      filename        - Pointer to name of file to be written
 *      rlen            - Max record size to use
 *
 * Outputs:
 *
 *      ...
 *
 * Returns:
 *
 *      None
 *
 --*/
static void writeFile(
  char *filename,
  int rlen
)
{
  FILE *file;
  size_t datalen;
  char record[MAXRCLNT];

  if ((file = fopen(filename, "r")) != NULL) {
    while ((datalen = fread(record, sizeof(char), rlen, file)) != 0) {
      if (ferror(file)) {
        fprintf(stderr, "Error reading %s\n", filename);
        exit(4);
      }
      if (WriteTapeRecord(dst, record, rlen) != 0) {
        fprintf(stderr, "Error writing record to destination tape\n");
        exit(5);
      }
    }
    if (WriteTapeMark(dst, 0) != 0) {
      fprintf(stderr, "Error writing tape mark to destination tape\n");
      exit(5);
    }
    fclose(file);
    return;
  }
  fprintf(stderr, "Failed to open file - %s\n", filename);
  exit(3);
}

/*++
 *      parse
 *
 *  Parse a command line switch argument of the form "n,filename" where n
 *  is an integerin the range 1 - MAXFILES.
 *
 * Inputs:
 *
 *      arg             - Pointer to "n,filename" string
 *      file            - Pointer to int to receive "n"
 *      name            - Pointer to char* to receive filename
 *
 * Outputs:
 *
 *      ...
 *
 * Returns:
 *
 *      0               - Success
 *      -1              - Invalid argument format
 *
 --*/
int parse(
  char *arg,
  int *file,
  char **name
)
{
  char *endptr;

  *file = strtoul(arg, &endptr, 0);

  if ((*endptr == ',') && (*++endptr == '\0'))
    return -1;
  if ((*file == 0) || (*file > MAXFILES))
    return -1;

  *name = endptr;
  return 0;
}

/*++
 *      main
 *
 *  Entry point for cpytap program.
 *
 * Inputs:
 *
 *      argc            - # of supplied arguments
 *      argv            - Array of argument strings
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Exit status for cpytap
 *
 --*/
int main(
  int argc,
  char *argv[]
)
{
  int ch, i, filenumber = 1, filenum;
  char *filename;

  if (argc < 3)
    usage();

  srcfile = argv[1];
  dstfile = argv[2];
  argc -= 2;
  argv += 2;

  memset(fileops, 0, sizeof(fileops));
  memset(&appendops, 0, sizeof(appendops));

  while ((ch = getopt(argc, argv, "r:A:I:R:S:")) != -1) {
    switch (ch) {
      case 'r':
        reclen = strtoul(optarg, NULL, 0);
        if (reclen < MINRECLEN)
          reclen = MINRECLEN;
        if (reclen > MAXRECLEN)
          reclen = MAXRECLEN;
    done:
        break;

      case 'A':
        for (i = 0; i < APPFILES; i++) {
          if (appendops.insfiles[i].filename == NULL) {
            appendops.insfiles[i].reclen = reclen;
            appendops.insfiles[i].filename = argv[optind];
            goto done;
          }
        }
        fprintf(stderr, "No space for appending file - %s\n", argv[optind]);
        return 5;

      case 'I':
        if (parse(optarg, &filenum, &filename) != 0) {
          fprintf(stderr, "Invalid argument - -I %s\n", optarg);
          return 7;
        }

        for (i = 0; i < APPFILES; i++) {
          if (fileops[filenum].insfiles[i].filename == NULL) {
            fileops[filenum].insfiles[i].reclen = reclen;
            fileops[filenum].insfiles[i].filename = filename;
            goto done;
          }
        }
        fprintf(stderr, "No space for inserting file - %s\n", filename);
        return 5;

      case 'R':
        if (parse(optarg, &filenum, &filename) != 0) {
          fprintf(stderr, "Invalid argument - -R %s\n", optarg);
          return 7;
        }
        if (fileops[filenum].repfile.filename != NULL) {
          fprintf(stderr, "File %u is already being replaced.\n", filenum);
          return 8;
        }
        fileops[filenum].repfile.reclen = reclen;
        fileops[filenum].repfile.filename = filename;
        break;

      case 'S':
        filenum = strtoul(optarg, NULL, 0);
        if ((filenum == 0) || (filenum > MAXFILES)) {
          fprintf(stderr, "Invalid file number - -D %u\n", filenum);
          return 6;
        }
        fileops[filenum].skipfile = 1;
        break;

      case '?':
      default:
        usage();
    }
  }

  /*
   * Begin processing the source tape.
   */
  switch (OpenTapeForRead(&src, srcfile)) {
    case TIO_SUCCESS:
      break;

    case TIO_ERROR:
      fprintf(stderr, "%s has errors and may not copy correctly\n", srcfile);
      break;

    case TIO_CORRUPT:
      fprintf(stderr, "%s is not a SIMH .tap container file\n", srcfile);
      exit(2);

    case TIO_OPENFAIL:
      fprintf(stderr, "%s open failed\n", srcfile);
      exit(3);
  }

  /*
   * Begin processing the destination tape.
   */
  switch (OpenTapeForWrite(&dst, dstfile)) {
    case TIO_SUCCESS:
      break;

    case TIO_IOERROR:
      fprintf(stderr, "Error writing to destination tape - %s\n", dstfile);
      exit(5);

    case TIO_CREATEFAIL:
      fprintf(stderr, "Failed to create destination tape - %s\n", dstfile);
      exit(3);
  }

  /*
   * Process the files on the tape.
   */
  for (;;) {
    if (filenumber <= MAXFILES) {
      struct fileop *op = &fileops[filenumber];

      /*
       * Process possible repalcement.
       */
      if (op->repfile.filename != NULL)
        writeFile(op->repfile.filename, op->repfile.reclen);

      /*
       * Process any insertions before the current file.
       */
      for (i = 0; i < APPFILES; i++)
        if (op->insfiles[i].filename != NULL)
          writeFile(op->insfiles[i].filename, op->insfiles[i].reclen);

      /*
       * Now either copy or skip over the next file on the source tape.
       */
      if ((op->skipfile != 0) || (op->repfile.filename != 0)) {
        if (SkipToNextTapeMark(src) == ST_EOM)
          break;
      } else {
        if (copyFile() == ST_EOM)
          break;
      }
      filenumber++;
    } else {
      /*
       * If there are more than MAXFILES files on the source tape, just copy
       * the remaining files.
       */
      if (copyFile() == ST_EOM)
        break;
    }
  }
  /*
   * Handle any appends
   */
  for (i = 0; i < APPFILES; i++)
    if (appendops.insfiles[i].filename != NULL)
      writeFile(appendops.insfiles[i].filename, appendops.insfiles[i].reclen);

  WriteTapeMark(dst, 0);
  CloseTape(src);
  CloseTape(dst);
  return 0;
}
