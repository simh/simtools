/* command line handling for rstsflx */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <readline/readline.h>
#include <readline/history.h>

/* Some unixes define this, some do not. */
#ifndef whitespace
#define whitespace(c) ((c) == ' ' || (c) == '\t')
#endif

#include "flx.h"
#include "fldef.h"
#include "silfmt.h"
#include "scancmd.h"
#include "fip.h"
#include "diskio.h"
#include "filename.h"
#include "doalloc.h"
#include "doget.h"
#include "doident.h"
#include "dolist.h"
#include "dotype.h"
#include "dodump.h"
#include "doput.h"
#include "dodelete.h"
#include "dorename.h"
#include "dorts.h"
#include "dodir.h"
#include "doprot.h"
#include "doinit.h"
#include "dohook.h"
#include "docomp.h"
#include "doclean.h"

swtab	sw;			/* switch flag table */

char	*progname;		/* name of program (argv[0]) */
char	*cmdname;		/* command name (argv[1]) */
char	**fargv = NULL;		/* array of pointers to arguments */
int	fargc;			/*  and count of how many we found */
char	*defdevice = NULL;	/* default device name */
char	*defsize = NULL;	/*  and size, as a string */
char	*histfile;		/* name of history file */
int	hfilesize;		/* size limit for history file */
long	newhistitems;		/* number of history items added */

typedef struct {
	const char	*kw;
	void		(*hand)(int, char *[]);
} cmdent;

/* This routine is used to ask (if -query was specified) whether a file
 * should be processed or not.  It returns the answer, which is a character
 * "y", "n" or "q".  Note that the answer "a" is returned as "y" and
 * further prompting is turned off.
 */

char doquery (void (*printfile)(void *), void *name)
{
	char	answer[200];
	char	first;

	if (sw.query == NULL) return ('y');
	else {
		for (;;) {
			printf (" %s ", cmdname);
			(*printfile) (name);
			printf (" (y,n,a,q)? ");
			fflush (stdout);
			fgets (answer, sizeof (answer), stdin);
			first = tolower (answer[0]);
			if (first == 'q' || first == 'n' || first == 'y')
				return (first);
			if (first == 'a')
			{
				sw.query = NULL;
				return ('y');
			}
			printf ("Invalid answer\n");
		}
	}
}

/* This routine is used as the generic main loop for many command
 * processing routines.  The first two arguments define the list of
 * command (file) arguments.  The third argument controls what to do
 * if the name part of the filespec is null:
 *	NOTNULL		error -- "missing filename"
 *	NULLISWILD	ok, substitute wildcard ("?") for the missing name
 *	NULLISNULL	ok, leave the name part null, so it's a directory
 *			reference -- and if it's [*,*] or [n,*], treat
 *			that as access to MFD and GFD respectively (rather
 *			than wild directories).
 *
 * -query switch processing is provided in this routine.  If a command
 * does not want -query to have effect, it should override the switch to
 * be absent (sw.query = NULL).
 */

void dofiles (int argc, char **argv, commandaction action, int nullflag)
{
	int	fnum, j;
	firqb	f;
	int	matches;
	char	answer;

	if (argc == 0) printf ("Usage: %s %s files...\n", progname, cmdname);
	for (fnum = 0; fnum < argc; fnum++)
	{
		if (!parse (argv[fnum], &f))
		{
			printf ("Invalid filespec %s\n", argv[fnum]);
			continue;
		}
		if (nullflag == NOTNULL && (f.flags & f_name) == 0)
		{
			printf ("Missing filename %s\n", argv[fnum]);
			continue;
		}
		else if (nullflag == NULLISWILD)
		{
			if ((f.flags & f_name) == 0) 
				for (j = 0; j < 6; j++) f.name[j] = '?';
				f.flags |= f_name | f_namw;
			if ((f.flags & f_ext) == 0)
			{
				f.name[7] = '?';
				f.name[8] = '?';
				f.name[9] = '?';
				f.flags |= f_ext | f_extw;
			}
		}
		else if (nullflag == NULLISNULL && (f.flags & f_name) == 0)
		{
			if (f.prog == 255)
			{
				if (plevel == RDS0)
				{
					printf ("GFD or MFD not legal for RDS.0 pack\n");
					continue;
				}
			}
			else if (f.proj == 255)
			{
				printf ("Invalid directory spec\n");
				continue;
			}
		}
		matches = 0;
		if (initfilescan (&f, gfddcntbl))	/* setup file scan */
		{
			answer = 'y';			/* dummy */
			while (nextfile (&f))
			{
				matches++;		/* found another */
				answer = doquery ((void (*)(void *))printcurname, &f);
				if (answer == 'q') break;
				if (answer == 'n') continue;
				(*action) (&f);		/* do whatever */
			}
			if (answer == 'q') break;	/* out two levels... */
		}
		if (matches == 0)
		{
			printf ("No files matching ");
			printfqbname (&f);
			printf ("\n");
		}
	}
}

void dodisk (int argc, char **argv)
{
	long	rsize, tsize;
	int	flag;
	
	if (defdevice != NULL) free (defdevice);
	if (defsize != NULL) free (defsize);
	defdevice = NULL;
	defsize = NULL;
	if (argc > 0)
	{
		if (argc > 1)			/* size also supplied */
		{
 			getsize (argv[1], &rsize, &tsize, &flag);
			if (rsize == 0)
			{
				printf ("Invalid size %s\n", argv[1]);
				return;
			}
			defdevice = (char *) malloc (strlen (argv[0]) + 1);
			defsize = (char *) malloc (strlen (argv[1]) + 1);
			if (defdevice == NULL || defsize == NULL)
				rabort(NOMEM);
			strcpy (defdevice, argv[0]);
			strcpy (defsize, argv[1]);
		}
		else
		{
 			defdevice = (char *) malloc (strlen (argv[0]) + 1);
			if (defdevice == NULL) rabort(NOMEM);
			strcpy (defdevice, argv[0]);
		}
	}
	if (sw.verbose != NULL)
	{
		setrname ();
		if (defsize != NULL)
			printf ("Default RSTS disk name is %s, size %ld\n",
				rname, rsize);
		else	printf ("Default RSTS disk name is %s\n", rname);
	}
}

void doexit (int argc, char **argv)
{
	int	i;
	
#ifndef __APPLE__
	i = append_history (newhistitems, histfile);
	if (i)	write_history (histfile);
	else	history_truncate_file (histfile, hfilesize);
	free (histfile);
#endif
	exit (EXIT_SUCCESS);
}

const cmdent commands[] =
{
	{ "list", dolist },
	{ "directory", dolist },
	{ "ls", dolist },
	{ "type", dotype },
	{ "cat", dotype },
	{ "dump", dodump },
	{ "exit", doexit },
	{ "quit", doexit },
	{ "bye", doexit },
	{ "get", doget },
	{ "put", doput },
	{ "delete", dodelete },
	{ "rm", dodelete },
	{ "protect", doprot },
	{ "rts", dorts },
	{ "runtime", dorts },
	{ "rename", dorename },
	{ "mv", dorename },
	{ "move", dorename },
	{ "hook", dohook },
	{ "mkdir", domkdir },
	{ "rmdir", dormdir },
	{ "identify", doident },
	{ "allocation", doalloc },
	{ "initialize", doinit },
	{ "dskint", doinit },
	{ "disk", dodisk },
	{ "compress", docomp },
	{ "clean", doclean },
	{ "rebuild", doclean },
	{ NULL, NULL } };

typedef struct
{
	char	*kw;
	char	**flag;
	int	arg;		/* TRUE if switch takes an argument */
} swname;

#define sd(x,y)		{ #x, &sw.y, FALSE }	/* switch w/o arg */
#define sda(x,y)	{ #x, &sw.y, TRUE }	/* switch with arg */

const swname switches[] =
{
	sd(ascii,ascii),
	sd(binary,bswitch),
	sd(brief,bswitch),
	sda(clustersize,clusiz),
	sd(confirm,query),
	sd(contiguous,contig),
	sda(create,create),
	sda(disk,rstsdevice),
	sd(Debug,debug),
	sda(end,end),
	sda(filesize,size),
	sd(full,full),
	sd(image,bswitch),
	sd(long,full),
	sda(merge,merge),
	sd(noprotect,unprot),
	sd(oattributes,oattr),
	sd(odt,odt),
	sda(offset,offset),
	sd(protect,prot),
	sd(query,query),
	sd(replace,replace),
	sd(r,rmsvar),
	sda(rf,rmsfix),
	sd(rv,rmsvar),
	sd(rs,rmsstm),
	sda(size,size),
	sda(Size,disksize),
	sda(start,start),
	sd(summary,summary),
	sd(tree,tree),
	sd(unprotect,unprot),
	sd(verbose,verbose),
	sd(wide, wide),
	sd(Write,overwrite),
	sd(1column,narrow),
	sd(user,user),
	sd(hex,hex),
	{ "", NULL, FALSE } };

commandhandler scanargs(int argc, char **argv)
{
	int		n, l;
	const cmdent	*c;
	const swname 	*s, *swn;
	commandhandler	command;

	memset (&sw, 0, sizeof (swtab));	/* Indicate no switches yet */
	if (fargv != NULL) free (fargv);
	if ((fargv = (char **) malloc (argc * sizeof(char *))) == NULL) rabort (NOMEM);
	fargc = 0;				/* No arguments yet */
	cmdname = NULL;				/*  and no command name yet */
	for (n = 1; n < argc; n++)		/* scan the program arguments */
	{
		if (argv[n][0] == '-')		/* it's a switch */
		{
			s = switches; 		/* point to switch names */
			l = strlen (argv[n]) - 1;
			swn = NULL;		/* no match yet */
			while (s->flag != NULL) 
			{
				if (strncmp (&argv[n][1], s->kw, l) == 0) {
					if (strlen (s->kw) == l) 
					{
						swn = s;
						break;	/* exact match */
					}
					if (swn == NULL) swn = s;
					else if (s->flag != swn->flag)
					{
						printf ("Ambiguous switch %s\n", 
							argv[n]);
						return (NULL);
					}
				}
				s++;
			}
			if (swn == NULL)
			{
				printf ("Unrecognized switch %s\n", argv[n]);
				return (FALSE);
			}
			*(swn->flag) = argv[n];
			if (swn->arg)			/* switch takes an argument */
			{
				if (++n == argc) 	/* skip it */
				{
					printf ("Missing argument for switch %s\n", argv[n - 1]);
					return (FALSE);
				}
				*(swn->flag) = argv[n];	/* point to that */
			}
		} 
		else
		{
			if (cmdname == NULL) cmdname = argv[n];
			else fargv[fargc++] = argv[n];
		}
		
	}
	c = commands;
	command = NULL;				/* Nothing found yet */
	if (cmdname == NULL)
	{
		printf ("Missing command keyword\n");
		return NULL;
	}
	l = strlen (cmdname);			/* get length of command */
	while (c->kw != NULL)	 		/* look for matching command */
	{
		if (strncmp (cmdname, c->kw, l) == 0) 
		{
			if (strlen (c->kw) == l) 
			{
				command = c->hand;
				break;		/* stop now on exact match */
			}
			if (command == NULL) command = c->hand;
			else if (command != c->hand) 
			{
				printf ("Ambiguous command %s\n", cmdname);
				return (NULL);
			}
		}
		c++;
	}
	if (command == NULL) 
	{
		printf ("Unrecognized command %s\n", cmdname);
		return (NULL);
	}
	return (command);
}
     
/* Strip whitespace from the start and end of STRING.  Return a pointer
 * into STRING.
 */
char *stripwhite (char *string)
{
	register char *s, *t;
     
	for (s = string; whitespace (*s); s++) ;
     
	if (*s == 0) return (s);
     
	t = s + strlen (s) - 1;
	while (t > s && whitespace (*t)) t--;
	*++t = '\0';
     
	return s;
}
     
/* **************************************************************** */
/*                                                                  */
/*                  Interface to Readline Completion                */
/*                                                                  */
/* **************************************************************** */
     
/* some forward declarations */
char * command_generator (const char *text, int state);
char * switch_generator (const char *text, int state);
char ** flx_completion (char *text, int start, int end);

/* Tell the GNU Readline library how to complete.  We want to try to complete
 * on command names if this is the first word in the line, or on filenames
 * if not.
 */
void initialize_readline (void)
{
	char	*histfilesize;
	char	*histsize;
	int	hsize = 0;
	char	*home;
	char	*hf;
	int	i;
	
	/* get history parameters */
	hf = getenv ("FLXHISTFILE");
	histfilesize = getenv ("FLXHISTFILESIZE");
	histsize = getenv ("FLXHISTSIZE");
	hfilesize = 0;
	newhistitems = 0;
	if (hf == NULL)
	{
		home = getenv ("HOME");
		i = strlen (home);
		histfile = malloc (i + strlen ("/.flx_history") + 1);
		strcpy (histfile, home);
		strcat (histfile, "/.flx_history");
	}
	else histfile = strdup (hf);
	if (histfilesize != NULL) hfilesize = atoi (histfilesize);
	if (histsize != NULL) hsize = atoi (histsize);
	if (hsize == 0) hsize = 100;
	if (hfilesize == 0) hfilesize = hsize;

	/* set up history stuff */
	using_history ();
	stifle_history (hsize);
	read_history (histfile);

	/* Allow conditional parsing of the ~/.inputrc file. */
	rl_readline_name = "Flx";
     
	/* Tell the completer that we want a crack first. */
	rl_attempted_completion_function = (CPPFunction *)flx_completion;

	/* supply some things that don't seem to be there in DOS... */
	rl_basic_word_break_characters = "n\"\\'`@$>";
	rl_completer_word_break_characters = 
		(char *) rl_basic_word_break_characters;
}
     
/* Attempt to complete on the contents of TEXT.  START and END show the
 * region of TEXT that contains the word to complete.  We can use the
 * entire line in case we want to do some simple parsing.  Return the
 * array of matches, or NULL if there aren't any.
 */
char ** flx_completion (char *text, int start, int end)
{
	char **matches;
     
	matches = NULL;
     
	/* If this word is at the start of the line, then it is a command
	 * to complete.  Otherwise it is the name of a file in the current
	 * directory.
	 */
	if (start == 0)
		matches = rl_completion_matches (text, command_generator);
	else if (*text == '-')
		matches = rl_completion_matches (text, switch_generator);
	
	return (matches);
}
     
/* Generator function for command completion.  STATE lets us know whether
 * to start from scratch; without any state (i.e. STATE == 0), then we
 * start at the top of the list.
 */
char * command_generator (const char *text, int state)
{
	static int list_index, len;
	char *name;
     
	/* If this is a new word to complete, initialize now.  This includes
	 * saving the length of TEXT for efficiency, and initializing the index
	 * variable to 0.
	 */
	if (!state)
	{
		list_index = 0;
		len = strlen (text);
	}
     
	/* Return the next name which partially matches from the command list. */
	while ((name = (char *) commands[list_index].kw))
	{
		list_index++;
		if (strncmp (name, text, len) == 0)
			return strdup (name);
	}
     
	/* If no names matched, then return NULL. */
	return NULL;
}
     
/* ditto but for switches */
char * switch_generator (const char *text, int state)
{
	static int list_index, len;
	char	*name;
	char	sw[20];
     
	/* If this is a new word to complete, initialize now.  This includes
	 * saving the length of TEXT for efficiency, and initializing the index
	 * variable to 0.
	 */
	if (!state)
	{
		list_index = 0;
		len = strlen (text) - 1;
	}
     
	/* Return the next name which partially matches from the switch list. */
	while (*(name = switches[list_index].kw))
	{
		list_index++;
		if (strncmp (name, text + 1, len) == 0)
		{
			sw[0] = '-';
			strcpy (sw + 1, name);
			return strdup (sw);
		}
	}
	
	/* If no names matched, then return NULL. */
	return NULL;
}
