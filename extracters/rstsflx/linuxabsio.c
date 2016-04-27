/* absolute I/O support for Linux
 * 
 * Paul Koning	95.01.05	created
 *		99.12.31	updated for Linux (from generic unxabsio.c)
 *		00.01.03	added size determination and geometry test
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <asm/ioctl.h>
#include <linux/fs.h>
#include <linux/hdreg.h>

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

static dev_t rdev;
static int floppy;
static byte trackbuf[BLKSIZE * 10];
static int curtrack = -1;
static int writecurrent;

static void makechs_rx50 (int block, int *trk, int *sec)
{
	int	t, s;
	
	t = block / 10;
	s = block % 10 + 1;
	if (s < 6) s = (s - 1) * 2 + 1;
	else	   s = (s - 5) * 2;
	s += t * 2;
	while (s > 10) s -= 10;
	t++;
	if (t == 80) t = 0;		/* wrap around last 10 blocks */
	*trk = t;
	*sec = s - 1;
}

static void flushtrack (void)
{
	if (writecurrent)
	{
		lseek (floppy, curtrack * sizeof (trackbuf), SEEK_SET);
		if (write (floppy, trackbuf, sizeof (trackbuf)) !=
		    sizeof (trackbuf))
			doabort (DISKIO, __FILE__, __LINE__);
		writecurrent = FALSE;
	}
}

static void gettrack (int track)
{
	if (track != curtrack)
	{
		flushtrack ();
		lseek (floppy, track * sizeof (trackbuf), SEEK_SET);
		if (read (floppy, trackbuf, sizeof (trackbuf)) !=
		    sizeof (trackbuf))
			doabort (DISKIO, __FILE__, __LINE__);
		curtrack = track;
	}
}

void absname (const char *rname)
{
	struct stat	sbuf;
	int fd, i = -1;
	long size;
	struct hd_geometry loc;

	if (stat (rname, &sbuf)) return;	/* try to get info about device */
	diskblocks = UP(sbuf.st_size,BLKSIZE) / BLKSIZE;
	if (diskblocks == 0)
	{
		if (S_ISBLK(sbuf.st_mode))
		{
			fd = open (rname, O_RDONLY);
			if (fd >= 0)
			{
				i = ioctl (fd, BLKGETSIZE, &size);
				if (i >= 0)
					diskblocks = size;
				i = ioctl (fd, HDIO_GETGEO, &loc);
				close (fd);
			}
			else
				diskblocks = rsize;
			/* user can always override autoconfigured size */
			if (rsize != 0)
				diskblocks = rsize;
			if (diskblocks == 0)
			{
				printf ("Size not specified for RSTS disk %s\n", rname);
				exit (EXIT_FAILURE);
			}
			if (diskblocks == 800 &&
			    (i < 0 ||
			     (loc.cylinders == 80 &&
			      loc.heads == 1)))
			{
				/* Looks like an RX50 floppy, turn on 
				 * special handling
				 */
				if (sw.debug != NULL)
					printf ("rx50 handling enabled\n");
				absflag = TRUE;
			}
			/* if size wasn't overridden in command line,
			 * use the size we found
			 */
			if (rsize == 0)
				rsize = diskblocks;
		}
		else 
		{
			printf ("RSTS disk container is null or not a block device\n");
			exit (EXIT_FAILURE);
		}
	}
}

/* This routine is called to do any device open actions that may be needed.
 * The "mode" argument is a standard C open mode.  The return value is zero
 * for success, non-zero for error.
 */
 
int absopen (const char *rname, const char *mode)
{
	int flags;
	
	if (strcmp (mode, DREADMODE) == 0)
		flags = O_RDONLY;
	else
		flags = O_RDWR;
	floppy = open (rname, flags);
	return (floppy < 0);
	curtrack = -1;
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
}

/* This routine is called to do any needed "close" actions for absolute I/O */

void absclose ()
{
	flushtrack ();
	close (floppy);
	floppy = -1;
}

/* This routine is called to do an absolute device read.  The block number
 * is supplied, even though it could have been derived from a preceding
 * absseek call.  This is because absseek is not called when sequential
 * reads are done.
 */
 
long absread (long sec, long count, void *buffer)
{
	int track, tsec;
	byte *b = (byte *) buffer;
	
	while (count)
	{
		makechs_rx50 (sec, &track, &tsec);
		gettrack (track);
		memcpy (b, trackbuf + (BLKSIZE * tsec), BLKSIZE);
		b += BLKSIZE;
		count -= BLKSIZE;
		sec++;
	}
	return 0;	/* success */
}

/* This routine is called to do an absolute device write. */

long abswrite (long sec, long count, void *buffer)
{
	int track, tsec;
	byte *b = (byte *) buffer;
	
	while (count)
	{
		makechs_rx50 (sec, &track, &tsec);
		gettrack (track);
		memcpy (trackbuf + (BLKSIZE * tsec), b, BLKSIZE);
		writecurrent = TRUE;
		b += BLKSIZE;
		count -= BLKSIZE;
		sec++;
	}
	return 0;	/* success */
}
