/* handler for the "list" command */

#include <stdio.h>
#include <string.h>

#include "flx.h"
#include "fldef.h"
#include "dolist.h"
#include "fip.h"
#include "rtime.h"
#include "filename.h"
#include "scancmd.h"

#define COLUMNS 5

long	files, blocks, tfiles, tblocks;
byte	curproj, curprog;

static void dolistfqb (firqb *f)
{
	word	utc;
	ufdae	*a;
	ufdrms1	*r1;
	ufdrms2	*r2;
	int	pos;
	int	line2;
	char	rts[RTSLEN];
	char	stat[4];
	char	credate[DATELEN], acdate[DATELEN], cretime[RTIMELEN];
	char	*sp;

	if (sw.narrow != NULL) sw.bswitch = sw.narrow;	/* -1 implies -b */
	if (f->cproj != curproj || f->cprog != curprog)
	{
		if (curproj != 0 || curprog != 0)
		{
			if (sw.bswitch != NULL)
			{
				if (sw.narrow != NULL 
				    || files % COLUMNS == 0) 
					printf ("\n");
				else	printf ("\n\n");
			}
			else 
			{
				printf (" Total of %ld blocks in %ld files in [%d,%d]\n",
					blocks, files, curproj, curprog);
			}
		}
		files = 0;
		blocks = 0;
		curproj = f->cproj;
		curprog = f->cprog;
		if (sw.summary == NULL)
		{
			if (sw.bswitch != NULL)
			{
				if (sw.narrow == NULL)
					printf ("[%d,%d]\n", curproj, curprog);
			}
			else
			{
				printf ("\nDirectory of [%d,%d]\n"
					" Name .Ext    Size    Prot    Access"
					"          Creation      Clu"
					"  RTS      Pos\n",
					curproj, curprog);
			}
		}
	}
	files++;		/* accumulate totals */
	tfiles++;
	blocks += f->size;
	tblocks += f->size;
	if (sw.summary != NULL) return;
	if (sw.bswitch != NULL) {	/* brief listing */
		if (sw.narrow != NULL || files % COLUMNS == 0)
			printf ("%-10s\n", f->cname);
		else	printf ("%-10s    ", f->cname);
		return;
	}
	sp = stat;		/* point to status buffer */
	if (f->stat & us_nox) *sp++ = 'C';
	if (f->stat & us_nok) *sp++ = 'P';
	if (f->stat & us_plc) *sp++ = 'L';
	*sp = '\0';		/* put in terminator */
	readlk (f->alink);	/* make sure we have the AE */
	a = use(ufdae,k);	/*  and point to it */
	cvtdate (a->udla, acdate); /* convert dates and time */
	cvtdate (a->udc, credate);
	utc = a->utc;		/* save time (for flags) */
	cvttime (utc, cretime);
	if (a->urts[0] != 0) 
		r50toascii2 (a->urts, rts, TRUE);
	else	memcpy (rts, "      ", RTSLEN);
	if (f->size == 0)
		printf ("%-10s%8ld%-3s <%3d> %11s %11s %8s %3d %-6s   ----\n",
			f->cname, f->size, stat, f->prot, 
			acdate, credate, cretime, f->clusiz, rts);
	else
	{
		if (!readlk (f->rlink)) rabort (CORRUPT);
		pos = use(ufdre,k)->uent[0]; /* Get first retrieval entry */
		printf ("%-10s%8ld%-3s <%3d> %9s %9s %8s %3d %-6s %6d\n",
			f->cname, f->size, stat, f->prot, 
			acdate, credate, cretime, f->clusiz, rts, pos);
	}
	if (sw.full != NULL) {		/* full listing */
		line2 = FALSE;		/* no flags line yet */
		if (f->nlink & ul_che)
		{
			line2 = TRUE;
			printf (" cache:on");
			if (f->alink & ul_che)	printf (":seq");
			else		    	printf (":ran");
		}
		if (utc & (utc_ig | utc_bk))
		{
			if (line2) printf ("  ");
			line2 = TRUE;
			if (utc & utc_ig) printf (" ignore");
			if (utc & utc_bk) printf (" nobackup");
		}
		if (line2) printf ("\n");
		if (readlk (f->rmslink)) {	/* if RMS attributes present */
			r1 = use(ufdrms1,k);
			printf ("  rfm:");
			switch (r1->fa_typ & fa_rfm)
			{
			    case rf_udf: printf ("ufd"); break;
			    case rf_fix: printf ("fix"); break;
			    case rf_var: printf ("var"); break;
			    case rf_vfc: printf ("vfc"); break;
			    case rf_stm: printf ("stm"); break;
			}
			switch (r1->fa_typ & fa_org)
			{
			    case fo_seq: printf (":seq"); break;
			    case fo_rel: printf (":rel"); break;
			    case fo_idx: printf (":idx"); break;
			}
			if (r1->fa_typ & fa_rat)
			{
				printf (" rat");
				if (r1->fa_typ & ra_ftn) printf (":ftn");
				if (r1->fa_typ & ra_imp) printf (":imp");
				if (r1->fa_typ & ra_spn) printf (":nospan");
			}
			printf (" rsz:%d size:%ld eof:%ld:%d",
				r1->fa_rsz,
				((long)(r1->fa_siz[0]) << 16) + r1->fa_siz[1],
				((long)(r1->fa_eof[0]) << 16) + r1->fa_eof[1],
				r1->fa_eofb);
			if (readlk (r1->ulnk))
			{
				r2 = use(ufdrms2,k);
				printf (" bkt:%d hdr:%d msz:%d ext:%d",
					r2->fa_bkt, r2->fa_hsz, 
					r2->fa_msz, r2->fa_ext);
			}
			printf ("\n");
		}
	}
	if (sw.oattr != NULL && readlk (f->rmslink))
	{
		r1 = use(ufdrms1,k);
		printf ("  %06o %06o %06o %06o %06o %06o %06o",
			r1->fa_typ, r1->fa_rsz,
			r1->fa_siz[0], r1->fa_siz[1],
			r1->fa_eof[0], r1->fa_eof[1],
			r1->fa_eofb);
		if (readlk(r1->ulnk))
		{
			r2 = use(ufdrms2,k);
			printf (" %03o %03o %06o %06o",
				r2->fa_bkt, r2->fa_hsz,
				r2->fa_msz, r2->fa_ext);
		}
		printf ("\n");
	}
}

void dolist (int argc, char **argv)
{
	if (argc == 0)
	{
		argv[0] = "[*,*]";
		argc = 1;
	}
	tfiles = 0;
	tblocks = 0;
	curproj = 0;
	curprog = 0;
	rmount ();			/* mount the disk */
	sw.query = NULL;		/* force -query to be absent */
	dofiles (argc, argv, dolistfqb, NULLISWILD);
	if (tfiles != 0)
	{
		if (sw.bswitch != NULL)
		{
			if (sw.narrow == NULL && files % COLUMNS != 0) 
				printf ("\n");
		}
		else
		{
			if (sw.summary == NULL)
				printf (" Total of %ld blocks in %ld files\n",
					blocks, files);
			else	printf (" Total of %ld blocks in %ld files in [%d,%d]\n",
				     blocks, files, curproj, curprog);
			if (tfiles != files) 
				printf (" Grand total of %ld blocks in %ld files\n",
					tblocks, tfiles);
		}
	}
	rumount ();			/* done with disk */
}

