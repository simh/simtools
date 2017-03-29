/* cosy.c: compress/decompress CDC1700 COSY format files

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

/*
 * The CDC1700 COSY format is well documented with respect to the compression
 * algorithm (Run length encoding of 3 - 62 consecutive blanks) but less so
 * for the higher level COSY constructs. This program is written to handle
 * COSY format files found on bitsavers.org. It operates in one of 2 modes:
 *
 *  1. Decompress a sequence of one or more COSY compressed decks. Each deck
 *     will start with a CSY/ card and end with a END/ card. If the input
 *     consists of a single compressed deck, the CSY/ and END/ cards may be
 *     omitted. If a deck name (e.g. "xxx") is provided on the CSY/ card,
 *     the output file will be named "deck_xxx", if an empty deck name is
 *     provided or the CSY/ card is missing, the output file will be named
 *     "nnnnn.deck" where nnnnn is the number of the deck within the input
 *     file. The CSY/ and END/ cards will not be passed through to the
 *     output file.
 *
 *  2. Compress a series of input decks, generating a single compressed COSY
 *     file. A CSY/ card will be prepended to each input file and a END/
 *     card will be appended. If the input file name is "deck_yyy" the deck
 *     name on the CSY/ card will be yyy. Any other input file name will leave
 *     the deck name empty on the CSY/ card. The output file will be padded
 *     to a multiple of 192 words (384 bytes).
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define PREFIX          0x5F

/*
 * Record lengths.
 */
#define RECLEN          384
#define CARDLEN          80

char card[CARDLEN + 1], pad[RECLEN];
int idx = 0, outcount = 0;

int compress = 0;

#define VALID(c)        ((c >= 0x20) && (c <= 0x5E) && (c != 0x26))

/*
 * Pre-defined strings to start and end a COSY deck.
 */
char *csy = "       CSY/";
char *end = "       END/";

/*
 * Compression table
 */
#define MAXSPACES       62

char *compr[MAXSPACES+1] = {
  NULL, " ", "  ",
  "\x5F\x21", "\x5F\x22", "\x5F\x23",  "\x5F\x24", "\x5F\x25",
  "\x5F\x27", "\x5F\x28", "\x5F\x29", "\x5F\x2A",
  "\x5F\x2B", "\x5F\x2C", "\x5F\x2D", "\x5F\x2E",
  "\x5F\x2F", "\x5F\x30", "\x5F\x31", "\x5F\x32",
  "\x5F\x33", "\x5F\x34", "\x5F\x35", "\x5F\x36",
  "\x5F\x37", "\x5F\x38", "\x5F\x39", "\x5F\x3A",
  "\x5F\x3B", "\x5F\x3C", "\x5F\x3D", "\x5F\x3E",
  "\x5F\x3F", "\x5F\x40", "\x5F\x41", "\x5F\x42",
  "\x5F\x43", "\x5F\x44", "\x5F\x45", "\x5F\x46",
  "\x5F\x47", "\x5F\x48", "\x5F\x49", "\x5F\x4A",
  "\x5F\x4B", "\x5F\x4C", "\x5F\x4D", "\x5F\x4E",
  "\x5F\x4F", "\x5F\x50", "\x5F\x51", "\x5F\x52",
  "\x5F\x53", "\x5F\x54", "\x5F\x55", "\x5F\x56",
  "\x5F\x57", "\x5F\x58", "\x5F\x59", "\x5F\x5A",
  "\x5F\x5B", "\x5F\x5C", "\x5F\x5D"
};

int doCompress(FILE *, int, char **), doDecompress(FILE *);

/*++
 *      compressCard
 *
 *  Write the current card, in COSY compressed format, to the destination
 *  COSY file.
 *
 * Inputs:
 *
 *      dest            - Destination COSY file
 *
 * Outputs:
 *
 *      ...
 *
 * Returns:
 *
 *      Exit status:
 *              0 - Success
 *              3 - File write error
 *              4 - Invalid input character
 *
 --*/
int compressCard(
  FILE *dest
)
{
  int i, spcount = 0, len = strlen(card);

  for (i = 0; i < len; i++) {
    if (!VALID(card[i]))
      return 4;

    if (card[i] != ' ') {
      if (spcount != 0) {
        size_t l = strlen(compr[spcount]);

        if (fwrite(compr[spcount], sizeof(char), l, dest) != l)
          return 3;
        outcount += l;
        spcount = 0;
      }
      if (fputc(card[i], dest) == EOF)
        return 3;
      outcount++;
    } else {
      if (++spcount == MAXSPACES) {
        size_t l = strlen(compr[spcount]);

        if (fwrite(compr[spcount], sizeof(char), l, dest) != l)
          return 3;
        outcount += l;
        spcount = 0;
      }
    }
  }
  /*
   * Terminate the card image.
   */
  fwrite("\x5F\x5E", sizeof(char), 2, dest);
  outcount += 2;
  return 0;
}

/*++
 *      decompressCard
 *
 *  Read the next card image from a source file while performing COSY
 *  decompression. The card image will be null terminated.
 *
 * Inputs:
 *
 *      src             - Source COSY file
 *
 * Outputs:
 *
 *      ...
 *
 * Returns:
 *
 *      0       - Card image successfully read
 *      -1      - EOF read
 *      -2      - End of deck read
 *
 --*/
int decompressCard(
  FILE *src
)
{
  int ch, i, spcount;

  idx = 0;

  while ((ch = fgetc(src)) != -1) {
    if (ch == 0)
      continue;

    if (ch == PREFIX) {
      if ((ch = fgetc(src)) == -1)
        break;

      switch (ch) {
        case 0x21: case 0x22: case 0x23: case 0x24: case 0x25:
          spcount = ch - 0x21 + 3;
          goto fill;

        case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B:
        case 0x2C: case 0x2D: case 0x2E: case 0x2F: case 0x30:
        case 0x31: case 0x32: case 0x33: case 0x34: case 0x35:
        case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A:
        case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F:
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44:
        case 0x45: case 0x46: case 0x47: case 0x48: case 0x49:
        case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E:
        case 0x4F: case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: case 0x57: case 0x58:
        case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D:
          spcount = ch - 0x27 + 8;
      fill:
          for (i = 0; i< spcount; i++) {
            if (idx != CARDLEN)
              card[idx++] = ' ';
          }
          break;

        case 0x5E:
          card[idx] = '\0';
          return 0;

        case 0x5F:
          card[idx] = '\0';
          return -2;
      }
    } else {
      if (idx != CARDLEN)
        card[idx++] = ch;
    }
  }
  card[idx] = '\0';
  return -1;
}

/*++
 *      writeCard
 *
 *  Remove trailing spaces from the card image and write the resulting line
 *  to the destination file with a terminating newline character.
 *
 * Inputs:
 *
 *      dest            - Destination file
 *
 * Outputs:
 *
 *      ...
 *
 * Returns:
 *
 *      0               - write was successful
 *      -1              - error on  write
 *
 --*/
int writeCard(
  FILE *dest
)
{
  /*
   * Remove trailing spaces.
   */
  while (idx && (card[idx - 1] == ' '))
    card[--idx] = '\0';

  if (fprintf(dest, "%s\n", card) < 0)
    return -1;
  return 0;
}

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
          "Usage: cosy [-cd] <cosyfile> [file ...]\n");
  fprintf(stderr,
          "   Compress/decompress a COSY file\n");
  fprintf(stderr, "\nSwitches:\n\n");
  fprintf(stderr, " -c   Perform COSY compression\n");
  fprintf(stderr, " -d   Perform COSY decompression (the default)\n");
  exit(1);
}

/*++
 *      main
 *
 *  Entry point for cosy program.
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
 *      Exit status
 *
 --*/
int main(
  int argc,
  char *argv[]
)
{
  int ch;
  FILE *cosy;

  while ((ch = getopt(argc, argv, "cd")) != -1) {
    switch (ch) {
      case 'c':
        compress = 1;
        break;

      case 'd':
        compress = 0;
        break;

      default:
        usage();
    }
  }
  argc -= optind;
  argv += optind;

  if (argc < (compress ? 2 : 1))
    usage();

  /*
   * Open the COSY file.
   */
  if ((cosy = fopen(argv[0], compress ? "w" : "r")) == NULL) {
    fprintf(stderr, "Failed to open COSY file - %s\n", argv[0]);
    return 2;
  }
  argc--, argv++;

  if (compress) {
    return doCompress(cosy, argc, argv);
  }
  return doDecompress(cosy);
}

/*++
 *      doCompress
 *
 *  Compress a sequence of 1 or more files into a single COSY file.
 *
 * Inputs:
 *
 *      dest            - Destination cosy file
 *      argc            - # of source files to compress
 *      argv            - Pointer to array of file names
 *
 * Outputs:
 *
 *      ...
 *
 * Returns:
 *
 *      Exit status:
 *              0 - Success
 *              2 - File open error
 *              3 - File write error
 *              4 - Invalid input character
 *
 --*/
int doCompress(
  FILE *dest,
  int argc,
  char *argv[]
)
{
  int status = 0;
  unsigned int i;
  char *filename;
  FILE *src;

  while (argc != 0) {
    filename = argv[0];

    if ((src = fopen(filename, "r")) == NULL) {
      status = 2;
      break;
    }

    /*
     * Build a leading CSY/ card.
     */
    strcpy(card, csy);
    if (strncmp(filename, "deck_", 5) == 0) {
      /*
       * Possible deck name derived from filename.
       */
      if (strlen(filename) <= 11) {
        for (i = 5; i < strlen(filename); i++)
          if (!VALID(filename[i]))
            goto noname;

        strncpy(card, &filename[5], strlen(filename) - 5);
        goto named;
      }
    noname:
      fprintf(stderr, "Unable to use filename (%s) for deck name\n", filename);
    }
  named:

    if ((status = compressCard(dest)) != 0)
      break;

    /*
     * Copy the source file to the destination while compressing.
     */
    while (fgets(card, sizeof(card), src) != NULL) {
      int len = strlen(card);

      if (card[len - 1] == '\n')
        card[len - 1] = '\0';

      if ((status = compressCard(dest)) != 0)
        goto error;
    }

    /*
     * Build a trailing END/ card.
     */
    strcpy(card, end);
    if ((status = compressCard(dest)) != 0)
      break;

    fclose(src);

    argc--, argv++;
  }
 error:

  /*
   * Report possible error.
   */
  switch (status) {
    case 0:
      if ((outcount % RECLEN) != 0)
        fwrite(pad, sizeof(char), outcount % RECLEN, dest);
      break;

    case 2:
      fprintf(stderr, "Failed to open file - %s\n", filename);
      break;

    case 3:
      fprintf(stderr, "Error writing COSY file\n");
      break;

    case 4:
      fprintf(stderr, "Invalid character in file - %s\n", filename);
      break;

    default:
      fprintf(stderr, "Unknown exit status - %u\n", status);
      break;
  }
  fclose(dest);
  return status;
}

/*++
 *      doDecompress
 *
 *  Decompress a COSY file.
 *
 * Inputs:
 *
 *      src             - Source COSY file
 *
 * Outputs:
 *
 *      ...
 *
 * Returns:
 *
 *      Exit status:
 *              0  - Success
 *              2  - File open error
 *              3  - File write error
 *
 --*/
int doDecompress(
  FILE *src
)
{
  int seqno = 0, valid;
  char filename[32], *eofn;
  FILE *dest = NULL;

  while (decompressCard(src) == 0) {
    /*
     * First card of next deck has been read - is is a CSY/ card?
     */
    sprintf(filename, "%05u.deck", seqno);
    valid = 1;
    if (strncmp("CSY/", &card[7], 4) == 0) {
      if (card[0] != ' ') {
        if ((eofn = strchr(card, ' ')) != NULL) {
          int len = eofn - card;

          if (len <= 6) {
            *eofn = '\0';
            sprintf(filename, "deck_%s", card);
          }
        }
      }
      valid = 0;
    }

    if ((dest = fopen(filename, "w")) == 0) {
      fprintf(stderr, "Failed to create file - %s\n", filename);
      return 2;
    }

    /*
     * If the first line was not a CSY/ card, write to the destination file
     */
    if (valid) {
      if (writeCard(dest) == -1) {
        fprintf(stderr, "Error writing to file - %s\n", filename);
        fclose(dest);
        return 3;
      }
    }

    /*
     * Now process the rest of this file.
     */
    while (dest != NULL) {
      switch (decompressCard(src)) {
        case 0:
          if (strncmp("END/", &card[7], 4) == 0) {
            fclose(dest);
            dest = NULL;
            break;
          }
          if (writeCard(dest) == -1) {
            fprintf(stderr, "Error writing to file%s\n", filename);
            fclose(dest);
            return 3;
          }
          break;

        case -1:
          fclose(dest);
          return 0;

        case -2:
          fclose(dest);
          dest = NULL;
          break;
      }
    }
    seqno++;
  }
  return 0;
}
