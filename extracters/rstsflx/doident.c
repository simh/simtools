/* handler for the "identify" command */

#include <stdio.h>

#include "flx.h"
#include "fldef.h"
#include "doident.h"
#include "fip.h"
#include "rtime.h"

void doident (int argc, char **argv)		/* show pack id data */
{
	packlabel	*p;
	char		rdate[DATELEN];
	char		rtime[RTIMELEN];

	rmount ();			/* mount the disk R/O */
	readdcn (1);			/* get the pack label */
	p = use(packlabel,0);
	if (p->fill1 == -1
	    && (pcs >= dcs)
	    && ((pcs & (-pcs)) == pcs)) {
		printf ("RSTS disk on %s -- \"%s\"\n", rname, pname);
		printf ("   Device clustersize: %d\n", dcs);
		printf ("   Pack clustersize:   %d\n", pcs);
		if (dcs > 1)
			printf ("   Device size:        %ld (%ld DCNs)\n",
			  	diskblocks, diskblocks / dcs);
		else	printf ("   Device size:        %ld\n", diskblocks);
		printf ("   Revision level:     %d.%d\n", plevel >> 8, plevel & 0377);
		if (plevel >= RDS12) {
			cvtdate (p->mntdat, rdate);
			cvttime (p->mnttim, rtime);
			printf ("   Last mount date:    %s\n", rdate);
			printf ("   Last mount time:    %s\n", rtime);
		}
		printf ("   Pack flags:        ");
		if (p->pstat & uc_mnt)	printf (" Dirty");
		if (p->pstat & uc_pri)	printf (" Private/system");
		else			printf (" Public");
		if (p->pstat & uc_ro)	printf (" Read-only");
		if (p->pstat & uc_dlw)	printf (" DLW");
		if (p->pstat & uc_top)	printf (" NFF");
		printf ("\n");
	} else	printf ("Disk on %s does not appear to be a RSTS format disk\n", rname);
	rumount ();			/* done with disk */
}

