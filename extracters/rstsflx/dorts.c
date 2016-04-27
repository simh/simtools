/* handler for the "rts" command */

#include <stdio.h>

#include "flx.h"
#include "fldef.h"
#include "dorts.h"
#include "fip.h"
#include "filename.h"
#include "fileio.h"
#include "scancmd.h"

char	*rtsname;				/* new rts in ascii */
word16	rtsr50[2];				/*  and in rad50 */

void rtsfile (firqb *f)
{
	ufdae	*a;

	if (protfile (f)) return;		/* quit if file protected */
	readlk (f->alink);			/* read the AE */
	a = use(ufdae,k);
	if (a->urts[0] == 0 && a->urts[1] != 0) {
		printcurname (f);
		printf (" is a large file\n");
		return;
	}
	a->urts[0] = rtsr50[0];
	a->urts[1] = rtsr50[1];
	MARKF;
	upddlw (f);
	if (sw.verbose != NULL) {
		printcurname (f);
		printf (" rts changed to %s\n", rtsname);
	}
}

void dorts (int argc, char **argv)
{
	firqb	rtsf;

	if (--argc < 1) {
		printf ("Usage: %s rts file ... rts\n", progname);
		return;
	}
	rtsf.flags = 0;				/* clear parse flags */
	rtsname = argv[argc];			/* save name pointer */
	if (*parsename (rtsname, &rtsf) != '\0' || rtsf.flags != f_name) {
		printf ("Invalid rts name %s\n", rtsname);
		return;
	}
	cvtnametor50 (rtsf.name, rtsr50);
	rmountrw ();				/* mount the disk */
	dofiles (argc, argv, rtsfile, NOTNULL);
	rumountrw ();				/* done with disk */
}
