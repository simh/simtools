/* handler for the "hook" command */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "flx.h"
#include "fldef.h"
#include "silfmt.h"
#include "dohook.h"
#include "fip.h"
#include "filename.h"
#include "fileio.h"
#include "diskio.h"

typedef struct {			/* boot data pointer in boot block */
	word16	lbn[2];			/* block number, LSW first */
	word16	wcnt;			/* word count */
} bdesc;

typedef struct {			/* header of boot block */
	word16	nop;			/* nop required for boot */
	word16	setup_br;		/* go do setup and relocation code */
	word16	trap4_vec[2];		/* if trap to  4, halt @  6 w/ 10 in lights */
	word16	trap10_vec[2];		/* if trap to 10, halt @ 12 w/ 14 in lights */
	word16	clustr;			/* 14 cluster size */
	word16	csr;			/* 16 csr base */
	word16	devnam;			/* 20 device name */
	word16	jmp;			/* 22 jump to start address */
	word16	transfer;		/* 24 transfer address (set by hook) */
	byte	unit;			/* 26 unit # in bits <2:0> (set by boot) */
	byte	flags;			/* 27 flags (definitions are in inidfn) */
	word16	unit_csr;		/* 30 unit # in bits for csr (set by boot) */
	byte	rc, wc;			/* 32 read and write codes */
	word16	function;		/* 34 function code: read */
	word16	blknum[2];		/* 36-40 block number to read, also spec func. */
	word16	memadr[2];		/* 42-44 memory address to read into */
	word16	wcnt;			/* 46 word count, also spec function parameter */
	word16	reset;			/* 50 do a device reset */
	word16	read;			/* 52 do a single transfer */
	word16	spec;			/* 54 do a magtape special function */
} bhdr;

int	stbcnt, ovrcnt;			/* SIL STB and OVR table entry count */
stbent	*stb = NULL;			/* pointer to STB buffer */
ovrent	*ovr = NULL;			/* pointer to OVR buffer */

/* note 0 rather than NULL because NULL is generally defined wrong 
 * (causing a warning that is a nuisance though it does no harm) 
 */
const char	disks[][3] = {	"dv","df","ds","dk","dl","dm",
				"dp","db","dr","dz","dw","du", 
				0 };

long findsym (const char *sym)
{
	word16	r50[2];
	int	n;
	stbent	*s;
	ovrent	*o;

	cvtnametor50 (sym, r50);	/* convert symbol to rad50 */
	s = stb;			/* point to start of stb */
	for (n = 0; n < stbcnt; n++, s++)
	{
		if (s->name[0] == r50[0] && s->name[1] == r50[1])
		{
			if (s->ovnum >= ovrcnt)
			{
				printf ("Bad STB entry for %s\n", sym);
				return (-1);
			}
			if (s->ovnum == 0) return (s->value);
			o = &ovr[s->ovnum];
			return (s->value - o->base +
				(o->offset[0] << 16) + o->offset[1]);
		}
	}
	return (-1);
}

void setptr (long blk, long size, void *buf)
{
	bdesc	*b;

	b = (bdesc *) buf;		/* point to where descriptor goes */
	b->wcnt = size / 2;		/* set word count */
	b->lbn[0] = blk & 0xffff;	/* low order LBN */
	b->lbn[1] = blk >> 16;		/*  and high order */
}

#define SIL	074064			/* rad50 "SIL" */

int silchk (firqb *f, savsilindex *x)
{
	word16	cs;
	word16	*p;

	p = &(x->si_sil);
	if (*p != SIL)			/* verify SIL marker */
	{
		printcurname (f);
		printf (" is not a SIL\n");
		return (FALSE);
	}
	cs = 0;				/* initialize checksum */
	do cs ^= *--p;			/* XOR checksum of the rest */
	while (p != (word16 *)x);
	if (cs != 0)
	{
		printf ("checksum error in SIL ");
		printcurname (f);
		printf ("\n");
		return (FALSE);
	}
	return (TRUE);			/* it's ok */
}

/* read file data for some given byte address, and return a pointer to
 * where it is.  There's at least 512 valid bytes after that point.
 */

/* this routine reads data from the file, doing the full count requested
 * even if it requires multiple calls to seqio.  (note that seqio only
 * reads a single contiguous piece of file.)
 */

void readfile (firqb *f, long count, void *buf)
{
	long	rcnt;
	byte	*b;				/* workaround cc bug */

	b = (byte *) buf;
	while (count)
	{
		rcnt = seqio (f, count, rread, b);
		if (rcnt == 0) return;		/* eof??? */
		b += rcnt;			/* adjust by what we read */
		count -= rcnt;			/* and count also */
	}
}
		
void *getdata (firqb *f, long address)
{
	fileseek (f, address / BLKSIZE);	/* seek to the right start block */
	readfile (f, BLKSIZE * 2, iobuf);
	return (iobuf + (address % BLKSIZE));
}

void cleanup ()
{
	rumountrw ();
	if (ovr != NULL) free (ovr);
	ovr = NULL;
	if (stb != NULL) free (stb);
	stb = NULL;
	return;
}

void dohook (int argc, char **argv)
{
	savsilindex	idx;		/* SIL index buffer */
	int		n;
	firqb		hf;		/* file to be hooked */
	firqb		bf;		/* file where bootstraps are */
	char		*disk;		/* bootstrap name */
	byte		bbuf[BLKSIZE];	/* boot block buffer */
	bdesc		*bd;		/* pointer to current boot data desc */
	bhdr		*bh;		/* pointer to boot header */
	int		dskio, dskioe, xxboot;	/* symbols from SIL */
	int		dskidx;		/* disk index for boot disk name */
	void		*btop;		/* pointer to end of boot code */
	short int	xxent;		/* xxboot table entry */
	int		pcoffset;	/* offset to apply to entry point */
	char		*p;
	
	if (argc == 0) disk = "dl";
	else
	{
		disk = argv[0];		/* disk is first argument */
		if (strlen (disk) != 2 ) {
			printf ("Invalid disk name %s\n", disk);
			return;
		}
		disk[0] = tolower (disk[0]);
		if (disk[0] == 'r') disk[0] = 'd';
		disk[1] = tolower (disk[1]);
	}
	for (dskidx = 0; ; dskidx++)
	{
		if (disks[dskidx] == NULL)
		{
			printf ("Unknown disk name %s\n", disk);
			return;
		}
		if (strcmp (disks[dskidx], disk) == 0) break;
	}
	if (sw.odt != NULL) pcoffset = 2;
	else if (sw.offset == NULL) pcoffset = 0;
	else
	{
		pcoffset = strtol (sw.offset, &p, 10);
		if (*p != '\0' || (pcoffset & 1) != 0)
		{
			printf ("Invalid offset %s\n", sw.offset);
			return;
		}
	}
	dskidx *= 2;			/* make even (byte offset) */
	rmountrw ();			/* mount disk for read/write */
	if (argc < 2) parse ("[0,1]init.sys", &hf);
	else
	{
		if (!parse (argv[1], &hf) || hf.flags & F_WILD)
		{
			cleanup ();
			printf ("Invalid filespec %s\n", argv[1]);
			return;
		}
		if ((hf.flags & f_ppn) == 0)
		{
			hf.proj = 0;
			hf.prog = 1;
		}
	}
	if (argc < 3) memcpy (&bf, &hf, sizeof (firqb));
	else {
		if (!parse (argv[2], &bf) || bf.flags & F_WILD)
		{
			cleanup ();
			printf ("Invalid filespec %s\n", argv[2]);
			return;
		}
		if ((bf.flags & f_ppn) == 0)
		{
			bf.proj = 0;
			bf.prog = 1;
		}
	}
	if (initfilescan (&bf, gfddcntbl) == 0 ||
	    nextfile (&bf) == 0)
	{
		printfqbname (&bf);
		printf (" not found\n");
		cleanup ();
		return;
	}
	initrandom (&bf);		/* set up random access to bootfile */
	seqio (&bf, sizeof(idx), rread, &idx);	/* read sil index */
	if (!silchk (&bf, &idx))
	{
		cleanup ();
		return;			/* quit if bad sil index */
	}
	stbcnt = idx.si_ent.se_stn;	/* copy STB and OVR sizes */
	ovrcnt = idx.si_ent.se_ovn;
	if ((stb = (stbent *) malloc (UP(stbcnt * sizeof (stbent),BLKSIZE))) == NULL)
	{
		cleanup ();
		rabort(NOMEM);
	}
	if ((ovr = (ovrent *) malloc (UP(ovrcnt * sizeof (ovrent),BLKSIZE))) == NULL)
	{
		cleanup ();
		rabort(NOMEM);
	}
	fileseek (&bf, idx.si_ent.se_stb);	/* read symbol table */
	readfile (&bf, UP(stbcnt * sizeof (stbent),BLKSIZE), stb);
	fileseek (&bf, idx.si_ent.se_ovb);	/* read symbol table */
	readfile (&bf, UP(ovrcnt * sizeof (ovrent),BLKSIZE), ovr);
	memset (bbuf, 0, BLKSIZE);		/* zero out boot buffer */
	if ((xxboot = findsym ("xxboot")) < 0)
	{
		cleanup ();
		printf ("Symbol XXBOOT not found\n");
		return;
	}
	if ((dskioe = findsym ("dskioe")) < 0)
	{
		cleanup ();
		printf ("Symbol DSKIOE not found\n");
		return;
	}
	if ((dskio = findsym ("dskio")) < 0) dskio = dskioe - 72;
	xxboot += dskidx;		/* point to entry for selected disk */
	xxent = *(short int *)(getdata (&bf, xxboot));
	if (xxent == 0)
	{
		cleanup ();
		printf ("No bootstrap for %s\n", disk);
		return;
	}
	xxboot += xxent;		/* point to bootstrap */
	bh = (bhdr *)(getdata (&bf, xxboot));	/* read it */
	n = bh->wcnt;			/* get bootstrap size in bytes */
	memcpy (bbuf, bh, n);		/* copy that into boot buffer */
	memcpy (bbuf + n, getdata (&bf, dskio), dskioe - dskio);
					/* append boot mainline */
	btop = bbuf + n + (dskioe - dskio);
	if (initfilescan (&hf, gfddcntbl) == 0 ||
	    nextfile (&hf) == 0)
	{
		printcurname (&hf);
		printf (" not found\n");
	}
	openfile (&hf);			/* set up to read file to hook */
	seqio (&bf, sizeof(idx), rread, &idx);	/* read sil index */
	if (!silchk (&hf, &idx))
	{
		cleanup ();
		return;			/* quit if bad sil index */
	}
	openfile (&hf);			/* reset to start over at VBN 0 */
	if (idx.usertop >= 0157000)
	{
		cleanup ();
		printcurname (&hf);
		printf (" high limit is too high (%06o)\n", idx.usertop);
		return;
	}
	
	n = UP(idx.usertop + 2,BLKSIZE); /* compute byte count to boot */
	((bhdr *) bbuf)->transfer = idx.initpc + pcoffset;
					/* set transfer address in bootstrap */
	((bhdr *) bbuf)->clustr = dcs;	/* set clustersize */
	bd = (bdesc *)(&bbuf[0772]);	/* initialize boot desc pointer */
	while (n)
	{
		if (--bd < (bdesc *)btop)	/* check for bootstrap full */
		{
			cleanup ();
			printcurname (&hf);
			printf (" is too fragmented to hook\n");
			return;
		}
		n -= seqio (&hf, n, setptr, bd);
	}
	rwrite (0, BLKSIZE, bbuf);	/* write boot block */
	if (sw.verbose != NULL)
	{
		printcurname (&hf);
		if (sw.offset == NULL && sw.odt == NULL)
			printf (" hooked, start PC = %06o\n", idx.initpc);
		else	printf (" hooked, start PC = %06o, offset %d\n",
				idx.initpc + pcoffset, pcoffset);
	}
	cleanup ();			/* clean up now */
	return;				/*  and we're done */
}
