/* fldef.h -- RSTS file system definitions
 *
 * Derived from fldef.mac, RSTS V10.1.
 */

/* rad50 constants we need */
#define MFD	0051064		/* rad50 "MFD" */
#define GFD	0026264		/* rad50 "GFD" */
#define UFD	0102064		/* rad50 "UFD" */
#define TMP	0077430		/* rad50 "TMP" */

/* Disk file structure definitions 
 * Any definitions that apply only for certain disk structure levels
 * are marked accordingly.  They apply to the rev level stated and those
 * after it.
 */

/* Note: except for the pack label, each of these struct definitions
 * must define a struct of size 16 bytes.
 */

typedef struct {       		/* Pack label entry */
	word16	ulnk;		/* Link if RDS0.0, otherwise 1  */
	int16	fill1;		/* Reserved (-1) */
	word16	mdcn;		/* Starting DCN of MFD (RDS1.1) */
	word16	plvl;		/* Pack revision level */
	word16	ppcs;		/* Pack cluster size */
	word16	pstat;		/* Pack status/flags */
	word16	pckid[2];	/* Pack ID */
	word16	tapgvn[2];	/* TAP generation-version number (RDS1.1) */
	word16	bckdat;		/* Date of last TAP full backup (RDS1.1) */
	word16	bcktim;		/* Time of last TAP full backup (RDS1.1) */
	word16	mntdat;		/* Date of last mount/dismount (RDS1.2) */
	word16	mnttim;		/* Time of last mount/dismount (RDS1.2) */
	byte	fill2[BLKSIZE-(14*sizeof(word16))]; /* Reserved */
} packlabel;

/* Flag bits in pack label field */

#define uc_top	0001000		/* New files first */
#define uc_dlw	0004000		/* Maintain date of last write */
#define uc_ro	0010000		/* Read-only pack */
#define uc_new	0020000		/* "New" pack (RDS1.1) */
#define uc_pri	0040000		/* Pack is private/system */
#define uc_mnt	0100000		/* Pack is mounted (dirty) */

/* Rev levels */
#define RDS0	0		/* RDS 0 -- V7.x and before */
#define RDS11	((1<<8)+1)	/* RDS 1.1 -- V8 */
#define RDS12	((1<<8)+2)	/* RDS 1.2 -- V9.0 and beyond */

/* MFD and GFD are new as of RDS1.1 */

typedef struct {		/* MFD label entry */
	word16	fill1;		/* Reserved (0) */
	int16	fill2;		/* Reserved (-1) */
	word16	fill3[3];	/* Reserved (0) */
	word16	malnk;		/* Link to pack attributes */
	byte	lppn[2];	/* PPN [255,255] */
	word16	lid;		/* Identification (RAD50 "MFD") */
} mfdlabel;

typedef struct {		/* Directory cluster map */
	byte	uclus;		/* Directory clustersize */
	byte	uflag;		/* RDS1 GFD/MFD flag in high bit */
	word16	uent[7];	/* The dcn's of the cluster(s) */
} fdcm;

#define fd_new	0200		/* flag bit for GFD/MFD in uflag (RDS1.1) */

typedef struct {		/* GFD label entry */
	word16	fill1;		/* Reserved (0) */
	int16	fill2;		/* Reserved (-1) */
	word16	fill3[4];	/* Reserved (0) */
	byte	lppn[2];	/* PPN [x,255] */
	word16	lid;		/* Identification (RAD50 "GFD") */
} gfdlabel;

/* mfd/gfd offsets */
#define gfddcntbl	1	/* block with DCN pointer table */
#define gfdatrtbl	2	/* block with attribute link table */

/* For RDS0, the "GFD NE" and "GFD AE" live in the MFD ([1,1] directory)
 * which starts at DCN 1.  They are in a linked list, possibly mixed
 * with files, in the usual UFD fashion.
 */

typedef struct {		/* GFD name entry */
	word16	ulnk;		/* Link to attributes */
	word16	unam[3];	/* PPN and password */
	byte	ustat;		/* Status byte */
	byte	uprot;		/* Protection code */
	word16	uacnt;		/* Access count */
	word16	uaa;		/* Link to accounting entry */
	word16	uar;		/* Dcn of start of UFD */
} gfdne;

typedef struct {		/* GFD accounting entry */
	word16	ulnk;		/* Flags */
	word16	mcpu;		/* Accum cpu time (LSB) */
	word16	mcon;		/* Accum connect time */
	word16	mkct;		/* Accum kct's (LSB) */
	word16	mdev;		/* Accum device time */
	word16	mmsb;		/* Accum cpu time and kct's (MSB's) */
	word16	mdper;		/* Disk quota */
	word16	uclus;		/* UFD cluster size */
} gfdae;

typedef struct {		/* UFD label entry */
	word16	ulnk;		/* Link to first name block in UFD */
	int16	fill2;		/* Reserved (-1) */
	word16	fill3[4];	/* Reserved (0) */
	byte	lppn[2];	/* PPN [x,y] */
	word16	lid;		/* Identification (RAD50 "UFD") */
} ufdlabel;

typedef struct {		/* UFD name entry */
	word16	ulnk;		/* Link to next name entry */
	word16	unam[3];	/* File name and extension */
	byte	ustat;		/* Status byte */
	byte	uprot;		/* Protection code */
	word16	uacnt;		/* Access count */
	word16	uaa;		/* Link to UFD accounting entry */
	word16	uar;		/* Link to retrieval entries */
} ufdne;

typedef struct {		/* UFD accounting entry */
	word16	ulnk;		/* Link to attributes and flags */
	word16	udla;		/* Date of last access (or write) */
	word16	usiz;		/* File size */
	word16	udc;		/* Date of creation */
	word16	utc;		/* Time of creation */
	word16	urts[2];	/* File's run-time system name or 0/MSB size */
	word16	uclus;		/* File cluster size */
} ufdae;

typedef struct {		/* UFD first RMS attribute blockette */
	word16	ulnk;		/* Link to second attributes blockette */
	word16	fa_typ;		/* File type (rfm, org, rat) */
	word16	fa_rsz;		/* Record size */
	word16	fa_siz[2];	/* File size (32 bits) */
	word16	fa_eof[2];	/* File EOF block number (32 bits) */
	word16	fa_eofb;	/* EOF byte offset */
} ufdrms1;

#define fa_rfm	0000007		/* record format field in fa_typ */
#define rf_udf	0		/* undefined organization */
#define rf_fix	1		/* fixed length records */
#define rf_var	2		/* variable length records */
#define rf_vfc	3		/* variable with fixed control header */
#define rf_stm	4		/* stream (cr/lf delimiter) */
#define fa_org	0000070		/* file organization format in fa_typ */
#define fo_seq	000		/* sequential organization */
#define fo_rel	020		/* relative organization */
#define fo_idx	040		/* indexed organization */
#define fa_rat	0007700		/* record attribute flags */
#define ra_ftn	0000400		/* fortran carriage control */
#define ra_imp	0001000		/* implied carriage control */
#define ra_spn	0004000		/* no-span records */

typedef struct {		/* UFD second RMS attribute blockette */
	word16	ulnk;		/* Link (reserved) */
	byte	fa_bkt;		/* Bucket size */
	byte	fa_hsz;		/* Header size */
	word16	fa_msz;		/* Max record size */
	word16	fa_ext;		/* Default extension amount */
	word16	filler[4];	/* Reserved */
} ufdrms2;

/* All directory attributes are new as of RDS1.1 or later */

typedef struct {		/* MFD/GFD attribute entry */
	word16	ulnk;		/* Link to next, flags */
	byte	uatyp;		/* Type */
	byte	uadat[16-3];	/* Data */
} uattr;

/* Time of creation flag bit definitions */

#define utc_tm	0003777		/* Bits needed for the time field */
#define utc_ig	0004000		/* IGNORE flag (RDS1.2) */
#define utc_bk	0010000		/* NOBACKUP flag (RDS1.2) */
				/* Other bits reserved */

typedef struct {		/* UFD retrieval entry */
	word16	ulnk;		/* Link to next retrieval entry */
	word16	uent[7];	/* The dcn's of the cluster(s) */
} ufdre;

/* Bit assignments in ustat and f$stat */

#define us_out	0001		/* File is 'out of sat' (historical) */
#define us_plc	0002		/* File is "placed" */
#define us_wrt	0004		/* Write access given out (not on disk if large files) */
#define us_upd	0010		/* File open in update mode (not on disk if large files) */
#define us_nox	0020		/* No file extending allowed (contiguous) */
#define us_nok	0040		/* No delete and/or rename allowed */
#define us_ufd	0100		/* Entry is MFD type entry */
#define us_del	0200		/* File marked for deletion */

/* Bit assignments in uprot and f$prot */

#define up_rpo	0001		/* Read  protect against owner */
#define up_wpo	0002		/* Write  "       "       " */
#define up_rpg	0004		/* Read   "       "      group */
#define up_wpg	0010		/* Write  "       "       " */
#define up_rpw	0020		/* Read   "       "      world */
#define up_wpw	0040		/* Write  "       "       " */
#define up_run	0100		/* Executable file */
#define up_prv	0200		/* Clear on delete, privileged if executable file */

/* Link and flag word fields */

#define ul_use	0000001		/* On to ensure entry is "in use" */
#define ul_bad	0000002		/* Some bad block exists in file */
#define ul_che	0000004		/* Cache (name entry) or sequential (accting entry) */
#define ul_cln	0000010		/* Reserved for 'clean' */
#define ul_eno	0000760		/* Entry offset within block   (5 bits) */
#define ul_clo	0007000		/* Cluster offset within UFD   (3 bits) */
#define ul_blo	0170000		/* Block offset within cluster (4 bits) */
#define sl_clo	9		/* Shift count for ul_clo field */
#define sl_blo	12		/* shift count for ul_blo field */

#define LINKBITS	(ul_eno | ul_clo | ul_blo)	/* bits to test for null */
#define NULLINK(l)	(((l) & LINKBITS) == 0)

/* Account attribute codes */

#define aa_quo	1		/* Quotas */
#define aa_prv	2		/* Privilege masks */
#define aa_pas	3		/* Password */
#define aa_dat	4		/* Date/time recording (creation, change, login) */
#define aa_nam	5    		/* User name (RDS1.2) */
#define aa_qt2	6    		/* Quotas part 2 (RDS1.2) */

/* Attribute blockette layouts */

typedef struct {		/* Disk Quota Attribute Blockette */
	word16	ulnk;		/* Link to next, flags */
	byte	uatyp;		/* Type */
	byte	aq_djb;		/* Detached job quota */
	word16	aq_lol;		/* Logged out quota (LSB) */
	word16	aq_lil;		/* Logged in quota  (LSB) */
	byte	aq_lim;		/* Logged in quota  (MSB) */
	byte	aq_lom;		/* Logged out quota (MSB) */
	byte	aq_rsm;		/* Reserved */
	byte	aq_crm;		/* Current usage    (MSB) */
	word16	aq_rsl;		/* Reserved */
	word16	aq_crl;		/* Current usage    (LSB) */
} ua_quo;

#define privsz 6		/* number of privilege bytes */

typedef struct {		/* Privilege mask data */
	word16	ulnk;		/* Link to next, flags */
	byte	uatyp;		/* Type */
	byte	fill1;		/* Filler */
	byte	ap_prv[privsz];	/* Authorized privileges */
	byte	fill2[020-privsz-1-3]; /* Filler */
} ua_prv;

typedef struct {		/* Date/time data */
	word16	ulnk;		/* Link to next, flags */
	byte	uatyp;		/* Type */
	byte	at_kb;		/* Keyboard of last login */
	word16	at_lda;		/* Date of last login */
	word16	at_lti;		/* Time of last login */
	word16	at_pda;		/* Date of last password16 change */
	word16	at_pti;		/* Time of last password change */
	word16	at_cda;		/* Date of creation */
	word16	at_exp;		/* Expiration date (RDS1.2) */
				/* Account creation time (RDS1.1 only) */
} ua_dat;

/* Fields within at_lti */
#define at_msk	0003777		/* Bits needed for the time field */
#define at_npw	0004000		/* No password required */
				/* Other bits reserved */

/* Fields within at_pti */
/*	at_msk	0003777		 * Bits needed for the time field */
#define at_nlk	0004000		/* Not readable password if set */
#define at_ndl	0010000		/* No-dialups flag */
#define at_nnt	0020000		/* No-network flag */
#define at_nlg	0040000		/* No-login account */
#define at_cap	0100000		/* Captive account */

typedef struct {		/* Second quota and date/time block */
	word16	ulnk;		/* Link to next, flags */
	byte	uatyp;		/* Type */
	byte	a2_job;		/* Total job quota */
	word16	a2_rib;		/* RIB quota */
	word16	a2_msg;		/* Message limit quota */
	word16	fill1;		/* Reserved */
	byte	fill2;		/* Reserved */
	byte	a2_pwf;		/* Password failed count */
	word16	a2_ndt;		/* Date of Last non-interactive login */
	word16	a2_nti;		/* Time of Last non-interactive login */
} ua_qt2;
