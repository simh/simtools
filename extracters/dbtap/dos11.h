/* dos11.h: DOS/BATCH-11 tape file format definitions

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

#include "defs.h"

/*
 * Actual file header as used in DOS/BATCH-11.
 */
struct dos11hdr1 {
  uint16		fname[2];	/* first 6 chars of filename (RAD50) */
  uint16		ext;		/* 3 letter file extension (RAD50) */
  uint8			proj;		/* project # (octal) */
  uint8			prog;		/* programmer # (octal) */
  uint16		prot;		/* protection code (octal) */
  uint16		date;		/* (year-1970) * 1000 + day of year */
};

/*
 * File header as seen on many archive tapes.
 */
struct dos11hdr2 {
  uint16		fname[2];	/* first 6 chars of filename (RAD50) */
  uint16		ext;		/* 3 letter file extension (RAD50) */
  uint8			proj;		/* project # (octal) */
  uint8			prog;		/* programmer # (octal) */
  uint16		prot;		/* protection code (octal) */
  uint16		date;		/* (year-1970) * 1000 + day of year */
  uint16		fname3;		/* optional, letters 7 - 9 of name */
};

/*
 * DOS/BATCH-11 processing functions
 */
int appendFile(char *, char *, uint8, uint8, int, int);
void extractFiles(char *, int);
void listDirectory(void);
