/* handler for the "get" command */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#if (defined(__MSDOS__) && !defined(__unix__))
#include <dir.h>
#endif

#include "flx.h"
#include "fldef.h"
#include "doget.h"
#include "fip.h"
#include "filename.h"
#include "fileio.h"
#include "scancmd.h"

#ifndef S_IRWXU
#define S_IRWXU (S_IREAD+S_IWRITE+S_IEXEC)
#endif
#ifndef S_IRWXG
#define S_IRWXG 0
#endif
#ifndef S_IRWXO
#define S_IRWXO 0
#endif
#ifndef S_ISDIR
#define S_ISDIR(x)      (((x) & S_IFMT) == S_IFDIR)
#endif

#define mkdirmode	(S_IRWXU | S_IRWXG | S_IRWXO)	/* mode for new directory */

static int		concat;			/* concatenating to one file */
static struct stat	sbuf;
static char		*on, *on2;
static FILE		*of;
static byte		cproj, cprog;
static long   		matches, tmatches;
static int		binmode;		/* true if doing binary copy */
static const char	*openmode;
static long		filebytes, tbytes;

static void dogetfile (firqb *f) {
	struct stat	sbuf2;
	char		tmpname[FILENAME_MAX];
	char		dirname[FILENAME_MAX];
	char		rname[NAMELEN];

	if (!concat) {
		if (S_ISDIR(sbuf.st_mode)) {
			if (f->stat & us_ufd) 
				sprintf (rname, "%03d%03d.dir", f->cproj, f->cprog);
			else {
				readlk (f->nlink);
				r50filename (use(ufdne,k)->unam,
						rname, FALSE);
			}
			if (sw.tree != NULL) {
				if (cproj != f->cproj || cprog != f->cprog) {
					cproj = f->cproj;
					cprog = f->cprog;
					sprintf (dirname, "%s/%03d%03d", on, cproj, cprog);
					if (stat (dirname, &sbuf2)) {
						if (errno != ENOENT) {
							printf ("Can't stat %s\n", dirname);
							perror (progname);
							return;
						}
#ifdef __unix__
						if (mkdir (dirname, mkdirmode)) {
#else
						if (mkdir (dirname)) {
#endif
							printf ("Can't mkdir %s\n", dirname);
							perror (progname);
							return;
						}
					}
				}
				sprintf (tmpname, "%s/%s", dirname, rname);
			} else	sprintf (tmpname, "%s/%s", on, rname);
			on2 = tmpname;
		} else	on2 = on;

/* open mode rule for copying to individual files:
 * if -b is specified, use binary mode
 * if -a is specified, use ascii mode
 * else (no mode switch), the choice depends on the
 * RMS file attributes, if any, of the input file:
 *  non-RMS files:
 *   if extension indicates text file, use ascii mode
 *   else use binary mode
 *  directory: use binary mode
 *  RMS sequential files:
 *   if fixed length records, recordsize a multiple of 512, use binary mode
 *   else use ascii mode
 *  Other RMS organizations, use binary mode
 */

		if (sw.bswitch != NULL)	binmode = TRUE;
		else if (sw.ascii != NULL) binmode = FALSE;
		else {
			if (f->stat & us_ufd) binmode = TRUE;
			else if (NULLINK(f->rmslink))
				binmode = !textfile (f->cname);
			else {
				if ((f->recfmt & fa_org) == fo_seq) {
					if ((f->recfmt & fa_rfm) == rf_fix
					    && (f->recsiz & 511) == 0)
						binmode = TRUE;
					else	binmode = FALSE;
				} else binmode = TRUE;
			}
		}
		if (binmode)	openmode = "wb";
		else		openmode = "w";
		if (sw.debug != NULL) 
			printf ("get mode %s\n", openmode);
		of = fopen (on2, openmode);
		if (of == NULL) {
			printf ("can't create %s\n", on2);
			perror (progname);
			return;
		}
	}
	matches++;			/* found another */
	tmatches++;
	filebytes = getfile (of, f, binmode);
	tbytes += filebytes;
	if (!concat) fclose (of);
	if (sw.verbose != NULL) {
		printcurname (f);
		if (concat && tmatches > 1)
			printf (" =>> %s (%ld bytes) in ", on, filebytes);
		else	printf (" => %s (%ld bytes) in ", on2, filebytes);
		if (binmode) printf ("block mode\n");
		else printf ("line mode\n");
	}
}

void doget (int argc, char **argv)
{
	int		s;
	firqb		f;

	if (argc < 2) {
		printf ("Usage: %s get file... dest\n", progname);
		return;
	}
	on = argv[--argc];			/* for convenience */
	cproj = cprog = 0;			/* no current PPN yet */
	tmatches = 0;				/* total matches */
	tbytes = 0;				/*  and total bytes */
	if (stat (on, &sbuf)) {			/* stat the output spec */
		if (errno != ENOENT) {
			printf ("can't stat %s\n", on);
			perror (progname);	/* report any details */
			return;			/* and give up right now */
		} else sbuf.st_mode = 0;	/* not found -> not a dir */
	}
	s = strlen (on);
	if (on[s - 1] == '/') on[s - 1] = '\0';
	on2 = on;
	/* concatenating if output is a filespec (not a directory spec)
	 * and input spec is wildcard, or multiple input specs
	 */
	parse (argv[1], &f);		/* parse first argument for wildcards */
	concat = (!S_ISDIR(sbuf.st_mode) &&
		(((f.flags & F_WILD) != 0) || argc >= 2));
	if (concat) {			/* create now if concatenating */
		if (sw.bswitch == NULL)	{
			openmode = "w";	/* ascii mode */
			binmode = FALSE;
		} else {
			openmode = "wb";	/* binary mode */
			binmode = TRUE;
		}
		if ((of = fopen (on, openmode)) == NULL) {
			printf ("can't create %s\n", on);
			perror (progname);	/* report any details */
			return;			/* and give up right now */
		}
	}
	rmount ();				/* mount the disk */
	dofiles (argc, argv, dogetfile, NULLISNULL);
	if (concat) fclose (of);
	if (sw.verbose != NULL && matches != 0)
		printf ("Total files: %ld, total bytes: %ld\n", tmatches, tbytes);
	rumount ();				/* done with the disk */
}

