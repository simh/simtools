/* rsts file processing subroutines */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "flx.h"
#include "fldef.h"
#include "fip.h"
#include "diskio.h"
#include "rtime.h"
#include "filename.h"

byte	fibuf[BLKSIZE];		/* buffer for directories */
byte	*sattbufp;		/* pointer to SATT buffer */
int	sattsize;		/* size of SATT.SYS file in bytes */
long	pcns;			/* number of pack clusters on this disk */
long	sattlbn;		/* start LBN of SATT */
long	satptr;			/* current allocation pointer (PCN) */
int	womsat;			/* TRUE if SATT needs to be written back */
long 	fiblk;			/* current block in FIBUF */
int	fiblkw;			/* TRUE if FIBUF needs to be written back */

void fbwrite (void)		/* write current block from fibuf */
{
	rwrite (fiblk, BLKSIZE, fibuf);
	fiblkw = FALSE;
}

void checkwrite (void)
{
	if (fiblkw) fbwrite ();
}

void fbread (long block)		/* read block into fibuf if needed */
{
	if (fiblk != block)
	{
		checkwrite ();
		rread (block, BLKSIZE, fibuf);
		fiblk = block;
	}
}

void readdcn (long dcn)
{
	fbread (dcntolbn(dcn));
}

long 	i,k;			/* current directory entry pointers */

/* ulk unpacks a rsts directory link, returning the LBN in i and
 * the byte offset in k.
 * it returns 0 if ok, BADLINK if not.  bad" means block offset out
 * of range (should be less than cluster size), cluster number 7,
 * or byte offset 760 octal (belongs to fdcm).
 */

int ulk (word link)
{
	int	clu, blk;

	k   = (link & ul_eno);			/* k = byte offset to entry */
	clu = (link & ul_clo) >> sl_clo;  	/* cluster number */
	blk = (link & ul_blo) >> sl_blo;	/* block in cluster */
	if (sw.debug != NULL) 
		printf ("ulk(%o), k=%lo, clu=%d, blk=%d, clumap=%d\n", 
			link, k, clu, blk, clumap->uent[clu]);
	if (blk >= clumap->uclus ||
	    clu > 6 ||
	    k == 0760 ||
	    clumap->uent[clu] == 0)
		return (BADLINK);
	i = blk + dcntolbn(clumap->uent[clu]);	/* LBN of entry */
        return (0);				/* ok */
}

/* readlk reads the directory block pointed to by the supplied link.
 * i and k are set as for ulk.  If the link is null, this routine 
 * returns FALSE; otherwise it returns TRUE.  readlk2 works similarly,
 * but it unconditionally reads what the link points to; thus it can be
 * used to read the label blockette of a directory (and readlk cannot!!!)
 * readlktbl is similar to readlk, except that it is meant to be called
 * when the block currently in fibuf is the GFD name entry link table --
 * which doesn't have a cluster map.  curgfd must be set for it to work.
 */

void readlk2 (word link)
{
	if (ulk (link))
		rabort(CORRUPT);		/* unpack the link */
	fbread (i);				/* read the directory block */
}

int readlk (word link)
{
	if (NULLINK(link))
		return (FALSE);			/* reject null link */
	readlk2 (link);				/* otherwise read it */
	return (TRUE);
}

int readlktbl (word link)
{
	if (NULLINK(link))
		return (FALSE);			/* null link, exit now */
	if (link & ul_clo)
		fbread (curgfd);		/* get gfd block with fdcm */
	else
		fbread (curgfd + ((link & ul_blo) >> sl_blo));
	return (readlk (link));			/* now do the actual read */
}

/* utility routines for getclu */

/* both of these scan the satt from "start" through "last", looking
 * to allocate a chunk of "clucount" clusters, with clustersize
 * "clusiz" (i.e., aligned on "clusiz" boundary) -- both of the latter
 * being expressed as a count of pack clusters.
 * scanbytes works for clusiz >= 8, where the scan is for whole bytes of
 * zero.  scanbits works for smaller clusiz values, and looks for fields
 * of zero bits.  In both cases, if a spot is found, it is allocated,
 * satptr is set to the next cluster after the allocated area, and the
 * start DCN returned; zero means failure (and SATT and satptr are 
 * unchanged).
 */

long scanbytes (long start, long last, int clusiz, long clucount)
{
	long	clusizbyt, clu, cluoff, clucountbyt, found;
	byte	*s;

	clusizbyt = clusiz / 8;		/* byte alignment needed */
	clucountbyt = clucount / 8;	/* allocation size in bytes */
	s = sattbufp + (start / 8);	/* byte pointer to start of scan */
	for (clu = start; clu <= last; clu += clusiz, s+= clusizbyt)
	{
		if (*s != 0)
			continue;	/* if not free, keep scanning */
		found = TRUE;		/* found something... */
		for (cluoff = 1; cluoff < clucountbyt; cluoff++) 
			if (*(s + cluoff) != 0)
			{
				cluoff = DOWN(cluoff,clusizbyt);
				s += cluoff;
				clu += cluoff * 8;
				found = FALSE;	/* never mind... */
				break;
			}
		if (!found)
			continue;	/* keep going if no luck */
		for (cluoff = 0; cluoff < clucountbyt; cluoff++) *s++ = 0xff;
		satptr = clu + clucount;	/* update satptr */
		MARKS;				/* SATT is dirty */
		return (pcntodcn(clu));
	}
	return (0);				/* nothing found */
}

long scanbits (long start, long last, int clusiz, long clucount)
{
	long	mask, mask1, mask2, cluoff, clu, found;
	byte	*s, *s2;

	clucount /= clusiz;		/* make it count of file clusters */
	mask1 = (1 << clusiz) - 1;	/* mask to match on */
	s = sattbufp + start / 8;	/* byte pointer to start of scan */
	mask = mask1 << (start % 8);	/* form starting bit field */
	for (clu = start; clu <= last; clu += clusiz)
	{
		if ((*s & mask) != 0)	/* not all free, keep looking... */
		{
			if (mask < 0x80)
				mask <<= clusiz;
			else
			{
				mask = mask1;
				s++;
			}
			continue;
		}
		found = TRUE;		/* found something... */
		mask2 = mask;		/* now look for contiguous piece */
		s2 = s;
		for (cluoff = 0; cluoff < clucount; cluoff++)
		{
			if ((*s2 & mask2) != 0)	/* not all free, keep looking... */
			{
				clu += cluoff;
				if (mask2 < 0x80)
					mask2 <<= clusiz;
				else
				{
					mask2 = mask1;
					s2++;
				}
				s = s2;
				mask = mask2;	/* continue just past here */
				found = FALSE;	/* never mind... */
				break;
			}
			if (mask2 < 0x80)
				mask2 <<= clusiz;
			else
			{
				mask2 = mask1;
				s2++;
			}
		}
		if (!found)
			continue;	/* keep going if no luck */
		for (cluoff = 0; cluoff < clucount; cluoff++)
		{
			*s |= mask;
			if (mask < 0x80)
				mask <<= clusiz;
			else
			{
				mask = mask1;
				s++;
			}
		}
		satptr = clu + clucount;	/* update satptr */
		MARKS;				/* SATT is dirty */
		return (pcntodcn(clu));
	}
	return (0);				/* nothing found */
}

/* getclu allocates clusters of the specified size, for a file of specified
 * total size.  before calling this routine, the pack should be mounted
 * read/write to ensure a rebuild is forced if things abort after this point.
 * If there is no room, zero is returned.  Otherwise the starting DCN
 * is returned.
 * A single allocation is done, so the "size" argument should be equal to
 * the clustersize unless a contiguous allocation is being done.
 */

int	pcs;				/* pack clustersize */

long getclu (int clusiz, long size)
{
	long	start, alloc, clucount;

	if (sw.debug != NULL)
		printf ("getclu(%d,%ld)\n", clusiz, size);
	if (sattsize == 0)
		rabort(INTERNAL);
	clusiz /= pcs;			/* fcs as count of clusters */
	clucount = size / pcs;		/* ditto for total size wanted */
	if (clusiz <= 0)
		rabort(INTERNAL);
	if (clucount <= 0)
		rabort(INTERNAL);
	start = UP(satptr,clusiz);	/* align start of scan */
	if (clusiz < 8)			/* scanning bitwise */
	{
		alloc = scanbits (start, pcns - clucount, clusiz, clucount);
		if (alloc)
			return (alloc);	/* found it in rest of satt */
		return (scanbits (0, start - clusiz, clusiz, clucount));
	}
	else				/* scanning whole bytes */
	{
		alloc = scanbytes (start, pcns - clucount, clusiz, clucount);
		if (alloc)
			return (alloc);	/* found it in rest of satt */
		return (scanbytes (0, start - clusiz, clusiz, clucount));
	}
}

/* retclu returns a single cluster.  "pos" is the DCN of the cluster,
 * (i.e., as found in retrieval entries); "clusiz" is the file clustersize.
 */

void retclu (long pos, int clusiz)
{
	long	m, n;
	byte	*s;

	if (sw.debug != NULL)
		printf ("retclu(%lo,%d)\n", pos, clusiz);
	if (sattsize == 0) 
		rabort(INTERNAL);
	pos = dcntopcn(pos);		/* convert to pcn */
	clusiz /= pcs;			/* fcs as count of clusters */
	if (clusiz <= 0)
		rabort(INTERNAL);
	s = sattbufp + pos / 8;		/* byte pointer to start of cluster */
	if (clusiz < 8)			/* scanning bitwise */
	{
		m = (1 << clusiz) - 1;	/* mask to match on */
		m <<= (pos % 8);	/* form starting bit field */
		*s &= ~m;		/* free this cluster */
	}
	else				/* scanning whole bytes */
	{
		clusiz /= 8;		/* change to count of bytes to free */
		for (n = 0; n < clusiz; n++) *s++ = 0;
	}
	MARKS;				/* mark SATT dirty */
}

int	pflags;			/* pack flags */
int	plevel;			/* pack structure revision level */
long	mfddcn;			/* DCN of start of MFD */
long	mfdlbn;			/* LBN of start of MFD */
char	pname[7];		/* pack ID in ascii */

void readlabel (void)
{
	packlabel	*p;

	readdcn (1);				/* get pack label */
	p = use(packlabel,0);
	pcs = p->ppcs;				/* get PCS */
	pflags = p->pstat;			/* get pack flags */
	if (pflags & uc_new)
	{
		plevel = p->plvl;			/* get RDS level */
		mfddcn = p->mdcn;		/* get MFD pointer */
	}
	else
	{
		plevel = 0;			/* set to 0 if old pack */
		mfddcn = 1;			/* MFD is at 1 for old format */
	}
	mfdlbn = dcntolbn(mfddcn);		/* for convenience, LBN also */
	r50toascii2 (p->pckid, pname, FALSE);	/* translate pack ID */
}

long	curgfd;			/* LBN of start of current GFD */
long	curqblbn;		/* LBN where current ppn quota block lives */
word	curqb;			/*  and link pointing to it */
int	entptr;			/* current directory entry allocation pointer */
word	nextlink;		/* link to next file */
word	prevlink;		/* link to prececessor of file */
word	nextppnlink;		/* link to next PPN for RDS 0 */
word	prevppnlink;		/* link to prececessor of PPN for RDS 0*/

void setppn (firqb *f, int proj, int prog, word ppnent, int which)
{
	f->cproj = proj;
	f->cprog = prog;
	curqb = 0;			/* haven't found quota block yet */
	if (which == gfddcntbl)		/* looking for UFD */
	{
		ppnent = dcntolbn(ppnent);
		fbread (ppnent);	/* read it */
		prevlink = 0;
		nextlink = use(ufdlabel,0)->ulnk;
		entptr = sizeof (ufdlabel);
	}
	else	readlktbl (ppnent);
}

word nextppn (firqb *f, int which)	/* find next ppn */
{
	int	firstproj, firstprog, lastproj, lastprog;
	int	j, n;
	word	ppnent;
	word	curlink;
	gfdne	*d;

	nextlink = 0;				/* assume nothing found */
	if (plevel == RDS0)			/* if old pack */
	{
		readlktbl (nextppnlink);		/* read next MFD NE */
		while (!NULLINK(nextppnlink))
		{
			readlk (nextppnlink);	/* read it */
			curlink = nextppnlink;
			d = use(gfdne,k);
			nextppnlink = d->ulnk;	/* point to next one */
			if ((d->ustat & (us_del | us_ufd)) == us_ufd
			    && (f->proj == 255 || f->proj == (d->unam[0] >> 8))
			    && (f->prog == 255 || f->prog == (d->unam[0] & 0xff)))
			{
				f->cproj = d->unam[0] >> 8;
				f->cprog = d->unam[0] & 0xff;
				if (which == gfddcntbl)	/* looking for UFD */
				{
					ppnent = dcntolbn(d->uar);
					if (ppnent == 0)
						/* PPN without UFD, skip */
						continue;
					fbread (ppnent);	/* read it */
					prevlink = 0;
					nextlink = use(ufdlabel,0)->ulnk;
					entptr = sizeof (ufdlabel);
				}
				else	ppnent = curlink;
				return (ppnent);
			}
			prevppnlink = curlink;
		}
		return (0);
	}
	firstproj = f->cproj;
	if (f->proj == 255)	lastproj = 254;
	else 			lastproj = f->proj;
	if (f->prog == 255)
	{
		firstprog = 0;
		lastprog = 254;
	}
	else 	firstprog = lastprog = f->prog;
	n = f->cprog + 1;		/* next prog number to try */
	for (j = firstproj; j <= lastproj; j++)
	{
		fbread (mfdlbn + gfddcntbl);
		if ((curgfd = dcntolbn(fibufw[j])) == 0)
			continue;
		fbread (curgfd + which);
		for ( ; n <= lastprog; n++)
		{
			if ((ppnent = fibufw[n]) == 0)
				continue;
			setppn (f, j, n, ppnent, which);
			return (ppnent);
		}
		n = firstprog;		/* next project, start at firstprog */
	}
	return (0);
}

word initfilescan (firqb *f, int which)	/* setup file scan to the beginning */
{
	int	firstproj, firstprog, lastproj, lastprog;
	int	j, n;
	word	ppnent;

	nextlink = prevlink = 0;			/* assume nothing found */
	if (plevel == RDS0)			/* if old pack */
	{
		prevppnlink = 0;
		curgfd = mfdlbn;		/* pretent MFD is also GFD */
		fbread (mfdlbn);			/* read start of MFD */
		nextppnlink = use(packlabel,0)->ulnk;
		return (nextppn (f, which));	/*  and look for first match */
	}
	if ((f->flags & f_name) == 0)
	{
		f->cproj = f->proj;
		f->cprog = f->prog;
		if (f->proj == 255)
		{
			fbread (mfdlbn);
			return (mfddcn);
		}
		if (f->prog == 255)
		{
			fbread (mfdlbn + gfddcntbl);
			if ((ppnent = fibufw[f->proj]) == 0)
				return (0);
			curgfd = dcntolbn (ppnent);
			fbread (curgfd);
			return (ppnent);
		}
		firstproj = lastproj = f->proj;
		firstprog = lastprog = f->prog;
	}
	else
	{
		if (f->proj == 255)
		{
			firstproj = 0;
			lastproj = 254;
		}
		else 	firstproj = lastproj = f->proj;
		if (f->prog == 255)
		{
			firstprog = 0;
			lastprog = 254;
		}
		else 	firstprog = lastprog = f->prog;
	}
	for (j = firstproj; j <= lastproj; j++)
	{
		fbread (mfdlbn + gfddcntbl);
		if ((curgfd = dcntolbn(fibufw[j])) == 0)
			continue;
		fbread (curgfd + which);
		for (n = firstprog; n <= lastprog; n++)
		{
			if (n == 0 && j == 0)
				continue;
			if ((ppnent = fibufw[n]) == 0)
				continue;
			setppn (f, j, n, ppnent, which);
			return (ppnent);
		}
	}
	return (0);
}

int wmatch (const char *wn, const char *n)	/* wildcard match */
{
	while (*wn != '\0')
	{
		if (*wn != *n && *wn != '?')
			return (FALSE);
		wn++;
		n++;
	}
	return (TRUE);
}

/* nextfileindir looks for a file in the current directory.  If found, it
 * updates the file informationin the supplied firqb (name, links, status, 
 * protection code, size, clustersize).
 * A return of TRUE means match, FALSE means nothing found.
 * Special case: if the filename is null, the indicated director is opened.
 * In that case, "prevlink" is set non-zero to indicate that this is the
 * only "match" on this directory.  "Indicated directory" can be the GFD 
 * or MFD if the proj and prog were wild.
 */

int nextfileindir (firqb *f)
{
	ufdne	*n;
	ufdae	*a;
	ufdrms1	*r;
	int	ent;

	if ((f->flags & f_name) == 0)	/* null name, open UFD */
	{
		if (prevlink)
			return (FALSE);
		prevlink++;		/* return no match next time */
		if (f->prog == 255)
		{
			if (f->proj == 255)
				fbread (mfdlbn);
			else
				fbread (curgfd);
		}
		f->stat = us_ufd | us_nok;
		f->prot = 63;
		f->clusiz = clumap->uclus;
		f->size = 0;
		for (ent = 0; ent < 7; ent++)
			if (clumap->uent[ent])
				f->size += clumap->uclus;
		f->rmslink = 0;
		f->recfmt = rf_stm;	/* default to stream */
		f->eofblk = f->size;
		f->eofbyte = 0;
		sprintf (f->cname, "%03d%03d.dir", f->cproj, f->cprog);
		return (TRUE);
	}
	while (readlk (nextlink))	/* read next entry, if any */
	{
		n = use(ufdne,k);
		f->nlink = nextlink;	/* save this link */
		nextlink = n->ulnk;	/* link to next entry */
		if ((n->ustat & (us_ufd | us_del)) == 0)
		{
			r50filename (n->unam, f->cname, TRUE);
			if (wmatch (f->name, f->cname))
			{
				f->stat = n->ustat;
				f->prot = n->uprot;
				f->rlink = n->uar;
				f->alink = n->uaa;
				if (!readlk (f->alink)) 
					rabort(CORRUPT);
				a = use(ufdae,k);
				f->size = a->usiz;
				f->clusiz = a->uclus;
				f->rmslink = a->ulnk;
				if (a->urts[0] == 0) 
					f->size += a->urts[1] << 16;
				if (NULLINK(f->rmslink))
				{
					f->recfmt = rf_stm;	/* default to stream */
					f->eofblk = f->size;
					f->eofbyte = 0;
				}
				else
				{
					readlk (f->rmslink);
					r = use(ufdrms1,k);
					f->recfmt = r->fa_typ;
					f->recsiz = r->fa_rsz;
					f->eofblk = ((long)(r->fa_eof[0]) << 16) + r->fa_eof[1];
					f->eofbyte = r->fa_eofb;
					if (f->eofbyte == BLKSIZE)
					{
						f->eofbyte = 0;
						f->eofblk++;
					}
					if ((r->fa_typ & fa_rfm) == rf_vfc)
					{
						if (NULLINK(r->ulnk))
							f->rechdrsiz = 0;
						else
						{
							readlk(r->ulnk);
							f->rechdrsiz = use(ufdrms2,k)->fa_hsz;
						}
					}
				}
				return (TRUE);
			}
		}
		prevlink = f->nlink;	/* save link to predecessor */
	}
	return (FALSE);			/* not found */
}

/* nextfile looks for the next matching filespec, going across directories
 * as needed.  If a match is found, it returns TRUE and loads the supplied
 * firqb with information about the file.  Otherwise, FALSE is returned.
 * Special case: if the filename is null, the current UFD is opened.
 */

int nextfile (firqb *f)			/* find next match for this filespec */
{
	for (;;)
	{
		if (nextfileindir (f))
		{
			return (TRUE);
		}
		if (nextppn (f, gfddcntbl) == 0)
			return (FALSE);
	}
}

int findfile (firqb *f, const char *name)	/* find a single file by name */
{
	parse (name, f);
	initfilescan (f, gfddcntbl);
	return (nextfile (f));
}

int findqb (const firqb *f)		/* find quota block for current ppn */
{
	word	link;
	ua_quo	*a;

	if (plevel < RDS11)
		rabort(INTERNAL);
	if (curgfd == 0)
		rabort(INTERNAL);
	fbread (curgfd + gfdatrtbl);	/* read attribute link table */
	link = fibufw[f->cprog];	/* fetch appropriate link */
	if (link & ul_clo) fbread (curgfd);	/* get gfd block with fdcm */
	else fbread (curgfd + ((link & ul_blo) >> sl_blo));
	if (!readlk (link)) rabort(INTERNAL);	/* read dir NE */
	link = use(gfdne,k)->ulnk;	/* get link to first attribute */
	while (link)
	{
		readlk (link);		/* read an attribute entry */
		a = use(ua_quo,k);
		if (a->uatyp == aa_quo) return (link);	/* found it */
		link = a->ulnk;		/* follow the link */
	}
	rabort(CORRUPT);		/* bogus -- no quota block */
	return (0);			/* to make the compiler happy */
}

void updqb (const firqb *f, long delta)	/* adjust quota by delta blocks */
{
	long	quo, savefiblk;
	ua_quo	*q;

	if (sw.debug != NULL)
		printf ("updqb(,%ld), RDS %d.%d\n", 
			delta, plevel >> 8, plevel & 0xff);
	if (plevel < RDS12)
		return;			/* NOP if not RDS 1.2 */
	if (f->cproj == 0 && f->cprog == 1)
		return;			/* NOP for [0,1] */
	savefiblk = fiblk;		/* remember current block */
	if (curqb == 0)			/* if we haven't been here yet */
	{
		curqb = findqb (f);	/* find it */
		curqblbn = fiblk;	/*  and remember LBN also */
	}
	else
	{
		fbread (curqblbn);	/* read block where it lives */
		ulk (curqb);		/* and set up "k" */
	}
	q = use(ua_quo,k);		/* point to it */
	quo = (q->aq_crm << 16) + q->aq_crl;
	quo += delta;			/* adjust the usage */
	q->aq_crl = quo & 0xffff;	/* update low order */
	q->aq_crm = quo >> 16;		/*  and high order */
	MARKF;				/* mark FIBUF for write */
	fbread (savefiblk);		/* restore caller's FIBUF content */
}

/* upddlw updates the dlw/dla field; it is called when the file has 
 * been changed.  note that we don't implement date of last access
 * recording; this avoids having to write to disks that would otherwise
 * only be read.
 */

void upddlw (const firqb *f)		/* update date of last write */
{
	if (sw.debug != NULL)
		printf ("upddlw( )\n");
	if (f->stat & us_ufd)
		return;			/* nop on directories! */
	if (!readlk (f->alink))
		rabort(INTERNAL);
	use(ufdae,k)->udla = curdate ();
	MARKF;
}

/* extdir extends the current directory by one cluster, if possible.
 * it returns the DCN of the cluster allocated, if successful, or 0
 * if allocation failed.  If it succeeded, all directory clustermaps
 * have been updated, and the new cluster has been otherwise set to zero.
 * This routine works for any kind of directory, MFD and GFD included.
 * On completion, some directory block is in FIBUF.  If successful,
 * it is the FIRST block of the directory (NOT any of the allocated
 * cluster!).
 * 
 * extdir2 either creates the first cluster of a directory or extends
 * a directory by one cluster.  It is called from extdir (and also
 * directly in processing the "init" command). It is passed the clustersize,
 * clustermap entry offset, clustermap flags, and directory label data
 * to be used.  (If a cluster other than first is being allocated, only
 * entry offset, flags, and clustersize arguments are used.)  The flags
 * argument is used to control whether an mfd/gfd vs. a ufd is being
 * extended.  Since this affects which blocks have to be updated,  it
 * is important to pass the correct value.
 */

int extdir2 (int newcm, int clusiz, byte flags, const ufdlabel *newl)
{
	long		clu, lbn;	/* new cluster and its lbn */
	int		cm, off;	/* clustermap entry and offset */
	int		realclu;	/* clustersize for getclu  */
	ufdlabel	*u;

	if (sw.debug != NULL)
	{
		printf ("extdir2(%d,%d,%o)\n", newcm, clusiz, flags);
		printf (" old map: %03o %03o %06o %06o %06o %06o %06o %06o %06o\n",
			clumap->uclus, clumap->uflag, clumap->uent[0],
			clumap->uent[1], clumap->uent[2],
			clumap->uent[3], clumap->uent[4],
			clumap->uent[5], clumap->uent[6]);
	}
	checkwrite ();			/* make sure pending write is done */
	realclu = clusiz;
	if (clusiz < pcs)
		realclu = pcs; 		/* for big disks, if pcs > 16 */
	clu = getclu (realclu, realclu); /* get one cluster that size */
	if (clu == 0)
		return (FALSE);		/* sorry, nothing available */
	lbn = dcntolbn(clu);		/* get corresponding start LBN */
	for (cm = 0; cm < newcm; cm++)	/* update old clusters, if any */
	{
		for (off = 0; off < clusiz; off++)
		{
			if (cm == 0)	/* first cluster needs some checks */
			{
				if (off == 0) 
					continue;	/* do this last */
				if ((flags & fd_new) && off <= gfdatrtbl)
					continue;	/* skip tables */
			}
			fbread (dcntolbn(clumap->uent[cm]) + off);
			clumap->uent[newcm] = clu;
			fbwrite ();	/* write updated data */
		}
	}

	if (newcm == 0)			/* creating the first cluster */
	{
		off = 1;		/* Assume updating a UFD */
		if (flags & fd_new)
			off = gfdatrtbl + 1; 	/* Adjust if MFD/GFD */
		memset (fibuf, 0, BLKSIZE);	/* zero entire block */
		if (flags & fd_new)	/* erase table blocks if MFD/GFD */
		{
			rwrite (lbn + gfddcntbl, BLKSIZE, fibuf);
	  		rwrite (lbn + gfdatrtbl, BLKSIZE, fibuf);
		}
		clumap->uclus = clusiz;	/* set up fixed fields in cluster map */
		clumap->uflag = flags;	/* flags too */
		clumap->uent[0] = clu;	/* this is the only cluster */
	}
	else
	{
		/* Special case: if we're creating the second cluster of a
		 * directory of cluster size 1, then the above code does
		 * nothing.  So we need to read cluster 0 explicitly to get
		 * the old cluster map.
		 */
		if (newcm == 1 && clusiz == 1) 
		{
			readdcn (clumap->uent[0]);
			clumap->uent[newcm] = clu;
		}
		memset (fibuf, 0, BLKSIZE - sizeof (fdcm));
		off = 0;	/* updating entire new cluster */
	}
	for ( ; off < clusiz; off++)
		rwrite (lbn + off, BLKSIZE, fibuf);
	if (newcm == 0)			/* initialize directory label */
	{
		fiblk = lbn;		/* we're building this block */
		u = use(ufdlabel,0);
		memcpy (u, newl, sizeof (ufdlabel));
	}
	else
	{
		fiblk = -1;			/* nothing in fibuf */
		readdcn (clumap->uent[0]);	/* read first block of dir */
		clumap->uent[newcm] = clu;	/* update that last */
	}
	fbwrite ();			/* write updated data */
	if (sw.debug != NULL)
		printf (" new map: %03o %03o %06o %06o %06o %06o %06o %06o %06o\n",
			clumap->uclus, clumap->uflag, clumap->uent[0],
			clumap->uent[1], clumap->uent[2],
			clumap->uent[3], clumap->uent[4],
			clumap->uent[5], clumap->uent[6]);
	return (clu);			/* it worked! return new DCN */
}

int extdir (void)			/* extend current directory */
{
	int	clusiz;			/* directory clustersize */
	int	newcm;			/* clustermap entry to update */

	if (sw.debug != NULL)
		printf ("extdir()\n");
	if (clumap->uent[6])
		return (0);		/* already max length dir */
	for (newcm = 0; ; newcm++) if (clumap->uent[newcm] == 0) break;
	clusiz = clumap->uclus;		/* get dir clustersize */
	return (extdir2 (newcm, clusiz, clumap->uflag, NULL));
}

/* Check if the UFD has been allocated yet for a PPN; if not, allocate
 * the first cluster.  Returns 0 on failure, or start LBN of the UFD
 * otherwise (including if the UFD already exists).
 */

/* the 255,255 is overwritten with the PPN */

ufdlabel newulabel = { 0, 0177777, {0, 0, 0, 0}, {255, 255}, UFD };

int allocufd (word dirne, firqb *f)
{
	long	savegfdlbn, savelbn;
	word	dirae;
	gfdne	*d;
	gfdae	*a;

	if (sw.debug != NULL)
		printf ("allocufd (%d) for [%d,%d]\n",
			dirne, f->cproj, f->cprog);
	readlk (dirne);				/* read the NE */
	d = use(gfdne,k);
	if ((savelbn = dcntolbn(d->uar)) == 0)	/* no UFD allocated yet */
	{
		savegfdlbn = fiblk;		/* remember where NE is */
		dirae = d->uaa;			/* read AE for PPN */
		if (!readlk (dirae))
			rabort(CORRUPT);
		a = use(gfdae,k);
		newulabel.lppn[1] = f->cproj;	/* fill in the label */
		newulabel.lppn[0] = f->cprog;
		if (!extdir2 (0, a->uclus, 0, &newulabel))
			return (0);
		savelbn = clumap->uent[0];	/* pick up DCN of UFD */
		fbread (savegfdlbn);		/* re-read GFD */
		readlk (dirne);			/*  and set up for NE */
		use(gfdne,k)->uar = savelbn;	/* set DCN of UFD */
		MARKF;
		fbread (curgfd + gfddcntbl);	/* read UFD pointer block */
		fibufw[f->cprog] = savelbn;	/* set the new pointer (DCN) */
		MARKF;
		savelbn = dcntolbn(savelbn);	/* now make it an LBN */
		entptr = sizeof (ufdlabel);	/* initialize the entry allocator */
	}
	return (savelbn);
}
/* free entry search runs from entptr through end of directory.  entptr
 * initially points to start of dir.  Anytime an entry is freed, entptr
 * should be backed up to that entry if necessary.
 * The value returned is the link to the entry, or 0 if none is available.
 * The directory is extended if necessary, so 0 will be returned only if
 * the disk is full or the directory already has 7 clusters, all full.
 * In all cases, fibuf is left with a valid directory block in it.
 * If the allocation succeeded, is it the block with the free entry, and
 * k has the offset to it.  If the allocation failed, it is some unspecified
 * block of the directory.
 * The entry is completely zeroed, and entptr points to it (not to the
 * entry after it).  So a second call to getent before any changes are
 * made will find the same entry again!
 */

int	getent (void)			/* get a free directory blockette */
{
	int	clu, blk, n;		/* cluster and block being scanned */
	lword32	*l;

	if (sw.debug != NULL)
		printf ("getent()\n");
	readlk2 (entptr);		/* read starting point */
	clu = (entptr & ul_clo) >> sl_clo;	/* convert entptr */
	blk = (entptr & ul_blo) >> sl_blo;
	for (;;)
	{
		if (*use(lword,k) == 0)		/* found an entry */
		{
			entptr = k + (clu << sl_clo) + (blk << sl_blo);
			readlk2 (entptr);	/* read it */
			l = use (lword32, k);	/* point to it */
			*l++ = 0;		/*  and clear it out */
			*l++ = 0;
			*l++ = 0;
			*l++ = 0;
			MARKF;			/* and mark it */
			return (entptr);	/* this is what we found */
		}
		if ((k += sizeof (ufdne)) == BLKSIZE - sizeof (fdcm))
		{
			k = 0;
			if (++blk < clumap->uclus)
			{
				if ((clumap->uflag & fd_new) 
				    && clu == 0 && blk == gfddcntbl)
					blk = gfdatrtbl + 1;
				fbread (dcntolbn(clumap->uent[clu]) + blk);
			}
			else
			{
				blk = 0;
				if (++clu > 6) return (0);	/* we're FULL */
				if (clumap->uent[clu])
					readdcn (clumap->uent[clu]);
				else
				{
					if ((n = extdir ()) != 0)
					{
						readdcn (n);
						entptr = clu << sl_clo;
						readlk2 (entptr); /* read it */
						return (entptr);
					}
					else
						return (0);	/* no room */
				}
			}
		}
	}
}

/* Release a directory entry.  entptr is updated if necessary. 
 * The entire entry is cleared out.
 */

void retent (word link)
{
	lword32	*l;

	if (sw.debug != NULL) printf ("retent(%o)\n", link);
	if (!readlk (link)) rabort(INTERNAL);	/* read the entry */
	l = use (lword32, k);			/* point to it */
	*l++ = 0;				/*  and mark it as free */
	*l++ = 0;
	*l++ = 0;
	*l++ = 0;
	MARKF;
	if ((link & ul_clo) < (entptr & ul_clo) ||
	    ((link & ul_clo) == (entptr & ul_clo) && link < entptr))
		entptr = link;			/* move entptr back if needed */
}

/* Release the directory entries and allocated clusters for a file.
 * This routine does not unlink it from the directory linked list;
 * the caller has to do that.  Nor does it make any protection checks.
 */

void retfile (word nlink)
{
	word	alink, rlink, rlink2, rmslink, clu, clusiz;
	ufdne	*n;
	ufdae	*a;
	ufdrms1	*rms;
	ufdre	*r;

	if (sw.debug != NULL)
		printf ("retfile(%o)\n", nlink);
	if (!readlk (nlink))
		rabort(INTERNAL);		/* read the NE */
	n = use(ufdne,k);
	alink = n->uaa;
	rlink = n->uar;				/* save the links */
	retent (nlink);				/* release the NE */
	if (!readlk (alink))
		rabort(CORRUPT);		/* read the AE */
	a = use(ufdae,k);
	clusiz = a->uclus;			/* save fcs */
	rmslink = a->ulnk;
	retent (alink);				/* release the AE */
	if (!NULLINK(rmslink))			/* if RMS attributes present */
	{
		readlk (rmslink);		/* read the first */
		rms = use(ufdrms1,k);
		if (!NULLINK(rms->ulnk))
			retent (rms->ulnk);
		retent (rmslink);
	}
	while (!NULLINK(rlink))			/* release all RE's */
	{
		readlk (rlink);			/* read this one */
		r = use(ufdre,k);
		rlink2 = r->ulnk;		/* save link to next */
		for (clu = 0; clu < 7; clu++)
			if (r->uent[clu])
				retclu (r->uent[clu], clusiz);
		retent (rlink);			/* release the RE */
		rlink = rlink2;
	}
}

void delfile (firqb *f)
{
	readlk2 (prevlink);
	use(ufdne,k)->ulnk = nextlink;		/* link previous to next */
	MARKF;
	retfile (f->nlink);			/* deallocate this file */
	updqb (f, -UP(f->size,f->clusiz));	/* adjust quota block */
	f->nlink = 0;				/* zap link to former file */
}

/* Create a file given a filled-in firqb.  The file is initially null,
 * unless it is to be contiguous; in that case, it is pre-extended to
 * the given size.  The rts name to be assigned is also passed.  The third
 * and fourth arguments are used to supply RMS attribute block data if
 * desired; if these are passed as NULL, no attribute blockettes are
 * allocated for the file.
 * prevlink must be set on entry (normally from an earlier call to nextfile).
 * Return is TRUE if success, FALSE if failure.  On failure, everything is
 * released with the exception of any directory extension that may have been
 * done.
 * NOTE: this routine assumes that the file does NOT currently exist.
 * The caller must verify this.
 */

int crefile (firqb *f, const word16 *rtsname,
	     const ufdrms1 *rms1, const ufdrms2 *rms2)
{
	long	dcn, startdcn, clu, clucount;
	int	dcnperfcs, reoff;
	word	re, prevre;
	word	rmslink2 = 0;
	ufdne	*n;
	ufdae	*a;
	ufdrms1	*a1;
	ufdrms2	*a2;

	if (sw.debug != NULL)
		printf ("crefile()\n");
	if (pflags & uc_top)
	{
		prevlink = 0;			/* link new file first */
		readlk2 (0);			/* find current first */
		nextlink = use(ufdlabel,0)->ulnk; /* that will be next */
	}
	else
		nextlink = 0;			/* otherwise no next */
	dcnperfcs = f->clusiz / dcs;		/* # dev clu per file clu */
	if ((f->stat & us_nox) == 0)
		f->size = 0;			/* if not contiguous */
	f->rmslink = 0;
	if (rms1 != NULL)			/* if allocating attributes */
	{
		if (rms2 != NULL)		/* two of them! */
		{
			if ((rmslink2 = getent ()) == 0)
				return (FALSE);
			readlk (rmslink2);
			if (sw.debug != NULL)
				printf ("crefile() rms2 at %o\n", rmslink2);
			a2 = use(ufdrms2,k);
			memcpy (a2, rms2, sizeof (ufdrms2));
			a2->ulnk = ul_use;	/* mark in use */
			MARKF;
		}
		if ((f->rmslink = getent ()) == 0)
		{
			if (rmslink2)
				retent (rmslink2);
			return (FALSE);
		}
		readlk (f->rmslink);
		if (sw.debug != NULL)
			printf ("crefile() rms1 at %o\n", f->rmslink);
		a1 = use(ufdrms1,k);
		memcpy (a1, rms1, sizeof(ufdrms1));
		a1->ulnk = rmslink2 | ul_use;
		MARKF;
	}
	if ((f->alink = getent ()) == 0)
	{
		if (f->rmslink)
			retent (f->rmslink);
		if (rmslink2)
			retent (rmslink2);
		return (FALSE);
	}
	readlk (f->alink);			/* read new AE */
	if (sw.debug != NULL)
		printf ("crefile() ae at %o\n", f->alink);
	a = use(ufdae,k);
	a->ulnk = f->rmslink | ul_use;		/* mark in use */
	a->usiz = f->size & 0xffff;		/* set low order size */
	a->udla = a->udc = curdate ();		/* set dates */
	a->utc = curtime ();			/*  and creation time */
	a->uclus = f->clusiz;			/*   and clustersize */
	if (f->size >> 16) 			/* if large file */
		a->urts[1] = f->size >> 16;	/* set high order size */
	else
	{
		a->urts[0] = rtsname[0];	/* set rts name */
		a->urts[1] = rtsname[1];
	}
	MARKF;
	if ((f->nlink = getent ()) == 0)	/* try to get NE */
	{
		if (f->rmslink) retent (f->rmslink);
		if (rmslink2) retent (rmslink2);
		retent (f->alink);		/* no go, release AE */
		return (FALSE);
	}
	readlk (f->nlink);
	if (sw.debug != NULL)
		printf ("crefile() ne at %o\n", f->nlink);
	n = use(ufdne,k);
	cvtnameexttor50 (f->cname, n->unam);	/* set file name.ext */
	n->uaa = f->alink;			/* link AE to NE */
	n->ustat = f->stat;
	n->uprot = f->newprot;			/* set status and protection */
	MARKF;
	f->rlink = 0;				/* no RE's allocated yet */
	if (f->size)				/* do contiguous extend */
	{
		clucount = UP(f->size,f->clusiz); /* round size up to cluster */
		startdcn = getclu (f->clusiz, clucount);
		clucount /=  f->clusiz;		/* now get cluster count */
		if (startdcn == 0)		/* if it failed... */
		{
			retfile (f->nlink);	/* make file go away */
			return (FALSE);		/*  and exit */
		}
		if (sw.debug != NULL) printf ("crefile() dcn %lo\n", startdcn);
		prevre = 0;			/* working on first RE */
		reoff = 0;
		dcn = startdcn;			/* start filling in RE's here */
		for (clu = 0; clu < clucount; clu++)
		{
			if (reoff == 0)		/* time to get another RE */
			{
				if ((re = getent ()) == 0)
				{
					for (clu = 0; clu < clucount; clu++)
						retclu (startdcn + (clu * dcnperfcs), f->clusiz);
					retfile (f->nlink);
					return (FALSE);
				}
				if (sw.debug != NULL) 
					printf ("crefile() re at %o\n", re);
				if (prevre)
				{
					readlk (prevre);
					use(ufdre,k)->ulnk = re;
				}
				else
				{
					readlk (f->nlink);
					use(ufdne,k)->uar = re;
					f->rlink = re;	/* save in firqb */
				}
				MARKF;
				prevre = re;	/* next RE will link to this */
				readlk (re);	/* read it */
			}
			use(ufdre,k)->uent[reoff++] = dcn;
			MARKF;
			dcn += dcnperfcs;	/* point to next file cluster */
			if (reoff > 6) reoff = 0; /* check for end of RE */
		}
	}
	readlk (f->nlink);			/* read NE again */
	use(ufdne,k)->ulnk = nextlink;		/* link it to next */
	MARKF;
	readlk2 (prevlink);			/* read previous */
	use(ufdne,k)->ulnk = f->nlink;		/* link it to new file */
	MARKF;
	prevlink = f->nlink;			/* this is new previous */
	if (f->size)
		updqb (f, UP(f->size,f->clusiz));
	return (TRUE);
}

/* This routine checks whether a file is protected.  It returns TRUE if so.
 * It must be called after a call to nextfile or nextfileindir; the
 * file information in the firqb is used to control the decisions.
 */

int protfile (firqb *f)
{
	if (f->stat & us_nok)
	{
		printf ("File ");
		printcurname (f);
		printf (" is marked no-delete\n");
		return (TRUE);
	}
	if (sw.overwrite == NULL && f->prot & up_wpo)
	{
		printf ("File ");
		printcurname (f);
		printf (" is protected\n");
		return (TRUE);
	}
	return (FALSE);
}

/* Makedir creates a new account.  It allocates the GFD, if needed, and
 * creates the accounting data for the specified PPN.  It does not actually
 * allocate a UFD; this will happen when a file is created there, or can
 * be done at any time by calling allocufd.
 * It returns TRUE on success, FALSE on failure.  On failure, an explanatory
 * message is also printed.
 * The caller must verify that the account does not currently exist.  curgfd
 * must be set up to point to the gfd, if it exists.  "initfilescan" can be
 * used to do both these things.
 */

/* the second (high order) 255 is overwritten with the project number */

ufdlabel newglabel = { 1, 0177777, {0, 0, 0, 0}, {255, 255}, GFD };

int makedir (firqb *f, int newclu)
{
	gfdne	*n;
	gfdae	*a;
	ua_quo	*q;
	ua_dat	*d;
	word	ne, ae, quo, dat;
	int	gfdclu;

/* note: when processing an RDS 0.0 disk, curgfd = mfdlbn = the start lbn
 * of the MFD (DCN 1)
 */

	if (sw.debug != NULL)
	{
		printf ("makedir() for");
		printcurname (f);
		printf ("\n");
	}
	if (plevel == RDS0 && f->cproj == 0)
	{
		printf ("Invalid PPN [%d,%d] for RDS 0.0 format disk\n", f->cproj, f->cprog);
		return (FALSE);
	}
	if (curgfd == 0)			/* must create GFD */
	{
		gfdclu = 4;
		if (gfdclu < pcs)
			gfdclu = pcs;
		if (gfdclu > 16)
			gfdclu = 16;
		newglabel.lppn[1] = f->cproj;
		if (!extdir2 (0, gfdclu, fd_new, &newglabel))
		{
			printf ("No room to allocate GFD for [%d,*]\n", f->cproj);
			return (FALSE);
		}
		curgfd = clumap->uent[0];	/* remember its DCN */
		fbread (mfdlbn + gfddcntbl);
		fibufw[f->cproj] = curgfd;	/* set new GFD pointer */
		MARKF;
		curgfd = dcntolbn(curgfd);	/* and remember the LBN */
	}
	fbread (curgfd);			/* read first block of GFD */
	entptr = sizeof (gfdlabel);		/* initialize getent search */
	if ((ae = getent ()) == 0)
	{
		printf ("No room to create [%d,%d]\n", f->cproj, f->cprog);
		return (FALSE);
	}
	readlk (ae);
	a = use(gfdae,k);
	a->ulnk = ul_use;
	a->uclus = newclu;
	MARKF;
	if (plevel > RDS0)
	{
		if ((dat = getent ()) == 0)
		{
			printf ("No room to create [%d,%d]\n", f->cproj, f->cprog);
			retent (ae);
			return (FALSE);
		}
		readlk (dat);
		d = use(ua_dat,k);
		d->ulnk = ul_use;		/* mark in use */
		d->uatyp = aa_dat;		/* set type code */
		d->at_lti = at_npw;		/* in case it's set to /user */
		d->at_cda = d->at_pda = curdate ();
		d->at_pti = curtime () | at_nlk; /* set no-lookup flag */
		if (plevel >= RDS12)
			d->at_exp = 65535U;
		MARKF;
		if ((quo = getent ()) == 0)
		{
			printf ("No room to create [%d,%d]\n", f->cproj, f->cprog);
			retent (ae);
			retent (dat);
			return (FALSE);
		}
		readlk (quo);
		q = use(ua_quo,k);
		q->ulnk = dat;			/* link to date/time */
		q->uatyp = aa_quo;		/* set type code */
		q->aq_lol = 65535U;		/* set disk quotas unlimited */
		q->aq_lil = 65535U;
		q->aq_lom = 255U;
		q->aq_lim = 255U;
		MARKF;
	}
	if ((ne = getent ()) == 0)
	{
		printf ("No room to create [%d,%d]\n", f->cproj, f->cprog);
		retent (ae);
		if (plevel > RDS0)
		{
			retent (dat);
			retent (quo);
		}
		return (FALSE);
	}
	readlk (ne);
	n = use(gfdne,k);
	n->unam[0] = (f->cproj << 8) + f->cprog;
	n->ustat = us_nok | us_ufd;
	n->uprot = 60;
	n->uaa = ae;
	MARKF;
	if (plevel > RDS0)
	{
		n->ulnk = quo;			/* link to quotas */
		fbread (curgfd + gfdatrtbl);	/* read GFD table block */
		fibufw[f->cprog] = ne;		/* set NE link */
	}
	else
	{
		n->ulnk = nextppnlink;		/* link this one to next */
		readlk2 (prevppnlink);		/* read previous MFD NE */
		use(gfdne,k)->ulnk = ne;	/*  and link it to new NE */
		prevppnlink = ne;		/* this is previous next time */
	}
	MARKF;
	return (TRUE);
}

/* remdir removes a directory, or prints a message explaining why
 * it can't.  Returns TRUE on success, FALSE on failure.  The name entry
 * link for the PPN is supplied by the caller. 
 * 
 */

int remdir (firqb *f, word ne)
{
	word	ae, attr, a2;
	long	savelbn, ufdclu;
	gfdne	*n;
	int	j;

	if (f->cproj == 0 && f->cprog == 1)
	{
		printf ("Directory [0,1] cannot be deleted\n");
		return (FALSE);
	}
	readlk (ne);				/* read its NE */
	n = use(gfdne,k);
	if (n->uar)				/* it has a UFD */
	{
		savelbn = fiblk;
		readdcn(n->uar);		/* read the UFD */
		if (!NULLINK(use(ufdlabel,0)->ulnk))
		{
			printf ("Directory [%d,%d] is not empty\n", 
				f->cproj, f->cprog);
			fbread (savelbn);
			return (FALSE);
		}
		ufdclu = clumap->uclus;		/* get clustersize */
		if (ufdclu < pcs)
			ufdclu = pcs;		/* for big disks */
		for (j = 0; j < 7; j++) 
			if (clumap->uent[j])
				retclu (clumap->uent[j], ufdclu);
		fbread (curgfd + gfddcntbl);	/* read UFD pointer table */
		fibufw[f->cprog] = 0;		/* no more UFD */
		MARKF;
		fbread (savelbn);		/* restore GFD block */
	}
	ae = n->uaa;
	attr = n->ulnk;
	if (NULLINK(ae))
		rabort(CORRUPT);
	retent (ne);				/* release NE */
	retent (ae);				/*  and AE */
	if (plevel > RDS0)
	{
		while (!NULLINK (attr))
		{
			readlk (attr);
			a2 = use(uattr,k)->ulnk;	/* remember next link */
			retent (attr);		/* release it */
			attr = a2;
		}
		fbread (curgfd + gfdatrtbl);
		fibufw[f->cprog] = 0;		/* no more PPN */
	}
	else
	{
		readlk2 (prevppnlink);		/* read previous MFD NE */
		use(gfdne,k)->ulnk = nextppnlink;	/*  and unlink this one */
	}
	MARKF;
	return (TRUE);
}

/* Set up the SATT parameters by searching for [0,1]satt.sys if that
 * hasn't been done yet.
 */

void findsat (void)
{
	firqb	f;

	if (sattsize) return;		/* don't do it twice */
	if (!findfile (&f, "[0,1]satt.sys"))
	{
		printf ("SATT.SYS not found\n");
		rabort(CORRUPT);
	}
	if ((f.stat & us_nox) == 0)
	{
		printf ("SATT.SYS is not contiguous\n");
		rabort(CORRUPT);
	}
	pcns = (diskblocks - dcs)/ pcs;	/* pack size in pack clusters */
	sattsize = UP(pcns,BLKSIZE*8);	/* round up to block boundary */
	sattsize /= BLKSIZE * 8;	/*  and change bits to blocks */
	if (f.size != sattsize)
	{
		printf ("Expected SATT.SYS size is %d, actual size is %ld\n",
		       sattsize, f.size);
		rabort(CORRUPT);
	}
	sattsize *= BLKSIZE;		/* now size in bytes */
	if ((sattbufp = (byte *) malloc (sattsize)) == NULL) rabort(NOMEM);
	readlk (f.rlink);
	sattlbn = dcntolbn(use(ufdre,k)->uent[0]);
	rread (sattlbn, sattsize, sattbufp);
	satptr = 0;			/* nothing allocated yet */
}

void rmount (void)			/* "mount" the rsts pack */
{
	ropen (DREADMODE);		/* open it first */
	readlabel ();			/* read and remember label data */
	if (pflags & uc_mnt)
		printf ("** warning: disk was not properly dismounted **\n");
	sattsize = 0;			/* SATT not looked up yet */
	womsat = FALSE;			/*  and SATT is clean */
	fiblkw = FALSE;			/*  FIBUF ditto */
}

void rmountrw (void)			/* mount the RSTS pack read/write */
{
	packlabel	*p;

	if (sw.debug != NULL)
		printf ("rmountrw()\n");
	ropen (DWRITEMODE);		/* open it first */
	readlabel ();			/* read and remember label data */
	if (pflags & uc_mnt)
		rabort(DIRTY);
	if ((pflags & uc_ro) && (sw.overwrite == NULL))
		rabort(ROPACK);
	sattsize = 0;			/* SATT not looked up yet */
	womsat = FALSE;			/*  and SATT is clean */
	fiblkw = FALSE;			/*  FIBUF ditto */
	findsat ();			/* make sure we know where satt is */
	readdcn (1);			/* read the pack label */
	p = use(packlabel,0);
	p->pstat |= uc_mnt;		/* mark mounted */
	if (plevel >= RDS12)		/* if new pack */
	{
		p->mntdat = curdate ();	/* update timestamps */
		p->mnttim = curtime ();
	}
	fbwrite ();			/* write it back */
}

void rumount (void)			/* dismount for users of rmount() */
{
	rclose ();			/* close the device */
}

void rumountrw (void)			/* dismount (no longer read/write) */
{
	packlabel	*p;

	if (sw.debug != NULL)
		printf ("rumount()\n");
	readdcn (1);			/* read the pack label */
	p = use(packlabel,0);
	if ((p->pstat & uc_mnt) == 0)
		rabort(INTERNAL);
	if (womsat)
		rwrite (sattlbn, sattsize, sattbufp);
	womsat = FALSE;			/* SATT written out if needed */
	free (sattbufp);		/* release satt memory copy */
	p->pstat &= ~uc_mnt;		/* mark no longer mounted */
	if (plevel >= RDS12)		/* if new pack */
	{
		p->mntdat = curdate ();	/* update timestamps */
		p->mnttim = curtime ();
	}
	fbwrite ();			/* write it back */
					/* this leaves FIBUF clean */
	rumount ();			/* now do common dismount */
}
