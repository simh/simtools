/* handler for the "allocation" command */

#include <stdio.h>
#include <stdlib.h>

#include "flx.h"
#include "fldef.h"
#include "doalloc.h"
#include "fip.h"

void doalloc (int argc, char **argv)		/* show allocated clusters */
{
	byte	*s;
	int	bit, szb;
	long	first, used, unused, biggest, size;
	long	n;

	rmount ();			/* mount the disk R/O */
	findsat ();			/* look up satt.sys */
	s = sattbufp;
	szb = (diskblocks - dcs) / pcs;	/* satt bits actually used */
	bit = 1;
	n = 0;				/* start looking at PCN = 0 */
	used = unused = biggest = 0;	/* nothing used, nor unused */
	if (sw.bswitch == NULL) printf ("\nFree pack cluster number ranges:\n");
	for (;;) {
		for ( ; n < szb; n++) {
			if (bit > 0x80) {
				bit = 1;
				s++;
			}
			if ((*s & bit) == 0) break;
			used++;		/* count allocated clusters */
			bit <<= 1;
		}
		if (n == szb) break;
		first = n;
		for ( ; n < szb; n++) {
			if (bit > 0x80) {
				bit = 1;
				s++;
			}
			if (*s & bit) break;
			unused++;	/* count free clusters */
			bit <<= 1;
		}
		size = (n - first) * pcs;
		if (size > biggest) biggest = size;
		if (sw.bswitch == NULL) {
			printf ("%6ld - %-6ld (%ld blocks)\n", first, n - 1, size);
		}
	}
	printf ("Blocks used: %ld, free: %ld, max contiguous free: %ld\n",
		used * pcs, unused * pcs, biggest);
	free (sattbufp);		/* release the satt memory copy */
	rumount ();			/* done with the disk */
}

