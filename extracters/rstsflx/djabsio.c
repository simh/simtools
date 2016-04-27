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
#include <ctype.h>
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
#define DISK_STATE(d)	((BIOS_DATA_SEG << 4) + BIOS_DSK_STATE + ((d) - 1))

#define BIOS_DSK_360K   0x74	/*  360kb media established */
#define BIOS_DSK_RX50   0x54	/*  RX50 media established in drive */
				/*    (same as 360kb except single steps */
				/*     for 96 tpi media) */

static int	secsize = 0;
static int	param_segment = 0;
static unsigned short int	dosparam[5];
static int	rxflag = 0;	/* set if accessing 5.25 inch floppy */
static int	gdrive = -1;	/* drive to which geometry data applies */
static int	drive;		/* disk unit number currently open */
static int	sectors, heads, cylinders, drives;
static _go32_dpmi_seginfo param_info;

/* The param_buffer is used to hold the dos abs disk I/O parameter block
 */

static void free_param_buffer()
{
	_go32_dpmi_free_dos_memory(&param_info);
	param_segment = 0;
}

static void alloc_param_buffer()
{
	if (param_segment) return;
	param_info.size = UP(sizeof(param_info),16) / 16;
	if (_go32_dpmi_allocate_dos_memory(&param_info)) {
		param_segment = 0;
		return;
	}
	param_segment = param_info.rm_segment;
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
		      int sec, int count, void *buffer, int dparam)
{
	int	tries, status;

	for (tries = 0; tries < BIOSTRIES; tries++) {
		if (dparam) dosmemput (&dparam, 1, DISK_STATE(drive));
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
	_go32_dpmi_registers	r;

	gdrive = drive;
	memset(&r, 0, sizeof(r));
	r.h.ah = 8;
	r.h.dl = biosdrive (drive);
	_go32_dpmi_simulate_int(0x13, &r);
	heads = r.h.dh + 1;
	drives = r.h.dl;
	cylinders = r.h.ch + ((r.h.cl >> 6) << 8) + 1;
	sectors = r.h.cl & 0x3f;
	if (drive < 3 && (r.h.bl & 0x0f) == 2)	/* floppy && drive = 1.2MB */
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
	int	cyl, sec;
	int	status;

	alloc_param_buffer();

	/* save old state and set up for RX50 I/O */
	dosmemget (DISK_STATE(drive), 1, &oldstate);
	makechs_rx50 (dsksec, &cyl, &sec);
	status = biosdiskr (func, drive, 0, cyl, sec, 1, buffer, BIOS_DSK_RX50);

	/* restore BIOS state as it was on entry */
	dosmemput (&oldstate, 1, DISK_STATE(drive));
	return (status);
}

/* do bios disk I/O call 
 * if this is a 5.25" floppy, we do the required magic to read it as
 * a DEC RX50 format floppy.
 */

static long biosio (int func, int drive, long dsksec, long count, void *buffer)
{
	long	track, sec, cyl;
	long	tcount, status;

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
					    sec, tcount / BLKSIZE, buffer, 0);
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

static long dosio (int func, int drive, int sec, long count, void *buffer)
{
	long	tcount;
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
		    printf ("disk size for drive %d is %ld\n", drive, rsize);
	}
}

int absopen (const char *rname, const char *mode)
{
	return (0);			/* always ok, nothing to do */
}

void absseek (long block) { }		/* nothing to do */

void absclose () { }			/* nothing to do */

long absread (long sec, long count, void *buffer)
{
	if (drive >= 3)		/* hard disk */
	    return (dosio (ABSREAD, drive, sec, count, buffer));
	else
	    return (biosio (BIOSREAD, drive, sec, count, buffer));
}

long abswrite (long sec, long count, void *buffer)
{
	if (drive >= 3)		/* hard disk */
	    return (dosio (ABSWRITE, drive, sec, count, buffer));
	else
	    return (biosio (BIOSWRITE, drive, sec, count, buffer));
}

