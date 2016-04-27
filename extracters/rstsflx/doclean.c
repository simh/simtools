/* handler for the "clean" (or "rebuild") command */

/* this code is inspired by the INICLN module of RSTS INIT, with some
 * significant changes:
 * 1. support for "read-only" clean, i.e., look, do not touch.
 * 2. directories are read in entirely and manipulated in memory.
 *    this speeds things up a lot and eliminates the need for the
 *    disk cache that ONLCLN maintains.
 * 3. blockette use status is kept in a separate bitmap rather than
 *    in the link words on disk.  this avoids two write passes and
 *    allows a totally read-only clean to be done.
 * 4. if a double allocation is found, the offending file is truncated
 *    (if it's a directory, it is zeroed).  you don't get the option
 *    to delete the file it conflicts with.  that saves a *lot* of code.
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "flx.h"
#include "fldef.h"
#include "diskio.h"
#include "filename.h"
#include "doclean.h"
#include "fip.h"
#include "rtime.h"

#define dirbufsize (7 * 16 * BLKSIZE)
#define dirmapsize (7 * 16 * BLKSIZE / sizeof (gfdne) / 8)
#define PPN11	0401

/* pointers to buffers used by clean.  to speed things up, we use
 * lots of memory and read whole file system structures into memory.
 * specifically we use:
 *	sattbf2	- storage allocation table (same form as sattbuf)
 *	badbmap	- map of bad blocks (same form as sattbuf)
 *	mfdbuf	- the entire MFD
 *	mfdmap	- bitmap of allocated MFD blockettes
 *	gfdbuf	- the entire current GFD
 *	gfdmap	- bitmap of allocated GFD blockettes
 *	ufdbuf	- the entire current UFD
 *	ufdmap	- bitmap of allocated UFD blockettes
 *
 * note: the blockette maps don't explicitly represent the label or
 * fdcm spots, since those are in use by definition.  however, the
 * label position (LSB of first byte) is used as a "dirty buffer"
 * marker for the corresponding directory buffer.
 */

#define MARK(map) *(map) |= 1

typedef enum
{
	ok, badblock, dup, badlink, nullink, other,
	range, align, delete
} retcode;

byte	*sattbf2 = NULL;
byte	*badbmap = NULL;
byte	*mfdbuf = NULL;
byte	*mfdmap = NULL;
byte	*gfdbuf = NULL;
byte	*gfdmap = NULL;
byte	*ufdbuf = NULL;
byte	*ufdmap = NULL;

byte	curproj, curprog;
char	curdir[10];
char	curfile[22];
long	filesize;
long	e;
long	gfds, ufds, files, clusters;
long	dirfiles, dirtsize;
int	badb, satt, init;		/* flags for special files */

/* get a yes or no answer; return true if yes, false if no.
 * loop until a valid answer is received.  the default is "no".
 * if -protect switch (read-only clean) was present, supply
 * a "yes" answer automatically (note that this won't actually
 * cause any changes, but it will allow the process to continue).
 */
static retcode yesno (void)
{
	char	c;
	char	answer[LSIZE];
	int	cmdlen;
	
	if (sw.prot != NULL)
	{
		printf ("Yes (read-only)\n");
		return other;
	}	
	for (;;)
	{
		if (fgets (answer, LSIZE, stdin) == NULL) return FALSE;
		cmdlen = strlen (answer);
		if (answer[cmdlen - 1] == '\n')
			answer[--cmdlen] = '\0';
		else
		{
			printf ("Reply too long\nYes or No? ");
			continue;
		}
		c = toupper (answer[0]);
		if (c == 'Y') return ok;
		else if (c == 'N' || c == '\0') return other;
		printf ("Invalid answer\nYes or No? ");
	}
}

/* similarly but a "no" answer aborts the clean */
static void yes (void)
{
	printf ("\nProceed (Yes or No)? ");
	if (yesno () == ok) return;
	printf ("Clean aborted\n");
	rabort (NOMSG);
}

/* this function reads an entire directory into the designated buffer.
 * it assumes the supplied dcn is valid.  it uses the first fdcm to
 * find all the other clusters, but does not do full validation on
 * the fdcm -- only enough to avoid making a mess of the reads.  
 * it's the caller's job to do more detailed checks.
 *
 * on the assumption that a directory often has a clustersize of 16,
 * and in any case may be more or less contiguous, we start with a 
 * read of 16 blocks.
 */
static void readdir (word dcn, byte *buf)
{
	int	i, clu;
	long	j, start, blks;
	fdcm	*m;
	byte	*p;

	if (sw.debug != NULL)
		printf ("\nreaddir(%d,%p)", dcn, buf);
	start = dcntolbn (dcn);
	if ((diskblocks - start) < 16)
		blks = diskblocks - start;
	else	blks = 16;
	if (blks < 1) rabort (INTERNAL);
	rread (start, BLKSIZE * blks, buf);
	m = (fdcm *) (buf + 0760);
	clu = m->uclus;
	if (dcn != m->uent[0] ||	/* some simple sanity checks... */
	    clu > blks ||
	    (pcs <= 16 && clu < pcs) ||
	    (clu & (-clu)) != clu)
		return;
	p = buf + (BLKSIZE * clu);	/* point to second cluster's data */
	i = 1;				/* and next r.e. index to use */
	if (clu != blks)		/* read more than a cluster? */
	{
		j = clu / dcs;		/* dcn increment if contiguous */
		for (;;)
		{
			if (i == 7 || i * clu >= blks) return;
			if (m->uent[i] == 0) return;
			if (m->uent[i] != m->uent[i - 1] + j) break;
			if (sw.debug != NULL)
				printf (" entry %d, dcn %d is contiguous",
					i, m->uent[i]);
			i++;
			p += BLKSIZE * clu;
		}
	}
	for ( ; i < 7; i++)
	{
		if (m->uent[i] == 0) return;	/* done */
		j = dcntolbn (m->uent[i]);
		if (sw.debug != NULL)
			printf (" read entry %d, dcn %ld, %d blocks",
				i, j, clu);
		if (j >= diskblocks) return;	/* out of range, quit */
		rread (j, BLKSIZE * clu, p);
		p += BLKSIZE * clu;
	}
}

/* this function writes back a directory.  no validation is done, since
 * that was all done before.
 */
static void writedir (byte *buf)
{
	int	i, clu;
	long	j;
	fdcm	*m;
	byte	*p;
		
	if (sw.prot != NULL) return;		/* read-only, don't write */
	m = (fdcm *) (buf + 0760);
	clu = m->uclus;
	for (i = 0 ; i < 7; i++)
	{
		if (m->uent[i] == 0) return;	/* done */
		j = dcntolbn (m->uent[i]);
		rwrite (j, BLKSIZE * clu, buf);
		buf += BLKSIZE * clu;
	}
}

/* this function is like the standard function ulk (in fip.c) except
 * that it operates on a previously read directory in its buffer.
 * i and k are set as usual; in addition, e (for "entry") is set to
 * be a byte offset to the entry.
 */

retcode ulk2 (word link, byte *buf)
{
	int	clu, blk;
	fdcm	*m;

	if (NULLINK (link)) return nullink;
	m = (fdcm *) (buf + 0760);
	k   = (link & ul_eno);			/* k = byte offset to entry */
	clu = (link & ul_clo) >> sl_clo;  	/* cluster number */
	blk = (link & ul_blo) >> sl_blo;	/* block in cluster */
	if (sw.debug != NULL) 
		printf ("\nulk2(%o), k=%lo, clu=%d, blk=%d, clumap=%d", 
			link, k, clu, blk, m->uent[clu]);
	if (blk >= m->uclus ||
	    clu > 6 ||
	    k == 0760 ||
	    m->uent[clu] == 0)
		return badlink;
	i = blk + dcntolbn(m->uent[clu]);	/* LBN of entry */
	e = k + (clu * m->uclus + blk) * BLKSIZE;
        return ok;				/* ok */
}

/* function to read an entry into fibuf given a link, with validity
 * checking but no abort for bad links.
 */
retcode readlkchk (word link)
{
	if (NULLINK(link)) return nullink;	/* indicate null link */
	if (ulk (link)) return badlink;		/* unpack and check link */
	fbread (i);				/* read the directory block */
	return ok;
}

/* function to mark a directory entry as allocated.  it takes a pointer
 * to the entry map to update.  the entry marked is the one most recently
 * referenced in a call to ulk2.
 */
static retcode allocent (byte *map)
{
	int	ent, bpos, m;
	
	if (e == 0 || k == 0760) rabort (INTERNAL);
	ent = e / sizeof (gfdne);		/* entry number */
	bpos = ent / 8;				/* byte offset */
	m = 1 << (ent % 8);			/* mask to touch */
	if (map[bpos] & m) return dup;		/* error if already marked */
	map[bpos] |= m;
	return ok;
}

/* similarly, mark entry as deallocated */
static void deallocent (byte *map)
{
	int	ent, bpos, m;
	
	if (e == 0 || k == 0760) rabort (INTERNAL);
	ent = e / sizeof (gfdne);		/* entry number */
	bpos = ent / 8;				/* byte offset */
	m = 1 << (ent % 8);			/* mask to touch */
	if (((map[bpos] & m) == 0))
		rabort (INTERNAL);		/* error if not marked */
	map[bpos] &= ~m;
}

/* mark a chain of directory blockettes as allocated (but do no
 * other processing on them).  this is used for attribute chains
 * and the like.  the "use" argument is non-zero to force the link
 * word of each entry to have the ul_use bit set.
 * it returns 0 if all is ok, or the last good link (i.e., the link for
 * the entry that contains the problem link) if there is a problem.
 * a suitable message is printed to indicate the problem.  if the
 * first link is bad, the return value is 1.  this allows the caller
 * to truncate the chain.  entries up to the problem spot are still
 * marked as allocated.
 */
static word allocchain (word link, byte *buf, byte * map, int use)
{
	word	prev = 1;
	retcode	st;
	
	for (;;)
	{
		st = ulk2 (link, buf);
		if (st == nullink) return 0;	/* end of chain, success */
		if (st == badlink) 
		{
			printf ("invalid link ");
			break;
		}
		/* ok, so the link is good.  does it point to a real entry? */
		if (*(lword32 *) (buf + e) == 0)
		{
			printf ("entry is a hole ");
			break;
		}
		st = allocent (map);
		if (st != ok)
		{
			printf ("doubly allocated entry ");
			break;
		}
		prev = link;
		link = *(word16 *) (buf + e);	/* follow link to next */
		if (use && (link & ul_use) == 0)
		{
			if (sw.debug != NULL)
				printf ("\nfixed link word for entry at offset %06lo\n",
					e);
			*(word16 *) (buf + e) |= ul_use;
			MARK (map);
		}
	}
	return prev;				/* we had a problem */
}

/* similarly, mark a chain as free. */
static void deallocchain (word link, byte *buf, byte * map)
{
	retcode	st;
	
	for (;;)
	{
		st = ulk2 (link, buf);
		if (st == nullink) return;	/* end of chain, done */
		if (st == badlink) rabort (INTERNAL);
		deallocent (map);
		link = *(word16 *) (buf + e);	/* follow link to next */
	}
}
		
/* function to mark a cluster as allocated.  this operates on sattbf2
 * (not sattbuf) -- it looks a lot like retclu except for the sense of
 * the change...
 */

static retcode alloc (word pos, int clusiz)
{
	long	m, n;
	byte	*s;
	int	bpos;
	retcode	ret = ok;

	if (sw.debug != NULL)
		printf ("\nalloc(%d,%d)", pos, clusiz);
	if (clusiz < dcs && dcs > 16)
		clusiz = dcs;		/* deal with directories on large disks */
	if ((pos - 1) % (clusiz / dcs))
		return align;
	pos = dcntopcn(pos);		/* convert to pcn */
	if (pos >= pcns)
		return range;
	clusiz /= pcs;			/* fcs as count of clusters */
	if (clusiz <= 0)
		rabort (INTERNAL);
	bpos = pos / 8;			/* byte offset to start of cluster */
	if (clusiz < 8)			/* scanning bitwise */
	{
		m = (1 << clusiz) - 1;	/* mask to match on */
		m <<= (pos % 8);	/* form starting bit field */
		if (badbmap[bpos] & m)
			ret = badblock;	/* bad block, remember it */
		if (sattbf2[bpos] & m)
			return dup;	/* double allocation */
		sattbf2[bpos] |= m;	/* ok, allocate it */
		clusters += clusiz;	/* update stats */
	}
	else				/* scanning whole bytes */
	{
		clusiz /= 8;		/* change to count of bytes to alloc */
		for (n = 0; n < clusiz; n++) 
			if (badbmap[bpos + n])
				ret = badblock;	/* bad block, remember it */
		for (n = 0; n < clusiz; n++) 
			if (sattbf2[bpos + n])
				return dup;	/* double allocation */
		for (n = 0; n < clusiz; n++) 
			sattbf2[bpos + n] = 0xff;	/* ok, allocate it */
		clusters += clusiz * 8;	/* update stats */
	}
	
	return ret;
}

/* sometimes we have to deallocate something -- to clean up from
 * allocating something that then messes up halfway.
 * this code is straight from fip.c retclu except for the use of
 * a different buffer.
 */
static void dealloc (word pos, int clusiz)
{
	long	m, n;
	byte	*s;

	if (sw.debug != NULL)
		printf ("\ndealloc(%o,%d)", pos, clusiz);
	if (sattsize == 0)
		rabort (INTERNAL);
	pos = dcntopcn(pos);		/* convert to pcn */
	clusiz /= pcs;			/* fcs as count of clusters */
	if (clusiz == 0 && pcs > 16)
		clusiz = 1;		/* handle directories on large disks */
	if (clusiz <= 0)
		rabort (INTERNAL);
	s = sattbf2 + pos / 8;		/* byte pointer to start of cluster */
	if (clusiz < 8)			/* scanning bitwise */
	{
		m = (1 << clusiz) - 1;	/* mask to match on */
		m <<= (pos % 8);	/* form starting bit field */
		*s &= ~m;		/* free this cluster */
	}
	else				/* scanning whole bytes */
	{
		clusiz /= 8;		/* change to count of bytes to free */
		for (n = 0; n < clusiz; n++)
			*s++ = 0;
	}
}

/* common directory cleanup
 * this function reads a directory into the indicated buffer, clears
 * out the blockette map, and validates all the fdcm's.  if it's happy,
 * it returns ok or badblock; if an uncorrectable problem is found it
 * generates a suitable message, deallocates the directory clusters,
 * and returns the corresponding error code.
 *
 * if the clustersize argument is non-zero, it is checked against the
 * fdcm.  otherwise, the clustersize found in the first fdcm is used.
 * (the latter applies to gfd/mfd, for which the clustersize is stored
 * nowhere else.)
 */
static retcode cleandir (word dcn, int clu,
			 byte *buf, byte *map,
			 byte proj, byte prog)
{
	fdcm	*m;
	fdcm	*first;
	int	i, j, ent, dirblks;
	retcode	st, ret = ok;
	mfdlabel *l;
	word16	id;
	
	/* figure out what we're cleaning */
	if (prog == 255)
	{
		if (proj == 255)
		{
			id = MFD;
			strcpy (curdir, "[*,*]");
		}
		else
		{
			id = GFD;
			sprintf (curdir, "[%d,*]", proj);
		}
	}
	else
	{
		id = UFD;
		sprintf (curdir, "[%d,%d]", proj, prog);
	}
	curproj = proj;
	curprog = prog;
	printf ("\rprocessing %s                ", curdir);
	fflush (stdout);
	
	if (dcntopcn (dcn) >= pcns)
	{
		printf ("\nstarting dcn %d for %s out of range\n",
			dcn, curdir);
		return range;
	}
	memset (map, 0, dirmapsize);
	readdir (dcn, buf);
	m = (fdcm *) (buf + 0760);
	if (clu == 0) clu = m->uclus;
	if (m->uclus != clu ||
	    clu > 16 ||
	    (clu < 4 && id != UFD) ||
	    (pcs <= 16 && clu < pcs) ||
	    (pcs > 16 && clu != 16) ||
	    (clu & (-clu)) != clu)
	{
		printf ("\ninvalid clustersize %d for %s\n", clu, curdir);
		return other;
	}
	if (m->uent[0] != dcn)
	{
		printf ("\nstarting dcn %d for %s doesn't match that in cluster map\n",
			dcn, curdir);
		return other;
	}

	/* in old disk, [1,1] label is pack label, don't touch 
	 * otherwise do some checks and fixups.
	 */
	if (plevel != RDS0 || proj != 1 || prog != 1)
	{
		l = (mfdlabel *) buf;
		if (l->lppn[1] != proj || l->lppn[0] != prog)
		{
			l->lppn[1] = proj;
			l->lppn[0] = prog;
			MARK (map);
			if (sw.verbose != NULL)
				printf ("\nfixed ppn in directory label for %s\n",
					curdir);
		}
		if (l->lid != id)
		{
			l->lid = id;
			MARK (map);
			if (sw.verbose != NULL)
				printf ("\nfixed identifier field in directory label for %s\n",
					curdir);
		}
		if (l->fill2 != -1 || (l->fill1 & ul_use) == 0)
		{
			l->fill1 |= ul_use;
			l->fill2 = -1;
			MARK (map);
			if (sw.debug != NULL)
				printf ("\nfixed reserved field in directory label for %s\n",
					curdir);
		}
	}
	
	/* go check the fdcm's in detail */
	if (id == UFD) i = 0;
	else i = fd_new;
	if (m->uflag != i)
	{
		m->uflag = i;
		MARK (map);
		if (sw.verbose != NULL)
			printf ("\nfixed directory type flag bit in cluster map for %s\n",
				curdir);
	}
	ent = 0;
	/* look for holes in cluster map */
	for (i = 6; i >= 0; i--)
	{
		if (m->uent[i] == 0)
		{
			if (ent != 0)
			{
				printf ("\nholes in cluster map for %s\n", 
					curdir);
				return other;
			}
		}
		else if (dcntopcn (m->uent[i]) >= pcns)
		{
			printf ("\ncluster %d out of range in cluster map for %s\n",
				m->uent[i], curdir);
			return range;
		}
		else
			ent = i;
	}
	if (ent == 0 && m->uent[0] == 0)
	{
		printf ("\ndirectory %s is empty\n", curdir);
		return other;
	}
	dirblks = ent * clu;		/* count of used directory blocks */
	first = m;			/* save copy to first fdcm */
	ent = -1;			/* no map mismatches yet */
	for (i = 1; i < dirblks; i++)
	{
		m = (fdcm *) (buf + (i * BLKSIZE) + 0760);
		if (memcmp (first, m, sizeof (fdcm)) == 0) continue;
		if (first->uclus != m->uclus)
		{
			printf ("\ninconsistent cluster sizes in maps for %s\n",
				curdir);
			return other;
		}
		if (first->uflag != m->uflag)
		{
			m->uflag = first->uflag;	/* fix this quietly */
			MARK (map);
		}
		for (j = 0; j < 7; j++)
		{
			if (first->uent[j] == m->uent[j]) continue;
			if (first->uent[j] == 0 &&
			    (ent == j || ent == -1))
			{
				ent = j;
				continue;
			}
			printf ("\ninconsistent cluster maps for %s\n", curdir);
			return other;
		}
	}

	/* things look cool, go mark the directory as allocated */
	for (i = 0; i < 7; i++)
	{
		if (first->uent[i] == 0) break;
		st = alloc (first->uent[i], clu);
		switch (st)
		{
		    case ok:
			continue;
		    case badblock:
			printf ("\nbad block");
			ret = badblock;
			break;
		    case dup:
			printf ("\ndoubly allocated block");
			break;
		    case align:
			printf ("\nmisaligned cluster");
			break;
		    case range:
			printf ("\ncluster number out of range");
			break;
		    default: rabort (INTERNAL);
		}
		printf (", dcn %d in directory %s\n", first->uent[i], curdir);
		if (st != badblock)	/* bad blk is warning, others quit */
		{
			while (i > 0) dealloc (first->uent[--i], clu);
			return st;
		}
	}
	return ret;
}

/* finish up cleaning a directory
 * this wipes out any unallocated blockettes.
 */
static void finishdir (byte *buf, byte *map)
{
	lword32	*l;
	lword32	or;
	int	clu, blk, off;
	fdcm	*m;
	byte	*p;
	byte	bit;
	mfdlabel *lb;
	int	ufd;
	
	/* figure out what we're cleaning */
	lb = (mfdlabel *) buf;
	if (lb->lppn[0] == 255)
	{
		ufd = 0;
		if (lb->lppn[1] == 255)
			strcpy (curdir, "[*,*]");
		else	sprintf (curdir, "[%d,*]", lb->lppn[1]);
	}
	else
	{
		ufd = 1;
		sprintf (curdir, "[%d,%d]", lb->lppn[1], lb->lppn[0]);
	}
	m = (fdcm *) (buf + 0760);
	l = (lword32 *) buf;
	p = map;
	bit = 1;
	for (clu = 0; clu < 7; clu++)
	{
		if (m->uent[clu] == 0) break;
		for (blk = 0; blk < m->uclus; blk++)
			for (off = 0; off < BLKSIZE; off += sizeof (gfdne))
			{
				/* check if it's a special spot */
				if (off == 0760 ||
				    (!ufd && (clu == 0 &&
					      (blk == 1 || blk == 2))) ||
				    (off == 0 && clu == 0 && blk == 0))
				{
					l += 4;
				}
				else if ((*p & bit) != 0)	/* in use */
				{
					if (*(byte *) l & ul_cln)
					{
						*(byte *) l &= ~ul_cln;
						MARK (map);
						if (sw.debug != NULL)
							printf ("\ncleared ul.cln bit in %s at %d, %d, %d\n",
								curdir, clu, blk, off);
					}
					l += 4;
				}
				else
				{
					or = l[0] | l[1] | l[2] | l[3];
					if (or)
					{
						*l++ = 0;
						*l++ = 0;
						*l++ = 0;
						*l++ = 0;
						if (sw.debug != NULL)
							printf ("\ncleaned up free entry in %s at %d, %d, %d\n",
								curdir, clu, blk, off);
						MARK (map);
					}
					else
					{
						l += 4;
					}
				}
				if (bit == 0x80)
				{
					p++;
					bit = 1;
				}
				else	bit <<= 1;
				continue;
			}
	}
	/* if directory needs writing, write it */
	if (*map & 1)
	{
		printf ("\rwriting %s                     ", curdir);
		fflush (stdout);
		writedir (buf);
	}
}

/* deallocate a directory (used when we zero it) */
static void deallocdir (byte *buf)
{
	int	i;
	fdcm	*m;
	
	m = (fdcm *) (buf + 0760);
	for (i = 0; i < 7; i++)
	{
		if (m->uent[i]) 
			dealloc (m->uent[i], m->uclus);
		else break;
	}
}

/* process a file.
 *
 * returns "ok" if things worked, something else if not.  an appropriate
 * message is printed if necessary.  if the status code is "delete", the
 * caller should just delete the file; otherwise, ask first.
 *
 * note: UFD entries in [1,1] for old disks don't come here.  also: the
 * caller has to validate the n.e. link and allocate the n.e.
 */
static retcode cleanfile (ufdne *n, byte *buf, byte *map)
{
	retcode	st, ret = ok;
	ufdae	*a;
	ufdre	*r;
	ufdlabel *l;
	int	clu, i, clurat, sysfile;
	int	hole = 0, badflag = 0, contig = 1, prevdcn = 0;
	word	prev;
	char	name[11];
	long	dfsize;			/* file size currently in dir */
	
	memset (name, 0, sizeof (name));
	init = satt = badb = 0;		/* not one of the special files */
	filesize = 0;
	r50filename (n->unam, name, 0);
	sprintf (curfile, "%s%s", curdir, name);
	if (curproj == 0 && curprog == 1)
	{
		badb = (strcmp (name, "badb.sys") == 0);
		satt = (strcmp (name, "satt.sys") == 0);
		init = (strcmp (name, "init.sys") == 0);
	}
	sysfile = badb || init || satt || (n->ustat & us_nok);
	printf ("\rprocessing %s      ", curfile);
	if (n->ustat & us_del)			/* marked for delete */
	{
		if (sw.verbose != NULL)
			printf ("\nfile %s is marked for delete\n", curfile);
		return delete;
	}
	if (n->ustat & us_ufd)			/* bogus ufd-like entry */
	{
		printf ("\nentry for file %s looks like a ufd\n", curfile);
		return other;
	}
	if (n->unam[2] == TMP)			/* .rad50 "tmp" */
	{
		if (sw.verbose != NULL)
			printf ("\nfile %s deleted\n", curfile);
		return delete;
	}
	st = ulk2 (n->uaa, buf);
	if (st != ok)
	{
		printf ("\ninvalid account entry link for file %s\n", curfile);
		return badlink;
	}
	a = (ufdae *) (buf + e);
	clu = a->uclus;
	if (clu > 256 ||
	    clu < pcs ||
	    (clu & (-clu)) != clu ||
	    (clu != pcs && satt))	/* badb.sys we checked earlier */
	{
		printf ("\nbad clustersize %d for file %s\n", clu, curfile);
		return other;
	}
	clurat = clu / dcs;		/* dcn delta if contiguous */
	st = allocent (map);
	if (st != ok)
	{
		printf ("\naccount entry for file %s is doubly allocated\n",
			curfile);
		return st;
	}
	if ((a->ulnk & ul_use) == 0)
	{
		if (sw.debug != NULL) 
			printf ("\nfixed link from account entry of file %s\n",
				curfile);
		a->ulnk |= ul_use;
		MARK (map);
	}
	prev = allocchain (a->ulnk, buf, map, ul_use);
	if (prev != 0)
	{
		printf (" in attributes chain for file %s -- will be truncated",
			curfile);
		if (sysfile) rabort (BADPAK);
		if (prev == 1)
			a->ulnk = (a->ulnk & ~LINKBITS) | ul_use;
		else 
		{
			ulk2 (prev, buf);
			*(word16 *) (buf + e) &= ~LINKBITS;
		}
		MARK (map);
	}
	prev = allocchain (n->uar, buf, map, 0);
	if (prev != 0)
	{
		printf (" in retrieval entry chain for file %s\n", curfile);
		if (sysfile) rabort (BADPAK);
		printf ("file will be truncated");
		if (prev == 1) n->uar = 0;
		else 
		{
			ulk2 (prev, buf);
			*(word16 *) (buf + e) = 0;
		}
		MARK (map);
	}
	/* we've allocated all the blockettes, now walk the r.e. chain
	 * again to allocate the clusters and accumulate the file size 
	 */
	st = ulk2 (n->uar, buf);
	while (st != nullink)
	{
		if (st != ok) rabort (INTERNAL);
		r = (ufdre *) (buf + e);
		if (prevdcn == 0) prevdcn = r->uent[0] - clurat;
		for (i = 0; i < 7; i++)
		{
			if (r->uent[i] == 0) break;
			if (contig)
			{
				contig = (prevdcn + clurat == r->uent[i]);
				prevdcn = r->uent[i];
			}
			if (badb || (n->ustat & us_out))
				st = ok;
			else	st = alloc (r->uent[i], clu);
			switch (st)
			{
			    case ok:
				filesize += clu;
				continue;
			    case badblock:
 				if (!badflag)
				{
					printf ("\nbad block in file %s, cluster %d (block %ld of the file)\n",
						curfile, r->uent[i],
						filesize + 1);
					if (!sysfile)
					{
						printf ("do you want it truncated (if not, it will be flagged)? ");
						if (yesno () == ok) break;
					}
					badflag = 1;
				}
				filesize += clu;
				continue;
			    case dup:
				printf ("\ndoubly allocated block");
				break;
			    case align:
				printf ("\nmisaligned cluster");
				break;
			    case range:
				printf ("\ncluster number out of range");
				break;
			    default:
				rabort (INTERNAL);
			}
			printf (", dcn %d in file %s", r->uent[i], curfile);
			if (sysfile) rabort (BADPAK);
			printf (" -- file will be truncated\n");
			while (i < 7) r->uent[i++] = 0;
			r->ulnk = 0;
			break;
		}
		if (i < 7)		/* if we stopped on eof */
		{
			for ( ; i < 7; i++)
			{
				hole |= r->uent[i];
				r->uent[i] = 0;
			}
			hole |= (r->ulnk & LINKBITS);
			if (hole != 0)
			{
				printf ("\nholes in retrieval entry for %s",
					curfile);
				if (sysfile) printf ("\n");
				else
				{
					printf (" fixed");
					r->ulnk = 0;
					MARK (map);
				}
			}
		}
		st = ulk2 (r->ulnk, buf);
	}
	dfsize = a->usiz;		/* pick up current size */
	if (a->urts[0] == 0)		/* possible large file */
		dfsize += a->urts[1] << 16;
	if (satt)			/* satt.sys gets a few checks more */
	{
		if (!contig)
		{
			printf ("\n[0,1]satt.sys is not contiguous\n");
			rabort (BADPAK);
		}
		if (filesize * BLKSIZE * 8 < pcns)
		{
			printf ("\n[0,1]satt.sys is too small\n");
			rabort (BADPAK);
		}
	}
	if (!badb && ((n->ustat & us_out) == 0))
	{
		if (filesize != ((dfsize + clu - 1) & (-clu)))
		{
			printf ("\nfile %s size %ld is incorrect (%ld) in ufd",
				curfile, filesize, dfsize);
			if (sysfile && !satt) printf ("\n");
			else
			{
				printf (" -- fixed");
				a->usiz = filesize;
				if (filesize >> 16)
				{
					a->urts[0] = 0;
					a->urts[1] = filesize >> 16;
				}
				else if (a->urts[0] == 0) a->urts[1] = 0;
				MARK (map);
			}
		}
	}
	if (badflag)
	{
		if ((a->ulnk & ul_bad) == 0)
		{
			a->ulnk |= ul_bad;
			MARK (map);
		}
	}
	else if (a->ulnk & ul_bad)
	{
		a->ulnk &= ~ul_bad;
		MARK (map);
	}
	if (!contig && (n->ustat & us_nox))
	{
		printf ("\nfile %s is marked as contiguous but is not\n",
			curfile);
		n->ustat &= ~us_nox;
		MARK (map);
	}
	if (n->ustat & (us_wrt | us_upd))
	{
		if (sw.debug != NULL)
			printf ("\ncleared write/update flags for file %s\n",
				curfile);
		n->ustat &= ~(us_wrt | us_upd);
		MARK (map);
	}
	if ((filesize >> 16) != 0 && (n->uprot & up_run) != 0)
	{
		if (sw.debug != NULL)
			printf ("\ncleared runnable bit for file %s\n",
				curfile);
		n->uprot &= ~up_run;
		MARK (map);
	}
	if ((n->uaa & ~(LINKBITS | ul_che)) != 0 ||
	    (n->uar & ~LINKBITS) != 0 ||
	    (n->ulnk & ~(LINKBITS | ul_che)) != 0)
	{
		if (sw.debug != NULL)
			printf ("\nfixed links in n.e. for file %s\n",
				curfile);
		n->uaa &= LINKBITS | ul_che;
		n->uar &= LINKBITS;
		n->ulnk &= LINKBITS | ul_che;
		MARK (map);
	}
	return ok;
}
	
/* process the content of a ufd.  this is separate from cleanufd so
 * we can call it for the MFD [1,1] of an RDS0 disk.
 * the buf and map pointers point to where the data and blockette maps
 * are, normally ufdbuf and ufdmap but mfdbuf and mfdmap for the
 * special case.
 */
static void processufd (byte *buf, byte *map)
{
	word	prev, link;
	retcode	st;
	ufdlabel *l;
	ufdne	*n;

	l = (ufdlabel *) buf;
	link = l->ulnk;
	prev = 1;
	for (;;)
	{
		st = ulk2 (link, buf);
		if (st == nullink) break;	/* all done */
		if (st == ok)
			st = allocent (map);	/* allocate n.e. */
		if (st != ok)			/* link bad or duplicate */
		{
			if (prev == 1)
			{
				printf ("\nbad link in label of %s", curdir);
				printf (" -- UFD will be zeroed");
				yes ();		/* ask for permission */
				l->ulnk = 0;
			}
			else
			{
				printf ("\nbad next file link in file %s"
					" -- remaining files will be lost",
					curfile);
				yes ();		/* ask for permission */
				ulk2 (prev, buf);
				n = (ufdne *) (buf + e);
				n->ulnk = 0;
			}
			MARK (map);
			break;
		}
		n = (ufdne *) (buf + e);
		st = cleanfile (n, buf, map);
		if (st != ok)
		{
			if (st != delete)
			{
				if (satt || init || badb ||
				    (n->ustat & us_nok))
				{
					printf ("\nfile %s cannot be deleted\n",
						curfile);
					rabort (BADPAK);
				}
				yes ();			/* ask for ok */
			}
			link = n->ulnk;			/* set next file */
			if (prev == 1) l->ulnk = link;
			else
			{
				ulk2 (prev, buf);
				n = (ufdne *) (buf + e);
				n->ulnk = link;
			}
		}
		else
		{
			files++;
			dirfiles++;
			dirtsize += filesize;
			prev = link;
			link = n->ulnk;
		}
	}
}

/* go clean a ufd */
static retcode cleanufd (word dcn, int proj, int prog)
{
	retcode	st, ret = ok;
	word	prev;
	
	dirfiles = dirtsize = 0;
	if (dcn == 0) return 0;			/* nothing there... */
	st = cleandir (dcn, 0, ufdbuf, ufdmap, proj, prog);
	if (st != ok)
	{
		if (st != badblock) return st;
		if (proj == 0 && prog == 1)
		{
			printf ("\nclean will continue, but there may be problems\n");
			yes ();				/* get approval */
		}
		else
		{
			printf ("\ncontinue cleaning [%d,%d] anyway? ",
				proj, prog);
			if (yesno () != ok)
			{
				deallocdir (ufdbuf);
				return other;
			}
		}
		ret = st;
	}

	/* go process the ufd contents */
	processufd (ufdbuf, ufdmap);
	finishdir (ufdbuf, ufdmap);
	ufds++;
	return ret;				/* success */
}

/* go clean a gfd */
static int cleangfd (word dcn, retcode proj)
{
	int	prog;
	retcode	st, ret = ok;
	word	prev;
	word16	*dcnp, *unep;
	gfdne	*n;
	gfdae	*a;
	uattr	*at;
	ua_quo	*q;
	
	if (dcn == 0) return 0;			/* nothing there... */
	st = cleandir (dcn, 0, gfdbuf, gfdmap, proj, 255);
	if (st != ok)
	{
		if (st != badblock) return st;
		if (proj == 0)
		{
			printf ("\nclean will continue, but there may be problems\n");
			yes ();				/* get approval */
		}
		else
		{
			printf ("\ncontinue cleaning [%d,*] anyway? ", proj);
			if (yesno () != ok)
			{
				deallocdir (gfdbuf);
				return other;
			}
		}
		ret = st;
	}
	
	/* check that there isn't an entry for user 255 */
	if (*(word16 *) (gfdbuf + 01776) != 0 ||
	    *(word16 *) (gfdbuf + 02776) != 0)
	{
		printf ("\ninvalid user [%d,255] found in gfd -- deleted\n",
			proj);
		*(word16 *) (gfdbuf + 01776) = 0;
		*(word16 *) (gfdbuf + 02776) = 0;
		MARK (gfdmap);
	}
	
	/* check that there isn't an entry for [0,0] either */
	if (proj == 0 &&
	    (*(word16 *) (gfdbuf + 01000) != 0 ||
	     *(word16 *) (gfdbuf + 02000) != 0))
	{
		printf ("\ninvalid user [0,0] found in gfd [0,*] -- deleted\n");
		*(word16 *) (gfdbuf + 01000) = 0;
		*(word16 *) (gfdbuf + 02000) = 0;
		MARK (gfdmap);
	}
	
	/* now process each user */
	for (prog = 0; prog < 255; prog++)
	{
		dcnp = (word16 *) (gfdbuf + 01000 + (2 * prog));
		unep = (word16 *) (gfdbuf + 02000 + (2 * prog));
		if (*unep & ~LINKBITS)
		{
			if (sw.debug != NULL)
				printf ("\nname entry link for [%d,%d] fixed\n",
					proj, prog);
			*unep &= LINKBITS;
			MARK (gfdmap);
		}
		st = ulk2 (*unep, gfdbuf);
		if (st == nullink)		/* no n.e. link */
		{
			if (*dcnp != 0)
			{
				*dcnp = 0;
				MARK (gfdmap);
				printf ("\ndcn pointer for non-existent account [%d,%d zeroed\n",
					proj, prog);
			}
			continue;
		}
		if (st)				/* bad link! */
		{
			printf ("\nname entry link for [%d,%d] is bad\n",
				proj, prog);
			if (proj == 0 && prog == 1) rabort (BADPAK);
			printf ("account will be deleted\n");
			yes ();			/* get approval */
			*unep = 0;
			*dcnp = 0;
			MARK (gfdmap);
			continue;
		}
		n = (gfdne *) (gfdbuf + e);
		if (n->ustat & us_del)		/* account marked for delete */
		{
			if (sw.verbose != NULL ||
			    (proj == 0 & prog == 1))
				printf ("\naccount [%d,%d] marked for delete\n",
					proj, prog);
			if (proj == 0 && prog == 1) rabort (BADPAK);
			*unep = 0;		/* so delete it... */
			*dcnp = 0;
			MARK (gfdmap);
			continue;
		}
		if ((n->ustat & us_ufd) == 0)	/* not marked as ufd! */
		{
			printf ("\nname entry for [%d,%d] not marked as ufd\n",
				proj, prog);
			if (proj == 0 && prog == 1) rabort (BADPAK);
			printf ("account will be deleted\n");
			yes ();			/* get approval */
			*unep = 0;
			*dcnp = 0;
			MARK (gfdmap);
			continue;
		}
		if (n->unam[0] != ((proj << 8) | prog))
		{
			printf ("\nname entry for [%d,%d] has wrong ppn [%d,%d]\n",
				proj, prog,
				n->unam[0] >> 8, n->unam[0] & 0xff);
			if (proj == 0 && prog == 1) rabort (BADPAK);
			*unep = 0;		/* so delete it... */
			*dcnp = 0;
			MARK (gfdmap);
			continue;
		}
		allocent (gfdmap);		/* allocate name entry */
		st = ulk2 (n->uaa, gfdbuf);
		if (st != ok)			/* something wrong with a.e. */
		{
			printf ("\naccount entry link for [%d,%d] is bad\n",
				proj, prog);
			if (proj == 0 && prog == 1) rabort (BADPAK);
			printf ("account will be deleted\n");
			yes ();			/* get approval */
			ulk2 (*unep, gfdbuf);	/* mark n.e. free */
			deallocent (gfdmap);
			*unep = 0;
			*dcnp = 0;
			MARK (gfdmap);
			continue;
		}
		a = (gfdae *) (gfdbuf + e);
		if (NULLINK (a->ulnk))
		{
			if (a->ulnk != ul_use)
			{
				if (sw.debug != NULL)
					printf ("\nlink from account entry for [%d,%d] fixed\n",
						proj, prog);
				a->ulnk = ul_use;
				MARK (gfdmap);
			}
		}
		else
		{
			printf ("\nlink from account entry for [%d,%d] cleared\n",
				proj, prog);
			a->ulnk = ul_use;
			MARK (gfdmap);
		}
		allocent (gfdmap);		/* allocate acct. entry */
		q = NULL;			/* assume no quota block */
		if (plevel == RDS12 && (proj != 0 || prog != 1))
		{
			st = ulk2 (n->ulnk, gfdbuf);	/* get attributes */
			while (st == ok)
			{
				at = (uattr *) (gfdbuf + e);
				if (at->uatyp == aa_quo)
				{
					q = (ua_quo *) at;
					break;
				}
				st = ulk2 (at->ulnk, gfdbuf);
			}
			if (q == NULL)
			{
				printf ("\nquota block missing for [%d,%d]\n"
					"account will be deleted\n",
					proj, prog);
				yes ();			/* get approval */
				ulk2 (n->uaa, gfdbuf);	/* mark a.e. free */
				deallocent (gfdmap);
				ulk2 (*unep, gfdbuf);	/* mark n.e. free */
				deallocent (gfdmap);
				*unep = 0;
				*dcnp = 0;
				MARK (gfdmap);
				continue;
			}
		}
		prev = allocchain (n->ulnk, gfdbuf, gfdmap, 0);
		if (prev)
		{
			/* note that the problem is after the quota block,
			 * for cases where we need one.  that's because
			 * the quota block scan is before this point.
			 */
			printf (" in account [%d,%d] attribute chain -- will be truncated\n",
				proj, prog);
			yes ();			/* get approval */
			if (prev == 1) n->ulnk = 0;
			else 
			{
				ulk2 (prev, gfdbuf);
				*(word16 *) (gfdbuf + e) = ul_use;
			}
			MARK (gfdmap);
		}
		if (*dcnp != n->uar)		/* inicln doesn't do this... */
		{
			n->uar = *dcnp;
			MARK (gfdmap);
			if (sw.verbose != NULL)
				printf ("\nfixed account [%d,%d] dcn in n.e.\n",
					proj, prog);
		}
		st = cleanufd (*dcnp, proj, prog);
		if (st == badblock)		/* bad block but it worked */
		{
			a->ulnk |= ul_bad;	/* mark bad block in account */
			MARK (gfdmap);
		}
		else if (st != ok)
		{
			if (proj == 0 && prog == 1) rabort (BADPAK);
			printf ("\naccount [%d,%d] will be zeroed\n", 
				proj, prog);
			yes ();				/* get approval */
			*dcnp = 0;
			MARK (gfdmap);
		}
		if ((st == ok || st == badblock) && q != NULL)
		{
			if (((q->aq_crm << 16) | q->aq_crl) != dirtsize)
			{
				if (sw.debug != NULL)
					printf ("\naccount [%d,%d] usage fixed, was %d now %ld\n",
						proj, prog,
						((q->aq_crm << 16) | q->aq_crl),
						dirtsize);
				q->aq_crm = dirtsize >> 16;
				q->aq_crl = dirtsize & 0xffff;
				MARK (gfdmap);
			}
		}
		if (sw.verbose != NULL)
			printf ("\ntotal of %ld files, %ld blocks in [%d,%d]\n",
				dirfiles, dirtsize, proj, prog);
	}
	finishdir (gfdbuf, gfdmap);
	gfds++;
	return ret;				/* success */
}

/* go clean the mfd for a new format (RDS1.1 or above) disk */
static void cleannewmfd (void)
{
	int	proj;
	retcode	st;
	word	prev;
	mfdlabel *l;
	word16	*dcnp, *attrp;
	
	st = cleandir (mfddcn, 0, mfdbuf, mfdmap, 255, 255);
	if (st != ok)
	{
		if (st != badblock) rabort (BADPAK);
		printf ("\nclean will continue, but there may be problems\n");
		yes ();				/* get approval */
	}
	l = (mfdlabel *) mfdbuf;

	/* allocate any mfd attributes */
	prev = allocchain (l->malnk, mfdbuf, mfdmap, 0);
	if (prev)
	{
		printf (" in mfd attribute chain -- will be truncated\n");
		yes ();				/* get approval */
		if (prev == 1) l->malnk = 0;
		else 
		{
			ulk2 (prev, mfdbuf);
			*(word16 *) (mfdbuf + e) = ul_use;
		}
		MARK (mfdmap);
	}
	
	/* check that there isn't an entry for group 255 */
	if (*(word16 *) (mfdbuf + 01776) != 0 ||
	    *(word16 *) (mfdbuf + 02776) != 0)
	{
		printf ("\ninvalid group [255,*] found in mfd -- deleted\n");
		*(word16 *) (mfdbuf + 01776) = 0;
		*(word16 *) (mfdbuf + 02776) = 0;
		MARK (mfdmap);
	}
	
	/* now process each group */
	for (proj = 0; proj < 255; proj++)
	{
		dcnp = (word16 *) (mfdbuf + 01000 + (2 * proj));
		attrp = (word16 *) (mfdbuf + 02000 + (2 * proj));
		if (*attrp & ~LINKBITS)
		{
			if (sw.debug != NULL)
				printf ("\nattribute link for group %d fixed\n",
					proj);
			*attrp &= LINKBITS;
			MARK (mfdmap);
		}
		prev = allocchain (*attrp, mfdbuf, mfdmap, 0);
		if (prev)
		{
			yes ();				/* get approval */
			printf (" in group [%d,*] attribute chain -- will be truncated\n",
				proj);
			if (prev == 1) *attrp = 0;
			else 
			{
				ulk2 (prev, mfdbuf);
				*(word16 *) (mfdbuf + e) = ul_use;
			}
			MARK (mfdmap);
		}
		st = cleangfd (*dcnp, proj);
		if (st != ok && st != badblock)
		{
			if (proj == 0) rabort (BADPAK);
			printf ("\ngfd [%d,*] will be zeroed\n", proj);
			yes ();				/* get approval */
			*dcnp = 0;
			MARK (mfdmap);
		}
	}
	finishdir (mfdbuf, mfdmap);
}

/* go clean the mfd for an old format (RDS0) disk */
static void cleanoldmfd (void)
{
	retcode	ret;
	
	ret = cleandir (mfddcn, 0, mfdbuf, mfdmap, 1, 1);
	if (sw.debug != NULL)
		printf ("\ncleandir returned %d\n", ret);
	finishdir (mfdbuf, mfdmap);
}

/* set up the bitmap of bad clusters
 * note that these are not marked (for the moment) in sattbf2 so they
 * don't appear as double allocations if a file contains a bad block.
 * instead, we check for the bad block map separately.
 */
static void allocbadb (void)
{
	firqb	f;
	ufdre	*r;
	ufdae	*a;
	int	i, clurat, fsize = 0;
	retcode	st;
	word	link, dcn;
	word16	*prevlink;
	
	memset (badbmap, 0, sattsize);		/* initially no bad blocks */
	if (!findfile (&f, "[0,1]badb.sys"))
	{
		printf ("\n[0,1]badb.sys is missing!\n");
		rabort (BADPAK);
	}
	if (f.clusiz != pcs)
	{
		printf ("\n[0,1]badb.sys clustersize is invalid (%d should be %d)",
			f.clusiz, pcs);
		rabort (BADPAK);
	}
	clurat = pcs / dcs;
	link = f.rlink;
	readlk (f.nlink);
	prevlink = &(use (ufdne, k)->uar);	/* in case link is bad */
	for (;;)
	{
		st = readlkchk (link);
		if (st == nullink) break;	/* null link */
		else if (st != ok)
		{
			printf ("\nbad link in [0,1]badb.sys -- truncated\n");
			if (sw.prot == NULL)
			{
				*prevlink = 0;
				fbwrite ();
			}
			break;
		}
		r = use (ufdre, k);
		for (i = 0; i < 7; i++)
		{
			dcn = r->uent[i];
			if (dcn == 0) break;
			fsize += pcs;		/* accumulate badb.sys size */
			if (dcn % clurat)	/* check for PCS boundary */
			{
				printf ("\nwarning, [0,1]badb.sys dcn %d not on PCS boundary\n", dcn);
				dcn = dcn / clurat * clurat;
			}
			st = alloc (dcn, pcs);
			if (st == 3)
			{
				printf ("\nwarning, [0,1]badb.sys dcn %d is too large\n", dcn);
			}
			else if (st)
			{
				printf ("\nwarning, [0,1]badb.sys dcn %d doubly allocated\n", dcn);
			}
		}
		prevlink = &(r->ulnk);
		link = r->ulnk;
	}
	readlk (f.alink);
	a = use (ufdae, k);
	if (a->usiz != fsize)
	{
		if (sw.prot == NULL)
		{
			a->usiz = fsize;
			fbwrite ();
			if (sw.verbose != NULL)
				printf ("\n[0,1]badb.sys filesize fixed\n");
		}
		else	printf ("\nnote: [0,1]badb.sys filesize doesn't match r.e. chain\n");
	}
	memcpy (badbmap, sattbf2, sattsize);	/* load the bad block map */
	memset (sattbf2, 0xff, sattsize);	/* and make all free again */
	memset (sattbf2, 0, pcns / 8);
	if (pcns & 7) sattbf2[pcns / 8] = 0xff << (pcns & 7);
	if (sw.verbose != NULL)
		printf ("total of %d bad blocks\n", fsize);
}

/* validate the pack label, fix up any inconsistencies, and mark the
 * pack as mounted in case we get a failure in mid-operation.
 */
static void cleanlabel (void)
{
	packlabel	*p;

	readdcn (1);				/* get pack label */
	p = use(packlabel,0);
	if (p->fill1 != -1)
	{
		if (sw.debug != NULL)
			printf ("pack label reserved field fixed\n");
		p->fill1 = -1;
	}
	if (pflags & uc_new)		/* "new" flag set */
	{
		if (alloc (1, pcs))	/* mark pack label allocated */
			rabort (INTERNAL);
		if (plevel < RDS11)
		{
			p->plvl = plevel = RDS11;
			if (sw.verbose != NULL)
				printf ("pack rev level field fixed\n");
		}
		else if (plevel > RDS12)
		{
			printf ("unsupported structure level %d.%d\n",
				plevel >> 8, plevel & 0xff);
			rabort (NOMSG);
		}
	}
	else				/* old pack */
	{
		if (p->plvl)
		{
			p->plvl = 0;
			if (sw.verbose != NULL)
				printf ("pack label rev level field cleared\n");
		}
		if (p->mdcn)
		{
			p->mdcn = 0;
			if (sw.verbose != NULL)
				printf ("pack label mfd cluster field cleared\n");
		}
	}
	/* while we're here, mark the pack as mounted in case of trouble 
	 * (and, for that matter, so the rumountrw() call will work)
	 */
	if (sw.prot == NULL)
	{
		p->pstat |= uc_mnt;
		fbwrite ();
	}
	else fiblk = -1;		/* readonly clean, invalidate buffer */
}

/* switches recognized:
 *	-Debug		lots of debug info
 *	-verbose	print more info about what's happening
 *	-protect	don't write anything, just inspect the disk
 */
void doclean (int argc, char **argv)
{
	int	i, flag;
	byte	*o, *n;
	byte	m;
	
	if (sw.prot == NULL)
		ropen (DWRITEMODE);	/* open it first */
	else	ropen (DREADMODE);
	readlabel ();			/* read and remember label data */
	if (pcs < dcs || pcs > 64 || (pcs > 16 && plevel < RDS12) ||
	    (pcs & -pcs) != pcs)
	{
		printf ("invalid pack cluster size %d\n", pcs);
		rabort (BADPAK);
	}
	if (sw.verbose != NULL)
	{
		if (pflags & uc_mnt) printf ("disk needs rebuilding.\n");
		else printf ("disk not marked as needing rebuilding.\n");
	}
	if ((pflags & uc_ro) &&
	    sw.overwrite == NULL &&
	    sw.prot == NULL)
		rabort (ROPACK);
	sattsize = 0;			/* SATT not looked up yet */
	womsat = FALSE;			/*  and SATT is clean */
	fiblkw = FALSE;			/*  FIBUF ditto */
	findsat ();			/* make sure we know where satt is */

	/* allocate some buffers we need */
	if (sattbf2 != NULL) free (sattbf2);
	if ((sattbf2 = (byte *) malloc (sattsize)) == NULL) rabort (NOMEM);
	if (badbmap != NULL) free (badbmap);
	if ((badbmap = (byte *) malloc (sattsize)) == NULL) rabort (NOMEM);
	if (mfdbuf != NULL) free (mfdbuf);
	if ((mfdbuf = (byte *) malloc (dirbufsize)) == NULL) rabort (NOMEM);
	if (mfdmap != NULL) free (mfdmap);
	if ((mfdmap = (byte *) malloc (dirmapsize)) == NULL) rabort (NOMEM);
	if (gfdbuf != NULL) free (gfdbuf);
	if ((gfdbuf = (byte *) malloc (dirbufsize)) == NULL) rabort (NOMEM);
	if (gfdmap != NULL) free (gfdmap);
	if ((gfdmap = (byte *) malloc (dirmapsize)) == NULL) rabort (NOMEM);
	if (ufdbuf != NULL) free (ufdbuf);
	if ((ufdbuf = (byte *) malloc (dirbufsize)) == NULL) rabort (NOMEM);
	if (ufdmap != NULL) free (ufdmap);
	if ((ufdmap = (byte *) malloc (dirmapsize)) == NULL) rabort (NOMEM);
	
	/* init stats */
	gfds = ufds = files = clusters = 0;

	/* start out with nothing allocated */
	memset (sattbf2, 0xff, sattsize);
	memset (sattbf2, 0, pcns / 8);
	if (pcns & 7) sattbf2[pcns / 8] = 0xff << (pcns & 7);

	/* set up the bad block map */
	allocbadb ();
	if (badbmap[0] & 1)
	{
		printf ("pack cluster 1 (pack label) is marked as bad\n");
		rabort (BADPAK);
	}
	
	/* process the pack label */
	cleanlabel ();
	
	/* now do the rest of the file structure */
	if (plevel) cleannewmfd ();
	else cleanoldmfd ();
	
	/* merge bad block map into satt */
	for (i = 0; i < sattsize; i++) sattbf2[i] |= badbmap[i];
	
	/* all done.  see if anything changed */
	if (memcmp (sattbufp, sattbf2, sattsize) != 0)
	{
		if (sw.verbose != NULL)
		{
			flag = 0;
			m = 1;
			o = sattbufp;
			n = sattbf2;
			for (i = 0; i < pcns; i++)
			{
				if ((*n & m) != 0 && (*o & m) == 0)
				{
					if (!flag) printf ("was free, now allocated: ");
					flag = 1;
					printf ("%d ", i);
				}
				if (m == 0x80)
				{
					m = 1;
					o++;
					n++;
				}
				else m <<= 1;
			}
			if (flag) printf ("\n");
			flag = 0;
			m = 1;
			o = sattbufp;
			n = sattbf2;
			for (i = 0; i < pcns; i++)
			{
				if ((*n & m) == 0 && (*o & m) != 0)
				{
					if (!flag) printf ("was allocated, now free: ");
					flag = 1;
					printf ("%d ", i);
				}
				if (m == 0x80)
				{
					m = 1;
					o++;
					n++;
				}
				else m <<= 1;
			}
			if (flag) printf ("\n");
		}
		if (sw.prot == NULL)
		{
			memcpy (sattbufp, sattbf2, sattsize);
			MARKS;		/* indicate SATT needs writing */
		}
	}
	if (sw.verbose != NULL)
		printf ("\n%ld gfds, %ld ufds, %ld files, %ld clusters processed\n",
			gfds, ufds, files, clusters);
	else
		printf ("\n");
	
	if (sw.prot == NULL)
		rumountrw ();		/* write satt, mark clean, and close */
	else	rumount ();
}

