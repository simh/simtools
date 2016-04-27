/* handler for "put" command */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "flx.h"
#include "fldef.h"
#include "doput.h"
#include "fip.h"
#include "filename.h"
#include "fileio.h"
#include "scancmd.h"

typedef struct {
	const char	*ext;
	const char	*rts;
} rtsent;

const rtsent	rtstbl[] = {
		{ "tsk", "...rsx" },
		{ "sav", "rt11  " },
		{ "4th", "forth " },
		{ "bas", "basic " },
		{ "tec", "teco  " },
		{ "com", "dcl   " },
		{ "alc", "algol " },
		{ "wps", "wpsedt" },
		{ NULL, NULL }};

/* check for extension match against runtime system runnable extensions.
 * Return TRUE and rts name if a match, false and zeroes otherwise.
 */

static int checkrts (firqb *f, word16 *rtsname)
{
	const rtsent	*r;

	for (r = rtstbl; r->ext != NULL; r++) {
		if (strncmp (&(f->cname[7]), r->ext, 3) == 0) {
			cvtnametor50 (r->rts, rtsname);
			return (TRUE);
		}
	}
	rtsname[0] = rtsname[1] = 0;
	return (FALSE);
}

static void putname (void *name) {
	fputs ((char *) name, stdout);
}

void doput (int argc, char **argv)
{
	char		*p, *pp;
	FILE		*inf;
	long		newsize;
	int		newclu;
	word16		rtsname[2];
	char		*inspec, *outspec;
	firqb		outf;
	firqb		tmpf;
	long		savelbn;
	int		arg, j;
	const char	*mode;
	int		binmode;
	char		answer;
	long		bytes, totalbytes, files, insize;
	word		dirne;
	byte		defprot;
	struct stat	sbuf;
	ufdrms1		rms1;
	ufdrms1		*a1;
	ufdrms2		rms2;
	ufdrms2		*a2;
	
	if (argc < 2) {
		printf ("Usage: %s put file... dest\n", progname);
		return;
	}
	outspec = argv[--argc];
	if (sw.size == NULL) newsize = 0;
	else {
		newsize = strtoul (sw.size, &p, 10);
		if (*p != '\0') {
			printf ("Invalid size %s\n", sw.size);
			return;
		}
	}
	if (sw.contig == NULL && newsize != 0) {
		newsize = 0;
		printf ("-size switch ignored, -contiguous not specified\n");
	}
	if (!parse (outspec, &outf)) {
		printf ("Invalid destination spec %s\n", outspec);
		return;
	}
	if ((outf.flags & f_name) == 0) {
		for (j = 0; j < 6; j++) outf.name[j] = '?';
		outf.flags |= f_name | f_namw;
	}
	if ((outf.flags & f_ext) == 0) {
		outf.name[7] = '?';
		outf.name[8] = '?';
		outf.name[9] = '?';
		outf.flags |= f_ext | f_extw;
	}
	if (sw.tree == NULL && (outf.flags & f_ppnw) != 0) {
		printf ("Wildcard PPN without -tree not allowed in %s\n", 
			outspec);
		return;
	}
	rmountrw ();				/* mount disk R/W */
	if (sw.clusiz == NULL) newclu = pcs;
	else {
		newclu = strtol (sw.clusiz, &p, 10);
		if (newclu < 0) {
			newclu = -newclu;
			if (newclu < pcs) newclu = pcs;
		}
		if (*p != '\0' || newclu < pcs || newclu > 128 ||
		    (newclu & (-newclu)) != newclu) {
			rumountrw ();		/* remember to dismount */
			printf ("Invalid clustersize %s\n", sw.clusiz);
			return;
		}
	}
	if (sw.tree == NULL) {
		if ((dirne = initfilescan (&outf, gfdatrtbl)) == 0) {
			rumountrw ();			/* remember to dismount */
			printf ("Account does not exist %s\n", outspec);
			return;
		}
		readlk (dirne);
		if ((savelbn = allocufd (dirne, &outf)) == 0) {	/* allocate UFD if needed */
			rumountrw ();			/* remember to dismount */
			printf ("No room to allocate UFD\n");
			return;
		}
	}
	files = 0;
	totalbytes = 0;
	for (arg = 0; arg < argc; arg++) {
		inspec = argv[arg];		/* pick up a name */
		mergename (inspec, &outf, sw.tree != NULL);
						/* construct output name */
		if (outf.cproj == 0 && outf.cprog == 0) {
			printf ("Bad directory spec from input filespec %s\n", inspec);
			continue;
		}
		answer = doquery (putname, inspec);
		if (answer == 'q') break;
		if (answer == 'n') continue;
		if (sw.ascii != NULL) binmode = FALSE;
		else if (sw.bswitch != NULL) binmode = TRUE;
		else binmode = !textfile (outf.cname);
		if (binmode) mode = "rb";
		else mode = "r";
		if (sw.debug != NULL) printf ("put mode %s\n", mode);
		if ((inf = fopen (inspec, mode)) == NULL) {
			printf ("Can't open %s\n", inspec);
			perror (progname);
			continue;		/* skip this one */
		}
		if (fstat (fileno(inf), &sbuf)) { /* get info about input file */
			printf ("Can't stat input file %s", inspec);
			perror (progname);
			fclose (inf);		/* close input */
			continue;		/*  and carry on */
		}
		insize = UP(sbuf.st_size,BLKSIZE) / BLKSIZE;
		if (sw.tree != NULL) {
			memcpy (&tmpf, &outf, sizeof (firqb));
			tmpf.proj = tmpf.cproj;
			tmpf.prog = tmpf.cprog;
			dirne = initfilescan (&tmpf, gfdatrtbl); /* does PPN exist yet? */
			if (dirne == 0) {
				if (!makedir (&outf, pcs)) {
					printf ("Cannot create PPN [%d,%d]\n",
						outf.cproj, outf.cprog);
					fclose (inf);
					continue;
				}
				if (plevel > RDS0) {
					fbread (curgfd + gfdatrtbl);
					dirne = fibufw[outf.cprog];
					readlktbl (dirne);
				} else {
					dirne = prevppnlink;
					readlk (dirne);
				}
			}
			if ((savelbn = allocufd (dirne, &outf)) == 0) {
				printf ("No room to allocate UFD [%d,%d]\n",
						outf.cproj, outf.cprog);
				fclose (inf);
				continue;
			}
		}
		fbread (savelbn);		/* read output directory */
		readlk2 (0);			/* read UFD label */
		prevlink = 0;			/* start there */
		nextlink = use(ufdlabel,0)->ulnk; /* and initialize scan */
		memcpy (&tmpf, &outf, sizeof (firqb));
		memcpy (tmpf.name, outf.cname, NAMELEN);
		if (nextfileindir (&tmpf)) {	/* file exists... */
			if (protfile (&tmpf)) {
				fclose (inf);	/* protected file, skip */
				continue;
			}
			if (sw.prot != NULL) {
				printf ("File ");
				printcurname (&tmpf);
				printf (" exist, not replaced\n");
				fclose (inf);
				continue;
			}
			delfile (&tmpf);	/* now really delete the file */
			fbread (savelbn);	/* read output directory */
			readlk2 (0);		/* read UFD label */
			prevlink = 0;		/* start there */
			nextlink = use(ufdlabel,0)->ulnk; /* and initialize scan */
			memcpy (tmpf.name, outf.cname, NAMELEN);
			nextfileindir (&tmpf);	/* find end of directory */
		}
		outf.clusiz = newclu;
		if (sw.contig == 0) outf.size = 0;
		else {
			if (newsize == 0) outf.size = insize;
			else outf.size = newsize;
		}
		defprot = 60;			/* default not runnable */
		if (checkrts (&outf, rtsname)) {
			if (newsize >> 16) { 	/* if large file */
				printf ("warning: can't set rts for large file ");
				printcurname (&outf);
				outf.newprot &= ~up_run;	/* never runnable */
			} else	defprot = 124;	/* default is now runnable */
		}
		if ((outf.flags & f_prot) == 0) outf.newprot = defprot;
		if (outf.size) outf.stat = us_nox;
		else outf.stat = 0;
		a1 = NULL;			/* assume no attributes */
		a2 = NULL;
		memset (&rms1, 0, sizeof(rms1)); /*  and clear them in case */
		memset (&rms2, 0, sizeof(rms2));
		if (sw.rmsvar != NULL || sw.rmsfix != NULL ||
		    sw.rmsstm != NULL) {
			a1 = &rms1;
			if (sw.rmsvar != NULL) {
				a1->fa_typ = rf_var | fo_seq;
				a2 = &rms2;
				memset (&rms2, 0, sizeof(rms2));
				a2->fa_msz = 512;	/* dummy value */
			}
			if (sw.rmsstm != NULL) a1->fa_typ = rf_stm | fo_seq;
			if (sw.rmsfix != NULL) {
				a1->fa_typ = rf_fix | fo_seq;
				a2 = &rms2;
				a1->fa_rsz = strtoul (sw.rmsfix, &pp, 10);
				if (*pp != '\0') a1->fa_rsz = 0;
				a2->fa_msz = a1->fa_rsz;
				insize = (insize / (a1->fa_rsz / BLKSIZE)) *
					 (a1->fa_rsz / BLKSIZE);
				insize++;
			}
			a1->fa_siz[0] = a1->fa_eof[0] = insize  >> 16;
			a1->fa_siz[1] = a1->fa_eof[1] = insize & 0xffff;
			a1->fa_eofb = 0;
		}
		if (crefile (&outf, rtsname, a1, a2)) {
			bytes = putfile (inf, &outf, binmode);
			if (bytes < 0 || (bytes == 0 && outf.size != 0)) {
				bytes = -bytes;
				printf ("Failure copying %s to ", inspec);
				printcurname (&outf);
				printf (" -- %ld bytes copied in ", bytes);
				if (binmode) printf ("block mode\n");
				else printf ("line mode\n");
			} else if (sw.verbose) {
				printf ("%s =>> ", inspec);
				printcurname (&outf);
				printf (" (%ld bytes) in ", bytes);
				if (binmode) printf ("block mode\n");
				else printf ("line mode\n");
			}
			files++;
			totalbytes += bytes;
		} else {
			printf ("No room to create file ");
			printcurname (&outf);
			printf ("\n");
		}
		fclose (inf);
	}
	if (sw.verbose != NULL)
		printf ("Total files: %ld, total bytes: %ld\n", files, totalbytes);
	rumountrw ();				/* done with R/W disk */
}
