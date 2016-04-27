/* handler for the "dump" command */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "flx.h"
#include "fldef.h"
#include "dodump.h"
#include "fip.h"
#include "filename.h"
#include "diskio.h"
#include "fileio.h"
#include "scancmd.h"

long	startblk, endblk;

static byte toprint (byte c)
{
	if (c > 31 && c < 127) return (c);
	if (c > 159 ) return (c);
	return ('.');
}

static void dumpbuf (firqb *f, word16 *buf, long count, long firstbyte)
{
	int	b;
	long	off, block;
	char	r50[4];

	for (off = 0; off < count; off += 16, buf += 8) {
		if ((off & (BLKSIZE - 1)) == 0) {
			block = (firstbyte + off) / BLKSIZE + startblk;
			if (f != NULL) {
				printf ("\nFile: ");
				printcurname (f);
			}	else printf ("\nRSTS disk");
			printf (" block %ld\n", block);
		}
		if (sw.hex != NULL) {
			printf ("%03lx/ ", off & (BLKSIZE - 1));
			for (b = 0; b < 8; b++) printf ("%04x ", buf[b]);
		} else {
			printf ("%03lo/ ", off & (BLKSIZE - 1));
			for (b = 0; b < 8; b++) printf ("%06o ", buf[b]);
		}
		for (b = 0; b < 8; b++) printf ("%c%c", toprint (buf[b] & 0xff), toprint (buf[b] >> 8));
		if (sw.wide != NULL) {
			for (b = 0; b < 8; b++) {
				r50toascii (buf[b], r50, TRUE);
				printf (" %s", r50);
			}
		}
		printf ("\n");
	}
}

static void dumpfile (firqb *f)
{
	long	iocount, totalbytes, end2;

	if (startblk >= f->size) return;	/* nothing to do */
	end2 = endblk;
	if (end2 > f->size) end2 = f->size;	/* get end for this file */
	initrandom (f);				/* set up for random read */
	fileseek (f, startblk);			/*  and seek to start vbn */
	totalbytes = 0;
	while (TRUE) {
		iocount = (end2 - startblk) * BLKSIZE - totalbytes;
		if (iocount > iobufsize) iocount = iobufsize;
		if (iocount <= 0) return;
		iocount = seqio (f, iocount, rread, iobuf);
		if (iocount == 0) return;	/* exit if EOF */
		dumpbuf (f, (word16 *) iobuf, iocount, totalbytes);
		totalbytes += iocount;
	}
} 

static void dumpdisk (void)
{
	long	lbn, iocount, totalbytes;

	if (endblk > diskblocks) endblk = diskblocks;
	totalbytes = 0;
	for (lbn = startblk; lbn < endblk; lbn += iobufsize / BLKSIZE) {
		iocount = (endblk - lbn) * BLKSIZE;
		if (iocount > iobufsize) iocount = iobufsize;
		if (iocount <= 0) return;
		rread (lbn, iocount, iobuf);
		dumpbuf (NULL, (word16 *) iobuf, iocount, totalbytes);
		totalbytes += iocount;
	}
} 

/* "dump" will dump the disk NFS, or a directory (UFD), or a file.  If no
 * name is specified, you get NFS disk.  In that case, there need not be
 * any meaningful file structure on the disk.  If a PPN is specified but no
 * file name, you get the UFD; if a filename is given, you get that file.
 */

void dodump (int argc, char **argv)
{
	char	*p;

	if (sw.start == NULL) startblk = 0;	/* start at the bottom */
	else {
		if (*sw.start == '0') startblk = strtoul (sw.start, &p, 8);
		else startblk = strtoul (sw.start, &p, 10);
		if (*p != '\0') {
			printf ("Invalid start block %s\n", sw.start);
			return;
		}
	}
	if (sw.end == NULL) endblk = 1L << 23;	/* dump everything */
	else {
		if (*sw.end == '0') endblk = strtoul (sw.end, &p, 8);
		else endblk = strtoul (sw.end, &p, 10);
		if (*p != '\0') {
			printf ("Invalid end block %s\n", sw.end);
			return;
		}
	}
	endblk++;				/* make end + 1 */
	if (argc == 0) {
		ropen (DREADMODE);		/* open the disk */
		dumpdisk ();			/* do NFS dump */
		rclose ();			/* now close it */
	} else {
		rmount ();			/* mount R/O */
		dofiles (argc, argv, dumpfile, NULLISNULL);
		rumount ();			/* done with the disk */
	}
}

