/* This program converts a tar file to a simulated 8192B blocked magtape

   Copyright (c) 2015, Mark Pizzolato

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
   MARK PIZZOLATO BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Mark Pizzolato shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Mark Pizzolato.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

main (int argc, char **argv)
{
FILE *fIn = NULL, *fOut = NULL;
size_t blocksize = 8192, bytes_read;
unsigned char *br = (unsigned char *)&bytes_read;
char *progname = argv[0];
char *buf = NULL;
char *tapname = NULL;

if (argc < 2) {
    fprintf (stderr, "Usage:  %s {-b blocksize} tarfile {tarfile2 ...}\n", progname);
    fprintf (stderr, "This will create a simh tap file named tarfile.tap from the input tar file\n");
    fprintf (stderr, "blocksize defaults to 8192\n");
    exit (0);
    }
if ((argc >= 3) && ((strcmp("-b", argv[1]) == 0) || (strcmp("--blocksize", argv[1]) == 0))) {
    if (atoi (argv[2]) <= 0) {
        fprintf (stderr, "Invalid blocksize: %s\n", argv[2]);
        exit (0);
        }
    blocksize = (size_t)atoi (argv[2]);
    argc -= 2;
    argv += 2;
    }
while (--argc > 0) {
    ++argv;
    tapname = realloc (tapname, 5 + strlen (argv[0]));
    if (NULL == (fIn = fopen (argv[0], "rb"))) {
        fprintf (stderr, "Error Opening tar file '%s': %s\n", argv[0], strerror (errno));
        break;
        }
    strcpy (tapname, argv[0]);
    strcat (tapname, ".tap");
    if (NULL == (fOut = fopen (tapname, "wb"))) {
        fprintf (stderr, "Error Opening tap file '%s': %s\n", tapname, strerror (errno));
        break;
        }
    buf = realloc (buf, blocksize);
    while (1) {
        bytes_read = fread (buf, 1, blocksize, fIn);
        if (bytes_read == 0)
            break;
        fputc (br[0], fOut); fputc (br[1], fOut); fputc (br[2], fOut); fputc (br[3], fOut);
        fwrite (buf, 1, bytes_read, fOut);
        fputc (br[0], fOut); fputc (br[1], fOut); fputc (br[2], fOut); fputc (br[3], fOut);
        }
    fputc (br[0], fOut); fputc (br[1], fOut); fputc (br[2], fOut); fputc (br[3], fOut);
    fputc (br[0], fOut); fputc (br[1], fOut); fputc (br[2], fOut); fputc (br[3], fOut);
    fclose (fIn);
    fIn = NULL;
    fclose (fOut);
    fOut = NULL;
    }
if (fIn)
    fclose (fIn);
if (fOut)
    fclose (fOut);
free (tapname);
free (buf);
exit(0);
}
