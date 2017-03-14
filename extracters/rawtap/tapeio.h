/* tapeio.h: Tape I/O definitions

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

#include "tap.h"
#include "defs.h"

/*
 * Tape open status return codes
 */
#define TIO_SUCCESS     0               /* operation successful */
#define TIO_ERROR       -1              /* error record seen */
#define TIO_CORRUPT     -2              /* tape format is corrupt */
#define TIO_OPENFAIL    -3              /* open operation failed */
#define TIO_CREATEFAIL  -4              /* create operation failed */
#define TIO_IOERROR     -5              /* I/O error */

/*
 * Tape open/close routines.
 */
extern int OpenTapeForRead(char *);
extern int OpenTapeForWrite(char *);
extern int OpenTapeForAppend(char *);
extern void CloseTape(void);

/*
 * Tape I/O routines.
 */
uint32 ReadTapeRecord(void *, int);
uint32 ReadTapeRecordLength(void);
int WriteTapeRecord(void *, int);
int WriteTapeMark(int);

/*
 * Buffered I/O routines
 */
void initTapeBuffering(int);
int flushTapeBuffering(void);
int writeTapeBuffering(char);
