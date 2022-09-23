/* subroutines to do rsts disk (logical block) I/O */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "flx.h"
#include "diskio.h"
#include "absio.h"

#define	DEFDEVICE	"rsts.dsk"

#define NOLAST		0
#define	LASTREAD	1
#define LASTWRITE	2

#ifndef S_ISCHR
#define S_ISCHR(x)      (((x) & S_IFMT) == S_IFCHR)
#endif

typedef struct {
	const char	*name;	/* Name of the disk */
	long		tsize;	/* Total container size */
	long		rsize;	/* Size RSTS uses */
	int		dec166;	/* TRUE if disk has factor bad block table */
} diskent;

int 		dcs;		/* device clustersize */
long		diskblocks;	/* device block count */
const char	*rname;		/* file name of disk/container */
const char	*rssize;	/*  and associated size */
long		rsize;		/* explicitly supplied size (for real disks) */
int		absflag;	/* doing absolute I/O to real disk if non-zero */
FILE		*rstsfile;    	/* rsts disk or container file */

int		lastio;		/* what type of I/O, if any, was last done */
long		nextblk;	/* next block after end of last I/O */

const diskent	sizetbl[] = {
	{ "rx50", 800, 800, FALSE },
	{ "rf11", 1024, 1024, FALSE },
	{ "rs03", 1024, 1024, FALSE },
	{ "rs04", 2048, 2048, FALSE },
	{ "rk05", 4800, 4800, FALSE },
	{ "rl01", 10240, 10220, TRUE },
	{ "rl02", 20480, 20460, TRUE },
	{ "rk06", 27126, 27104, TRUE },
	{ "rk07", 53790, 53768, TRUE },
	{ "rp04", 171798, 171796, FALSE },
	{ "rp05", 171798, 171796, FALSE },
	{ "rp06", 340670, 340664, FALSE },
	{ "rp07", 1008000, 1007950, TRUE },
	{ "rm02", 131680, 131648, TRUE },
	{ "rm03", 131680, 131648, TRUE },
	{ "rm05", 500384, 500352, TRUE },
	{ "rm80", 251328, 242575, TRUE },
	{ NULL, 0, 0, 0 }};

/* getsize takes as input a container file size specifier, which is either
 * a decimal string or a disk type name, and returns the corresponding
 * size.  In the case of disk names, it returns both the size as RSTS
 * knows it, and the full hardware-defined size.  In addition, it returns
 * a flag indicating whether that type has a bad block table.
 * In the case of a numeric size, it looks for a match in the table
 * (against either RSTS size or "full" size) and returns what the
 * table entry specifies.  If no match is found, it returns the
 * specified value for both and the dec166 flag is false.
 * If the size specifier is invalid, the RSTS size is returned as zero
 * and the other two return values are undefined.
 */

void getsize (const char *name, long *tsize, long *rsize, int *dec166)
{
	const diskent	*d;
	char		*n, *n2, *pp;

	if (isdigit (*name))				/* numeric size given */
	{
		*rsize = strtoul (name, &pp, 10);	/* scan size */
		if (*pp != '\0') *rsize = 0;
		*tsize = *rsize;
		*dec166 = FALSE;
		for (d = sizetbl; d->name != NULL; d++)
			if (d->rsize == *rsize || d->tsize == *rsize)
			{
				*tsize = d->tsize;
				*rsize = d->rsize;
				*dec166 = d->dec166;
				break;
			}
	}
	else
	{
		if ((n = (char *) malloc (strlen (name) + 1)) == NULL) rabort(NOMEM);
		strcpy (n, name);
		for (n2 = n; *n2; n2++) *n2 = tolower (*n2);
		for (d = sizetbl; d->name != NULL; d++)
		    if (strcmp (d->name, n) == 0) break;
		free (n);
		*tsize = d->tsize;
		*rsize = d->rsize;
		*dec166 = d->dec166;
	}
}

typedef struct {
    char        Signature[4];           /* must be 'simh' */
    char        CreatingSimulator[64];  /* name of simulator */
    char        DriveType[16];
    lword32     SectorSize;             /* Stored in Network Byte Order */
    lword32     SectorCount;            /* Stored in Network Byte Order */
    char        Reserved[420];          /* Currently unimportant */
} simh_footer;

long byteswap32 (lword32 value)
{
unsigned char *b = (unsigned char *)&value;

return (long)((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]);
}

/* adjsize takes as input a container file size.  it returns the 
 * corresponding RSTS size, based on a match against a table of
 * known disk sizes.  if it finds a match, it returns the RSTS
 * size given in the table; otherwise it returns the value that
 * was passed.  This process adjust for simh disk container 
 * metadata if present at the end of the container file.
 */

long adjsize (long tsize)
{
	const diskent	*d;

	if (rstsfile != NULL)
	{
		struct stat	sbuf;

		if ((fstat (fileno(rstsfile), &sbuf) == 0) &&
		    ((sbuf.st_size % BLKSIZE) == 0)) /* get info about disk/file */
		{
			simh_footer f;

			if ((fseek (rstsfile, sbuf.st_size - BLKSIZE, SEEK_SET) == 0) &&
				(fread (&f, sizeof (f), 1, rstsfile) == 1) &&
				(memcmp (f.Signature, "simh", 4) == 0) && 
				((byteswap32(f.SectorSize) * byteswap32(f.SectorCount)) == (long)(sbuf.st_size - BLKSIZE)))
				return (long)((byteswap32(f.SectorSize) * byteswap32(f.SectorCount)) / BLKSIZE);
		}
	}
	for (d = sizetbl; d->name != NULL; d++)
		if (d->tsize == tsize)
			return d->rsize;
	return tsize;
}

void setrname ()
{
	long	t1;
	int	t2;

	if ((rname = sw.rstsdevice) == NULL)
	    if ((rname = defdevice) == NULL)
		if ((rname = getenv ("RSTSDISK")) == NULL) 
		    rname = DEFDEVICE;
	if ((rssize = sw.disksize) == NULL)
	    if ((rssize = defsize) == NULL)
		if ((rssize = getenv ("RSTSDISKSIZE")) == NULL) 
		    rssize = NULL;
	rsize = 0;
	absflag = FALSE;
	if (rssize != NULL) {
		getsize (rssize, &t1, &rsize, &t2);	/* scan size */
		if (rsize == 0) {
			printf ("Invalid device size %s\n", rssize);
			return;				/*  and quit */
		}
	}
	absname (rname);			/* see if name is special */
	if (absflag && rsize == 0) {
		printf ("Disk size must be specified explicitly for disk %s\n",
			rname);
		return;
	}
}

void ropen (const char *mode)
{
	long		d;
	struct stat	sbuf;

	fiblk = -1;			/* indicate no valid FIBUF */
	lastio = NOLAST;		/*  and no previous I/O */
	womsat = FALSE;			/* SATT is clean */
	setrname ();
	if (absflag) {			/* open real disk */
		if (absopen (rname, mode)) {
			printf ("Error opening RSTS device %s\n", rname);
			perror (progname); /* report any details */
			rabort (NOMSG);
		}
		diskblocks = rsize;	/* set block count */
	} else {			/* not absolute disk */
		if ((rstsfile = fopen (rname, mode)) == NULL) {
			printf ("Error opening RSTS device %s\n", rname);
			perror (progname); /* report any details */
			rabort (NOMSG);
		}
		if (fstat (fileno(rstsfile), &sbuf)) { /* get info about disk/file */
			printf ("Can't stat RSTS device %s", rname);
			perror (progname);
			fclose (rstsfile); /* close it */
			rabort (NOMSG);
		}
		diskblocks = UP(sbuf.st_size,BLKSIZE) / BLKSIZE;
		if (diskblocks == 0) {
			if (!S_ISCHR(sbuf.st_mode) &&
			    !S_ISBLK(sbuf.st_mode))
			{
				printf ("Null RSTS container file %s\n", rname);
				fclose (rstsfile);
				rabort (NOMSG);
			}
			if (rsize == 0) {
				printf ("Size not specified for RSTS disk %s\n", rname);
				fclose (rstsfile);
				rabort (NOMSG);
			}
			diskblocks = rsize;
		}
	}
	diskblocks = adjsize (diskblocks);	/* adjust for bad block tbl */
	d = (diskblocks - 1) >> 16;	/* high order bits of last LBN */
	dcs = 1;			/* compute DCS */
	while (d) {
		d >>= 1;
		dcs <<= 1;
	}
	if (dcs > 64) rabort(BADDCS);	/* Sorry, disk too big! */
}

void rseek (long block)
{
	if (block >= diskblocks) rabort(BADBLK);
	if (absflag)	absseek (block);
	else		fseek (rstsfile, block * BLKSIZE, SEEK_SET);
	if (sw.debug != NULL)
		printf ("seek to: %ld\n", block);
}

void rread (long block, long size, void *buffer)
{
	long	iosize;

	if (lastio != LASTREAD || nextblk != block) {
		rseek (block);
		lastio = LASTREAD;
	}
	if (absflag) {
		if (absread (block, size, buffer))
		    iosize = 0;
		else iosize = size;
	}
	else iosize = fread (buffer, 1, size, rstsfile);
	if (sw.debug != NULL)
		printf ("size requested: %ld, read: %ld\n", size, iosize);
	if (iosize != size) rabort(DISKIO);
	nextblk = block + size / BLKSIZE;
}

void rwrite (long block, long size, void *buffer)
{
	long	iosize;

	if (lastio != LASTWRITE || nextblk != block) {
		rseek (block);
		lastio = LASTWRITE;
	}
	if (absflag) {
		if (abswrite (block, size, buffer))
			iosize = 0;
		else iosize = size;
	}
	else iosize = fwrite (buffer, 1, size, rstsfile);
	if (iosize != size) rabort(DISKIO);
	nextblk = block + size / BLKSIZE;
}

void rclose ()
{
	if (absflag)	absclose ();
	else		fclose (rstsfile);
}
