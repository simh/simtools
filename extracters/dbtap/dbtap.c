/* dbtap.c: process DOS/BATCH-11 tape files within a SIMH .tap container

   Copyright (c) 2015, John Forecast

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
#include "dos11.h"
#include "tapeio.h"
char *ascii = "";
int extra = 0, strict = 0, reclen = 512;
uint8 prog = 1, proj = 1;

int list = 0, create = 0, append = 0, extract = 0;

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
  fprintf(stderr,
          "Usage: dbtap -lcae [-r len -A <list> -E -S -P pg,pj] container [...]\n\n");
  fprintf(stderr,
          "   List contents of container file(s):\n");
  fprintf(stderr,
          "     dbtap -l container ...\n\n");
  fprintf(stderr,
          "   Create new container and append file(s):\n");
  fprintf(stderr,
          "     dbtap -c [-r len -A <list> -S -P pg,pj] container file ...\n\n");
  fprintf(stderr,
          "   Append file(s) to an existing container:\n");
  fprintf(stderr,
          "     dbtap -a [-r len -A <list> -S -P pg,pj] container file ...\n\n");
  fprintf(stderr,
          "   Extract files from container(s):\n");
  fprintf(stderr,
          "     dbtap -e [-A <list> -E] container ...\n\n");
  fprintf(stderr, "\nSwitches:\n\n");
  fprintf(stderr,
          "-r len     - Specifiy max tape record size (512 <= len <= 65536)\n");
  fprintf(stderr,
          "-A <list>  - Specify filename extension for ASCII tranefer mode\n");
  fprintf(stderr,
          "             e.g. \".MAC,.BAT\" or \".MAC:.BAT\"\n");
  fprintf(stderr,
          "-E         - Include prog,proj in filename when extracting\n");
  fprintf(stderr,
          "             e.g. FILE_ggg_jjj.EXT\n");
  fprintf(stderr,
          "-S         - Use DOS/BATCH-11 6+3 filenames (rather than 9+3)\n");
  fprintf(stderr,
          "-P pg,pj   - Specify prog,proj number when writing to tape\n");
  fprintf(stderr,
          "             Numbers are in octal. Default is [1,1].\n");
  exit(1);
}

/*++
 *      main
 *
 *  Entry point for the dbtap program.
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
 *      Exit status for dbtap
 *
 --*/
int main(
  int argc,
  char *argv[]
)
{
  int ch;
  uint8 myprog, myproj;

  while ((ch = getopt(argc, argv, "lcaer:A:EP:S")) != -1) {
    switch (ch) {
      case 'l':
        list = 1;
        break;

      case 'c':
        create = 1;
        break;

      case 'a':
        append = 1;
        break;

      case 'e':
        extract = 1;
        break;

      case 'r':
        reclen = strtoul(optarg, NULL, 0);
        if (reclen < 512)
          reclen = 512;
        if (reclen > MAXRCLNT)
          reclen = MAXRCLNT;
        break;

      case 'A':
        ascii = optarg;
        break;

      case 'E':
        extra = 1;
        break;

      case 'P':
        if (sscanf(optarg, "%hho,%hho", &myprog, &myproj) != 2)
          usage();

        prog = myprog;
        proj = myproj;
        break;

      case 'S':
        strict = 1;
        break;

      case '?':
      default:
        usage();
    }
  }
  argc -= optind;
  argv += optind;

  /*
   * Only one of -l, -c, -a, -e can be specified.
   */
  if ((list + create + append + extract) != 1)
    usage();

  if (list != 0) {
    /*
     * List directories on one or more container files.
     */
    if (argc == 0)
      usage();

    while (argc >= 1) {
      switch (OpenTapeForRead(argv[0])) {
        case TIO_SUCCESS:
        case TIO_ERROR:
          printf("%s:\n\n", argv[0]);
          listDirectory();
          printf("\n");
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
  } else if (create != 0) {
    /*
     * Create a new container file and append 0 or more files to the tape.
     */
    if (argc <= 1)
      usage();

    switch (OpenTapeForWrite(argv[0])) {
      case TIO_SUCCESS:
        argc--, argv++;

        while (argc >= 1) {
          switch (appendFile(argv[0], ascii, prog, proj, reclen, strict)) {
            case -1:
              fprintf(stderr, "Failed to append %s to tape\n", argv[0]);
              fprintf(stderr, "Container file is probably corrupt\n");
              exit(6);

            case 1:
              fprintf(stderr, "Failed to open file %s - skipping\n", argv[0]);
              /* FALLTHROUGH */

            case 0:
              break;
          }
          argc--, argv++;
        }
        CloseTape();
        break;

      case TIO_IOERROR:
        fprintf(stderr, "Error writing to container file - %s\n", argv[0]);
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
        argc--, argv++;

        while (argc >= 1) {
          switch (appendFile(argv[0], ascii, prog, proj, reclen, strict)) {
            case -1:
              fprintf(stderr, "Failed to append %s to tape\n", argv[0]);
              fprintf(stderr, "Container file is probably corrupt\n");
              exit(6);

            case 1:
              fprintf(stderr, "Failed to open file %s - skipping\n", argv[0]);
              /* FALLTHROUGH */

            case 0:
              break;
          }
          argc--, argv++;
        }
        CloseTape();
        break;

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
      switch (OpenTapeForRead(argv[0])) {
        case TIO_SUCCESS:
        case TIO_ERROR:
          printf("%s:\n\n", argv[0]);
          extractFiles(ascii, extra);
          printf("\n");
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

  return 0;
}
