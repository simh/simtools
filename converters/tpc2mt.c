/* This program converts a TPC simulated magtape to simh format

   Copyright (c) 2015, Robert M. Supnik, Mark Pizzolato

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.
*/

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define FLPSIZ 65536
int main (int argc, char *argv[])
{
int i, k, fc, wc, rc;
unsigned char bc[4] = { 0 };
unsigned char buf[FLPSIZ];
char *progname = argv[0];
char *ppos, oname[256];
FILE *ifile, *ofile;

if (argc < 2) {
    fprintf (stderr, "Usage:  %s file {file2 ...}\n", progname);
    fprintf (stderr, "This will create a simh tap file named file.tap from the input tpc image file\n");
    exit (0);
    }

for (i = 1; i < argc; i++) {
	strcpy (oname, argv[i]);
        if (ppos = strrchr (oname, '.')) strcpy (ppos, ".tap");
                else strcat (oname, ".tap");
	ifile = fopen (argv[i], "rb");
	if (ifile == NULL) {
		printf ("Error opening file: %s\n", argv[i]);
		exit (0);  }
	ofile = fopen (oname, "wb");
	if (ofile == NULL) {
		printf ("Error opening file: %s\n", oname);
		exit (0);  }

	printf ("Processing file %s\n", argv[i]);
	fc = 1; rc = 0;
	for (;;) {
		k = fread (bc, sizeof (char), 2, ifile);
		if (k == 0) break;
		wc = ((unsigned int) bc[1] << 8) | (unsigned int) bc[0];
		wc = (wc + 1) & ~1;
		fwrite (bc, sizeof (char), 4, ofile);
		if (wc) {
			k = fread (buf, sizeof (char), ((unsigned int) bc[1] << 8) | (unsigned int) bc[0], ifile);
			for ( ; k < wc; k++) buf[k] =0;
			fwrite (buf, sizeof (char), wc, ofile);
			k = fwrite (bc, sizeof (char), 4, ofile);
			rc++;  }
		else {	if (rc) printf ("End of file %d, record count = %d\n", fc, rc);
			else printf ("End of tape\n");
			fc++;
			rc = 0;  }
		}
	fclose (ifile);
	fclose (ofile);
	}

return 0;
}
