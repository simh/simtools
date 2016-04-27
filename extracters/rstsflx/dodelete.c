/* handler for the "delete" command */

#include <stdio.h>

#include "flx.h"
#include "fldef.h"
#include "dodelete.h"
#include "fip.h"
#include "filename.h"
#include "fileio.h"
#include "scancmd.h"

void dodel2 (firqb *f)
{
	if (protfile (f)) return;		/* quit if protected file */
	delfile (f);				/* now really delete the file */
	if (sw.verbose != NULL) {
		printcurname (f);
		printf (" deleted\n");
	}
}

void dodelete (int argc, char **argv)
{
	rmountrw ();				/* mount r/w */
	dofiles (argc, argv, dodel2, NOTNULL);
	rumountrw ();				/* done with r/w */
}
