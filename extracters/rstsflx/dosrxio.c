/* floppy disk I/O for DOS
 *
 * based on RXRTDVRA.ASM by Robert Morse and John Dudeck
 */

/* ---------------------------------------------------------------
 *                   IBM ROM BIOS Definitions
 * ---------------------------------------------------------------
 */

#define DKOP_RUPT	013h	/* interrupt to call ROM BIOS */
#define DKOP_RESET	000h	/*  reset controller */
#define DKOP_STATUS     001h	/*  read status from last operation */
#define DKOP_READ	002h	/*  read sectors */
#define DKOP_WRITE	003h	/*  write sectors */
#define DKOP_VERIFY	004h	/*  verify sectors */
#define DKOP_CHANGE	016h	/*  test changed status */
#define DKOP_SETTYPE	017h	/*  set media type in drive */

#define DKST_TIMEOUT	080h	/* drive not ready */
#define DKST_BADSEEK	040h	/* seek failed */
#define DKST_BADNEC	020h	/* NEC controller failed */
#define DKST_BADCRC	010h	/* read CRC error */
#define DKST_BADDMA	009h	/* attempt to DMA over 64K boundary */
#define DKST_OVERRUN	008h	/* DMA overrun */
#define DKST_CHANGED	006h	/* media changed */
#define DKST_RNF	004h	/* sector not found */
#define DKST_WRPROT	003h	/* write-protected diskette */
#define DKST_ADRMARK	002h	/* address mark not found */
#define DKST_BADCMD	001h	/* invalid command */


#define BIOS_DATA_SEG	0040h

/* #define BIOSDATA  segment at BIOS_DATA_SEG	/* BIOS data segment-- */

#define bios_dsk_state	(*(byte *)(0x0090))	/* drive 0 media state */

#define BIOS_DSK_360K   074h	/*  360kb media established */
#define BIOS_DSK_RX50   054h	/*  RX50 media established in drive */
				/*    (same as 360kb except single steps */
				/*     for 96 tpi media) */

/* Convert block number to cylinder, head, sector for RT11-RX50.
 * This is different for RT11 than for Rainbow DOS:
 * For DOS, for cylinders 2 through 79, the sectors are interleaved 2:1.
 * (DOS capability is not supported in this RT11 version).
 * For RT11, all sectors are interleaved 2:1, and each subsequent 
 * track has the first logical block offset by 2 more sectors.
 */

void makechs_rx50 (int block, int *trk, int *sec)
{
	int	t, s;
	
	t = block / 10;
	s = block % 10 + 1;
	if (s < 6) s = (s - 1) * 2 + 1;
	else	   s = (s - 5) * 2;
	s += t * 2;
	while (s > 10) s -= 10;
	t++;
	*trk = t;
	*sec = s;
}

;Common routine for read, write and verify.
;
;Given:    AL = operation code
;	   ES:DI = pointer to IOP, which contains
;		iop_block = starting block number
;		iop_bufptr = starting buffer address
;		iop_count = number of blocks
;Returns:  AX = IOP error code
;	   xx_count = number of requested blocks NOT transferred

do_readwrite:
	mov	es:iop_rwvoloff[di], offset vol_name
	mov	es:iop_rwvolseg[di], cs

	mov	xx_oper, al		;save operation code

	mov	ax, es:iop_block[di]	;set starting block number

	test	ax, ax			;JRD check for negative
	jge	do_rw1			;JRD
	xor	ax, ax			;JRD
do_rw1:
	push	si			;JRD
	mov	si, bpb_pointer		;JRD
	cmp	ax, bpb_totsects[si]	;JRD test for too big
	jle	do_rw2			;JRD
	mov	ax, bpb_totsects[si]	;JRD limit to maximum
do_rw2:
	pop	si			;JRD

	mov	xx_block, ax
	mov	ax, es:iop_count[di]	;set block count
	mov	xx_count, ax
	test	ax, ax
	jz	dorw_success		;  quit if 0 sectors to do

	mov	ax, es:iop_bufoff[di]	;set starting buffer offset
	mov	xx_offset, ax		;  and segment
	mov	ax, es:iop_bufseg[di]
	mov	xx_seg, ax

dorw_loop:
	mov	xx_retries, 5		;set retry counter
dorw_again:
	mov	ax, BIOS_DATA_SEG	;set diskette status to single
	mov	es, ax			;  stepping for 96 tpi
	mov	es:bios_dsk_state, BIOS_DSK_RX50

	mov	ax, xx_block		;load block number
	call	makechs_rx50		;  and convert to CHS
	mov	dl, PHYS_DRIVE_0	;set drive number
	mov	ah, xx_oper		;operation code
	mov	al, 1			;transfer 1 sector
	les	bx, xx_buf		;set ES:BX to buffer address
	int	DKOP_RUPT		;invoke ROM BIOS to do it
	mov	xx_status, ah		;  and save returned status
	test	ah, ah			;test for error
	jnz	dorw_error		;  break loop on error

	inc	xx_block		;advance to next block
	add	xx_offset, PHYS_BLKSIZE	;advance buffer pointer
	dec	xx_count		;count blocks
	jnz	dorw_loop		;  and continue until done
dorw_success:
	xor	ax, ax			;set no-error code
	ret
;Analyze read/write errors and either make another attempt or
;set the error code and return.

dorw_error:
	mov	al, IOPST_NOTRDY
	test	ah, DKST_TIMEOUT
	jnz	dorw_giveup

	mov	al, IOPST_SEEK
	test	ah, DKST_BADSEEK
	jnz	dorw_retry

	mov	al, IOPST_IOERR
	test	ah, DKST_BADNEC
	jnz	dorw_retry

	cmp	ah, DKST_OVERRUN
	je	dorw_retry

	mov	al, IOPST_CRC
	cmp	ah, DKST_BADCRC
	je	dorw_retry

	mov	al, IOPST_BADCMD
	cmp	ah, DKST_BADDMA
	je	dorw_giveup

	cmp	ah, DKST_BADCMD
	je	dorw_giveup

	mov	al, IOPST_BADCHNG
	cmp	ah, DKST_CHANGED
	jne	dorw_nochange
	cmp	open_count, 0
	jg	dorw_giveup		;error if change with any files open
	jmp	short dorw_reset	;  else do it again
dorw_nochange:
	mov	al, IOPST_RNF
	cmp	ah, DKST_RNF
	je	dorw_retry

	mov	al, IOPST_WRPROT
	cmp	ah, DKST_WRPROT
	je	dorw_giveup

	mov	al, IOPST_UNKMEDIA
	cmp	ah, DKST_ADRMARK
	je	dorw_retry

	mov	al, IOPST_IOERR
	jmp	short dorw_giveup
dorw_retry:
	dec	xx_retries		;count retries
	jle	dorw_giveup
dorw_reset:
	mov	ah, DKOP_RESET		;reset the disk controller
	int	DKOP_RUPT
	jmp	dorw_again		;  and try again
dorw_giveup:
	mov	ah, high IOPST_ERR	;complete the driver error return code
	ret

int rxread (int block, int size, void *buffer)
{
	return (rxio (DKOP_READ, block, size, buffer));
}

int rxwrite (int block, int size, void *buffer)
{
	return (rxio (DKOP_WRITE, block, size, buffer));
}
