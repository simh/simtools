/* absread and abswrite services for use with DJGPP GNU C implementation 
 * 
 * Paul Koning	94.10.16
 *		94.11.18	added bios i/o
 *		94.11.21	dos i/o for hard disk, bios i/o for floppy
 *		94.12.19	add retry for bios I/O
 *		95.01.05	update for generalized abs i/o in flx
 */

#include <bios.h>
#include <dos.h>
#include <go32.h>
#include <dpmi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flx.h"
#include "absio.h"

#define tb _go32_info_block.linear_address_of_transfer_buffer

#define BIOSBUF (_go32_info_block.size_of_transfer_buffer)
				/* size of djgpp bios disk I/O buffer */
#define BIOSMAX	(18 * BLKSIZE)	/* max size in biosdisk() call */
#define BIOSRESET 0		/* error reset */
#define BIOSREAD 2		/* bios disk read function code */
#define BIOSWRITE 3		/* bios disk write function code */
#define BIOSTRIES 4		/* retry count */
#define ABSREAD	0x25		/* abs read int code */
#define ABSWRITE 0x26		/* abs write int code */
#define BIOS_DATA_SEG	0x0040	/* BIOS data segment number */
#define BIOS_DSK_STATE	0x0090	/* offset to drive 0 media state */
#define DISK_STATE	((BIOS_DATA_SEG << 4) + BIOS_DSK_STATE)

#define BIOS_DSK_360K   0x74	/*  360kb media established */
#define BIOS_DSK_RX50   0x54	/*  RX50 media established in drive */
				/*    (same as 360kb except single steps */
				/*     for 96 tpi media) */

static int	secsize = 0;
static int	param_segment = 0;
static int	rx_segment;
static _go32_dpmi_seginfo oldvec;
static _go32_dpmi_seginfo rx50table_vec;
static unsigned short int	dosparam[5];
static int	rxflag = 0;	/* set if accessing 5.25 inch floppy */
static int	gdrive = -1;	/* drive to which geometry data applies */
static int	drive;		/* disk unit number currently open */
static int	sectors, heads, cylinders, drives;
static _go32_dpmi_seginfo param_info;

/*
 * Disk parameter table for RX50 floppies in 1.2MB drive
 * (set INT 1Eh vector to point to this)
 */
static byte dparm[] = {     0xDF,0x02,	/* "specify" cmd bytes */
			    0x25,	/* motor turn-on time */
			    2,10,	/* 512 b/s, 10 sec/tk */
			    20,		/* gap length */
			    -1,		/* max transfer */
			    24,		/* gap length (format) */
			    0xE5,	/* fill byte (format) */
			    15,		/* head settle time (ms) */
			    8 };	/* motor start time (1/8 sec) */

/* The param_buffer is used for two things: 
 * 1. to hold the dos abs disk I/O parameter block
 * 2. to hold the rx50 disk parameter table data
 * We put both into the same buffer, in separate segments.
 */

static void free_param_buffer()
{
	_go32_dpmi_free_dos_memory(&param_info);
	param_segment = 0;
}

static void alloc_param_buffer()
{
	if (param_segment) return;
	param_info.size = (UP(sizeof(param_info),16) + 
			   UP(sizeof(dparm),16)) / 16;
	if (_go32_dpmi_allocate_dos_memory(&param_info)) {
		param_segment = 0;
		return;
	}
	param_segment = param_info.rm_segment;
	rx_segment = param_segment + UP(sizeof(param_info),16) / 16;
	dosmemput(dparm, sizeof(dparm), rx_segment << 4);
	rx50table_vec.rm_segment = rx_segment;
	rx50table_vec.rm_offset = 0;
	atexit(free_param_buffer);
}

/* convert dos style drive number to bios style number */

static int biosdrive (int drive)
{
	if (drive < 3) return (drive - 1);
	else return ((drive - 3) + 0x80);	/* need to do partitions */
}

/* do bios I/O with retries */

static int biosdiskr (int func, int drive, int track, int cyl,
		      int sec, int count, void *buffer)
{
	int	tries, status;

	for (tries = 0; tries < BIOSTRIES; tries++) {
		status = biosdisk (func, biosdrive (drive), track, cyl,
				   sec, count, buffer);
		if (status == 0) break;

		/* strictly speaking one should do error classification
		 * at this point...
		 */
		biosdisk (BIOSRESET, biosdrive (drive), 0, 0, 0, 0, NULL);
	}
	return (status);
}

static void getgeom (int drive)
{
	byte	status[4];

	gdrive = drive;
	biosdisk (8, biosdrive (drive), 0, 0, 0, 1, status);
	heads = status[3] + 1;
	drives = status[2];
	cylinders = status[1] + ((status[0] >> 6) << 8) + 1;
	sectors = status[0] & 0x3f;
	if (status[0] == 15 && status[1] == 79 & status[3] == 1)
	    rxflag = 1;
	else rxflag = 0;
}

/* Convert block number to cylinder, head, sector for RT11-RX50.
 * This is different for RT11 than for Rainbow DOS:
 * For DOS, for cylinders 2 through 79, the sectors are interleaved 2:1.
 * (DOS capability is not supported in this RT11 version).
 * For RT11, all sectors are interleaved 2:1, and each subsequent 
 * track has the first logical block offset by 2 more sectors.
 */

static void makechs_rx50 (int block, int *trk, int *sec)
{
	int	t, s;
	
	t = block / 10;
	s = block % 10 + 1;
	if (s < 6) s = (s - 1) * 2 + 1;
	else	   s = (s - 5) * 2;
	s += t * 2;
	while (s > 10) s -= 10;
	t++;
	if (t == 80) t = 0;		/* wrap around last 10 blocks */
	*trk = t;
	*sec = s;
}

/* do single block disk I/O to DEC RX50 floppy */
static int rx50io (int func, int drive, int dsksec, void *buffer)
{
	byte	oldstate;
	byte	newstate = BIOS_DSK_RX50;
	int	cyl, sec;
	int	status;

	alloc_param_buffer();

	/* save old state and set up for RX50 I/O */
	_go32_dpmi_get_real_mode_interrupt_vector (0x1E, &oldvec);
	_go32_dpmi_set_real_mode_interrupt_vector (0x1E, &rx50table_vec);
#ifdef NOT
	dosmemget (DISK_STATE, 1, &oldstate);
	dosmemput (&newstate, 1, DISK_STATE);
#endif

	makechs_rx50 (dsksec, &cyl, &sec);
	status = biosdiskr (func, drive, 0, cyl, sec, 1, buffer);

	/* restore BIOS state as it was on entry */
	_go32_dpmi_set_real_mode_interrupt_vector (0x1E, &oldvec);
#ifdef NOT
	dosmemput (&oldstate, 1, DISK_STATE);
#endif
	return (status);
}

/* do bios disk I/O call 
 * if this is a 5.25" floppy, we do the required magic to read it as
 * a DEC RX50 format floppy.
 */

static int biosio (int func, int drive, int dsksec, int count, void *buffer)
{
	int	track, sec, cyl;
	int	tcount, status;

	if (drive != gdrive) getgeom (drive);
	while (count) {
		if (rxflag) {
			tcount = BLKSIZE;
			status = rx50io (func, drive, dsksec, buffer);
		} else {
			tcount = count;
			if (tcount > BIOSMAX) tcount = BIOSMAX;
			sec = dsksec % sectors;
			if ((sectors - sec) * BLKSIZE < tcount)
			    tcount = (sectors - sec) * BLKSIZE;
			sec++;			/* weird 1-based numbering */
			track = (dsksec / sectors) % heads;
			cyl = dsksec / (sectors * heads);
			status = biosdiskr (func, drive, track, cyl, 
					    sec, tcount / BLKSIZE, buffer);
		}
		if (status) return (status);
		count -= tcount;
		buffer += tcount;
		dsksec += (tcount / BLKSIZE);
	}
	return (0);
}

/* do absolute dos disk read/write 
 * arguments:
 *	function code (0x25 = read, 0x26 = write)
 *	drive number (1 = a:, etc)
 *	starting sector number
 *	byte count (must be multiple of sector size)
 *	buffer pointer
 */

static int dosio (int func, int drive, int sec, int count, void *buffer)
{
	int bseg, bofs, xfer=0, before=0, tcount;
	_go32_dpmi_registers	r;
  
	while (count) {
		tcount = count;
		if (tcount > BIOSBUF) tcount = BIOSBUF;
		alloc_param_buffer();
		dosparam[0] = sec & 0xffff;
		dosparam[1] = sec >> 16;
		dosparam[2] = (tcount / secsize) & 0xffff;
		dosparam[3] = (unsigned int) tb & 15;
		dosparam[4] = (unsigned int) tb >> 4;
		dosmemput(dosparam, sizeof(dosparam), param_segment << 4);
		if (func == ABSWRITE)
		    dosmemput(buffer, tcount, tb);
		memset(&r, 0, sizeof(r));
		r.h.al = drive - 1;		/* 0-based numbering here */
		r.x.ds = param_segment;
		r.x.bx = 0;
		r.x.cx = -1;
		_go32_dpmi_simulate_int(func, &r);
		if (func == ABSREAD)
		    dosmemget(tb, tcount, buffer);
		if (r.x.flags & 1)
		    return (r.h.al);
		count -= tcount;
		buffer += tcount;
		sec += (tcount / secsize);
	}
	return (0);
}

/* return size of specified disk, in blocks.  Saves sector size in a local
 * static variable as a side effect.  A flag is set if the disk is a
 * 5.25 inch floppy.
 * Note: this must be called before any abs I/O is done.
 */

static int disksize (int drive)
{
	_go32_dpmi_registers	r;
	byte			mid;

	if (drive >= 3) {		/* hard disk */
		memset(&r, 0, sizeof(r));
		r.h.ah = 0x1c;
		r.h.dl = drive;
		_go32_dpmi_simulate_int(0x21, &r);
		secsize = r.x.cx;
		return (r.h.al * r.x.dx * secsize / BLKSIZE);
	} else {
		getgeom (drive);
		secsize = 512;
		return (cylinders * heads * sectors);
	}
}

/* this routine is called to scan a supplied container file/device
 * name.  If the name refers to a real disk, then absflag is set
 * and rsize is set to the device size.  Otherwise, no action is taken.
 */

void absname (const char *rname)
{
	if (rname[strlen(rname) - 1] == ':') {	/* device name alone */
		absflag = TRUE;
		drive = tolower (rname[0]) - 'a' + 1; /* set drive number */
		rsize = disksize (drive);
		if (sw.debug != NULL)
		    printf ("disk size for drive %d is %d\n", drive, rsize);
	}
}

int absopen (const char *rname, const char *mode)
{
	return (0);			/* always ok, nothing to do */
}

void absseek (int block) { }		/* nothing to do */

void absclose () { }			/* nothing to do */

int absread (int sec, int count, void *buffer)
{
	if (drive >= 3)		/* hard disk */
	    return (dosio (ABSREAD, drive, sec, count, buffer));
	else
	    return (biosio (BIOSREAD, drive, sec, count, buffer));
}

int abswrite (int sec, int count, void *buffer)
{
#ifdef NOT_YET
	if (drive >= 3)		/* hard disk */
	    return (dosio (ABSWRITE, drive, sec, count, buffer));
	else
	    return (biosio (BIOSWRITE, drive, sec, count, buffer));
#endif
}

