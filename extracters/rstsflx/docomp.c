/* handler for the "compress" command */

#include <stdio.h>
#include <string.h>

#include "flx.h"
#include "fldef.h"
#include "docomp.h"
#include "diskio.h"
#include "fip.h"

void docomp (int argc, char **argv)		/* zero unused clusters */
{
	byte	*s;
	int	bit, szb;
	long	first, size, iosize;
	long	n;

	rmountrw ();			/* mount the disk R/W */
	memset (iobuf, 0, iobufsize);	/* clear out I/O buffer */
	s = sattbufp;
	szb = (diskblocks - dcs) / pcs;	/* satt bits actually used */
	bit = 1;
	n = 0;				/* start looking at PCN = 0 */
	for (;;) {
		for ( ; n < szb; n++) {	/* scan past allocated clusters */
			if (bit > 0x80) {
				bit = 1;
				s++;
			}
			if ((*s & bit) == 0) break;
			bit <<= 1;
		}
		if (n == szb) break;
		first = n;
		for ( ; n < szb; n++) {	/* find length of free area */
			if (bit > 0x80) {
				bit = 1;
				s++;
			}
			if (*s & bit) break;
			bit <<= 1;
		}
		size = (n - first) * pcs;
		first = pcntolbn(first);	/* get starting lbn */
		if (sw.verbose != NULL) {
			printf ("clearing %ld..%ld\015", first, first + size - 1);
			fflush (stdout);
		}
		for (;;) {
			iosize = size * BLKSIZE;
			if (iosize > iobufsize) iosize = iobufsize;
			rwrite (first, iosize, iobuf);
			size -= iobufsize / BLKSIZE;
			if (size <= 0) break;
			first += iobufsize / BLKSIZE;
		}
	}
	if (sw.verbose != NULL) printf ("\n");
	rumountrw ();			/* done with the disk */
}

