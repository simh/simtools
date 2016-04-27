/* handler for the "type" command */

#include <stdio.h>

#include "flx.h"
#include "fldef.h"
#include "dotype.h"
#include "fip.h"
#include "filename.h"
#include "fileio.h"
#include "scancmd.h"

void typefile (firqb *f)
{
	if (sw.verbose != NULL) {
		printf ("\n----- ");
		printcurname (f);
		printf (" -----\n");
	}
	getfile (stdout, f, FALSE);		/* put it on stdout */
}

void dotype (int argc, char **argv)
{
	rmount ();				/* mount the disk */
	dofiles (argc, argv, typefile, NOTNULL);
	rumount ();				/* done with disk */
}
