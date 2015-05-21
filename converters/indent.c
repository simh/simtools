/* This program converts a SIMH source to 4-tabbing

   Copyright (c) 2005, Robert M. Supnik

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
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#define COMM_POS	56

int main (int argc, char *argv[])
{
int i, j, fill, itc, incomm, in8tab, ip, op;
char *ppos, iline[256], oline[256], oname[256];
FILE *ifile, *ofile;

if ((argc < 2) || (argv[0] == NULL)) {
	printf ("Usage is: asc file [file...]\n");
	exit (0);  }

for (i = 1; i < argc; i++) {
	strcpy (oname, argv[i]);
        if (ppos = strrchr (oname, '.')) strcpy (ppos, ".new");
            else strcat (oname, ".new");
	ifile = fopen (argv[i], "ra");
	if (ifile == NULL) {
	    printf ("Error opening file: %s\n", argv[i]);
	    exit (0);  }
	ofile = fopen (oname, "wa");
	if (ofile == NULL) {
	    printf ("Error opening file: %s\n", oname);
	    exit (0);  }

	printf ("Processing file %s\n", argv[i]);
	for (incomm = in8tab = 0;;) {
	    for (j = 0; j < 256; j++) iline[j] = oline[j] = 0;
	    if (fgets (iline, 256, ifile) == NULL) break;
	    ip = 0;
	    if (!incomm) {
		    if (strncmp (iline, "    ", 4) == 0) in8tab = 1;
		    else if (!isspace (iline[0]) && (iline[0] != '#') && strncmp (iline, "/*", 2)) in8tab = 0;
		    if ((strncmp (iline, "else {\t", 7) == 0) &&
		        !isspace (iline[7])) {
		        fputs ("else {\n", ofile);
		        ip = 6;
		        }
		    else if ((strncmp (iline, "do {\t", 5) == 0) &&
		        !isspace (iline[5])) {
		        fputs ("do {\n", ofile);
		        ip = 4;
		        }
		    }
	    for (itc = op = 0; iline[ip]; ip++) {
		    if (!incomm && (op == 0)) {
		        if (iline[ip] == '\t') {
			        itc++;
			        continue;
			        }
		        else if (itc) {
			        fill = (itc * 8) - (in8tab? 0: 4);
			        while (fill--) oline[op++] = ' ';
			        }
		        }
		    switch (iline[ip]) {
		    case '/':
		        if (!incomm && (iline[ip + 1] == '*')) {
			    incomm = 1;
			    if (ip != 0) {
			        while (op < COMM_POS) oline[op++] = ' ';
			        }
			    }
		        oline[op++] = iline[ip];
		        break;
		    case '*':
		        if (incomm && (iline[ip + 1] == '/')) incomm = 0;
		        oline[op++] = iline[ip];
		        break;
		    case '\t':
		        fill = 8 - (op % 8);
		        while (fill--) oline[op++] = ' ';
		        break;
		    case '\r':
		        break;
		    default:
		        oline[op++] = iline[ip];
		        break;
		        }
		    }
	    if (op) fputs (oline, ofile);
	    }
	fclose (ifile);
	fclose (ofile);
	}
return 0;
}
