/* This program assembles 8 PROM files into a 32bit binary file

   Copyright (c) 1993-2001, Robert M. Supnik

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

static const int fnum[8] = { 60, 58, 74, 73, 90, 89, 106, 105 };

int main (int argc, char *argv[])
{
FILE *fi[8], *outf;
char fname[256];
int c, i, j;
unsigned int wd[1024];

for (i = 0; i < 8; i++) {
	sprintf (fname, "C:\\temp\\m8376\\m8376e%03d.bin", fnum[i]);
	fi[i] = fopen (fname, "rb");
	if (fi[i] == NULL) {
	    printf ("Can't open file %s\n", fname);
	    return 0;
	    }
	}

for (i = 0; i < 1024; i++) {
	wd[i] = 0;
	for (j = 7; j >=0; j--) {
	    c = fgetc (fi[j]);
	    if (c == EOF) {
		printf ("Premature end of file on file %d\n", i);
		return 0;
		}
	    wd[i] = (wd[i] << 4) | (c & 0xF);
	    }
	}

for (i = 0; i < 8; i++) fclose (fi[i]);
outf = fopen ("c:\\prom.bin", "wb");
if (outf == NULL) {
	printf ("Can't open output file\n");
	return 0;
	}
fwrite (wd, 1024, sizeof (int), outf);
fclose (outf);
return 0;
}
