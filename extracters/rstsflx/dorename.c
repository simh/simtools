/* handler for the "rename" command */

#include <stdio.h>
#include <string.h>

#include "flx.h"
#include "fldef.h"
#include "dorename.h"
#include "fip.h"
#include "filename.h"
#include "fileio.h"
#include "scancmd.h"

firqb	newf;					/* new name */

void renamefile (firqb *f)
{
	ufdne	*n;
	firqb	tmpf;
	word	saveprev, savenext;

	if (protfile (f)) return;		/* quit if file protected */
	mergename (f->cname, &newf, FALSE);
	readlk2 (0);				/* read UFD label */
	saveprev = prevlink;			/* save current scan pointers */
	savenext = nextlink;
	prevlink = 0;				/* start there */
	nextlink = use(ufdlabel,0)->ulnk;	/* and initialize scan */
	memcpy (&tmpf, &newf, sizeof (firqb));
	memcpy (tmpf.name, newf.cname, NAMELEN);
	if (nextfileindir (&tmpf))		/* file exists... */
	{
		if (f->nlink == tmpf.nlink)
		{
			printf ("New name matches old name ");
			printcurname (f);
			printf ("\n");
			return;
		}
		if (protfile (&tmpf)) return;	/* protected file, skip */
		if (sw.replace == NULL)
		{
			printf ("Cannot rename ");
			printcurname (f);
			printf (" to %s -- file already exists\n", tmpf.name);
			return;
		}
		delfile (&tmpf);		/* now really delete the file */
	}
	prevlink = saveprev;			/* restore pointers */
	nextlink = savenext;
	readlk (f->nlink);			/* read the NE */
	n = use(ufdne,k);
	cvtnameexttor50 (newf.cname, n->unam);	/* write new RAD50 name into NE */
	MARKF;
	upddlw (f);
	if (sw.verbose != NULL)
	{
		printcurname (f);
		printf (" renamed to %s\n", newf.cname);
	}
}

void dorename (int argc, char **argv)
{
	char	*newname;
	int	j;

	if (--argc < 1)
	{
		printf ("Usage: %s rename file ... newname\n", progname);
		return;
	}
	newf.flags = 0;				/* clear parse flags */
	newname = argv[argc];			/* save name pointer */
	if (*parsenameext (newname, &newf) != '\0')
	{
		printf ("Invalid new name %s\n", newname);
		return;
	}
	if ((newf.flags & f_name) == 0)
	{
		for (j = 0; j < 6; j++) newf.name[j] = '?';
		newf.flags |= f_name | f_namw;
	}
	if ((newf.flags & f_ext) == 0)
	{
		newf.name[7] = '?';
		newf.name[8] = '?';
		newf.name[9] = '?';
		newf.flags |= f_ext | f_extw;
	}
	rmountrw ();				/* mount the disk */
	dofiles (argc, argv, renamefile, NOTNULL);
	rumountrw ();				/* done with disk */
}
