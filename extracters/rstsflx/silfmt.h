/*
 *	Edit history for SILFMT 
 *
 *			[RSTS V9_0]
 *  000  WJS  22-Sep-83	Creation
 *
 *			[RSTS V9_6]
 *  001  KPH  13-Nov-87	Increase number of phases to 47
 *
 *  002  GPK  23-Aug-94	Convert from .mac to .h
 *  003  GPK  29-Aug-94	Add definition for SAV format SIL index, add
 *			STB and overlay descriptor format.
 */

/* Set the number of SIL index blocks allowed for this release */

#define si_nbl	3			/* Maximum number of SIL index blocks */

/* Definitions of SIL Index entry */

typedef struct {
	word16	se_nam[2];		/* Name of module */
	word16	se_idn[2];		/* Ident of module */
	word16	se_blk;			/* Starting block of module */
	word16	se_stb;			/* Starting block of STB */
	word16	se_stn;			/* Number of entries in STB */
	word16	se_lod;			/* Load address of this module */
	word16	se_siz;			/* Size of module (in bytes) */
	word16	se_xfr;			/* Transfer address */
	word16	se_szd;			/* Size of module image of disk in blocks */
	word16	se_ovb;			/* Block offset to module's overlay descriptors */
	word16	se_ovn;			/* Number of overlay descriptors for module */
	word16	se_off;			/* Starting offset on disk for this module */
	word16	filler;			/* (reserved) */
	word16	se_xxx;			/* Reserved for SAV format SILs */
} silent;

#define se_len	(sizeof (silent))	/* Size of SIL Index entry */

/* Derive the number of modules si_nbl SIL index blocks can contain */

#define si_oth	(1000/se_len)		/* Number of entries in a non-first block */
#define si_1st	(si_oth-1)		/* Number of entries in first block */
#define si_nmd	((si_nbl-1)*si_oth+si_1st)	/* Maximum total no of index entries */

/* Layout for SIL index as it would be directly read from disk */

typedef struct {
	word16	si_num;			/* Number of modules in SIL */
	silent	si_ent[si_1st];		/* Space for first block's SIL Index entries */
	word16	si_rsv[12];		/* Reserved */
	word16	si_bls;			/* Size of this SIL in blocks */
	word16	si_chk;			/* Checksum of SIL Index */
	word16	si_sil;			/* RAD50 "SIL" for identification */
	silent	si_mor[si_nmd - si_1st];   /* Space for non-first blocks' entries */
} silindex;

/* Layout for SIL index in the case of a .SAV file SIL */

typedef struct {
	word16	si_num;			/* Number of modules (must be 1) */
	silent	si_ent;			/* One SIL index entry */
#define	initpc	si_ent.se_xxx		/* Initial PC value lives here */
	word16	initsp;			/* Initial SP */
	word16	rtjsw;			/* Reserved for JSW in RT11 */
	word16	rtusr;			/* Reserved for USR ptr in RT11 */
	word16	usertop;		/* High address of program */
	word16	si_rsv[(0774-052)/2];	/* Fill to end of block */
	word16	si_chk;			/* Checksum of SIL Index */
	word16	si_sil;			/* RAD50 "SIL" for identification */
} savsilindex;

/* Layout of SIL symbol table entry */

typedef struct {
	word16	name[2];		/* Symbol name in RAD50 */
	word16	ovnum;			/* Overlay number */
	word16	value;			/* Symbol value */
} stbent;

/* Layout of overlay descriptor entry */

typedef struct {
	word16	base;			/* Base virtual address of overlay */
	word16	size;			/* Size in bytes of overlay */
	word16	offset[2];		/* Offset (MSW, LSW) to overlay */
} ovrent;
