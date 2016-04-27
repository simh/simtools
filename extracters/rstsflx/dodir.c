/* handler for the mkdir" and "rmdir" commands */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "flx.h"
#include "fldef.h"
#include "dodir.h"
#include "fip.h"
#include "filename.h"

void domkdir (int argc, char **argv)
{
	int	n;
	firqb	f;
	int	newclu;
	char	*p;
	word	link, ulink, alink, qlink;
	uattr	*u;
	ua_qt2	*q;

	if (argc == 0) {
		printf ("Usage: %s mkdir dir...\n", progname);
		return;
	}
	rmountrw ();
	if (sw.clusiz == NULL) {
		newclu = pcs;
		if (newclu > 16) newclu = 16;
	} else {
		newclu = strtol (sw.clusiz, &p, 10);
		if (newclu < 0) {
			newclu = -newclu;
			if (newclu < pcs) {
				newclu = pcs;
				if (newclu > 16) newclu = 16;
			}
		}
		if (*p != '\0' || (newclu < pcs && newclu < 16)
		    || newclu > 16 ||
		    (newclu & (-newclu)) != newclu) {
			rumountrw ();		/* dismount first */
			printf ("Invalid clustersize %s\n", sw.clusiz);
			return;
		}
	}
	for (n = 0; n < argc; n++) {
		if (!parse (argv[n], &f) || f.flags != f_ppn) {
			printf ("Invalid PPN %s\n", argv[n]);
			continue;
		}
		if (initfilescan (&f, gfdatrtbl)) {
			printf ("Directory [%d,%d] already exists\n", 
				f.cproj, f.cprog);
		} else {
			f.cproj = f.proj;	/* set up for makedir */
			f.cprog = f.prog;
			if (makedir (&f, newclu)) {
				if (sw.user != NULL) {
					fbread (curgfd + gfdatrtbl);
					link = fibufw[f.cprog];
					readlktbl (link);
					alink = use(gfdne,k)->ulnk;
					if ((ulink = getent ()) == 0)
					    printf ("No room to mark account as user account\n");
					else {
						readlk (ulink);
						u = use(uattr,k);
						u->ulnk = alink;
						u->uatyp = aa_pas;
						MARKF;
						if ((qlink = getent ()) == 0) {
							printf ("No room for second quota block\n");
							qlink = ulink;
						} else {
							readlk (qlink);
							q = use(ua_qt2,k);
							q->ulnk = ulink;
							q->uatyp = aa_qt2;
							q->a2_job = 255U;
							q->a2_rib = 4;
							q->a2_msg = 12;
							MARKF;
						}
						readlk (link);
						use(gfdne,k)->ulnk = qlink;
						MARKF;
					}
				}
				if (sw.verbose != NULL)
				printf ("Account [%d,%d] created\n", 
					f.proj, f.prog);
			}
		}
	}
	rumountrw ();
}

void dormdir (int argc, char **argv)
{
	int	n;
	firqb	f;
	word	ne;

	if (argc == 0) {
		printf ("Usage: %s rmdir dir...\n", progname);
		return;
	}
	rmountrw ();
	for (n = 0; n < argc; n++) {
		if (!parse (argv[n], &f) || (f.flags & ~f_ppnw) != f_ppn) {
			printf ("Invalid PPN %s\n", argv[n]);
			continue;
		}
		if ((ne = initfilescan (&f, gfdatrtbl)) == 0) {
			printf ("No PPNs matching ");
			printfqbppn (&f);
			continue;
		}
		do {
			if (remdir (&f, ne) && sw.verbose != NULL)
				printf ("Account [%d,%d] deleted\n", 
					f.cproj, f.cprog);
		} while ((ne = nextppn (&f, gfdatrtbl)) != 0);
	}
	rumountrw ();
}
