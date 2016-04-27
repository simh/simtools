/* absolute I/O support for Unix
 * 
 * Paul Koning	95.01.05	created
 *
 * Note that this particular version of absio is mostly a dummy, since
 * there is very little special that has to be done on Unix for absolute
 * I/O.  Among other things, this file should serve as a template for
 * those who want to create the equivalent for another operating system.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "flx.h"
#include "absio.h"

/* this routine is called to scan a supplied container file/device
 * name.  If the name refers to a real disk, then absflag is set
 * and rsize is set to the device size.  Otherwise, no action is taken.
 *
 * More precisely, there are three things this routine may want to do,
 * depending on the OS and the name supplied.
 *
 * If, for the specified name, the absolute I/O routines in this module
 * need to be called, then absflag should be set to TRUE, and rsize must
 * be set to the size of the device.  Otherwise, absflag should be left
 * alone (which leaves it FALSE); that way the other absolute I/O routines
 * in this module will not be called and standard C I/O is used instead.
 *
 * If, for the specified name, "fstat" will not return a size (i.e., returns
 * zero) then rsize must specify the correct size.  This can be set in this
 * module, based on OS specific operations, or it can simply be left
 * unchanged, in which case it has to be supplied by the user.
 *
 * For example, in Unix the fstat function doesn't return a size for raw
 * devices (it returns zero) but the stdio routines are otherwise perfectly
 * suitable.  So this routine does NOT set absflag since no special abs I/O
 * routines are needed.  It does check the return from stat(); if this
 * indicates a raw device, it checks that rsize has been supplied and
 * complains if not.  Note that the code below does not attempt to obtain
 * the device size, since there is no portable Unix way of doing this that
 * I know of.
 */

void absname (const char *rname)
{
	struct stat	sbuf;

	if (stat (rname, &sbuf)) return;	/* try to get info about device */
	diskblocks = UP(sbuf.st_size,BLKSIZE) / BLKSIZE;
	if (diskblocks == 0) {
		if (S_ISCHR(sbuf.st_mode)) {
			diskblocks = rsize;
			if (rsize == 0) {
				printf ("Size not specified for RSTS disk %s\n", rname);
				exit (EXIT_FAILURE);
			}
		}
	}
}

/* This routine is called to do any device open actions that may be needed.
 * The "mode" argument is a standard C open mode.  The return value is zero
 * for success, non-zero for error.
 */
 
int absopen (const char *rname, const char *mode)
{
	rabort(INTERNAL);		/* should never get here */
}

/* This routine is called to do a seek on a device.  You can do it here
 * or as part of the read/write, whichever is easier.  This routine is
 * called only if the I/O operations are changing between read and write,
 * or are not sequential.  So you would want to do the work here if your
 * OS does implicit sequential I/O, and make this a NOP if the read and
 * write primitives take an explicit block number.
 */
 
void absseek (long block)
{
	rabort(INTERNAL);		/* should never get here */
}

/* This routine is called to do any needed "close" actions for absolute I/O */

void absclose ()
{
	rabort(INTERNAL);		/* should never get here */
}

/* This routine is called to do an absolute device read.  The block number
 * is supplied, even though it could have been derived from a preceding
 * absseek call.  This is because absseek is not called when sequential
 * reads are done.
 */
 
long absread (long sec, long count, void *buffer)
{
	return (0);			/* should never get here */
}

/* This routine is called to do an absolute device write. */

long abswrite (long sec, long count, void *buffer)
{
	return (0);			/* should never get here */
}
