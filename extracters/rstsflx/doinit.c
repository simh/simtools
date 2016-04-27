/* handler for the "initialize" command */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "flx.h"
#include "fldef.h"
#include "doinit.h"
#include "fip.h"
#include "diskio.h"
#include "filename.h"

#define MFDCLU	16
#define UFDCLU	16
#define DEC166COUNT	10		/* number of bad block tables */

int getclusize ()
{
	int     newclu;
	char    *p;

	if (sw.clusiz == NULL) return (dcs);
	else {
		newclu = strtol (sw.clusiz, &p, 10);
		if (newclu < 0) {
			newclu = -newclu;
			if (newclu < dcs) newclu = dcs;
		}
		if (*p != '\0' || newclu < dcs || newclu > 64 ||
		    (newclu & (-newclu)) != newclu) {
			printf ("Invalid clustersize %s\n", sw.clusiz);
			return (0);
		}
	}
	return (newclu);
}

#define PSTAT   (uc_dlw | uc_pri)	/* pack flags to use */
#define MRECNT	20			/* max RE's for merge.sys */

const ufdlabel  newmlabel = { 0, 0177777, {0, 0, 0, 0}, {255, 255}, MFD};
const word16      rstsrts[2] = { 001343, 077770 };	/* rad50 " RSTS " */

/* Dummy bootstrap */

const word16	dummyboot[] = {
	0000240,		/*	NOP			; */
	0000005,		/*	RESET			;UGH */
	0000402,		/*	BR	5$		;Go to the real code */

	0000020,0153430,0000400,/*	BOOTID	5$,<>		;Boot ID block, no controllers */

	0005067,0000002,	/* 5$:	CLR	20$		;COUNT DOWN MEMORY TO DELAY */
	0005327,		/* 10$:	DEC	(PC)+		;CHURN */
	0000000,		/* 20$:	 .WORD	0		; */
	0001375,		/*	BNE	10$		; */
	0010700,		/*	MOV	PC,R0		;GETTA POSITION INDEPENDENT MESSAGE */
	0062700,0000026,	/*	ADD	#40$-.,R0	;ADDRESS INTO R0 */
	0105737,0177564,	/* 30$:	TSTB	@#177564	;CONSOLE READY ? */
	0100375,		/*	BPL	30$		;PATIENCE */
	0112037,0177566,		/*	MOVB	(R0)+,@#177566	;TALK TO ME */
	0105710,		/*	TSTB	(R0)		;ANY MORE ? */
	0001371,		/*	BNE	30$		;YUP */
	0000000,		/*	HALT			;NOPE */
	0000751			/*	BR	DUMBOT		;DO IT AGAIN IF HE CONTINUES */

				/* 40$:				;String starts here */
	};

#define dummytext	"\a\a\r\nPlease boot from the system disk\r\n"

void doinit (int argc, char **argv)
{
	long		n, count, sattblks;
	firqb		f, packid;
	long		newclu, newsize, newrsize;
	int		dec166;
	long		newlevel, mfdclu, ufdlbn;
	char 		*p;
	char		answer[20];
	struct stat	sbuf;
	word		dirne;
	packlabel	*l;
	ufdae		*a;
	FILE		*mf;
	long		mblocks, moblk, mbytes, mreoff;
	word		mprevre, mre;

	if (argc == 0) {
		printf ("Usage: %s initialize packid [level]\n", progname);
		return;
	}
	packid.flags = 0;
	if (*parsename (argv[0], &packid) != '\0' || (packid.flags & F_WILD)) {
		printf ("Invalid pack id %s\n", argv[0]);
		return;
	}
	if (argc < 2) newlevel = RDS12;
	else {
		p = argv[1];
		if (tolower (*p) == 'r') {
			p++;
			if (tolower (*p++) != 'd' || tolower (*p++) != 's') {
				printf ("Invalid revision level %s\n", argv[1]);
				return;
			}
		}
		while (*p == ' ') p++;
		if (strcmp (p, "0") == 0) newlevel = RDS0;
		else if (strcmp (p, "0.0") == 0) newlevel = RDS0;
		else if (strcmp (p, "1.1") == 0) newlevel = RDS11;
		else if (strcmp (p, "1.2") == 0) newlevel = RDS12;
		else {
			printf ("Invalid revision level %s\n", argv[1]);
			return;
		}
	}
	if (sw.merge != NULL) {		/* merging another filesystem */
		if (newlevel == RDS0) {
			printf ("-merge not allowed for RDS 0.0\n");
			return;
		}
		if ((mf = fopen (sw.merge, "rb")) == NULL) {
			printf ("Can't open merge file %s\n", sw.merge);
			perror (progname);
			return;
		}
		if (fstat (fileno(mf), &sbuf)) { /* get info about input file */
			printf ("Can't stat merge file %s", sw.merge);
			perror (progname);
			fclose (mf);		/* close input */
			return;
		}
		mblocks = UP(sbuf.st_size,BLKSIZE) / BLKSIZE;
	}
	if (sw.create != NULL) {
		getsize (sw.create, &newsize, &newrsize, &dec166);
		if (newsize == 0 ||
		    newsize < 800 ||
		    newsize >= (1 << 23)) {
			printf ("Invalid container size %s\n", sw.create);
			return;
		}
		diskblocks = newsize;		/* allow writing it all */
		n = (newrsize - 1) >> 16;	/* high order bits of last LBN */
		dcs = 1;			/* compute DCS */
		while (n) {
			n >>= 1;
			dcs <<= 1;
		}
		if (dcs > 16 && newlevel != RDS12)
		{
			printf ("Large disk requires RDS 1.2\n");
			return;
		}
		newclu = getclusize ();
		setrname ();			/* set container name */
		if (stat (rname, &sbuf)) {
			if (errno != ENOENT) {
				printf ("Can't stat %s\n", rname);
				return;
			}
		} else {
			printf ("Container file %s already exists\n", rname);
			return;
		}
		if ((rstsfile = fopen (rname, DCREATEMODE)) == NULL) {
			printf ("Error opening container file %s\n", rname);
			perror (progname);	/* report any details */
			return;
		}
		memset (iobuf, 0, iobufsize);	/* zero the entire buffer */
		for (n = 0; n < newsize; n += iobufsize / BLKSIZE) {
			count = iobufsize / BLKSIZE;
			if (newsize - n < count) count = newsize - n;
			rwrite (n, count * BLKSIZE, iobuf);
		}
		if (dec166) {			/* set up bad block table */
			word16 *w;
			memset (iobuf, -1, BLKSIZE);	/* set end marker */
			w = (word16 *)iobuf;
			*w++ = 031416;		/* random serial number */
			*w++ = 0;		/*  and high order */
			*w++ = 0;		/* reserved word */
			*w = 0;			/* mark as data pack */
			for (n = 0; n < DEC166COUNT; n++)
				rwrite (newrsize + n, BLKSIZE, iobuf);
		}
		diskblocks = newrsize;
	} else {
		ropen (DWRITEMODE);
		if (dcs > 16 && newlevel != RDS12)
		{
			printf ("Large disk requires RDS 1.2\n");
			return;
		}
		newclu = getclusize ();
		readlabel ();
		if (use(packlabel,0)->fill1 == -1
		    && (pcs >= dcs)
		    && ((pcs & (-pcs)) == pcs)) {
			printf ("Disk %s appears to be a RSTS format disk:\n", rname);
			printf ("   Clustersize: %d\n", pcs);
			printf ("   Revision:    %d.%d\n", plevel >> 8, plevel & 0xff);
			printf ("   Pack label:  %s\n", pname);
			printf ("\nRe-initialize it (Y/N)? ");
		} else	printf ("Initialize %s (Y/N)? ", rname);
		fgets (answer, sizeof (answer), stdin);
		if (tolower(answer[0]) != 'y') return;
	}
	fiblk = dcntolbn(1);
	memset (fibuf, 0, BLKSIZE);		/* clear out fibuf to make an invalid pack */
	fbwrite ();				/* write that out */
	pcs = newclu;				/* set new PCS */
	f.clusiz = pcs;				/* clustersize for files */
	plevel = newlevel;			/* set up pack level being created */
	if (plevel == RDS0) pflags = PSTAT;
	else pflags = PSTAT | uc_new;		/*  and flags also */
	pcns = (diskblocks - dcs) / pcs;	/* compute PCN count */
	sattblks = UP(pcns,BLKSIZE*8) / (BLKSIZE * 8);  /* SATT size in blocks */
	sattsize = sattblks * BLKSIZE;		/*  and in bytes */
	sattlbn = 1 << 23;			/* set a fake SATT LBN */
	if ((sattbufp = (byte *) malloc (sattsize)) == NULL) rabort(NOMEM);
	memset (sattbufp, 0xff, sattsize);	/* first mark everything in use */
	memset (sattbufp, 0, pcns / 8);		/* make all real clusters free */
	if (pcns & 7) sattbufp[pcns / 8] = 0xff << (pcns & 7); /* ditto any leftover bits */
	satptr = 0;				/* MFD/label goes at the start */
	if (plevel == RDS0) {			/* doing an old pack */
		if (!extdir2 (0, MFDCLU, 0, &newmlabel)) rabort(INTERNAL);
		mfddcn = clumap->uent[0];	/* remember MFD start */
		if (sw.debug != NULL) printf ("mfd at %lo\n", mfddcn);
		if (mfddcn != 1) rabort(INTERNAL);
		curgfd = mfdlbn = dcntolbn(mfddcn);
		f.cproj = f.cprog = 1;		/* prepare to create [1,1] */
		prevppnlink = nextppnlink = 0;	/* nothing in the MFD yet */
		if (!makedir (&f, MFDCLU)) rabort(INTERNAL); /* create MFD */
		readlk2 (0);			/* re-read MFD label */
		dirne = use(packlabel,0)->ulnk;	/* get [1,1] NE link */
		readlk (dirne);			/* read that */
		use(gfdne,k)->uar = 1;		/* fill in cluster pointer */
		MARKF;
		satptr = pcns / 2;		/* put the rest in the middle */
	} else {				/* new (RDS1.1 or later) pack */
		if (sw.merge != 0) {		/* do the merge */
			if (mblocks <= dcs) {	/* merge fits in pack label */
				*sattbufp = 0x01; /* mark first cluster (pack label) allocated */
			} else {
				if (getclu (pcs, UP(mblocks-dcs,pcs)) != 1) {
					printf ("No room for merge data\n");
					rabort(INTERNAL);
				}
			}
			moblk = 0;
			while ((mbytes = fread (iobuf, 1, iobufsize, mf)) != 0) {
				mbytes  = UP(mbytes, BLKSIZE);
				rwrite (moblk, mbytes, iobuf);
				moblk += mbytes / BLKSIZE;
			}
			if (sw.verbose != NULL) 
				printf ("merged %s (%ld blocks)\n",
					sw.merge, mblocks);
		} else	*sattbufp = 0x01;	/* mark first cluster (pack label) allocated */
		satptr = pcns / 2;		/* put the rest in the middle */
		mfdclu = MFDCLU;		/* default MFD clustersize */
		if (pcs > mfdclu) mfdclu = pcs;
		if (mfdclu > 16) mfdclu = 16;	/* adjust it as needed */
		if (!extdir2 (0, mfdclu, fd_new, &newmlabel)) rabort(INTERNAL);
		mfddcn = clumap->uent[0];	/* remember MFD start */
		if (sw.debug != NULL) printf ("mfd at %lo\n", mfddcn);
		mfdlbn = dcntolbn(mfddcn);	/*  and LBN also */
		curgfd = 0;			/* no [0,*] GFD yet */
	}
	parse ("[0,1]badb.sys<63>", &f);	/* set up firqb */
	f.cproj = f.proj;
	f.cprog = f.prog;			/* current also */
	memcpy (f.cname, f.name, NAMELEN);
	if (!makedir (&f, UFDCLU)) rabort(INTERNAL);	/* create [0,1] account */
	dirne = initfilescan (&f, gfdatrtbl);	/* look up [0,1] NE pointer */
	if (sw.debug != NULL) printf ("[0,1] NE %o\n", dirne);
	if ((ufdlbn = allocufd (dirne, &f)) == 0) rabort(INTERNAL);
						/* allocate the UFD */
	fbread (ufdlbn);			/* get it into fibuf */
	nextlink = prevlink = 0;
	entptr = sizeof (ufdlabel);		/* initialize pointers for crefile */
	f.stat = us_nok;			/* we want P set for badb.sys */
	f.clusiz = pcs;				/* default clustersize */
	if (!crefile (&f, rstsrts, NULL, NULL)) rabort(INTERNAL);
	parse ("[0,1]satt.sys<63>", &f);	/* set up firqb for the second file */
	f.cproj = f.proj;
	f.cprog = f.prog;			/* current also */
	memcpy (f.cname, f.name, NAMELEN);
	f.stat = us_nok | us_nox;		/* P set and contiguous */
	f.size = sattblks;			/* we need this size */
	f.clusiz = pcs;				/* default clustersize */
	if (!crefile (&f, rstsrts, NULL, NULL)) rabort(INTERNAL);
	readlk (f.rlink);			/* read first RE */
	sattlbn = dcntolbn(use(ufdre,k)->uent[0]);
	if (sw.merge != NULL && (mblocks -= dcs + pcs) > 0) {
		moblk = 1 + pcs / dcs;		/* first DCN for merge.sys */
		parse ("[0,1]merge.sys<63>", &f);
		f.cproj = f.proj;
		f.cprog = f.prog;		/* current also */
		memcpy (f.cname, f.name, NAMELEN);
		f.stat = us_nok | us_nox;	/* P set and contiguous */
		f.size = 0;			/* don't allocate it now */
		f.clusiz = pcs;			/* clustersize is PCS */
		if (!crefile (&f, rstsrts, NULL, NULL)) rabort(INTERNAL);
		readlk (f.alink);
		a = use(ufdae,k);
		a->usiz = mblocks;
		if (mblocks >> 16) {		/* large file ! */
			a->urts[0] = 0;
			a->urts[1] = mblocks >> 16;
		}
		MARKF;
		mprevre = 0;
		mreoff = 0;			/* fill RE from top */
		while (mblocks > 0) {
			if (mreoff == 0) {	/* need a new RE */
				if ((mre = getent ()) == 0) {
					printf ("No room in [0,1] for merge.sys\n");
					rabort(INTERNAL);
				}
				if (mprevre) {
					readlk (mprevre);
					use(ufdre,k)->ulnk = mre;
				} else {
					readlk (f.nlink);
					use(ufdne,k)->uar = mre;
				}
				MARKF;
				mprevre = mre;	/* link to this next time */
			}
			readlk (mre);
			use(ufdre,k)->uent[mreoff] = moblk;
			moblk += pcs / dcs;	/* advance to next cluster */
			mblocks -= pcs;		/* count down size to fill */
			mreoff++;
			if (mreoff > 6) mreoff = 0;
		}
	}
	rwrite (sattlbn, sattsize, sattbufp);	/* now write the SATT data */
	womsat = FALSE;
	free (sattbufp);			/* deallocate SATT */
	checkwrite ();				/* write FIBUF if needed */
	if (sw.merge != NULL) {			/* see if overwriting merge data */
		fbread (0);			/* read the boot block */
		for (n = 0; n < BLKSIZE; n++) {
			if (fibuf[n] != 0) {
				printf ("Warning: non-zero merge file data in boot block\n");
				break;
			}
		}
	}
	fiblk = 0;				/* build the dummy boot */
	memset (fibuf, 0, BLKSIZE);		/* clear it out */
	memcpy (fibuf, dummyboot, sizeof (dummyboot));
	memcpy (fibuf + sizeof (dummyboot), dummytext, sizeof (dummytext));
	fbwrite ();				/*  and write it */
	readdcn (1);				/* re-read pack label */
	if (sw.merge != NULL) {			/* see if overwriting merge data */
		for (n = 0; n < BLKSIZE; n++) {
			if (fibuf[n] != 0) {
				printf ("Warning: non-zero merge file data in pack label\n");
				break;
			}
		}
	}
	l = use(packlabel,0);
	l->fill1 = 0177777;
	l->ppcs = pcs;				/* pack cluster size */
	if (plevel > RDS0) {			/* if new pack, set new fields: */
		l->mdcn = mfddcn;		/*  MFD DCN */
		l->plvl = newlevel;		/*   and rev level */
		l->ulnk = 1;			/* link field = 1 by convention */
		l->mntdat = l->mnttim = 0;	/* never mounted yet */
	} 
	l->pstat = pflags;			/* set pack flags */
	cvtnametor50 (packid.name, l->pckid);	/* set the pack label */
	fbwrite ();				/* write the pack label */
	rclose ();				/*  and that's all! */
}
