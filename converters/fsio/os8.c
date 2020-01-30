/*
 * Copyright (C) 2019 John Forecast. All Rights Reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN FORECAST "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Support routines for handling OS/8 file systems under fsio
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <regex.h>

#include "fsio.h"

static int rk05BlockPresent(struct mountedFS *, uint8_t, unsigned int);
static int rk05ReadBlock(struct mountedFS *, uint8_t, unsigned int, void *);
static int rk05WriteBlock(struct mountedFS *, uint8_t, unsigned int, void *);
static int rx01BlockPresent(struct mountedFS *, uint8_t, unsigned int);
static int rx01ReadBlock(struct mountedFS *, uint8_t, unsigned int, void *);
static int rx01WriteBlock(struct mountedFS *, uint8_t, unsigned int, void *);
static int rx02BlockPresent(struct mountedFS *, uint8_t, unsigned int);
static int rx02ReadBlock(struct mountedFS *, uint8_t, unsigned int, void *);
static int rx02WriteBlock(struct mountedFS *, uint8_t, unsigned int, void *);

/*
 * Floppy interleave tables:
 *
 * RX01:
 *      Each OS/8 logical block maps into 4 sectors (interleave 2)
 *
 * RX02:
 *      Each OS/8 logical block maps into 2 sectors (interleave 3)
 */
#define RX01INTLV(t, s) ((t * OS8_RX0xNSECT) + s - 1)
#define RX02INTLV(t, s) ((t * OS8_RX0xNSECT) + s - 1)

static uint16_t rx01interleave[] = {
  RX01INTLV(0, 1),  RX01INTLV(0, 3),   RX01INTLV(0, 5),  RX01INTLV(0, 7),
  RX01INTLV(0, 9),  RX01INTLV(0, 11),  RX01INTLV(0, 13), RX01INTLV(0, 15),
  RX01INTLV(0, 17), RX01INTLV(0, 19),  RX01INTLV(0, 21), RX01INTLV(0, 23),
  RX01INTLV(0, 25), RX01INTLV(0, 2),   RX01INTLV(0, 4),  RX01INTLV(0, 6),
  RX01INTLV(0, 8),  RX01INTLV(0, 10),  RX01INTLV(0, 12), RX01INTLV(0, 14),
  RX01INTLV(0, 16), RX01INTLV(0, 18),  RX01INTLV(0, 20), RX01INTLV(0, 22),
  RX01INTLV(0, 24), RX01INTLV(0, 26),  RX01INTLV(1, 1),  RX01INTLV(1, 3),
  RX01INTLV(1, 5),  RX01INTLV(1, 7),   RX01INTLV(1, 9),  RX01INTLV(1, 11),
  RX01INTLV(1, 13), RX01INTLV(1, 15),  RX01INTLV(1, 17), RX01INTLV(1, 19),
  RX01INTLV(1, 21), RX01INTLV(1, 23),  RX01INTLV(1, 25), RX01INTLV(1, 2),
  RX01INTLV(1, 4),  RX01INTLV(1, 6),   RX01INTLV(1, 8),  RX01INTLV(1, 10),
  RX01INTLV(1, 12), RX01INTLV(1, 14),  RX01INTLV(1, 16), RX01INTLV(1, 18),
  RX01INTLV(1, 20), RX01INTLV(1, 22),  RX01INTLV(1, 24), RX01INTLV(1, 26)
};
#define RX01REPEAT ((sizeof(rx01interleave)/sizeof(rx01interleave[0])) / 4)

static uint16_t rx02interleave[] = {
  RX02INTLV(0, 1),  RX02INTLV(0, 4),
  RX02INTLV(0, 7),  RX02INTLV(0, 10),
  RX02INTLV(0, 13), RX02INTLV(0, 16),
  RX02INTLV(0, 19), RX02INTLV(0, 22),
  RX02INTLV(0, 25), RX02INTLV(0, 2),
  RX02INTLV(0, 5),  RX02INTLV(0, 8),
  RX02INTLV(0, 11), RX02INTLV(0, 14),
  RX02INTLV(0, 17), RX02INTLV(0, 20),
  RX02INTLV(0, 23), RX02INTLV(0, 26),
  RX02INTLV(0, 3),  RX02INTLV(0, 6),
  RX02INTLV(0, 9),  RX02INTLV(0, 12),
  RX02INTLV(0, 15), RX02INTLV(0, 18),
  RX02INTLV(0, 21), RX02INTLV(0, 24)
};
#define RX02REPEAT ((sizeof(rx02interleave)/sizeof(rx02interleave[0])) / 2)

/*
 * Six-bit code => character mapping table
 */
static char sixbit[64] = {
    ' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
    'X', 'Y', 'Z', '[', '\\', ']', '^', '_',
    ' ', '!', '"', '#', '$', '%', '&', '\'',
    '(', ')', '*', '+', ',', '-', '.', '/',
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8','9', ':', ';', '<', '=', '>', '?'
};

/*
 * Month of the year - allow 4-bit index field
 */
static char *os8Month[] = {
  "???", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul",
  "Aug", "Sep", "Oct", "Nov", "Dec", "???", "???", "???"
};

/*
 * Table of "set" commands
 */
static char *setCmds[] = {
  "year",
  NULL
};
#define OS8SET_YEAR     0

static void os8CloseFile(void *);

extern int args;
extern char **words;
extern int quiet;

/*++
 *      o s 8 D e v i c e s
 *
 * List of OS/8 disk device types supported by this program.
 *
 --*/
struct OS8device OS8Devices[] = {
  { "rk05", 2, 2 * OS8_RK05FS_BLKS,
    0,
    { OS8_RK05FS_BLKS, OS8_RK05FS_BLKS, 0, 0, 0, 0, 0,0 },
       rk05BlockPresent, rk05ReadBlock, rk05WriteBlock },

  { "rx01", 1, (OS8_RX0xSZ * OS8_RX01SS_W) / OS8_BLOCKSIZE,
    OS8_RX0xNSECT * OS8_RX01SS,
    { ((OS8_RX0xSZ - OS8_RX0xNSECT) * OS8_RX01SS_W) / OS8_BLOCKSIZE,
        0, 0, 0, 0, 0, 0, 0 },
       rx01BlockPresent, rx01ReadBlock, rx01WriteBlock },

  { "rx02", 1, (OS8_RX0xSZ * OS8_RX02SS_W) / OS8_BLOCKSIZE,
    OS8_RX0xNSECT * OS8_RX02SS,
    { ((OS8_RX0xSZ - OS8_RX0xNSECT) * OS8_RX02SS_W) / OS8_BLOCKSIZE,
        0, 0, 0, 0, 0, 0, 0 },
       rx02BlockPresent, rx02ReadBlock, rx02WriteBlock },

  { NULL, 0, 0,
    0,
    { 0, 0, 0, 0, 0, 0, 0, 0 },
       NULL, NULL, NULL }
};

/*++
 *      o s 8 W o r d
 *
 *  Extract an unsigned 12-bit value.
 *
 * Inputs:
 *
 *      value           - 16-bit incoming value
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      16-bit value with the high 4-bit zeroed
 *
 --*/
static uint16_t os8Word(
  uint16_t value
)
{
  return value & 0xFFF;
}

/*++
 *      o s 8 S X T
 *
 *  Extract a 12-bit value extending the size bit.
 *
 * Inputs:
 *
 *      value           - 16-bit incoming value
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Original value with sign extended to 16-bits
 *
 --*/
static uint16_t os8SXT(
  uint16_t value
)
{
  return (value & 0xFFF) | (((value & 0x800) != 0) ? 0xF000 : 0);
}

/*++
 *      o s 8 N e g
 *
 *  Negate a 12-bit value to a 16-bit value leading to a result in the range
 *  0 - 4095.
 *
 * Inputs:
 *
 *      value           - 16-bit incoming value
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Negated value
 *
 --*/
static uint16_t os8Neg(
  uint16_t value
)
{
  uint16_t value16 = os8SXT(value);

  return (uint16_t)((0 - value16) & 0xFFF);
}

/*++
 *      o s 8 V a l u e
 *
 *  Truncate a 16-bit value to 12-bits.
 *
 * Inputs:
 *
 *      value           - 16-bit incoming value
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      The 12-bit truncated value
 *
 --*/
static uint16_t os8Value(
  uint16_t value
)
{
  return value & 0xFFF;
}

/*++
 *      o s 8 D a t e
 *
 *  Construct a user readable date string given an OS/8 date value. The caller
 *  must supply a suitably sized buffer to contain the date string. If the
 *  supplied date value is zero, the date string will be all spaces.
 *
 * Inputs:
 *
 *      date            - OS/8 date value
 *      buf             - pointer to buffer to receive the file name
 *                        (Must be at least 12 bytes)
 *
 * Outputs:
 *
 *      The buffer will be overwritten with the date string.
 *
 * Returns:
 *
 *      Pointer to the NULL terminated file name string
 *
 --*/
static char *os8Date(
  uint16_t date,
  char *buf
)
{
  if (date != 0) {
    sprintf(buf, "%2d-%s-%4d",
            (date >> 3) & 0x1F,
            os8Month[(date >> 8) & 0xF],
            1970 + (date & 0x7));
  } else strcpy(buf, "           ");

  return buf;
}

/*++
 *      o s 8 F i l e N a m e
 *
 *  Construct a user readable filename string from a directory entry in the
 *  mount point specific buffer.  The caller must supply a suitably sized
 *  buffer to contain the file name string.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      off             - offset of the directory entry
 *      buf             - pointer to buffer to receive the file name
 *                        (Must be at least 10 bytes)
 *
 * Outputs:
 *
 *      The buffer will be overwritten with the file name
 *
 * Returns:
 *
 *      Pointer to the NULL terminated file name string
 *
 --*/
static char *os8FileName(
  struct mountedFS *mount,
  uint16_t off,
  char *buf
)
{
  struct OS8data *data = &mount->os8data;
  uint16_t fname1 = os8Word(le16toh(data->buf[off + OS8_DI_FNAME1]));
  uint16_t fname2 = os8Word(le16toh(data->buf[off + OS8_DI_FNAME2]));
  uint16_t fname3 = os8Word(le16toh(data->buf[off + OS8_DI_FNAME3]));
  uint16_t ext = os8Word(le16toh(data->buf[off + OS8_DI_EXT]));

  buf[0] = sixbit[(fname1 >> 6) & 0x3F];
  buf[1] = sixbit[fname1 & 0x3F];
  buf[2] = sixbit[(fname2 >> 6) & 0x3F];
  buf[3] = sixbit[fname2 & 0x3F];
  buf[4] = sixbit[(fname3 >> 6) & 0x3F];
  buf[5] = sixbit[fname3 & 0x3F];
  buf[6] = '.';
  buf[7] = sixbit[(ext >> 6) & 0x3F];
  buf[8] = sixbit[ext & 0x3F];
  buf[9] = '\0';

  return buf;
}

/*++
 *      o s 8 U n i t V a l i d
 *
 *  Determine if a provided unit # is valid.
 *
 * Inputs:
 *
 *      data            - pointer to OS/8 specific data area
 *      unit            - file system unit number
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if unit # is valid, 0 otherwise
 *
 --*/
static int os8UnitValid(
  struct OS8data *data,
  uint8_t unit
)
{
  struct OS8device *device;

  if ((device = data->device) != NULL) {
    if (unit < device->filesys)
      return (data->valid & (1 << unit)) != 0 ? 1 : 0;
  }
  return 0;
}

/*++
 *      r k 0 5 B l o c k P r e s e n t
 *
 *  Check if the specified block is present in the container file.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      block           - logical block # in the range 0 - N
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if block is present in the container file, 0 otherwise
 *
 --*/
static int rk05BlockPresent(
  struct mountedFS *mount,
  uint8_t unit,
  unsigned int block
)
{
  struct OS8data *data = &mount->os8data;
  unsigned int base = unit == 0 ? 0 : OS8_RK05FS_BLKS;

  return data->blocks > (base + block);
}

/*++
 *      r k 0 5 R e a d B l o c k
 *
 *  Read a block from an OS/8 file system on an RK05 disk.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      block           - logical block # in the range 0 - N
 *      buf             - buffer to receive data
 *
 * Outputs:
 *
 *      The block will be read into the specified buffer
 *
 * Returns:
 *
 *      1 if successful, 0 otherwise
 *
 --*/
static int rk05ReadBlock(
  struct mountedFS *mount,
  uint8_t unit,
  unsigned int block,
  void *buf
)
{
  int status = 0;
  unsigned int base = unit == 0 ? 0 : OS8_RK05FS_BLKS;

  if (block >= OS8_RK05FS_BLKS) {
    ERROR("Attempt to read block (%u) outside file system \"%s%o:\"\n",
          block, mount->name, unit);
    return 0;
  }

  status = FSioReadBlock(mount, base + block, buf);

  if (status == 0)
    ERROR("I/O error on \"%s%o:\"\n", mount->name, unit);

  return status;
}

/*++
 *      r k 0 5 W r i t e B l o c k
 *
 *  Write a block to an OS/8 file system on an RK05 disk.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      block           - logical block # in the range 0 - N
 *      buf             - buffer containing the data
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if successful, 0 otherwise
 *
 --*/
static int rk05WriteBlock(
  struct mountedFS *mount,
  uint8_t unit,
  unsigned int block,
  void *buf
)
{
  int status = 0;
  unsigned int base = unit == 0 ? 0 : OS8_RK05FS_BLKS;

  if (block >= OS8_RK05FS_BLKS) {
    ERROR("Attempt to write block (%u) outside file system \"%s%o:\"\n",
          block, mount->name, unit);
    return 0;
  }

  status = FSioWriteBlock(mount, base + block, buf);

  if (status == 0)
    ERROR("I/O error on \"%s%o:\"\n", mount->name, unit);

  return status;
}

/*++
 *      r x 0 1 B l o c k P r e s e n t
 *
 *  Check if the specified block is present in the container file.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      block           - logical block # in the range 0 - N
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if block is present in the container file, 0 otherwise
 *
 --*/
static int rx01BlockPresent(
  struct mountedFS *mount,
  uint8_t UNUSED(unit),
  unsigned int block
)
{
  struct OS8data *data = &mount->os8data;

  return data->blocks > block;
}

/*++
 *      r x 0 1 R e a d B l o c k
 *
 *  Read a block from an OS/8 file system on an RX01 disk.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      block           - logical block # in the range 0 - N
 *      buf             - buffer to receive data
 *
 * Outputs:
 *
 *      The block will be read into the specified buffer
 *
 * Returns:
 *
 *      1 if successful, 0 otherwise
 *
 --*/
static int rx01ReadBlock(
  struct mountedFS *mount,
  uint8_t unit,
  unsigned int block,
  void *buf
)
{
  unsigned int base = (block / RX01REPEAT) * RX01REPEAT;
  unsigned int offset = block % RX01REPEAT;
  uint16_t *buffer = buf;
  int i, j, k, status;

#ifdef DEBUG
  if ((mount->flags & FS_DEBUG) != 0)
    fprintf(DEBUGout, "  >>%s%o: rx01ReadBlock, base %u, offset %u\n",
            mount->name, unit, base, offset);
#endif

  /*
   * Convert to sector number
   */
  base *= OS8_BLOCKSIZE / OS8_RX01SS_W;

  /*
   * Read 4 sectors into the buffer
   */
  for (i = 0; i < 4; i++) {
    unsigned int sector = base + rx01interleave[(offset * 4) + i];
    unsigned char temp[OS8_RX01SS];

#ifdef DEBUG
    if ((mount->flags & FS_DEBUG) != 0)
      fprintf(DEBUGout, "  >> %s%o: (os8) Reading %u, track %u sector %u\n",
              mount->name, unit, sector,
              base + ((rx01interleave[(offset * 4) + i] + 1) / OS8_RX01SS),
              (rx01interleave[(offset * 4) + i] + 1) % OS8_RX01SS);
#endif

    if ((status = FSioReadSector(mount, sector, OS8_RX01SS, temp)) == 0) {
      ERROR("I/O error on \"%s%o:\"\n", mount->name, unit);
      return 0;
    }

    /*
     * Unpack 64 words from the temporary buffer
     */
    for (j = 0, k = 0; j < OS8_RX01SS_W; j += 2, k += 3) {
      buffer[j] = ((temp[k] << 4) | ((temp[k + 1] >> 4) & 0xF)) & 0xFFF;
      buffer[j + 1] = (((temp[k + 1] << 8) & 0xF00) | temp[k + 2]) & 0xFFF;
    }
    buffer += OS8_RX01SS_W;
  }
  return 1;
}

/*++
 *      r x 0 1 W r i t e B l o c k
 *
 *  Write a block to an OS/8 file system on an RX01 disk.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      block           - logical block # in the range 0 - N
 *      buf             - buffer containing the data
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if successful, 0 otherwise
 *
 --*/
static int rx01WriteBlock(
  struct mountedFS *mount,
  uint8_t unit,
  unsigned int block,
  void *buf
)
{
  unsigned int base = (block / RX01REPEAT) * RX01REPEAT;
  unsigned int offset = block % RX01REPEAT;
  uint16_t *buffer = buf;
  int i, j, k, status;

#ifdef DEBUG
  if ((mount->flags & FS_DEBUG) != 0)
    fprintf(DEBUGout, "  >>%s%o: rx01WriteBlock, base %u, offset %u\n",
            mount->name, unit, base, offset);
#endif

  /*
   * Convert to sector number
   */
  base *= OS8_BLOCKSIZE / OS8_RX01SS_W;

  /*
   * Write 4 sectors from the buffer
   */
  for (i = 0; i < 4; i++) {
    unsigned int sector = base + rx01interleave[(offset * 4) + i];
    unsigned char temp[OS8_RX01SS];

    /*
     * Pack 64 words into the temporary buffer
     */
    for (j = 0, k = 0; j < OS8_RX01SS_W; j += 2, k += 3) {
      temp[k] = (buffer[j] >> 4) & 0xFF;
      temp[k + 1] = ((buffer[j] << 4) & 0xF0) | ((buffer[j + 1] >> 8) & 0xF);
      temp[k + 2] = buffer[j + 1] & 0xFF;
    }

#ifdef DEBUG
    if ((mount->flags & FS_DEBUG) != 0)
      fprintf(DEBUGout, "  >> %s%o: (os8) Writing %u, track %u sector %u\n",
              mount->name, unit, sector,
              base + ((rx01interleave[(offset * 4) + i] + 1) / OS8_RX01SS),
              (rx01interleave[(offset * 4) + i] + 1) % OS8_RX01SS);
#endif

    if ((status = FSioWriteSector(mount, sector, OS8_RX01SS, temp)) == 0) {
      ERROR("I/O error on \"%s%o:\"\n", mount->name, unit);
      return 0;
    }
    buffer += OS8_RX01SS_W;
  }
  return 1;
}

/*++
 *      r x 0 2 B l o c k P r e s e n t
 *
 *  Check if the specified block is present in the container file.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      block           - logical block # in the range 0 - N
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if block is present in the container file, 0 otherwise
 *
 --*/
static int rx02BlockPresent(
  struct mountedFS *mount,
  uint8_t UNUSED(unit),
  unsigned int block
)
{
  struct OS8data *data = &mount->os8data;

  return data->blocks > block;
}

/*++
 *      r x 0 2 R e a d B l o c k
 *
 *  Read a block from an OS/8 file system on an RX01 disk.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      block           - logical block # in the range 0 - N
 *      buf             - buffer to receive data
 *
 * Outputs:
 *
 *      The block will be read into the specified buffer
 *
 * Returns:
 *
 *      1 if successful, 0 otherwise
 *
 --*/
static int rx02ReadBlock(
  struct mountedFS *mount,
  uint8_t unit,
  unsigned int block,
  void *buf
)
{
  unsigned int base = (block / RX02REPEAT) * RX02REPEAT;
  unsigned int offset = block % RX02REPEAT;
  uint16_t *buffer = buf;
  int i, j, k, status;

#ifdef DEBUG
  if ((mount->flags & FS_DEBUG) != 0)
    fprintf(DEBUGout, "  >>%s%o: rx02ReadBlock, base %u, offset %u\n",
            mount->name, unit, base, offset);
#endif

  /*
   * Convert to sector number
   */
  base *= OS8_BLOCKSIZE / OS8_RX02SS_W;

  /*
   * Read 2 sectors into the buffer
   */
  for (i = 0; i < 2; i++) {
    unsigned int sector = base + rx02interleave[(offset * 2) + i];
    unsigned char temp[OS8_RX02SS];

#ifdef DEBUG
    if ((mount->flags & FS_DEBUG) != 0)
      fprintf(DEBUGout, "  >> %s%o: (os8) Reading %u, track %u sector %u\n",
              mount->name, unit, sector,
              base + ((rx02interleave[(offset * 2) + i] + 1) / OS8_RX02SS),
              (rx02interleave[(offset * 2) + i] + 1) % OS8_RX02SS);
#endif

    if ((status = FSioReadSector(mount, sector, OS8_RX02SS, temp)) == 0) {
      ERROR("I/O error on \"%s%o:\"\n", mount->name, unit);
      return 0;
    }

    /*
     * Unpack 128 words from the temporary buffer
     */
    for (j = 0, k = 0; j < OS8_RX02SS_W; j += 2, k += 3) {
      buffer[j] = ((temp[k] << 4) | ((temp[k + 1] >> 4) & 0xF)) & 0xFFF;
      buffer[j + 1] = (((temp[k + 1] << 8) & 0xF00) | temp[k + 2]) & 0xFFF;
    }
    buffer += OS8_RX02SS_W;
  }
  return 1;
}

/*++
 *      r x 0 2 W r i t e B l o c k
 *
 *  Write a block to an OS/8 file system on an RX01 disk.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      block           - logical block # in the range 0 - N
 *      buf             - buffer containing the data
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if successful, 0 otherwise
 *
 --*/
static int rx02WriteBlock(
  struct mountedFS *mount,
  uint8_t unit,
  unsigned int block,
  void *buf
)
{
  unsigned int base = (block / RX02REPEAT) * RX02REPEAT;
  unsigned int offset = block % RX02REPEAT;
  uint16_t *buffer = buf;
  int i, j, k, status;

#ifdef DEBUG
  if ((mount->flags & FS_DEBUG) != 0)
    fprintf(DEBUGout, "  >>%s%o: rx01WriteBlock, base %u, offset %u\n",
            mount->name, unit, base, offset);
#endif

  /*
   * Convert to sector number
   */
  base *= OS8_BLOCKSIZE / OS8_RX02SS_W;

  /*
   * Write 2 sectors from the buffer
   */
  for (i = 0; i < 2; i++) {
    unsigned int sector = base + rx02interleave[(offset * 2) + i];
    unsigned char temp[OS8_RX02SS];

    /*
     * Pack 128 words into the temporary buffer
     */
    for (j = 0, k = 0; j < OS8_RX02SS_W; j += 2, k += 3) {
      temp[k] = (buffer[j] >> 4) & 0xFF;
      temp[k + 1] = ((buffer[j] << 4) & 0xF0) | ((buffer[j + 1] >> 8) & 0xF);
      temp[k + 2] = buffer[j + 1] & 0xFF;
    }

#ifdef DEBUG
    if ((mount->flags & FS_DEBUG) != 0)
      fprintf(DEBUGout, "  >> %s%o: (os8) Writing %u, track %u sector %u\n",
              mount->name, unit, sector,
              base + ((rx02interleave[(offset * 2) + i] + 1) / OS8_RX02SS),
              (rx02interleave[(offset * 2) + i] + 1) % OS8_RX02SS);
#endif

    if ((status = FSioWriteSector(mount, sector, OS8_RX02SS, temp)) == 0) {
      ERROR("I/O error on \"%s%o:\"\n", mount->name, unit);
      return 0;
    }
    buffer += OS8_RX02SS_W;
  }
  return 1;
}

/*++
 *      o s 8 B l o c k P r e s e n t
 *
 *  Check if a block is present in the container file.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      block           - logical block # in the range 0 - N
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if the block is present, 0 otherwise
 *
 --*/
int os8BlockPresent(
  struct mountedFS *mount,
  uint8_t unit,
  unsigned int block
)
{
  struct OS8data *data = &mount->os8data;

  return (*data->device->blockPresent)(mount, unit, block);
}

/*++
 *      o s 8 R e a d B l o c k
 *
 *  Read a block from an OS/8 file system.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      block           - logical block # in the range 0 - N
 *      buf             - buffer to receive data, if NULL use the mount
 *                        point specific buffer
 *
 * Outputs:
 *
 *      The block will be read into the specified buffer.
 *
 * Returns:
 *
 *      1 if successful, 0 otherwise
 *
 --*/
int os8ReadBlock(
  struct mountedFS *mount,
  uint8_t unit,
  unsigned int block,
  void *buf
)
{
  struct OS8data *data = &mount->os8data;
  void *buffer = buf == NULL ? data->buf : buf;

  if ((unit >= data->device->filesys) || ((data->valid & (1 << unit)) == 0)) {
    ERROR("Invalid device \"%s%o:\"\n", mount->name, unit);
    return 0;
  }

#ifdef DEBUG
  if ((mount->flags & FS_DEBUG) != 0)
    fprintf(DEBUGout, ">> %s%o: (os8) Reading logical block %u\n",
            mount->name, unit, block);
#endif

  return (*data->device->readBlock)(mount, unit, block, buffer);
}

/*++
 *      o s 8 W r i t e B l o c k
 *
 *  Write a block to an OS/8 file system.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      block           - logical block # in the range 0 - N
 *      buf             - buffer to receive data, if NULL use the mount
 *                        point specific buffer
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if successful, 0 otherwise
 *
 --*/
int os8WriteBlock(
  struct mountedFS *mount,
  uint8_t unit,
  unsigned int block,
  void *buf
)
{
  struct OS8data *data = &mount->os8data;
  void *buffer = buf == NULL ? data->buf : buf;

  if ((unit >= data->device->filesys) || ((data->valid & (1 << unit)) == 0)) {
    ERROR("Invalid device \"%s%o:\"\n", mount->name, unit);
    return 0;
  }

#ifdef DEBUG
  if ((mount->flags & FS_DEBUG) != 0)
    fprintf(DEBUGout, ">> %s%o: (os8) Writing logical block %u\n",
            mount->name, unit, block);
#endif

  return (*data->device->writeBlock)(mount, unit, block, buffer);
}

/*++
 *      o s 8 C h e c k D i r e c t o r y
 *
 *  Check if there is sufficient space available for a new permanent entry
 *  in the directory segment currently in the mount specific buffer.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if there is sufficient space available, 0 otherwise
 *
 --*/
static int os8CheckDirectory(
  struct mountedFS *mount
)
{
  struct OS8data *data = &mount->os8data;
  uint16_t i, entrysz, entries, off = OS8_DH_SIZE;

  entrysz = OS8_DI_SIZE + (-os8SXT(le16toh(data->buf[OS8_DH_EXTRA])));
  entries = os8Neg(le16toh(data->buf[OS8_DH_ENTRIES]));

  for (i = 0; i < entries; i++)
    if (os8Word(le16toh(data->buf[off + OS8_DI_FNAME1])) != 0)
      off += entrysz;
    else off += OS8_ED_SIZE;

  return (off + entrysz) < OS8_BLOCKSIZE ? 1 : 0;
}

/*++
 *      o s 8 F i n d S p a c e
 *
 *  Determine which directory segment has available free space. This routine
 *  may be called to find the "best fit" (i.e. smallest free space >= the
 *  requested size) or the largest available free space. if successful,
 *  the directory segment will be loaded in the mount specific buffer.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      space           - # of blocks of free space required, 0 means find
 *                        the largest block of free space
 *      segment         - return directory segment here
 *      offset          - return directory offset here
 *      start           - return starting block # here
 *
 * Outputs:
 *
 *      The mount specific buffer will be modifed.
 *
 * Returns:
 *
 *      1 if free space was found, 0 otherwise
 *
 --*/
static int os8FindSpace(
  struct mountedFS *mount,
  uint8_t unit,
  uint16_t space,
  uint16_t *segment,
  uint16_t *offset,
  uint16_t *start
)
{
  struct OS8data *data = &mount->os8data;
  int found = 0;
  uint16_t dirseg, diroff, dirstart, size = space == 0 ? 0 : 0xFFFF;
  uint16_t dirblk = OS8_DSSTART;

  if (os8UnitValid(data, unit)) {
    do {
      uint16_t i, entrysz, entries, extra, startblk, off = OS8_DH_SIZE;

      if (os8ReadBlock(mount, unit, dirblk, NULL) == 0)
        return 0;

      entrysz = OS8_DI_SIZE + (-os8SXT(le16toh(data->buf[OS8_DH_EXTRA])));
      entries = os8Neg(le16toh(data->buf[OS8_DH_ENTRIES]));
      extra = os8Neg(le16toh(data->buf[OS8_DH_EXTRA]));

      startblk = os8Word(le16toh(data->buf[OS8_DH_START]));

      for (i = 0; i < entries; i++) {
        uint16_t blks;

        if (os8Word(le16toh(data->buf[off + OS8_DI_FNAME1])) == 0) {
          blks = os8Neg(le16toh(data->buf[off + OS8_ED_LENGTH]));

          if (space == 0) {
            if (blks > size) {
              size = blks;
              dirseg = dirblk;
              diroff = off;
              dirstart = startblk;
              found = 1;
            }
          } else {
              if (blks >= space)
                if (blks < size) {
                  size = blks;
                  dirseg = dirblk;
                  diroff = off;
                  dirstart = startblk;
                  found = 1;
                }
          }
          off += OS8_ED_SIZE;
        } else {
          blks = os8Neg(le16toh(data->buf[off + extra + OS8_DI_LENGTH]));
          off += entrysz;
        }
        startblk += blks;
      }

      dirblk = os8Word(le16toh(data->buf[OS8_DH_NEXT]));
    } while (dirblk != 0);

    /*
     * If we found a suitable entry, load the directory segment and update
     * return information.
     */
    if (found) {
      if (os8ReadBlock(mount, unit, dirseg, NULL) == 0)
        return 0;

      *segment = dirseg;
      *offset = diroff;
      *start = dirstart;
    }
  }
  return found;
}

/*++
 *      o s 8 S l i d e D o w n
 *
 *  Open up a new permanent directory entry at the specified offset within
 *  a directory segment. This routine assumes thatnthe caller has verified
 *  that there is sufficient free space at the end of the directory for a new
 *  entry. The directory segment is in the mount specific buffer.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      off             - offset to add the new directory entry
 *      entries         - # of directory entries
 *      entrysz         - size of a permanent directory entry
 *      remain          - return remaining entries here
 *
 * Outputs:
 *
 *      The mount specific buffer will be modified.
 *
 * Returns:
 *
 *      None
 *
 --*/
static void os8SlideDown(
  struct mountedFS *mount,
  uint16_t off,
  uint16_t entries,
  uint16_t entrysz,
  uint16_t *remain
)
{
  struct OS8data *data = &mount->os8data;
  uint16_t i, j, src = OS8_DH_SIZE, dst = OS8_DH_SIZE;
  uint16_t buf[OS8_BLOCKSIZE];

  /*
   * Build an updated directory header
   */
  buf[OS8_DH_ENTRIES] = htole16(os8Value(-(entries + 1)));
  buf[OS8_DH_START] = data->buf[OS8_DH_START];
  buf[OS8_DH_NEXT] = data->buf[OS8_DH_NEXT];
  buf[OS8_DH_FLAGWD] = htole16(os8Value(0));
  buf[OS8_DH_EXTRA] = data->buf[OS8_DH_EXTRA];

  for (i = 0; i < entries; i++) {
    uint16_t len;

    if (src == off) {
      dst += entrysz;
      *remain = entries - (i + 1);
    }

    len = 
      os8Word(le16toh(data->buf[src + OS8_DI_FNAME1])) ? entrysz : OS8_ED_SIZE;

    for (j = 0; j < len; j++)
      buf[dst++] = data->buf[src++];
  }

  /*
   * Now copy the updated directory segment back to the mount speccific buffer
   */
  memcpy(data->buf, buf, sizeof(buf));
}

/*++
 *      o s 8 S l i d e U p
 *
 *  Remove a full or partial directory entry by sliding the remaining entries
 *  up. The directory segment is in the mount specific buffer.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      dst             - offset to entry being removed
 *      src             - offset to first entry to me moved
 *      entrysz         - size of a permanent directory entry
 *      count           - # of entries to "slide up"
 *
 * Outputs:
 *
 *      The mount specific buffer may be modified
 *
 * Returns:
 *
 *      None
 *
 --*/
static void os8SlideUp(
  struct mountedFS *mount,
  uint16_t dst,
  uint16_t src,
  uint16_t entrysz,
  uint16_t count
)
{
  struct OS8data *data = &mount->os8data;
  uint16_t i, j, len;

  if (count != 0) {
    for (i = 0; i < count; i++) {
      len = os8Word(le16toh(data->buf[src + OS8_DI_FNAME1])) ? entrysz : OS8_ED_SIZE;

      for (j = 0; j < len; j++)
        data->buf[dst++] = data->buf[src++];
    }
  }
}

/*++
 *      o s 8 S p l i t D i r e c t o r y
 *
 *  Split the directory segment currently in the mount specific buffer. The
 *  split attempts to optimize for maximal use of each directory segment
 *  while using a minimal number of directory segments.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      segment         - directory segment to be split
 *
 * Outputs:
 *
 *      The mount specific buffer may be modified.
 *
 * Returns:
 *
 *      1 if directory segment was successfully split, 0 otherwise
 *
 --*/
static int os8SplitDirectory(
  struct mountedFS *mount,
  uint8_t unit,
  uint16_t segment
)
{
  struct OS8data *data = &mount->os8data;
  uint16_t inuse = 0;
  uint16_t dirblk = OS8_DSSTART;
  uint16_t buf[OS8_BLOCKSIZE];
  uint16_t empoff[OS8_BLOCKSIZE], emplen[OS8_BLOCKSIZE];
  uint16_t emptotal = 0, emplast = 0, empcount = 0;
  uint16_t i, j, entries, entrysz, extra, startblk, split = 0;
  uint16_t off, target, dst = OS8_DH_SIZE;

  /*
   * First we need to find an unused directory segment.
   */
  do {
    inuse |= (1 << dirblk);

    if (os8ReadBlock(mount, unit, dirblk, buf) == 0)
      return 0;

    dirblk = os8Word(le16toh(buf[OS8_DH_NEXT]));
  } while (dirblk != 0);

  for (i = OS8_DSSTART; i <= OS8_DSLAST; i++) {
    if ((inuse & (1 << i)) == 0) {
      /*
       * Build a new directory segment
       */
      buf[OS8_DH_ENTRIES] = 0;
      buf[OS8_DH_NEXT] = data->buf[OS8_DH_NEXT];
      buf[OS8_DH_FLAGWD] = htole16(os8Value(0));
      buf[OS8_DH_EXTRA] = data->buf[OS8_DH_EXTRA];

      entrysz = OS8_DI_SIZE + (-os8SXT(le16toh(data->buf[OS8_DH_EXTRA])));
      entries = os8Neg(le16toh(data->buf[OS8_DH_ENTRIES]));
      extra = os8Neg(le16toh(data->buf[OS8_DH_EXTRA]));

      startblk = os8Word(le16toh(data->buf[OS8_DH_START]));

      /*
       * Analyze the current directory segment by finding all the empty
       * descriptors.
       */
      off = OS8_DH_SIZE;

      for (j = 0; j < entries; j++) {
        if (os8Word(le16toh(data->buf[off + OS8_DI_FNAME1])) == 0) {
          emplen[empcount] = os8Neg(le16toh(data->buf[off + OS8_ED_LENGTH]));
          emptotal += emplen[empcount];
          empoff[empcount] = off;
          empcount++;
          emplast = j == (entries - 1);

          off += OS8_ED_SIZE;
        } else off += entrysz;
      }

      /*
       * We want to avoid "stranding" unused directory entries where there
       * is no free space so we apply the following heuristics:
       *
       * 1. If the last directory entry is "empty" and contains at least
       *    50% of the free space, just move this entry to the newly
       *    created directory segment
       *
       * 2. Start the new directory segment at the "empty" entry which would
       *    exceed 50% of the free space.
       */
      target = empoff[empcount - 1];

      if (!emplast || (emplen[empcount - 1] < (emptotal / 2))) {
        uint16_t empty = 0;

        for (j = 0; j < empcount; j++)
          if ((empty + emplen[j]) >= (emptotal / 1)) {
            target = empoff[j];
            break;
          } else empty += emplen[j];
      }

      /*
       * Find the location of the split point and copy all directory entries
       * after this point to the newly created directory segment.
       */
      off = OS8_DH_SIZE;

      for (j = 0; j < entries; j++) {
        uint16_t blks, size;

        if (off == target) {
          split = j + 1;
          buf[OS8_DH_START] = htole16(os8Value(startblk));
        }

        if (os8Word(le16toh(data->buf[off + OS8_DI_FNAME1])) != 0) {
          blks = os8Neg(le16toh(data->buf[off + extra + OS8_DI_LENGTH]));
          size = OS8_DI_SIZE + extra;
        } else {
          blks = os8Neg(le16toh(data->buf[off + OS8_ED_LENGTH]));
          size = OS8_ED_SIZE;
        }

        if (split != 0) {
          uint16_t k;

          for (k = 0; k < size; k++)
            buf[dst++] = data->buf[off + k];

          buf[OS8_DH_ENTRIES]++;
        }
        off += size;
        startblk += blks;
      }

      /*
       * Update the new directory segment
       */
      buf[OS8_DH_ENTRIES] = htole16(os8Value(-buf[OS8_DH_ENTRIES]));

      if (os8WriteBlock(mount, unit, i, buf) == 0)
        return 0;

      /*
       * Update the original directory segment
       */
      data->buf[OS8_DH_ENTRIES] = htole16(os8Value(-(split - 1)));
      data->buf[OS8_DH_NEXT] = htole16(os8Value(i));

      if (os8WriteBlock(mount, unit, segment, NULL) == 0)
        return 0;

      /*
       * Directory segment split complete
       */
      return 1;
    }
  }
  return 0;
}

/*++
 *      o s 8 M e r g e E m p t y R e g i o n s
 *
 *  Scan a directory segment looking for 2 consecutive empty entries which
 *  can be merged into a single entry. The directory segment is in the mount
 *  specific buffer.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *
 * Outputs:
 *
 *      The mount specific buffer may be modified
 *
 * Returns:
 *
 *      1 if a merge occured, 0 otherwise
 *
 --*/
static int os8MergeEmptyRegions(
  struct mountedFS *mount
)
{
  struct OS8data *data = &mount->os8data;
  uint16_t i, entrysz, entries, off = OS8_DH_SIZE;

  entrysz = OS8_DI_SIZE + (-os8SXT(le16toh(data->buf[OS8_DH_EXTRA])));
  entries = os8Neg(le16toh(data->buf[OS8_DH_ENTRIES]));

  if (entries > 1) {
    for (i = 0; i < (entries - 1); i++) {
      if (os8Word(le16toh(data->buf[off + OS8_DI_FNAME1])) == 0) {
        if (os8Word(le16toh(data->buf[off + OS8_ED_SIZE + OS8_DI_FNAME1])) == 0) {
          data->buf[off + OS8_ED_LENGTH] =
            htole16(os8Value(le16toh(data->buf[off + OS8_ED_LENGTH]) +
                             le16toh(data->buf[off + OS8_ED_SIZE + OS8_ED_LENGTH])));
          data->buf[OS8_DH_ENTRIES] = htole16(os8Value(-(entries - 1)));

          os8SlideUp(mount, off + OS8_ED_SIZE,
                     off + (2 * OS8_ED_SIZE), entrysz, entries - 2);
          return 1;
        }
      } else off += entrysz;
    }
  }
  return 0;
}

/*++
 *      i n f o
 *
 *  Display information about the internal structure of a single file system.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void info(
  struct mountedFS *mount,
  uint8_t unit
)
{
  struct OS8data *data = &mount->os8data;
  unsigned int dirblk = OS8_DSSTART;

  printf("\n%s%o:\n\n", mount->name, unit);

  do {
    uint16_t i, entrysz, entries, extra, startblk, off = OS8_DH_SIZE;

    if (os8ReadBlock(mount, unit, dirblk, NULL) == 0)
      return;

    entrysz = OS8_DI_SIZE + (-os8SXT(le16toh(data->buf[OS8_DH_EXTRA])));
    entries = os8Neg(le16toh(data->buf[OS8_DH_ENTRIES]));
    extra = os8Neg(le16toh(data->buf[OS8_DH_EXTRA]));

    startblk = os8Word(le16toh(data->buf[OS8_DH_START]));

    printf("\nDirectory Segment %d:\n\n", dirblk);
    printf(" File  Type  Length    Date       Disk Region\n\n");

    for (i = 0; i < entries; i++) {
      char temp1[16], temp2[16];
      uint16_t length;

      if (os8Word(le16toh(data->buf[off + OS8_DI_FNAME1])) != 0) {
        uint16_t dateval = 0;

        length = os8Neg(le16toh(data->buf[off + extra + OS8_DI_LENGTH]));
        if (extra != 0)
          dateval = os8Word(le16toh(data->buf[off + OS8_DI_DATE]));

        printf("%s    %4hu  %s    %4d - %4d\n",
               os8FileName(mount, off, temp1), length,
               os8Date(dateval, temp2), startblk, startblk + length - 1);

        off += entrysz;
      } else {
        length = os8Neg(le16toh(data->buf[off + OS8_ED_LENGTH]));

        printf("<EMPTY>      %4hu                 %4d - %4d\n",
               os8Neg(le16toh(data->buf[off + OS8_ED_LENGTH])),
               startblk, startblk + length - 1);

        off += OS8_ED_SIZE;
      }
      startblk += length;
    }

    dirblk = os8Word(le16toh(data->buf[OS8_DH_NEXT]));
  } while (dirblk != 0);
}

/*++
 *      v a l i d a t e
 *
 *  Validate the integrity of an OS/8 file system. OS/8 file systems do not
 *  include any form of signature on the disk so we have to run a set of
 *  heuristics to verify the integrity of the file system.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      files           - return # of permanent files here
 *      freeblks        - return # of free blocks here
 *      addinfo         - return # of additional information words here
 *
 * Outputs:
 *
 *      The mount point specific buffer will be overwritten.
 *
 * Returns:
 *
 *      1 if file system is valid, 0 otherwise
 *
 --*/
static int validate(
  struct mountedFS *mount,
  uint8_t unit,
  uint16_t *files,
  uint16_t *freeblks,
  uint16_t *addinfo
)
{
  struct OS8data *data = &mount->os8data;
  unsigned int dirblk = OS8_DSSTART;
  uint8_t seen[OS8_DSLAST + 1];
  uint16_t extraVal;

  memset(seen, 0, sizeof(seen));
  
  do {
    uint16_t i, entrysz, entries, flagwd, extra, startblk, off = OS8_DH_SIZE;

    /*
     * If we've already seen this directory segment, there must be a loop
     */
    if (seen[dirblk] != 0)
      return 0;

    if (!os8BlockPresent(mount, unit, dirblk) ||
        !os8ReadBlock(mount, unit, dirblk, NULL))
      return 0;

    seen[dirblk] = 1;

    entrysz = OS8_DI_SIZE + (-os8SXT(le16toh(data->buf[OS8_DH_EXTRA])));
    entries = os8Neg(le16toh(data->buf[OS8_DH_ENTRIES]));
    flagwd = os8Word(le16toh(data->buf[OS8_DH_FLAGWD]));
    extra = os8Neg(le16toh(data->buf[OS8_DH_EXTRA]));

    startblk = os8Word(le16toh(data->buf[OS8_DH_START]));

    /*
     * The number of additional information words MUST be the same in all
     * directory segments.
     */
    if (dirblk == OS8_DSSTART) {
      extraVal = extra;
      *addinfo = extra;
    }

    if (extraVal != extra)
      return 0;

    /*
     * Up to 077 extra words may be allocated to each directory entry.
     */
    if ((extra & 07700) != 0)
      return 0;

    /*
     * The flag word must be either 0 or in the range 01400 - 01777.
     */
    if ((flagwd != 0) && ((flagwd & 01400) != 01400))
      return 0;

    /*
     * There must be at least one directory entry (an empty file occupying
     * all the space) and, at maximum, alternating permanent and empty
     * entries (256 / ((N + 5) + 2)) * 2.
     */
    if ((entries == 0) || (entries > ((256 / (extra + 7))) * 2))
      return 0;

    /*
     * Scan the directory checking for valid block addresses.
     */
    for (i = 0; i < entries; i++) {
      uint16_t length;

      if (os8Word(le16toh(data->buf[off + OS8_DI_FNAME1])) != 0) {
        length = os8Neg(le16toh(data->buf[off + extra + OS8_DI_LENGTH]));
        off += entrysz;
        *files += 1;
      } else {
        length = os8Neg(le16toh(data->buf[off + OS8_ED_LENGTH]));
        off += OS8_ED_SIZE;
        *freeblks += length;
      }
      startblk += length;

      /*
       * Make sure the block addresses are valid for this disk device
       */
      if (startblk > data->device->blocks[unit])
        return 0;
    }

    dirblk = os8Word(le16toh(data->buf[OS8_DH_NEXT]));

    if ((dirblk != 0) && ((dirblk < OS8_DSSTART) || (dirblk > OS8_DSLAST)))
      return 0;

  } while (dirblk != 0);

  return 1;
}

/*++
 *      t o S i x b i t
 * 
 *  Convert an alphanumeric character to sixbit encoding and place it into
 *  the appropriate 6-bit field of a 12-bit value. The character will have
 *  already been converted to upper-case if needed.
 *
 * Inputs:
 *
 *      ch              - the character to convert
 *      ptr             - pointer to the 12-bit location to update
 *      which           - which 6-bit field to update:
 *                              0 - high 6-bit field
 *                              1 - low 6-bit field
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void toSixbit(
  char ch,
  uint16_t *ptr,
  int which
)
{
  unsigned long i;

  for (i = 0; i < sizeof(sixbit); i++)
    if (ch == sixbit[i]) {
      if (which == 0)
        *ptr = (i << 6) | (*ptr & 0x3F);
      else *ptr = (*ptr & 0xFC0) | i;
      return;
    }
}

/*++
 *      o s 8 P a r s e F i l e s p e c
 *
 *  Parse a character string representing an OS/8 file specification.
 *
 * Inputs:
 *
 *      ptr             - pointer to the file specification string
 *      spec            - pointer to the file specification block
 *      wildcard        - wildcard processing options:
 *                        0 (OS8_M_NONE)        - wildcards not allowed
 *                        1 (OS8_M_ALLOW)       - wildcards allowed
 *                        2 (OS8_M_NONAME)      - wildcards allowed
 *                                                if filename + ext not
 *                                                present default to *.*
 *
 * Outputs:
 *
 *      The file specification block will be filled with the file information.
 *
 * Returns:
 *
 *      1 if parse is successful, 0 otherwise
 *
 --*/
#define P_DONE          0
#define P_NAME          1
#define P_EXT           2

int os8ParseFilespec(
  char *ptr,
  struct os8FileSpec *spec,
  int wildcard
)
{
  int state = P_NAME;
  unsigned int i;

  memset(spec, 0, sizeof(struct os8FileSpec));
  memset(&spec->fname, ' ', sizeof(spec->fname));
  memset(&spec->fext, ' ', sizeof(spec->fext));

  if (wildcard == OS8_M_NONAME)
    if (*ptr == '\0') {
      spec->flags = OS8_WC_NAME | OS8_WC_EXT;
      return 1;
    }

  while (state != P_DONE) {
    i = 0;

    switch (state) {
      case P_NAME:
        while ((*ptr != '\0') &&
               (*ptr != '.') &&
               (i < sizeof(spec->fname))) {
          char ch = toupper(*ptr++);

          if (isalnum(ch)) {
            toSixbit(ch, &spec->name[i >> 1], i & 1);
            spec->fname[i++] = ch;
          } else {
            if (ch == '*')
              if (i == 0) {
                spec->flags |= OS8_WC_NAME;
                break;
              }
            return 0;
          }
        }

        switch (*ptr) {
          case '\0':
            state = P_DONE;
            break;

          case '.':
            state = P_EXT;
            ptr++;
            break;

          default:
            return 0;
        }
        break;

      case P_EXT:
        while ((*ptr != '\0') &&
               (i < sizeof(spec->fext))) {
          char ch = toupper(*ptr++);

          if (isalnum(ch)) {
            toSixbit(ch, &spec->ext, i);
            spec->fext[i++] = ch;
          } else {
            if (ch == '*')
              if (i == 0) {
                spec->flags |= OS8_WC_EXT;
                break;
              }
            return 0;
          }
        }

        if (*ptr != '\0')
          return 0;

        state = P_DONE;
        break;
    }
  }
  return 1;
}

/*++
 *      o s 8 C r e a t e F i l e
 *
 *  Create a new file within the OS/8 file system.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      spec            - pointer to the file specification block
 *      file            - pointer to open file descriptor to receive results
 *      size            - file size in 256-word blocks
 *                        0 means use the largest free space region
 *
 * Outputs:
 *
 *      The mount point specific buffer area will be modified
 *
 * Returns:
 *
 *      1 if file successfully created, 0 otherwise
 *
 --*/
int os8CreateFile(
  struct mountedFS *mount,
  uint8_t unit,
  struct os8FileSpec *spec,
  struct os8OpenFile *file,
  uint16_t size
)
{
  struct OS8data *data = &mount->os8data;
  uint16_t segment, off, start, entries, extra, entrysz, space, left;

 retry:
  /*
   * Check if suitable free space is available
   */
  if (os8FindSpace(mount, unit, size, &segment, &off, &start) == 0) {
    ERROR("Free space not available on \"%s%o:\"\n", mount->name, unit);
    return 0;
  }

  /*
   * Check if there is space to create a new directory entry in this
   * directory segment. If not, split the directory segment in 2 and retry
   * from the beginning.
   */
  if (!os8CheckDirectory(mount)) {
    if (!os8SplitDirectory(mount, unit, segment)) {
      ERROR("No directory space on \"%s%o:\"\n", mount->name, unit);
      return 0;
    }
    goto retry;
  }

  entries = os8Neg(le16toh(data->buf[OS8_DH_ENTRIES]));
  extra = -os8SXT(le16toh(data->buf[OS8_DH_EXTRA]));
  entrysz = OS8_DI_SIZE + extra;

  space = size == 0 ? os8Neg(le16toh(data->buf[off + OS8_ED_LENGTH])) : size;
  left = os8Neg(le16toh(data->buf[off + OS8_ED_LENGTH])) - space;

  /*
   * Remove the requested space from the empty directory entry.
   */
  data->buf[off + OS8_ED_LENGTH] = htole16(os8Value(-left));

  os8SlideDown(mount, off, entries, entrysz, &file->remain);

  /*
   * Build the directory entry
   */
  data->buf[off + OS8_DI_FNAME1] = htole16(os8Value(spec->name[0]));
  data->buf[off + OS8_DI_FNAME2] = htole16(os8Value(spec->name[1]));
  data->buf[off + OS8_DI_FNAME3] = htole16(os8Value(spec->name[2]));
  data->buf[off + OS8_DI_EXT] = htole16(os8Value(spec->ext));

  if (extra != 0)
    data->buf[off + OS8_DI_DATE] = htole16(os8Value(data->date));

  data->buf[off + extra + OS8_DI_LENGTH] = htole16(os8Value(-space));

  if (os8WriteBlock(mount, unit, segment, NULL) == 0)
    return 0;

  /*
   * Fill in the open file descriptor
   */
  file->name[0] = spec->name[0];
  file->name[1] = spec->name[1];
  file->name[2] = spec->name[2];
  file->ext = spec->ext;
  file->creation = data->date;
  file->length = space;

  file->segment = segment;
  file->offset = off;
  file->entrysz = entrysz;
  file->extra = extra;

  file->mount = mount;
  file->unit = unit;

  file->current = 0;
  file->start = start;
  file->last = start + space - 1;

  file->wordpos = OS8_BLOCKSIZE;
  file->bytepos = OS8_CHECK;
  file->written = 0;

  return 1;
}

/*++
 *      o s 8 L o o k u p F i l e
 *
 *  Lookup a specific file within the OS/8 file system. This routine fills in
 *  an open file descriptor with information about the file and the directory
 *  entry it resides in.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      spec            - pointer to the file specification block
 *      file            - pointer to open file descriptor to receive results
 *
 * Outputs:
 *
 *      The mount point specific buffer will be modified
 *
 * Returns:
 *
 *      1 if file found, 0 otherwise
 *
 --*/
int os8LookupFile(
  struct mountedFS *mount,
  uint8_t unit,
  struct os8FileSpec *spec,
  struct os8OpenFile *file
)
{
  struct OS8data *data = &mount->os8data;
  unsigned int dirblk = OS8_DSSTART;

  if (os8UnitValid(data, unit)) {
    do {
      uint16_t i, entrysz, entries, extra, startblk, off = OS8_DH_SIZE;

      if (os8ReadBlock(mount, unit, dirblk, NULL) == 0)
        return 0;

      entrysz = OS8_DI_SIZE + (-os8SXT(le16toh(data->buf[OS8_DH_EXTRA])));
      entries = os8Neg(le16toh(data->buf[OS8_DH_ENTRIES]));
      extra = os8Neg(le16toh(data->buf[OS8_DH_EXTRA]));

      startblk = os8Word(le16toh(data->buf[OS8_DH_START]));

      for (i = 0; i < entries; i++) {
        if (os8Word(le16toh(data->buf[off + OS8_DI_FNAME1])) != 0) {
          if ((os8Word(le16toh(data->buf[off + OS8_DI_FNAME1])) == spec->name[0]) &&
              (os8Word(le16toh(data->buf[off + OS8_DI_FNAME2])) == spec->name[1]) &&
              (os8Word(le16toh(data->buf[off + OS8_DI_FNAME3])) == spec->name[2]) &&
              (os8Word(le16toh(data->buf[off + OS8_DI_EXT])) == spec->ext)) {
            file->name[0] = spec->name[0];
            file->name[1] = spec->name[1];
            file->name[2] = spec->name[2];
            file->ext = spec->ext;
            file->creation =
              extra != 0 ? os8Word(le16toh(data->buf[off + OS8_DI_DATE])) : 0;
            file->length = os8Neg(le16toh(data->buf[off + extra + OS8_DI_LENGTH]));

            file->segment = dirblk;
            file->offset = off;
            file->entrysz = entrysz;
            file->extra = extra;
            file->remain = entries - (i + 1);

            file->mount = mount;
            file->unit = unit;

            file->current = 0;
            if (file->length != 0) {
              file->start = startblk;
              file->last = startblk + file->length - 1;
            } else file->start = file->last = 0;
            
            file->wordpos = OS8_BLOCKSIZE;
            file->bytepos = OS8_CHECK;
            file->written = 0;

            return 1;
          }
          startblk += os8Neg(le16toh(data->buf[off + extra + OS8_DI_LENGTH]));
          off += entrysz;
        } else {
          startblk += os8Neg(le16toh(data->buf[off + OS8_ED_LENGTH]));
          off += OS8_ED_SIZE;
        }
      }
      dirblk = os8Word(le16toh(data->buf[OS8_DH_NEXT]));
    } while (dirblk != 0);
  }
  return 0;
}

/*++
 *      o s 8 U p d a t e F i l e
 *
 *  Update an OS/8 file by writing back the directory entry associated with
 *  the file.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number
 *      file            - pointer to open file descriptor
 *
 * Outputs:
 *
 *      The mount point specific buffer will be modified
 *
 * Returns:
 *
 *      None
 *
 --*/
void os8UpdateFile(
  struct mountedFS *mount,
  uint8_t unit,
  struct os8OpenFile *file
)
{
  struct OS8data *data = &mount->os8data;
  uint16_t off = file->offset;
  uint16_t empty = off + file->extra + OS8_DI_SIZE;
  uint16_t length = file->current == 0 ? 0 : file->current - file->start;
  uint16_t left = file->length - length;
  uint16_t space;

  if (os8ReadBlock(mount, unit, file->segment, NULL) == 0)
    return;

  /*
   * The directory entry is complete except for the # of blocks in use
   * which we can now update.
   */
  data->buf[off + file->extra + OS8_DI_LENGTH] = htole16(os8Value(-length));

  /*
   * Update the remaining space in the following empty file descriptor.
   */
  space = os8Neg(le16toh(data->buf[empty + OS8_ED_LENGTH])) + left;
  data->buf[empty + OS8_ED_LENGTH] = htole16(os8Value(-space));

  if (space == 0)
    /*
     * Remove the empty directory entry since it now occupies 0 blocks
     */
    os8SlideUp(mount, empty, empty + OS8_ED_SIZE, file->entrysz, file->remain);

  os8WriteBlock(mount, unit, file->segment, NULL);
}

/*++
 *      o s 8 R e a d B y t e s
 *
 *  Read a sequence of bytes from an open file. Each pair of 12-bit words in
 *  the file will be unpacked into 3 bytes.
 *
 * Inputs:
 *
 *      file            - pointer to an open file descriptor
 *      buf             - pointer to a buffer to receive the data
 *      len             - # of bytes of data to read
 *
 * Outputs:
 *
 *      The buffer will be overwritten by up to "len" bytes
 *
 * Returns:
 *
 *      # of bytes read from the file (may be less than "len"), 0 if EOF
 *
 --*/
int os8ReadBytes(
  struct os8OpenFile *file,
  char *buf,
  int len
)
{
  struct mountedFS *mount = file->mount;
  int count = 0;
  uint16_t temp1, temp2;
  char mask = SWISSET('a') ? 0x7F : 0xFF;

  while (len) {
    switch (file->bytepos) {
      case OS8_CHECK:
        if (file->wordpos == OS8_BLOCKSIZE) {
          if (file->current == 0) {
            /*
             * For an empty file (does OS/8 support this?)
             * start == current == last == 0
             */
            if (file->start == 0)
              return 0;

            file->current = file->start;
          } else {
            if (file->current == file->last)
              return count;
            file->current++;
          }
          if (os8ReadBlock(mount, file->unit, file->current, file->buffer) == 0)
            return 0;
          file->wordpos = 0;
        }
        file->bytepos = OS8_BYTE0;
        /* FALLTHROUGH */

      case OS8_BYTE0:
        temp1 = os8Word(le16toh(file->buffer[file->wordpos]));
        *buf++ = temp1 & mask;
        file->bytepos = OS8_BYTE1;
        break;

      case OS8_BYTE1:
        temp1 = os8Word(le16toh(file->buffer[file->wordpos + 1]));
        *buf++ = temp1 & mask;
        file->bytepos = OS8_BYTE2;
        break;

      case OS8_BYTE2:
        temp1 = os8Word(le16toh(file->buffer[file->wordpos]));
        temp2 = os8Word(le16toh(file->buffer[file->wordpos + 1]));
        *buf++ = (((temp1 & 0xF00) >> 4) | ((temp2 & 0xF00) >> 8)) & mask;
        file->wordpos += 2;
        file->bytepos = OS8_CHECK;
        break;
    }
    len--;
    count++;
  }
  return count;
}

/*++
 *      o s 8 W r i t e B y t e s
 *
 *  Write a sequence of bytes to an open file. Each sequence of 3 bytes will
 *  generate 2 words in the file.
 *
 * Inputs:
 *
 *      file            - pointer to an open file descriptor
 *      buf             - pointer to a buffer to receive the data
 *      len             - # of bytes of data to read
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      # of bytes written to the file (may be less than "len"), 0 if errort
 *
 --*/
int os8WriteBytes(
  struct os8OpenFile *file,
  char *buf,
  int len
)
{
  struct mountedFS *mount = file->mount;
  int count = 0;

  while (len) {
    switch (file->bytepos) {
      case OS8_CHECK:
        if (file->wordpos == OS8_BLOCKSIZE) {
          if (file->current == file->last)
            return count;

          if (file->current != 0) {
            if (os8WriteBlock(mount, file->unit, file->current, file->buffer) == 0)
              return 0;

            file->current++;
          } else file->current = file->start;

          file->wordpos = 0;
          memset(file->buffer, 0, OS8_BLOCKSIZE * sizeof(uint16_t));
        }
        file->bytepos = OS8_BYTE0;
        /* FALLTHROUGH */

      case OS8_BYTE0:
        file->buffer[file->wordpos++] = os8Value(*buf++);
        file->bytepos = OS8_BYTE1;
        break;

      case OS8_BYTE1:
        file->buffer[file->wordpos++] = os8Value(*buf++);
        file->bytepos = OS8_BYTE2;
        break;

      case OS8_BYTE2:
        file->buffer[file->wordpos - 2] =
          os8Value(file->buffer[file->wordpos - 2] | ((*buf & 0xF0) << 4));
        file->buffer[file->wordpos - 1] =
          os8Value(file->buffer[file->wordpos - 1] | ((*buf++ & 0xF) << 8));
        file->bytepos = OS8_CHECK;
        break;
    }
    len--;
    count++;
    file->written++;
  }
  return count;
}

/*++
 *      o s 8 M o u n t
 *
 *  Verify that the open container file contains 1 or more OS/8 file systems.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *                        (not in the mounted file system list)
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *       1 if 1 or more valid OS/8 file systems
 *       0 if no valid OS/8 file system present
 *      -1 if no valid OS/8 file system present and an error message has
 *         already been printed
 *
 --*/
static int os8Mount(
  struct mountedFS *mount
)
{
  struct OS8data *data = &mount->os8data;
  struct stat stat;
  uint8_t valid = 0;
  uint16_t files[8], freeblks[8], extra[8];

  memset(files, 0, sizeof(files));
  memset(freeblks, 0, sizeof(freeblks));
  memset(extra, 0, sizeof(extra));

  /*
   * Allow access to all filesystems for validation
   */
  data->device = NULL;
  data->valid = 0xFF;

  if (fstat(fileno(mount->container), &stat) == 0) {
    data->blocks = stat.st_size / OS8_BLOCKSIZE;

    if (SWISSET('t')) {
      struct OS8device *dev = OS8Devices;

      while (dev->name != NULL) {
        if (strcmp(SWGETVAL('t'), dev->name) == 0) {
          uint8_t i, count = 0;

          mount->skip = dev->skip;
          data->device = dev;

          /*
           * Validate all possible file systems.
           */
          for (i = 0; i < dev->filesys; i++)
            if (validate(mount, i, &files[i], &freeblks[i], &extra[i]) != 0) {
              count++;
              valid |= 1 << i;
            }

          if (valid != 0) {
            if (!quiet)
              printf("%s: successfully mounted (%d file system%s)\n\n",
                     mount->name, count, count == 1 ? "" : "s");

            for (i = 0; i < dev->filesys; i++)
              if ((valid & 1 << i) != 0) {
                printf("%s%o:\n", mount->name, i);
                printf("  Total Blocks: %4d, Free Blocks: %4d\n"
                       "  Files: %4d\n"
                       "  Extra words/directory entry: %d\n",
                       dev->blocks[i], freeblks[i], files[i], extra[i]);
              }
          }

          data->valid = valid;
          return valid != 0 ? 1 : 0;
        }
        dev++;
      }
      fprintf(stderr, "mount: unknown disk type - \"%s\"\n",
              SWGETVAL('t'));
      return -1;
    } else {
      fprintf(stderr, "mount: unable to determine disk type,"
              " use \"-t type\"\n");
      return -1;
    }
  }
  return 0;
}

/*++
 *      o s 8 U m o u n t
 *
 *  Unmount the OS/8 file system(s), releasing any storage allocated.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void os8Umount(
  struct mountedFS *UNUSED(mount)
)
{
}

/*++
 *      o s 8 S i z e
 *
 *  Return the size of an OS/8 container file.
 *
 * Inputs:
 *
 *      None
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Size of the container file in block of default file system size
 *
 --*/
static size_t os8Size(void)
{
  if (SWISSET('t')) {
    struct OS8device *dev = OS8Devices;
    
    while (dev->name != NULL) {
      if (strcmp(SWGETVAL('t'), dev->name) == 0)
        return dev->diskSize;
     
      dev++;
    }
  }
  return OS8Devices[0].diskSize;
}

/*++
 *      o s 8 N e w f s
 *
 *  Create empty OS/8 file system(s).
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *                        (nit in the mounted file system list)
 *      size            - the sized (in blocks) of the filesystem
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if the file system(s) were successfully created, 0 otherwise
 *
 --*/
static int os8Newfs(
  struct mountedFS *mount,
  size_t UNUSED(size)
)
{
  struct OS8data *data = &mount->os8data;
  uint16_t extra = 1;
  struct OS8device *dev = OS8Devices;
  uint8_t unit;
  uint16_t i;

  if (SWISSET('t')) {
    while (dev->name != NULL) {
      if (strcmp(SWGETVAL('t'), dev->name) == 0)
        goto found;

      dev++;
    }
    dev = OS8Devices;
  }
 found:
  mount->skip = dev->skip;
  data->device = dev;

  if (SWISSET('e')) {
    char *endptr;

    extra = strtoul(SWGETVAL('e'), &endptr, 10);
    if ((extra > 63) || (*endptr != '\0'))
      return 0;
  }

  for (unit = 0; unit < dev->filesys; unit++) {
    data->valid |= (1 << unit);

    for (i = OS8_DSSTART; i <= OS8_DSLAST; i++) {
      uint16_t buf[OS8_BLOCKSIZE];
      uint16_t entries = i == OS8_DSSTART ? 1 : 0;

      buf[OS8_DH_ENTRIES] = htole16(os8Value(-entries));
      buf[OS8_DH_START] = htole16(os8Value(OS8_DATA));
      buf[OS8_DH_NEXT] = htole16(os8Value(0));
      buf[OS8_DH_FLAGWD] = htole16(os8Value(0));
      buf[OS8_DH_EXTRA] = htole16(os8Value(-extra));

      buf[OS8_DH_SIZE + OS8_ED_IND] = htole16(os8Value(0));
      buf[OS8_DH_SIZE + OS8_ED_LENGTH] =
        htole16(os8Value(-(dev->blocks[unit] - OS8_DATA)));

      if (os8WriteBlock(mount, unit, i, buf) == 0)
        return 0;
    }
  }
  return 1;
}

/*++
 *      o s 8 S e t
 *
 *  Set mount point specific values.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - file system unit number (unused)
 *      present         - file system unit number present (unused)
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void os8Set(
  struct mountedFS *mount,
  uint8_t UNUSED(unit),
  uint8_t UNUSED(present)
)
{
  struct OS8data *data = &mount->os8data;
  int idx = 0;
  unsigned long year;
  char *endptr;

  while (setCmds[idx] != NULL) {
    if (strcmp(words[1], setCmds[idx]) == 0) {
      switch (idx) {
        case OS8SET_YEAR:
          if (args == 3) {
            if (strcmp(words[2], "none") == 0) {
              data->date = 0;
              return;
            }
            year = strtoul(words[2], &endptr, 10);
            if ((*endptr == '\0') && (year <= 7)) {
              struct tm tm;
              time_t now = time(NULL);

              localtime_r(&now, &tm);

              data->date = (tm.tm_mon + 1) << 8;
              data->date |= (tm.tm_mday + 1) << 3;
              data->date |= year;
              return;
            }
          }
          fprintf(stderr, "os8: Invalid syntax for \"set year\"\n");
          return;

        default:
          fprintf(stderr, "os8: \"%s\" not implemented\n", words[1]);
          return;
      }
    }
    idx++;
  }
  fprintf(stderr, "os8: Unknown set command \"%s\"\n", words[1]);
}

/*++
 *      o s 8 I n f o
 *
 *  Display information about the internal structure of the OS/8 file
 *  system(s).
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - file system unit number
 *      present         - file system unit number present
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void os8Info(
  struct mountedFS *mount,
  uint8_t unit,
  uint8_t present
)
{
  struct OS8data *data = &mount->os8data;
  struct OS8device *device = data->device;

  if (present) {
    if (os8UnitValid(data, unit))
      info(mount, unit);
  } else {
    uint16_t i;

    /*
     * Display information about all valid units
     */
    for (i = 0; i < device->filesys; i++)
      if (os8UnitValid(data, i))
        info(mount, i);
  }
}

/*++
 *      o s 8 D i r
 *
 *  Produce a full or brief directory listing.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - file system unit number
 *      fname           - pointer to filename string
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void os8Dir(
  struct mountedFS *mount,
  uint8_t unit,
  char *fname
)
{
  struct OS8data *data = &mount->os8data;
  struct os8FileSpec spec;

  if (os8ParseFilespec(fname, &spec, OS8_M_NONAME) == 0) {
    fprintf(stderr, "dir: syntax error in file spec \"%s\"\n", fname);
    return;
  }

  if (os8UnitValid(data, unit)) {
    unsigned int dirblk = OS8_DSSTART;

    do {
      uint16_t i, entrysz, entries, extra, off = OS8_DH_SIZE;

      if (os8ReadBlock(mount, unit, dirblk, NULL) == 0)
        return;

      entrysz = OS8_DI_SIZE + (-os8SXT(le16toh(data->buf[OS8_DH_EXTRA])));
      entries = os8Neg(le16toh(data->buf[OS8_DH_ENTRIES]));
      extra = os8Neg(le16toh(data->buf[OS8_DH_EXTRA]));

      for (i = 0; i < entries; i++) {
        char temp1[16], temp2[16];

        if (os8Word(le16toh(data->buf[off + OS8_DI_FNAME1])) != 0) {
          if ((spec.flags & OS8_WC_NAME) == 0)
            if ((os8Word(le16toh(data->buf[off + OS8_DI_FNAME1])) != spec.name[0]) ||
                (os8Word(le16toh(data->buf[off + OS8_DI_FNAME2])) != spec.name[1]) ||
                (os8Word(le16toh(data->buf[off + OS8_DI_FNAME3])) != spec.name[2])) {
              off += entrysz;
              continue;
            }

          if ((spec.flags & OS8_WC_EXT) == 0)
            if (os8Word(le16toh(data->buf[off + OS8_DI_EXT])) != spec.ext) {
              off += entrysz;
              continue;
            }

          if (SWISSET('f')) {
            uint16_t length, dateval = 0;
            
            length = os8Neg(le16toh(data->buf[off + extra + OS8_DI_LENGTH]));
            if (extra)
              dateval = os8Word(le16toh(data->buf[off + OS8_DI_DATE]));

            printf("%s  %4hu  %s\n",
                   os8FileName(mount, off, temp1), length,
                   os8Date(dateval, temp2));
          } else printf("%s\n", os8FileName(mount, off, temp1));
          off += entrysz;
        } else off += OS8_ED_SIZE;
      }

      dirblk = os8Word(le16toh(data->buf[OS8_DH_NEXT]));
    } while (dirblk != 0);
  }
}

/*++
 *      o s 8 O p e n F i l e R
 *
 *  Open an OS/8 file for reading.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - file system unit number
 *      fname           - pointer to filename string
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Pointer to open file descriptor, NULL if open fails
 *
 --*/
static void *os8OpenFileR(
  struct mountedFS *mount,
  uint8_t unit,
  char *fname
)
{
  struct os8OpenFile *file;
  struct os8FileSpec spec;

  if (os8ParseFilespec(fname, &spec, OS8_M_NONE) == 0) {
    fprintf(stderr, "Failed to parse filename \"%s\"\n", fname);
    return NULL;
  }

  if ((file = malloc(sizeof(struct os8OpenFile))) != NULL) {
    memset(file, 0, sizeof(struct os8OpenFile));

    if (os8LookupFile(mount, unit, &spec, file) != 0) {
      /*
       * Allocate local buffer space for the file.
       */
      if ((file->buffer = malloc(mount->blocksz)) == NULL) {
        free(file);
        return NULL;
      }
      file->mode = M_RD;
    } else {
      free(file);
      return NULL;
    }
  }
  return file;
}

/*++
 *      o s 8 O p e n F i l e W
 *
 *  Open an OS/8 file for writing.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - file system unit number
 *      fname           - pointer to filename string
 *      size            - estimated file size (in bytes)
 *                        0 means allocate as much space as possible
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Pointer to open file descriptor, NULL is open fails
 *
 --*/
void *os8OpenFileW(
  struct mountedFS *mount,
  uint8_t unit,
  char *fname,
  off_t size
)
{
  struct os8OpenFile *file;
  struct os8FileSpec spec;
  uint16_t blocks = 0;

  if (size != 0)
    blocks = (size + ((OS8_BLOCKSIZE * 3) / 2) - 1) / (((OS8_BLOCKSIZE) * 3) / 2);

  if (os8ParseFilespec(fname, &spec, OS8_M_NONE) == 0) {
    fprintf(stderr, "Failed to parse filename \"%s\"\n", fname);
    return NULL;
  }

  if ((file = malloc(sizeof(struct os8OpenFile))) != NULL) {
    memset(file, 0, sizeof(struct os8OpenFile));

    if (os8LookupFile(mount, unit, &spec, file) == 0) {
      /*
       * Allocate local buffer space for the file.\
       */
      if ((file->buffer = malloc(mount->blocksz)) != NULL) {
        memset(file->buffer, 0, mount->blocksz);

        if (os8CreateFile(mount, unit, &spec, file, blocks) == 0) {
          ERROR("Failed to create file \"%s\"\n", fname);
          free(file->buffer);
          free(file);
          return NULL;
        } else file->mode = M_WR;
      } else {
        ERROR("Buffer allocation failure for \"%s\"\n", fname);
        free(file);
        return NULL;
      }
    } else {
      ERROR("File \"%s\" already exists\n", fname);\
      free(file);
      return NULL;
    }
  } else ERROR("Memory allocation failure\n");
  return file;
}

/*++
 *      o s 8 F i l e S i z e
 *
 *  Return an estimate of the size of a currently open file. This routine
 *  bases the file size on the number of blocks allocated to the file and
 *  may over-report the actual sized of the file.
 *
 * Inputs:
 *
 *      filep           - pointer to open file descriptor
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Estimate of the file size
 *
 --*/
static off_t os8FileSize(
  void *filep
)
{
  struct os8OpenFile *file = filep;

  return (file->length * RT11_BLOCKSIZE * 2) / 3;
}

/*++
 *      o s 8 D e l e t e F i l e
 *
 *  Delete a file from an O/8 file system.
 *
 * Inputs:
 *
 *      filep           - pointer to open file descriptor
 *      fname           - pointer to filename string
 *
 * Outputs:
 *
 *      The mount specific buffer will be modified.
 *
 * Returns:
 *
 *      None
 *
 --*/
static void os8DeleteFile(
  void *filep,
  char *UNUSED(fname)
)
{
  struct os8OpenFile *file = filep;
  struct mountedFS *mount = file->mount;
  struct OS8data *data = &mount->os8data;

  if (os8ReadBlock(mount, file->unit, file->segment, NULL) == 0)
    return;

  data->buf[file->offset + OS8_DI_FNAME1] = 0;
  data->buf[file->offset + OS8_ED_LENGTH] =
    data->buf[file->offset + file->extra + OS8_DI_LENGTH];
  data->buf[OS8_DH_ENTRIES] =
    htole16(os8Value(-(os8Neg(le16toh(data->buf[OS8_DH_ENTRIES])) - 1)));

  os8SlideUp(mount, file->offset + OS8_ED_SIZE,
             file->offset + file->entrysz, file->entrysz, file->remain);

  /*
   * Merge any adjacent empty directory entries.
   */
  if (os8MergeEmptyRegions(mount) != 0)
    os8MergeEmptyRegions(mount);

  if (os8WriteBlock(mount, file->unit, file->segment, NULL) == 0)
    return;

  os8CloseFile(file);
}

/*++
 *      o s 8 C l o s e F i l e
 *
 *  Close an open OS/8 file.
 *
 * Inputs:
 *
 *      filep           - pointer to open file descriptor
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      None
 *
 --*/
static void os8CloseFile(
  void *filep
)
{
  struct os8OpenFile *file = filep;

  if (file->mode == M_WR) {
    if (file->current != 0)
      /*
       * Flush the current buffer.
       */
      os8WriteBlock(file->mount, file->unit, file->current++, file->buffer);

    os8UpdateFile(file->mount, file->unit, file);
  }

  if (file->buffer != NULL)
    free(file->buffer);
  free(file);
}

/*++
 *      o s 8 R e a d F i l e
 *
 *  Read data from an OS/8 file to a supplied buffer
 *
 * Inputs:
 *
 *      filep           - pointer to open file descriptor
 *      buf             - pointer to buffer
 *      buflen          - length of the supplied buffer
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      # of bytes of data read, 0 means EOF or error
 *
 --*/
static size_t os8ReadFile(
  void *filep,
  void *buf,
  size_t buflen
)
{
  struct os8OpenFile *file = filep;

  return os8ReadBytes(file, buf, buflen);
}

/*++
 *      o s 8 W r i t e F i l e
 *
 *  Write data to an OS/8 file from a supplied buffer.
 *
 * Inputs:
 *
 *      filep           - pointer to open file descriptor
 *      buf             - pointer to buffer
 *      buflen          - length of the supplied buffer
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      # of bytes of data written, 0 means EOF or error
 *
 --*/
static size_t os8WriteFile(
  void *filep,
  void *buf,
  size_t buflen
)
{
  struct os8OpenFile *file = filep;

  return os8WriteBytes(file, buf, buflen);
}

/*++
 *      o s 8 F S
 *
 * Descriptor for accessing OS/8 file systems.
 *
 --*/
struct FSdef os8FS = {
  NULL,
  "os8",
  "os8              PDP-8 OS/8 file system (RX01, RX02 or RK05 disks)\n",
  FS_UNITVALID,
  OS8_BLOCKSIZE * sizeof(uint16_t),
  os8Mount,
  os8Umount,
  os8Size,
  os8Newfs,
  os8Set,
  os8Info,
  os8Dir,
  os8OpenFileR,
  os8OpenFileW,
  os8FileSize,
  os8CloseFile,
  os8ReadFile,
  os8WriteFile,
  os8DeleteFile,
  NULL,                                 /* No tape support functions */
  NULL,
  NULL,
  NULL
};
