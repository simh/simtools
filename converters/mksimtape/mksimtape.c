/*
 * File: mksimtape.c
 *
 * Make a SIMH tape image
 *
 * Bob Eager   March 2016
 *
 */

/*
 * History:
 *
 *	1.0	Initial version.
 *
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define	VERSION			1	/* Major version number */
#define	EDIT			0	/* Edit number within major version */

#define	FALSE			0
#define	TRUE			1

#define	DEFBLKSIZE		10240

/* General type definitions */

typedef	int		BOOL,*PBOOL;
typedef	char		CHAR,*PCHAR;
typedef	int		INT,*PINT;
typedef	unsigned char	UCHAR,*PUCHAR;
typedef unsigned int	UINT,*PUINT;
typedef	void		VOID,*PVOID;

#if 0
typedef	short		SHORT, *PSHORT;
typedef	unsigned short	USHORT, *PUSHORT;
typedef	long		LONG,*PLONG;
typedef	unsigned long	ULONG,*PULONG;
#endif

/* Forward references */

static	INT	blksize(INT);
static	VOID	error(PCHAR, ...);
static	INT	little_endian(VOID);
static	VOID	putusage(VOID);
static	BOOL	tape_mark(VOID);
static	BOOL	write_file(PCHAR, INT);

/* Local storage */

static	PCHAR	progname;		/* Name of program, as a string */

/* Help text */

static	const	PCHAR helpinfo[] = {
"%s: make SIMH tape image",
"Synopsis: %s file ...",
" ",
"The tape image is written to standard output.",
" ",
"A block size of %2$d is assumed for each file. A different block size",
"may be specified by adding it to the end of the filename, separated by a",
"colon; e.g.:",
"              stand:512",
""
};


/*
 * Parse arguments and handle options.
 *
 */

INT main(INT argc, PCHAR argv[])
{	BOOL rc = TRUE;
	INT i, bs;
	PCHAR p;

	progname = strrchr(argv[0], '/');
	if(progname == (PCHAR) NULL) progname = argv[0]; else progname++;

	if(argc == 1) {
		error("at least one file must be specified", argv[0]);
		putusage();
		exit(EXIT_FAILURE);
	}

	for(i = 1; i < argc; i++) {
		bs = DEFBLKSIZE;
		p = strchr(argv[i], ':');
		if(p != (PCHAR) NULL) {
			*p = '\0';
			bs = strtol(++p, (CHAR **) NULL, 10);
			if(bs == 0) {
				error("block size for file '%s' is invalid",
					argv[i]);
				rc = FALSE;
				break;
			}
		}
		write_file(argv[i], bs);
		tape_mark();
	}

	tape_mark();

	return(rc == TRUE ? EXIT_SUCCESS : EXIT_FAILURE);
}


/*
 * Write a file to the 'tape' on stdout.
 *
 * Inputs:
 *	file	name of file to be written
 *	bs	blocksize to be used
 *
 * Outputs:
 *	TRUE	written OK
 *	FALSE	write (or other) error
 *
 * Blocks are rounded up to and even number of bytes, padded with a
 * zero byte.
 * Each block is preceded and followed by a 4 byte count, the actual
 * block data size, in little endian format.
 *
 */

static BOOL write_file(PCHAR file, INT bs)
{	BOOL rc = TRUE;
	INT n;
	INT obs;
	INT le_bs;
	UCHAR sbuf[4];
	FILE *fp;
	PCHAR buf;

	obs = (bs+1) & ~1;/* Round up output block size to make even */
	le_bs = blksize(bs);	/* Little endian true block size */
	sbuf[0] = (le_bs & 0x000000ff);
	sbuf[1] = (le_bs & 0x0000ff00) >> 8;
	sbuf[2] = (le_bs & 0x00ff0000) >> 16;
	sbuf[3] = (le_bs & 0xff000000) >> 24;
	fprintf(stderr, "Writing file %s with block size %d %s\n",
		file, obs, bs == obs ? "": "(rounded up)");

	fp = fopen(file, "r");
	if(fp == (FILE *) NULL) {
		error("cannot open file '%s'", file);
		return(FALSE);
	}

	buf = (PCHAR) malloc(obs);
	if(buf == (PCHAR) NULL) {
		error("cannot allocate memory for buffer");
		return(FALSE);
	}

	for(;;) {
		n = fread(buf, 1, bs, fp);
		if(n < bs) {
			if(!feof(fp)) {
				error("error reading file '%s'", file);
				rc = FALSE;
				break;
			}
			if(n == 0) break;	/* No partial buffer */
			memset(&buf[n], '\0', bs-n);	/* Zero fill */
		}
		if(bs != obs) buf[obs-1] = '\0';	/* Pad */
		n = fwrite(sbuf, 1, 4, stdout);	/* Record header */
		if(n != 4) {
			error("Error writing to tape image");
			rc = FALSE;
			break;
		}
		n = fwrite(buf, 1, obs, stdout);/* Record data */
		if(n < obs) {
			error("Error writing to tape image");
			rc = FALSE;
			break;
		}
		n = fwrite(sbuf, 1, 4, stdout);	/* Record trailer */
		if(n != 4) {
			error("Error writing to tape image");
			rc = FALSE;
			break;
		}
	}

	free((PCHAR) buf);
	fclose(fp);
	return(rc);
}


/*
 * Write a tape mark to the tape image. This consists of four
 * consecutive zero bytes.
 *
 * Inputs:
 *	none
 *
 * Outputs:
 *	TRUE	written OK
 *	FALSE	write (or other) error
 *
 */

static BOOL tape_mark(VOID)
{	INT n;
	UCHAR sbuf[4] = { 0,0,0,0 };

	n = fwrite(sbuf, 1, 4, stdout);
	if(n != 4) {
		error("Error writing to tape image");
		return(FALSE);
	}

	return(TRUE);
}


/*
 * Return a 4 byte block size, in little endian format.
 * The input value is never negative.
 *
 * Inputs:
 *	mbs	block size in machine format
 *
 * Outputs:
 *	lbs	block size in little endian format
 *
 */

static INT blksize(INT mbs)
{	UINT result = 0;

	if(little_endian()) return(mbs);

	result |= (mbs & 0x000000ff) << 24;
	result |= (mbs & 0x0000ff00) << 8;
	result |= (mbs & 0x00ff0000) >> 8;
	result |= (mbs & 0xff000000) >> 24;
	return(result);
}


/*
 * Check whether we are on a big endian or little endian machine.
 *
 * Inputs:
 *	none
 *
 * Outputs:
 *	0	Big endian
 *	1	Little endian
 *
 */
 
static INT little_endian(VOID)
{	UINT i = 1;
	PCHAR c = (PCHAR) &i;

	return (INT) *c;
}


/*
 * Print message on standard error in printf style,
 * accompanied by program name.
 *
 * Inputs:
 *	mes	message to be printed
 *	...	optional arguments
 *
 * Outputs:
 *	none
 *
 */

static VOID error(PCHAR mes, ...)
{	va_list ap;

	fprintf(stderr, "%s: ", progname);

	va_start(ap, mes);
	vfprintf(stderr, mes, ap);
	va_end(ap);

	fputc('\n', stderr);
}


/*
 * Output program usage information.
 *
 * Inputs:
 *	none
 *
 * Outputs:
 *	none
 *
 */

static VOID putusage(VOID)
{	PCHAR *p = (PCHAR *) helpinfo;
	PCHAR q;

	for(;;) {
		q = *p++;
		if(*q == '\0') break;

		fprintf(stderr, q, progname, DEFBLKSIZE);
		fputc('\n', stderr);
	}
	fprintf(stderr, "\nThis is version %d.%d\n", VERSION, EDIT);
}

/*
 * End of file: mksimtape.c
 *
 */

