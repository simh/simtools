/* subroutines to do rsts file (virtual block) I/O */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "flx.h"
#include "fldef.h"
#include "fileio.h"
#include "filename.h"
#include "diskio.h"
#include "fip.h"

long	totalbytes;		/* total bytes transferred for get/put */
long	curvbn;		       	/* current file vbn (0-based) */
word	curre;			/* link of current RE */
word	lastre;			/*  and last one seen */
int	curreoff;		/* offset into current RE */
int	cluoff;			/* offset into current file cluster */
int	dcnperfcs;		/* fcs / dcs */


static int rmseof (firqb *f, char *recp, long iocount)
{
	long	curblk;
	int	curbyt;

	curblk = curvbn + (recp - iobuf - iocount) / BLKSIZE;
        curbyt = (recp - iobuf) % BLKSIZE;
	if (curblk > f->eofblk) return (TRUE);
	return (curblk == f->eofblk && curbyt >= f->eofbyte);
}
 
/* in each of the following "get a record" routines (one for each record
 * format defined for RMS-11) the arguments are:
 *	f	pointer to firqb struct
 *	len	pointer to record length return variable
 *	eor	pointer to end of record return variable (flag)
 *	iocount	amount of data in I/O buffer (from last seqio call)
 *
 * len is set to the length of the record or partial record returned.
 * the pointer to the record is the return value of the function.
 * eor is set true if a whole record was transferred, and false if only
 * a partial record is returned.  (If eor is set, then a line delimiter
 * should be added by the caller if it is not contained in the record
 * itself.)
 * If nothing was transferred (i.e., the caller should retry) then
 * these functions return NULL.  Otherwise they return a record pointer.
 * Note that in the latter case the length may be zero, which means
 * the record was an empty line.
 */

static char *getfix (firqb *f, long *len, int *eor, long iocount)
{
	long	left;			/* amount left in I/O buffer */
	char	*recp;

	recp = f->currec;
	left = &iobuf[iocount] - recp;	/* compute what's left */
	*eor = FALSE;			/* default to not EOR */
	if (left <= 0 || rmseof (f, recp, iocount)) {
		*len = 0;
		f->currec = NULL;
		return (NULL);
	}
	if (left < f->recsiz) {
		*len = left;
		f->currec = NULL;
		return (recp);
	}
	*len = f->recsiz;
	f->currec += f->recsiz;
	*eor = TRUE;
	return (recp);
}

static char *getvar (firqb *f, long *len, int *eor, long iocount)
{
	char	*recp;
	word16	reclen;
	word16	*reclenp;
	long	left;

	recp = f->currec;
	*eor = FALSE;			/* default to not EOR */
	left = (&iobuf[iocount] - recp);
	if (left <= 0 || rmseof (f, recp, iocount)) {
		*len = 0;
		f->currec = NULL;
		f->currecsiz = 0;
		return (NULL);
        }
	if ((reclen = f->currecsiz) == 0) {
		reclenp = (word16 *)recp;
		reclen = *reclenp;
		if (reclen == 0xffff) {		/* end of data in block */
			left &= -BLKSIZE;	/* get what's left in remaining blocks */
			if (left == 0) {
				*len = 0;
				f->currec = NULL;
				f->currecsiz = 0;
				return (NULL);
			} else {
				recp = &iobuf[iocount - left];
				reclenp = (word16 *) recp;
				reclen = *reclenp;
			}
		}
		left -= 2;			/* discount length field */
		recp += 2;			/*  and skip it */
	}
	if (reclen > left) {
		*len = left;
		f->currecsiz = reclen - left;	/* do this next time */
		f->currec = NULL;
		return (recp);
	}
	f->currec = recp + UP(reclen,2); /* point to where next count is */
	f->currecsiz = 0;		/* no partial record left to do */
	*len = reclen;
	*eor = TRUE;
	return (recp);
}

static char *getvfc (firqb *f, long *len, int *eor, long iocount)
{
	char	*recp;
	word16	reclen;
	word16	*reclenp;
	long	left, skip;

	recp = f->currec;
	*eor = FALSE;			/* default to not EOR */
	left = (&iobuf[iocount] - recp);
	if ((reclen = f->currecsiz) != 0) skip = f->recskip;
	else {
		reclenp = (word16 *)recp;
		reclen = *reclenp;
		if (reclen == 0xffff) {		/* end of data in block */
			left &= -BLKSIZE;	/* get what's left in remaining blocks */
			if (left == 0) {
				*len = 0;
				f->currec = NULL;
				f->currecsiz = 0;
				return (NULL);
			} else {
				recp = &iobuf[iocount - left];
				reclenp = (word16 *) recp;
				reclen = *reclenp;
			}
		}
		left -= 2;			/* discount length field */
		recp += 2;			/*  and skip it */
		skip = f->rechdrsiz;		/* skip over whole header */
	}
	if (reclen > left) {
		f->currecsiz = reclen - left;	/* do this next time */
		if (left > skip) {
			*len = left - skip;
			f->recskip = 0;		/* nothing to skip */
			f->currec = NULL;
			return (recp + skip);
		} else {
			*len = 0;
			f->recskip = skip - left;
			return (NULL);
		}
	}
	f->currec = recp + UP(reclen,2); /* point to where next count is */
	f->currecsiz = 0;		/* no partial record left to do */
	*len = reclen - skip;
	*eor = TRUE;
	return (recp + skip);
}

static char *getstm (firqb *f, long *len, int *eor, long iocount)
{
	char	*lfpos;
	char	*recp;

	*eor = FALSE;				/* default to not EOR */
	while (f->currec < &iobuf[iocount])
		if (*(f->currec)) break; 
		else f->currec++;		/* skip nulls */
	if (f->currec == &iobuf[iocount]) {
		*len = 0;
		f->currec = NULL;
		return (NULL);
	}
	lfpos = (char *) memchr (f->currec, 012, &iobuf[iocount] - f->currec);
	recp = f->currec;
	if (lfpos == NULL || lfpos == f->currec) {
		*len = &iobuf[iocount] - f->currec;
		*eor = FALSE;
		f->currec = NULL;
	} else {
		if (*(lfpos - 1) == '\015') *len = lfpos - f->currec - 1;
		else *len = lfpos - f->currec;
		*eor = TRUE;
		f->currec = lfpos + 1;
	}
	return (recp);
}

/* note that a partial record may be returned; if so, eor is set to false */

static char *getrec (firqb *f, long *len, int *eor, long iocount)	/* get next text record */
{
	switch (f->recfmt & fa_rfm) {
	    case rf_udf:
	    case rf_stm:
		return (getstm (f, len, eor, iocount));
	    case rf_fix:
		return (getfix (f, len, eor, iocount));
	    case rf_var:
		return (getvar (f, len, eor, iocount));
	    case rf_vfc:
		return (getvfc (f, len, eor, iocount));
	}
	rabort (INTERNAL);		/* should never get here */
        return (NULL);			/* to make the compiler happy */
}

/* Routine to do sequential I/O, either read or write according to the
 * third argument passed.
 */
long seqio (firqb *f, long iolen, iohandler io, void *buffer)
{
	long	startlbn;		/* lbn at which to start transfer */
	long	count;			/*  and byte count */
	word	prevdcn = 0;		/* previous RE entry */

	if (curvbn >= f->size) return (0);	/* nothing left */
	for (count = 0; ; ) {
		if (cluoff == 0) {
			if (curreoff == 0 && (f->stat & us_ufd) == 0) {
				if (!readlk (curre)) rabort(BADRE);
				lastre = curre;
			}
			if (count != 0) {
				if (f->stat & us_ufd) {
					if (clumap->uent[curreoff] - prevdcn != dcnperfcs)
						break;
				} else {
					if (use(ufdre,k)->uent[curreoff] - prevdcn != dcnperfcs)
						break;
				}
			}
			if (sw.debug != 0) 
				printf ("seqio() RE entry %o\n", use(ufdre,k)->uent[curreoff]);
		}
		if (count++ == 0) {
			if (f->stat & us_ufd) 
				startlbn = dcntolbn(clumap->uent[curreoff])
					   + cluoff;
			else	startlbn = dcntolbn(use(ufdre,k)->uent[curreoff]) 
					   + cluoff;
			if (sw.debug != NULL) 
				printf ("seqio() start lbn %lo\n", startlbn);
		}
		/* contiguous file is treated as having one giant cluster... */
		if (++cluoff == f->clusiz && ((f->stat & us_nox) == 0)) {
			cluoff = 0;
			prevdcn = use(ufdre,k)->uent[curreoff];
			curreoff++;		/* on to the next RE entry */
			if (curreoff == 7) {	/* time to read anothe RE */
				if (f->stat & us_ufd) curre = 0;
				else	curre = use(ufdre,k)->ulnk;
				curreoff = 0;
			}
		}
		if (count == iolen / BLKSIZE || curvbn + count == f->size) break;
	}
	(*io) (startlbn, count * BLKSIZE, buffer);
	curvbn += count;		/* account for what we transferred */
	return (count * BLKSIZE);	/*  and return the byte count */
}

void openfile (firqb *f)		/* set up file I/O at VBN 0 */
{
	curvbn = 0;			/* currently at first block */
	lastre = 0;			/* working on first RE */
	curre = f->rlink;		/* set current RE link */
	curreoff = 0;			/* working on RE entry 0 */
	cluoff = 0;			/* and start of that cluster */
/* note: on big disks (dcs > 16) dcnperfcs will end up 0 for directories.
 * that's fine; the result is that no transfer that crosses retrieval
 * entries will be done in a single I/O -- exactly what we want.
 */
	dcnperfcs = f->clusiz / dcs;	/* how many dcn's in file cluster */
	if (f->size)			/* read first RE if non-null */
		if ((f->stat & us_ufd) == 0 && !readlk (f->rlink))
			rabort(BADRE);
}

word	*relist = NULL;			/* list of pointers to RE's */

void initrandom (firqb *f)
{
	long	recount;

	if (relist != NULL) free (relist);
	if (f->stat & us_ufd) return;	/* UFDs are easy */
	openfile (f);			/* first do common setup */
	/* Note: do not use UP() here since we don't round to
	 * a power of 2!
	 */
	recount = (f->size + 7 * f->clusiz - 1) / (7 * f->clusiz);
	if ((relist = (word *) malloc (recount * sizeof (word))) == NULL)
		rabort(NOMEM);
	memset (relist, 0, recount * sizeof (word));
	relist[0] = f->rlink;
}

void fileseek (firqb *f, long vbn)	/* seek to (0-based) vbn */
{
	int	clu, re, renum;
	word	prevre;

	if (vbn >= f->size) rabort(INTERNAL);
	clu = vbn / f->clusiz;		/* get cluster number */
	if ((f->stat & us_ufd) == 0) {	/* more work for non-UFDs */
		renum = clu / 7;	/* get RE number */
		if (relist[renum] == 0) { /* load RE list if we haven't been here */
			for (re = 0; re <= renum; re++) {
				if (relist[re]) prevre = relist[re];
				else {
					if (!readlk (prevre)) rabort(CORRUPT);
					relist[re] = prevre = use(ufdre,k)->ulnk;
				}
			}
		}
		readlk (curre = relist[renum]);	/* read appropriate RE */
	}
	curreoff = clu % 7;		/* set index into RE */
	cluoff = vbn % f->clusiz;	/*  and offset into cluster */
	curvbn = vbn;			/*   and finally, current vbn */
}

/* get a RSTS file and copy it to a specified local file.  Transfers in
 * binary (block) mode or ascii (record) mode according to the third
 * argument.  The return value is the count of bytes transferred.
 */

long getfile (FILE *to, firqb *f, int binary)
{
	long	reclen, iocount;
        int	eor;
	char	*recp;

	if (f->size == 0) return (0);	/* null file, nothing transferred */
	openfile (f);			/* set up file transfer */
	totalbytes = 0;
	if (binary) {
		while ((iocount = seqio (f, iobufsize, rread, iobuf)) != 0) {
			fwrite (iobuf, 1, iocount, to);
			totalbytes += iocount;
		}
	} else {
		iocount = seqio (f, iobufsize, rread, iobuf);	/* do initial buffer fill */
		f->currec = iobuf;		/* init current record pointer */
		f->currecsiz = 0;		/* no current record size */
		while (TRUE) {
			recp = getrec (f, &reclen, &eor, iocount);
			if (recp != NULL) {
				totalbytes += reclen;
				if (reclen) fwrite (recp, 1, reclen, to);
				if (eor) {
					fputc ('\n', to);
#if (defined(__MSDOS__) && !defined(__unix__))
					totalbytes += 2;
#else
					totalbytes++;
#endif
				}
			}
			if (f->currec == NULL) {
				if ((iocount = seqio (f, iobufsize, rread, iobuf)) == 0)
					break;
				else	f->currec = iobuf;	/* init current record pointer */
			}
		}
	}
	return (totalbytes);
}

/* extend the currently open file to the specified size, if not already
 * that big.
 */

long extfile (firqb *f, long blocks)
{
	int	clus, reoff, clunum;
	word	re;

	if (sw.debug != NULL) 
		printf ("extfile(,%ld) from %ld\n", blocks, f->size);
	if (f->size >= blocks) return (TRUE);	/* already big enough */
	if (f->stat & us_nox) return (FALSE);	/* error if contiguous */
	clus = (UP(blocks,f->clusiz) - UP(f->size,f->clusiz)) / f->clusiz;
	if (clus == 0) {			/* no new clusters needed */
		f->size = blocks;		/* so just do it */
		return (TRUE);
	}
	if (cluoff == 0) reoff = curreoff;
	else {
		reoff = curreoff + 1;
		if (reoff > 6) reoff = 0;	/* overflowed current RE */
	}
	f->size = UP(f->size,f->clusiz);	/* round up to full cluster */
	while (clus > 0) {			/* allocate what we need */
		if (reoff == 0) {		/* need to start new RE */
			if ((re = getent ()) == 0) return (FALSE);
			if (NULLINK(curre)) curre = re;	/* this is now current */
			readlk (re);		/* read it */
			use(ufdre,k)->ulnk = 1;	/* mark it allocated */
			MARKF;
			if (lastre) {		/* link it to earlier RE */
				readlk (lastre);
				use(ufdre,k)->ulnk = re;
			} else {		/* link first RE to NE */
				readlk (f->nlink);
				use(ufdne,k)->uar = re;
				f->rlink = re;	/* record in firqb also */
			}
			MARKF;
			lastre = re;		/* this is now the last RE */
			readlk (re);		/* read the new RE */
		}
		if ((clunum = getclu (f->clusiz, f->clusiz)) == 0) return (FALSE);
		use(ufdre,k)->uent[reoff] = clunum;
		MARKF;
		f->size += f->clusiz;		/* adjust file size */
		reoff++;
		if (reoff > 6) reoff = 0;	/* overflowed current RE */
		clus--;				/* any clusters left to do? */
	}
	f->size = blocks;			/* it worked, all done */
	readlk (curre);				/* make sure we have the right RE */
	return (TRUE);				/* return with success */
}

/* read a local file and put it to a specified RSTS file.  Transfers in
 * binary (block) mode or ascii (record) mode according to the third
 * argument.  The return value is the count of bytes transferred if the
 * copy completed, or the negative of what was transferred if the transfer
 * did not finish (i.e., due to no room on the disk).
 */

/* writefile is a support routine that writes some number of bytes from iobuf
 * to the rsts output file.  It returns FALSE if the write did not finish, e.g.,
 * due to lack of disk space.
 */

long writefile (firqb *f, long count)
{
	long	offset, blocks, wcount;

	if (sw.debug != NULL) printf ("writefile(,%ld)\n", count);
	count = UP(count,BLKSIZE);
	offset = 0;
	blocks = (totalbytes + count) / BLKSIZE;
	extfile (f, blocks);			/* extend if needed */
	while (count) {
		wcount = seqio (f, count, rwrite, iobuf + offset);
		if (wcount == 0) {
			totalbytes = -totalbytes;
			return (FALSE);
		}
		offset += wcount;
		totalbytes += wcount;
		count -= wcount;
	}
	return (TRUE);				/* write suceeded */
}

long putfile (FILE *from, firqb *f, int binary)
{
	long	blocks, wcount, iocount, iocount2, adjust;
	long	oldsize, offset;
	long	ilen, llen;
	char	*iptr, *eol;
	ufdne	*n;
	ufdae	*a;

	openfile (f);			/* set up file transfer */
	totalbytes = 0;
	if (binary) {
		while ((iocount = fread (iobuf, 1, iobufsize, from)) != 0) {
			iocount2 = UP(iocount, BLKSIZE);
			adjust = iocount2 - iocount;
			if (adjust) memset (iobuf + iocount, 0, adjust);
			if (!writefile (f, iocount2)) break;
			if (adjust) {
				totalbytes -= adjust;
				break;	/* short read, done with file */
			}
		}
	} else {
		iptr = iobuf;		/* start getting lines at the start */
		ilen = iobufsize + 2;	/* use all including 2 extra bytes */
		for ( ; ; ) {
			if (fgets (iptr, ilen, from) == NULL) break;
			llen = strlen (iptr);	/* get length of this line */
			eol = iptr + llen;		/* point to terminator */
			if (*--eol == '\n') {	/* if we got a complete line */
				if (*(eol - 1) == '\r') {	/* terminator = cr-lf? */
					eol++;		/* next line starts here */
				} else {			/* only \n at eol */
					*eol++ = '\r';	/* put in the cr */
					*eol++ = '\n';	/* and overwrite null with lf */
					llen ++;	/* account for cr */
				}
			} else eol++;			/* skip to end of data */
			iptr = eol;			/* advance read ptr */
			ilen -= llen;			/* compute space left */
			if (ilen <= 2) {		/* full buffer */
				if (!writefile (f, iobufsize)) {
					ilen = 0;	/* don't write more */
					break;		/* quit this file */
				}
				iptr = iobuf;		/* reinit pointer */
				ilen = 2 - ilen;	/* # bytes to move */
				eol = iobuf + iobufsize; /* move from here */
				for ( ; ilen > 0; ilen--) *iptr++ = *eol++;
				ilen = iobufsize + 2 - (iptr - iobuf);
			}
		}
		ilen = iptr - iobuf;			/* get amount buffered */
		adjust = UP(ilen,BLKSIZE) - ilen;	/* get amount to pad */
		if (adjust) memset (iptr, 0, adjust);	/* do it */
		iptr += adjust;				/* point past it */
		if (iptr != iobuf) writefile (f, iptr - iobuf);	/* write pending data */
		totalbytes -= adjust;			/* don't report pad */
	}
	readlk (f->alink);		/* read file's AE */
	a = use(ufdae,k);
	oldsize = a->usiz;		/* get old file size */
	if (a->urts[0] == 0) oldsize += a->urts[1] >> 16;
	if (f->size != oldsize ) {	/* size changed */
		if (f->size < oldsize) rabort(INTERNAL);	/* can't shrink */
		updqb (f, UP(f->size,f->clusiz) - UP(oldsize,f->clusiz));
		a->usiz = f->size & 0xffff;
		MARKF;
		if (f->size >> 16) {	/* large file */
			a->urts[1] = f->size >> 16;
			if ((oldsize >> 16) == 0) {	/* it was small */
				if (a->urts[0]) {
					printf ("Warning - RTS name cleared for large file ");
					printcurname (f);
					printf ("\n");
					a->urts[0] = 0;
				}
				readlk (f->nlink);
				n = use(ufdne,k);
				if (n->uprot & up_run) {
					n->uprot &= ~up_run;
					MARKF;
					printf ("Warning - runnable protection bit cleared for large file ");
					printcurname (f);
					printf ("\n");
				}
			}
		}
	}			
	return (totalbytes);
}

/* return TRUE if the filename has an extension that suggests it's a text
 * file, and FALSE otherwise.
 */

const char	textlist[][4] = 
	{ "txt", "lst", "map", "sid", "log", "lis",
	  "rno", "doc", "mem", "bas", "b2s", "mac",
	  "for", "ftn", "fth", "cbl", "dbl", "com",
	  "cmd", "bat", "tec", "ctl", "odl", "ps ",
	  "c  ", "h  ", "ps",  "c",   "h",   "src",
	  "alg", "" };

int textfile (char *name)
{
	char	*p;
	int	i;

	p = strchr (name, '.');
	if (p == NULL) return (FALSE);	/* no extension */
	p++;				/* skip past the dot */
	for (i = 0; textlist[i][0] != '\0'; i++)
		if (strcmp (textlist[i], p) == 0) return (TRUE);
	return (FALSE);
}
