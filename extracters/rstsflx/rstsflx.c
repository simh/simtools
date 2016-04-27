/* Program to manipulate RSTS disks and disk container files */

#include "version.h"			/* set version number */

/* system includes */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include <readline/readline.h>
#include <readline/history.h>

/* application specific includes */

#include "flx.h"			/* common definitions */
#include "rstsflx.h"
#include "fldef.h"			/* RSTS file system definitions */
#include "scancmd.h"
#include "diskio.h"

#define IOBLOCKS 64

#define IOSIZE	IOBLOCKS*BLKSIZE	/* size of general I/O buffer */
#define IOEXTRA	10			/* number of extra bytes to allocate */

char	*iobuf;				/* pointer to buffer */
long	iobufsize;			/*  and size we allocated */
char	*cmdbuf;			/* command line from readline */

static jmp_buf mainbuf;

void doabort(int status, const char *srcfile, int srcline)
{
	switch (status)
	{
	    case BADRE: 
		printf ("Invalid retrieval entries for file\n"); 
		break;
	    case BADBLK: 
		printf ("Block number out of range\n");
		break;
	    case BADDCS: 
		printf ("Invalid DCS (device too big)\n"); 
		break;
	    case CORRUPT: 
		printf ("Corrupt disk structure\n");
		break;
	    case NOMEM: 
		printf ("malloc failure\n");
		break;
	    case DIRTY: 
		printf ("Disk was not properly dismounted\n"); 
		break;
	    case ROPACK: 
		printf ("Disk is read-only and -Write was not specified\n"); 
		break;
	    case DISKIO: 
		printf ("I/O error on RSTS disk\n");
		perror (progname);			/* print details */
		break;
	    case NOMSG:
		/* no message, caller did that */
		break;
	    case BADPAK:
		printf ("Disk cannot be rebuilt\n");
		break;
	    case INTERNAL:
	    default:
		printf ("Internal error in program...\n");
	}
	/* if it looks like our problem, report where it happened */
	if (status != DIRTY &&
	    status != ROPACK &&
	    status != NOMSG &&
	    status != BADPAK)
	{
		printf ("  in module %s, line %d\n", srcfile, srcline);
	}
	longjmp (mainbuf, 1);
}

int main (int argc, char **argv)
{
	int	cmdlen;
	char	**cmdargv = NULL;	/* for scanning command line */
	int	cmdargc;
	char	*word;
	void	(*command)(int, char **);
	int	endian;
	char	*s;
	
	/* set initial abort handler */
	if (setjmp (mainbuf))
	{
		exit (EXIT_FAILURE);
	}
	
	endian = 0;
	*((char *)&endian) = 'A';
#if (INTSIZE == 2)
	if (endian == 0x4100)
#else
	if (endian == 0x41000000)
#endif
	{
		printf ("FLX is not supported on big endian systems\n");
		exit (EXIT_FAILURE);	/* quit right now */
	}
	else if (endian != 0x41)
	{
		printf ("Error in endian test: %08x\n", endian);
		exit (EXIT_FAILURE);	/* quit right now */
	}
	progname = argv[0];		/* remember our name */
	if ((iobuf = (char *) malloc (IOSIZE + IOEXTRA)) == NULL)
		rabort (NOMEM);
	iobufsize = IOSIZE;
	initialize_readline ();		/* Bind our completer. */
	if (argc > 1)			/* arguments on command line */
	{
		if ((command = scanargs (argc, argv)) == NULL)
			exit (EXIT_FAILURE);	/* scan arguments */
		(*command) (fargc, fargv);	/* go execute the command */
		if (command != dodisk) return 0; /* done, exit */
	}
	else
	{
		setrname ();
		printf ("FLX %s\nDefault RSTS disk is %s\n\n", IDENT, rname);
	}
	for (;;)
	{
		/* in the case of abort, we come back here */
		setjmp (mainbuf);
		cmdbuf = readline ("flx> ");
		if (cmdbuf == NULL) break;	/* control-d to quit */
		s = stripwhite (cmdbuf);
		if (*s)
		{
			add_history (s);
			newhistitems++;
			if (cmdargv != NULL) free (cmdargv);
			if ((cmdargv = (char **) malloc (sizeof (char *))) == NULL)
				rabort (NOMEM);
			cmdargv[0] = progname;	/* match conventions */
			cmdargc = 1;
			word = strtok (s, " ");
			while (word != NULL)
			{
				cmdargv = (char **) realloc (cmdargv,
							     ++cmdargc * sizeof (char *));
				if (cmdargv == NULL) rabort (NOMEM);
				cmdargv[cmdargc - 1] = word;
				word = strtok (NULL, " ");
			}
			if (cmdargc == 1) continue;	/* ignore null command */
			/* go execute the command */
			if ((command = scanargs (cmdargc, cmdargv)) != NULL)
				(*command)(fargc, fargv);
		}
		free (cmdbuf);
	}
	doexit (0, NULL);
}
