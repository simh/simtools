/* tap.h: simh tape representation definitions

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

/*
 * Metadata markers
 */
#define ST_EOM          0xFFFFFFFF      /* end of medium */
#define ST_GAP          0xFFFFFFFE      /* erase gap */
#define ST_TM           0x00000000      /* tape mark */

/*
 * Record length field layout
 */
#define ST_ERROR        0x80000000      /* record contains an error */
#define ST_MBZ          0x7F000000      /* must be zero */
#define ST_LENGTH       0x00FFFFFF      /* record length */

/*
 * Data in the .tap container file is rounded up to an even number of bytes
 */
#define RECLEN(c)       (((c) + 1) & ~1)

/*
 * The maximum record length supported by this code is 64K.
 */
#define MAXRCLNT        65536
