/* rstsflx common definitions */

/* define BROKEN_ANSI when compiling on systems that have a defective
 * set of include files, like SunOS 4.1
 */
#ifdef BROKEN_ANSI
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define strtoul strtol
#define SEEK_SET 0
#define FILENAME_MAX 160
#endif

/* Apple doesn't define __unix__ */
#ifdef __APPLE__
#define __unix__ 1
#endif

#include "rstsflx.h"			/* include mainline's prototypes */

/* macro to map a struct onto a given offset of fibuf */

#define use(str,off)	((str *)(&fibuf[(off)]))
#define clumap		(use(fdcm,BLKSIZE-sizeof(fdcm)))
#define fibufw		((word16 *)(&fibuf[0]))

/* block numbering conversions */

#define dcntolbn(dcn)	((dcn)*dcs)		/* convert DCN to LBN */
#define pcntolbn(pcn)	((pcn)*pcs+dcs)		/* convert PCN to LBN */
#define lbntodcn(lbn)	((lbn)/dcs)		/* convert LBN to DCN */
#define lbntopcn(lbn)	(((lbn)-dcs)/pcs)	/* convert LBN to PCN */
#define dcntopcn(dcn)	(((dcn)-1)*dcs/pcs)	/* convert DCN to PCN */
#define pcntodcn(pcn)	((pcn)*pcs/dcs+1)	/* convert PCN to DCN */

/* other handy macros */

#define UP(x,y)		(((x)+(y)-1)&(-(y)))	/* round x up to mult of y */
#define DOWN(x,y)	((x)&(-(y)))		/* round x down to mult of y */

/* buffer marking */

#define MARKS	(womsat = TRUE)	/* mark SATT dirty */
#define MARKF	(fiblkw = TRUE)	/* mark FIBUF dirty */

/* common constants */

#define FALSE 0
#define TRUE 1

/* Useful general data types
 *
 * Note: "int" is being used here whenever 16 bits suffice;
 * "long int" where 32 bits are needed.  In some platforms,
 * we'll end up with more bits than that, which is not a problem.
 * Whenever data types of a specific size (rather than just a
 * minimum size) are needed, the types defined in platform.h are used.
 */

typedef unsigned char		byte;
typedef unsigned int		word;
typedef unsigned long int	lword;

/* bring in platform specific type definitions */

#include "platform.h"

/* lengths of various strings, including null terminator */

#define	NAMELEN	11		/* file name.ext */
#define DATELEN	12		/* Y2k style date (4 digit year) */
#define RTIMELEN 9		/* RSTS style time */
#define RTSLEN	7		/* Runtime system name */

/* buffer sizes */

#define BLKSIZE	512			/* one disk block */
#define	LSIZE	512			/* length of line buffer */

/* error codes; sometimes return codes, sometimes fatal error codes */

#define BADRE	0
#define BADLINK	1
#define BADBLK	3
#define BADDCS	4
#define CORRUPT	5
#define NOMEM	6
#define INTERNAL 7
#define DIRTY	8
#define ROPACK	9
#define DISKIO	10
#define NOMSG	11		/* used when caller generates the message */
#define	BADPAK	12		/* pack cannot be rebuilt */

#define rabort(code)	doabort (code, __FILE__, __LINE__)

/* file scan codes for "dofiles" */

#define NOTNULL		0
#define NULLISNULL	1
#define NULLISWILD	2

/* open modes */

#define DREADMODE	"rb"	/* read only disk access */
#define DWRITEMODE	"r+b"	/* read/write disk access */
#define DCREATEMODE	"w+b"	/* creating new container file */

/* data types */

typedef struct {		/* switch flag table layout */
	char	*ascii;
	char	*bswitch;	/* binary or brief (in list) */
	char	*clusiz;
	char	*contig;
	char	*create;
	char	*rstsdevice;
	char	*debug;
	char	*end;
	char	*full;
	char	*merge;
	char	*oattr;
	char	*odt;
	char	*offset;
	char	*prot;
	char	*query;
	char	*replace;
	char	*rmsvar;
	char	*rmsfix;
	char	*rmsstm;
	char	*size;
	char	*disksize;
	char	*start;
	char	*summary;
	char	*tree;
	char	*unprot;
	char	*verbose;
	char	*wide;
	char	*overwrite;
	char	*narrow;
	char	*user;
	char	*hex;
} swtab;

#define f_name	1		/* name present */
#define	f_namw	2		/* name wild */
#define f_ext	4		/* extension present */
#define f_extw	8		/* extension wild */
#define f_ppn	16		/* PPN present */
#define f_ppnw	32		/* PPN wild */
#define f_prot	64		/* protection code present */

#define F_WILD	(f_namw | f_extw | f_ppnw)	/* something wild if set */

typedef struct {
	long	size;		/* filesize */
	int	clusiz;		/* cluster size */
	int	flags;		/* parse flags */
	word	nlink;		/* link to NE */
	word	alink;		/* link to AE */
	word	rlink;		/* link to first RE */
	word	rmslink;	/* link to attributes */
	word	recfmt;		/* record format and flags for fileio */
	word	recsiz;		/* record size (if fixed records) */
	word	rechdrsiz;	/* header size if vfc */
	long	eofblk;		/* EOF block number from RMS */
	int	eofbyte;	/* EOF byte from RMS */
	word	currecsiz;	/* size of current (split across buffers) rec */
	word	recskip;	/* amount to skip for vfc */
	char	*currec;	/* current record pointer */
	byte	stat;		/* saved file status */
	byte	prot;		/* saved file protection */
	byte	proj;		/* supplied PPN as two bytes */
	byte	prog;
	char	name[NAMELEN];	/* supplied file name.ext */
	byte	newprot;	/* protection code from filespec */
	byte	cproj;		/* current PPN as two bytes */
	byte	cprog;
	char	cname[NAMELEN];	/* current name.ext */
} firqb;			/* well, sort of... :-) holds parsed filename */

/* typedefs for procedure datatypes, used to work around cc -protoi bug */

typedef void (*commandaction)(firqb *);
typedef void (*commandhandler)(int, char **);
typedef void (*iohandler)(long, long, void *);

/* external references: */

extern swtab	sw;			/* switch and argument pointers */
extern char	*progname;		/* name of this program */
extern byte	fibuf[BLKSIZE];		/* buffer for directories */
extern byte	*sattbufp;		/* pointer to SATT buffer */
extern int	sattsize;		/* size of SATT in bytes */
extern long	sattlbn;		/* start LBN of SATT */
extern long	satptr;			/* current allocation pointer (PCN) */
extern int	womsat;			/* TRUE if SATT needs to be written */
extern long 	fiblk;			/* current block in FIBUF */
extern int	fiblkw;			/* TRUE if FIBUF needs to be written */
extern long 	i,k;			/* current directory entry pointers */
extern int	pcs;			/* pack clustersize */
extern long	pcns;			/* number of pack clusters on this disk */
extern int	pflags;			/* pack flags */
extern int	plevel;			/* pack structure rev level */
extern long	mfddcn;			/* DCN of start of MFD */
extern long	mfdlbn;			/* LBN of start of MFD */
extern char	pname[7];		/* pack ID in ascii */
extern int 	dcs;			/* device clustersize */
extern long	diskblocks;		/* device block count */
extern const char	*rname;		/* file name of disk/container */
extern const char	*rssize;	/*  and size, as a string */
extern long	rsize;			/* size of RSTS disk or container file */
extern int	absflag;		/* flag for absolute I/O */
extern FILE	*rstsfile;		/* file for the rsts disk/container */
extern char	**fargv;		/* array of pointers to arguments */
extern int	fargc;			/*  and count of how many we found */
extern char	*iobuf;			/* general file I/O buffer */
extern long	iobufsize;		/* size of the buffer */
extern int	entptr;			/* current directory entry allocation pointer */
extern word	nextlink;		/* link to next file (ulnk of current) */
extern word	prevlink;		/* link to predecessor file */
extern word	nextppnlink;		/* link to next PPN for RDS 0 */
extern word	prevppnlink;		/* link to prececessor of PPN for RDS 0 */
extern long	curgfd;			/* LBN of start of current GFD */
extern char	*defdevice;		/* default RSTS device name */
extern char	*defsize;		/*  and size */
extern long	newhistitems;		/* new history items this session */
