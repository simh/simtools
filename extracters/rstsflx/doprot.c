/* handler for the "prot" command */

#include <stdio.h>

#include "flx.h"
#include "fldef.h"
#include "doprot.h"
#include "fip.h"
#include "filename.h"
#include "fileio.h"
#include "scancmd.h"

void doprot2 (firqb *f)
{
	ufdne	*n;

	readlk (f->nlink);			/* read the NE for this file */
	n = use(ufdne,k);
	if (sw.prot != NULL) {
		n->ustat |= us_nok;		/* set no-kill bit */
		if (sw.verbose != NULL) {
			printcurname (f);
			printf (" marked no-delete\n");
		}
	} else if (sw.unprot != NULL) {
		n->ustat &= ~us_nok;		/* clear no-kill bit */
		if (sw.verbose != NULL) {
			printcurname (f);
			printf (" no longer marked marked no-delete\n");
		}
	} else {
		if ((f->flags & f_prot) == 0) {
			printf ("File ");
			printcurname (f);
			printf (" not changed, no protection specified\n");
			return;
		}
		n->uprot = f->newprot;
		if (sw.verbose != NULL) {
			printf ("File ");
			printcurname (f);
			printf (" protection changed to <%d>\n", f->newprot);
		}
	}
	MARKF;
	upddlw (f);
}

void doprot (int argc, char **argv)
{
	rmountrw ();				/* mount the disk */
	dofiles (argc, argv, doprot2, NOTNULL);
	rumountrw ();				/* done with disk */
}
