/* absread and abswrite services for use with Borland C implementation 
 * 
 * Paul Koning	95.01.17	Dummy module (no absio in TC++ for Windows)
 */

#include <string.h>
#include <stdio.h>

#include "flx.h"
#include "absio.h"

/* this routine is called to scan a supplied container file/device
 * name.  If the name refers to a real disk, then absflag is set
 * and rsize is set to the device size.  Otherwise, no action is taken.
 */

void absname (const char *rname)
{
	if (rname[strlen(rname) - 1] == ':') {	/* device name alone */
		printf ("Absolute I/O not supported\n");
	}
}

int absopen (const char *rname, const char *mode)
{
	return (1);			/* should never get here... */
}

void absseek (long block) { }		/* nothing to do */

void absclose () { }			/* nothing to do */

long absread (long sec, long count, void *buffer)
{
	return (0);			/* should never get here... */
}

long abswrite (long sec, long count, void *buffer)
{
	return (0);			/* should never get here... */
}

