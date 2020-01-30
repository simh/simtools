/*
 * Copyright (C) 2018 John Forecast. All Rights Reserved.
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
 * Support routines for handling RT-11 file systems under fsio
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

static struct DiskSize {
  char          *name;                  /* Disk name */
  size_t        size;                   /* Disk size */
} rt11DiskSize[] = {
  { "rk05", RT11_RK05SZ * RT11_BLOCKSIZE },
  { "rl01", RT11_RL01SZ * RT11_BLOCKSIZE },
  { "rl02", RT11_RL02SZ * RT11_BLOCKSIZE },
  { "rx01", RT11_RX0xSZ * RT11_RX01SS },
  { "rx02", RT11_RX0xSZ * RT11_RX02SS },
  { NULL, 0 }
};

static int rt11BestFit(struct mountedFS *, uint8_t, uint16_t,
                       uint16_t *, uint16_t *, uint16_t *, uint16_t *);
static void rt11CloseFile(void *);

extern int quiet;

/*++
 *      r t 1 1 P a r s e F i l e s p e c
 *
 *  Parse a character string representing an RT-11 file specification. If
 *  wildcards are permitted, '*' may be used to represent zero or more
 *  characters in the file specification and '%' represents a single
 *  character. For the purpose of wildcard matches, filename and filetype
 *  are considered separate strings.
 *
 * Inputs:
 *
 *      ptr             - pointer to the file specification string
 *      spec            - pointer to the file specification block
 *      wildcard        - wildcard processing options:
 *                        0 (RT11_M_NONE)      - wildcards not allowed
 *                        1 (RT11_M_ALLOW)     - wildcards allowed
 *                        2 (RT11_M_NONAME)    - wildcards allowed
 *                                               if filename + ext not
 *                                               present default to *.*
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
#define P_TYPE          2

int rt11ParseFilespec(
  char *ptr,
  struct rt11FileSpec *spec,
  int wildcard
)
{
  int state = P_NAME;
  unsigned int i;

  memset(spec, 0, sizeof(struct rt11FileSpec));
  memset(&spec->fname, ' ', sizeof(spec->fname));
  memset(&spec->ftype, ' ', sizeof(spec->ftype));

  if (wildcard == RT11_M_NONAME)
    if (*ptr == '\0') {
      spec->flags = RT11_WC_NAME | RT11_WC_TYPE;
      spec->fname[0] = spec->ftype[0] = '*';
      return 1;
    }

  while (state != P_DONE) {
    i = 0;

    switch (state) {
      case P_NAME:
        while ((*ptr != '\0') &&
               (*ptr != '.') &&
               (i < sizeof(spec->fname))) {
          char ch = *ptr++;

          spec->fname[i++] = ch;

          if (!isalnum(ch))
            switch (ch) {
              case '*':
              case '%':
                if (wildcard) {
                  spec->flags |= RT11_WC_NAME;
                  break;
                }
                /* FALLTHROUGH */

              default:
                return 0;
            }
        }

        switch (*ptr) {
          case '\0':
            state = P_DONE;
            break;

          case '.':
            state = P_TYPE;
            ptr++;
            break;

          default:
            return 0;
        }
        break;

      case P_TYPE:
        while ((*ptr != '\0') &&
               (i < sizeof(spec->ftype))) {
          char ch = *ptr++;

          spec->ftype[i++] = ch;

          if (!isalnum(ch))
            switch (ch) {
              case '*':
              case '%':
                if (wildcard) {
                  spec->flags |= RT11_WC_TYPE;
                  break;
                }
                /* FALLTHROUGH */

              default:
                return 0;
            }
        }

        if (*ptr != '\0')
          return 0;

        state = P_DONE;
        break;
    }
  }

  if ((spec->flags & RT11_WC_NAME) == 0) {
    spec->name[0] = ascR50(&spec->fname[0]);
    spec->name[1] = ascR50(&spec->fname[3]);
  }
  if ((spec->flags & RT11_WC_TYPE) == 0)
    spec->type = ascR50(&spec->ftype[0]);

  return 1;
}

/*++
 *      r t 1 1 B u i l d R e g e x
 *
 *  Build and compile a regular expression to match a filename with wildcard
 *  characters.
 *
 * Inputs:
 *
 *      spec            - pointer to the file specification block
 *      preg            - pointer to regex_t structure to receive the
 *                        compiled regular expression
 *
 * Outputs:
 *
 *      The compilation may dynamically allocate storage which must be
 *      released using regfree().
 *
 * Returns:
 *
 *      1 if regular expression was successfully compiled, 0 otherwise
 *
 --*/
static int rt11BuildRegex(
  struct rt11FileSpec *spec,
  regex_t *preg
)
{
  char temp[64];
  unsigned int i, j = 0;

  /*
   * Convert the filename into a regular expression suitable for compilation.
   */
  temp[j++] = '^';

  for (i = 0; i < sizeof(spec->fname); i++) {
    if (spec->fname[i] == ' ')
      break;

    switch (spec->fname[i]) {
      case '*':
        temp[j++] = '.';
        temp[j++] = '*';
        break;

      case '%':
        temp[j++] = '.';
        break;

      default:
        temp[j++] = toupper(spec->fname[i]);
        break;
    }
  }

  temp[j++] = '\\';
  temp[j++] = '.';

  for (i = 0; i < sizeof(spec->ftype); i++) {
    if (spec->ftype[i] == ' ')
      break;

    switch (spec->ftype[i]) {
      case '*':
        temp[j++] = '.';
        temp[j++] = '*';
        break;

      case '%':
        temp[j++] = '.';
        break;

      default:
        temp[j++] = toupper(spec->ftype[i]);
        break;
    }
  }

  temp[j++] = '$';
  temp[j++] = '\0';

  /*
   * Compile the regular expression.
   */
  if (regcomp(preg, temp, 0) == 0)
    return 1;

  return 0;
}

/*++
 *      r t 1 1 M a t c h R e g e x
 *
 *  Match a pre-compiled regular expression against a filename and type.
 *
 * Inputs:
 *
 *      preg            - pointer to pre-compiler regular expression
 *      fname1          - first 3 characters of filename (RAD50)
 *      fname2          - next 3 characters of filename (RAD50)
 *      ftype           - 3 characters of file type (RAD50)
 *
 * Outputs:
 *
 *      ...
 *
 * Returns:
 *
 *      1 if regular expression matches, 0 otherwise
 *
 --*/
int rt11MatchRegex(
  regex_t *preg,
  uint16_t fname1,
  uint16_t fname2,
  uint16_t ftype
)
{
  char temp[16];
  int i = 5;

  /*
   * Build a filename + type string with no embedded spaces.
   */
  r50Asc(fname1, &temp[0]);
  r50Asc(fname2, &temp[3]);

  while ((i >= 0) && (temp[i] == ' '))
    i--;

  temp[++i] = '.';

  r50Asc(ftype, &temp[++i]);

  i += 2;
  while (temp[i] == ' ')
    i--;

  temp[i + 1] = '\0';

  if (regexec(preg, &temp[0], 0, NULL, 0) == 0)
    return 1;

  return 0;
}

/*++
 *      M a p L o g T o P h y s
 *
 *  Map a logical sector address to a physical sector address for an RX01/RX02
 *  drive.
 *
 * Inputs:
 *
 *      sectno          - logical sector number
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Physical sector number
 *
 --*/
unsigned int MapLogToPhys(
  unsigned int sectno
)
{
  unsigned int i, track, sector;

  track = sectno / RT11_RX0xNSECT;
  i = (sectno % RT11_RX0xNSECT) << 1;
  if (i >= RT11_RX0xNSECT)
    i++;
  sector = (i + (6 * track)) % RT11_RX0xNSECT;
  return sector + (track * RT11_RX0xNSECT);
}

/*++
 *      r t 1 1 R e a d B l o c k
 *
 *  Read a block from an RT-11 file system.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - partition number
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
int rt11ReadBlock(
  struct mountedFS *mount,
  uint8_t unit,
  unsigned int block,
  void *buf
)
{
  struct RT11data *data = &mount->rt11data;
  char *buffer = buf == NULL ? data->buf : buf;
  int status = 0;

  if (RT11_PARTITIONVALID(data, unit)) {
    if (block > data->maxblk[unit]) {
      ERROR("Attempt to read block (%u) outside file system \"%s%o:\"\n",
            block, mount->name, unit);
      return 0;
    }

#ifdef DEBUG
    if ((mount->flags & FS_DEBUG) != 0)
      fprintf(DEBUGout, ">> %s%o: (rt11) Reading logical block %o\n",
              mount->name, unit, block);
#endif

    if (data->sectorsz != 0) {
      /*
       * The sectors making up the disk are sector-interleaved. The algorithm
       * below is for RX01/RX02 interleave. Note that all such devices are
       * small enough that there can only be a single file system present.
       */
      unsigned int count = RT11_BLOCKSIZE / data->sectorsz;
      unsigned int sectno = block * count;

      do {
        unsigned int sector = MapLogToPhys(sectno);

        status = FSioReadSector(mount, sector, data->sectorsz, buffer);

        count--;
        buffer += data->sectorsz;
        sectno++;
      } while ((count != 0) && (status != 0));
    } else status = FSioReadBlock(mount, (unit << 16) | block, buffer);

    if (status == 0)
      ERROR("I/O error on \"%s%o:\"\n", mount->name, unit);
  } else ERROR("Invalid device \"%s%o:\"\n", mount->name, unit);

  return status;
}

/*++
 *      r t 1 1 W r i t e B l o c k
 *
 *  Write a block to an RT-11 file system.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - partition number
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
int rt11WriteBlock(
  struct mountedFS *mount,
  uint8_t unit,
  unsigned int block,
  void *buf
)
{
  struct RT11data *data = &mount->rt11data;
  char *buffer = buf == NULL ? data->buf : buf;
  int status = 0;

  if (RT11_PARTITIONVALID(data, unit)) {
    if (block > data->maxblk[unit]) {
      ERROR("Attempt to write block (%u) outside file system \"%s%o:\"\n",
            block, mount->name, unit);
      return 0;
    }

#ifdef DEBUG
    if ((mount->flags & FS_DEBUG) != 0)
      fprintf(DEBUGout, ">> %s%o: (rt11) Writing logical block %o\n",
              mount->name, unit, block);
#endif

    if (data->sectorsz != 0) {
      /*
       * The sectors making up the disk are sector-interleaved. The algorithm
       * below is for RX01/RX02 interleave. Note that all such devices are
       * small enough that there can only be a single file system present.
       */
      unsigned int count = RT11_BLOCKSIZE / data->sectorsz;
      unsigned int sectno = block * count;

      do {
        unsigned int sector = MapLogToPhys(sectno);

        status = FSioWriteSector(mount, sector, data->sectorsz, buffer);

        count--;
        buffer += data->sectorsz;
        sectno++;
      } while ((count != 0) && (status != 0));
    } else status = FSioWriteBlock(mount, (unit << 16) | block, buffer);

    if (status == 0)
      ERROR("I/O error on \"%s%o:\"\n", mount->name, unit);
  } else ERROR("Invalid device \"%s%o:\"\n", mount->name, unit);

  return status;
}

/*++
 *      r t 1 1 R e a d D i r S e g m e n t
 *
 *  Read a directory segment (2 file system blocks) into the mount specific
 *  buffer.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - partition number
 *      segment         - logical directory segment # (1 - 31)
 *
 * Outputs:
 *
 *      The directory segment will be read into the mount specific buffer.
 *
 * Returns:
 *
 *      1 if successful, 0 otherwise
 *
 --*/
int rt11ReadDirSegment(
  struct mountedFS *mount,
  uint8_t unit,
  uint16_t segment
)
{
  struct RT11data *data = &mount->rt11data;
  unsigned int block = data->first[unit] + ((segment - 1) * 2);

  if (rt11ReadBlock(mount, unit, block, &data->buf[0]) != 0)
    if (rt11ReadBlock(mount, unit, block + 1, &data->buf[256]) != 0)
      return 1;

  return 0;
}

/*++
 *      r t 1 1 W r i t e D i r S e g m e n t
 *
 *  Write a directory segment (2 disk blocks) from the mount specific buffer.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - partition number
 *      segment         - logical directory segment # (1 - 31)
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
int rt11WriteDirSegment(
  struct mountedFS *mount,
  uint8_t unit,
  uint16_t segment
)
{
  struct RT11data *data = &mount->rt11data;
  unsigned int block = data->first[unit] + ((segment - 1) * 2);

  if (rt11WriteBlock(mount, unit, block, &data->buf[0]) != 0)
    if (rt11WriteBlock(mount, unit, block + 1, &data->buf[256]) != 0)
      return 1;

  return 0;
}

/*++
 *      r t 1 1 M a k e D i r e c t o r y E n t r y
 *
 *  Make space available for a new directory entry. The directory segment
 *  has been read into the mount point specific buffer.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      entrysz         - size of directory entries
 *      offset          - offset to empty directory entry
 *
 * Outputs:
 *
 *      If space is available, the directory segment will be modified
 *
 * Returns:
 *
 *      1 if a new directory entry has been created, 0 otherwise
 *
 --*/
int rt11MakeDirectoryEntry(
  struct mountedFS *mount,
  uint16_t entrysz,
  uint16_t offset
)
{
  struct RT11data *data = &mount->rt11data;
  uint16_t off = offset;

  /*
   * Scan forward looking for the end-of-segment marker.
   */
  while (!RT11EOS(le16toh(data->buf[off + RT11_DI_STATUS])) &&
         ((RT11_DS_SIZE - off) >= entrysz))
    off += entrysz;
  
  if ((RT11_DS_SIZE - off) > entrysz) {
    /*
     * There is enough space available for a new directory entry. Slide
     * the existing entries up to make a new one available at the
     * requested position.
     */
    data->buf[off + entrysz + RT11_DI_STATUS] = htole16(RT11_E_EOS);

    while (off != offset) {
      uint16_t i, prev = off - entrysz;

      for (i = RT11_DI_STATUS; i < entrysz; i++)
        data->buf[off + i] = data->buf[prev + i];

      off -= entrysz;
    }
    return 1;
  }
  return 0;
}

/*++
 *      r t 1 1 B e s t F i t
 *
 *  Search the entire directory structure for the best fit for the requested
 *  file size. If the requested file size is 0, we return the largest
 *  empty directory entry.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - partition number
 *      rqsize          - requested size in 512-byte blocks
 *      start           - return starting block # here
 *      segment         - return directory segment here
 *      offset          - return directory segment offset here
 *      next            - return next available directory segment here
 *                        NULL if not required
 *
 * Outputs:
 *
 *      The mount specific buffer will be modified
 *
 * Returns:
 *
 *      1 if space is available, 0 if not
 *
 --*/
static int rt11BestFit(
  struct mountedFS *mount,
  uint8_t unit,
  uint16_t rqsize,
  uint16_t *start,
  uint16_t *segment,
  uint16_t *offset,
  uint16_t *next
)
{
  struct RT11data *data = &mount->rt11data;
  uint16_t bestsize, beststart, bestsegment, bestoffset, dsseg = 1;

  bestsize = rqsize == 0 ? 0 : 0177777;
  bestsegment = 0;

  do {
    uint16_t entrysz, startblk, off = RT11_DH_SIZE;

    if (rt11ReadDirSegment(mount, unit, dsseg) == 0)
      return 0;

    /*
     * Return the next available directory segment or 0 if no more are
     * available.
     */
    if ((next != NULL) && (dsseg == 1)) {
      if (data->buf[RT11_DH_COUNT] != data->buf[RT11_DH_HIGHEST])
        *next = le16toh(data->buf[RT11_DH_HIGHEST]) + 1;
      else *next = 0;
    }

    entrysz = RT11_DI_SIZE + (le16toh(data->buf[RT11_DH_EXTRA]) >> 1);
    startblk = le16toh(data->buf[RT11_DH_START]);

    /*
     * Loop until we see and end-of-segment marker or there is no room for
     * another directory entry.
     */
    while (!RT11EOS(le16toh(data->buf[off + RT11_DI_STATUS])) &&
           ((RT11_DS_SIZE - off) >= entrysz)) {
      uint16_t status = le16toh(data->buf[off + RT11_DI_STATUS]);
      uint16_t length = le16toh(data->buf[off + RT11_DI_LENGTH]);

      if ((status & RT11_E_MPTY) != 0) {
        /*
         * Check for largest or best fit
         */
        if (((rqsize == 0) && (length > bestsize)) ||
            ((rqsize != 0) &&
             ((length >= rqsize) && (length < bestsize)))) {
          bestsize = length;
          beststart = startblk;
          bestsegment = dsseg;
          bestoffset = off;
        }
      }
      startblk += length;
      off += entrysz;
    }
    dsseg = le16toh(data->buf[RT11_DH_NEXT]);
  } while (dsseg != 0);

  if (bestsegment != 0) {
    *start = beststart;
    *segment = bestsegment;
    *offset = bestoffset;
    return 1;
  }
  return 0;
}

/*++
 *      r t 1 1 M e r g e E m p t y R e g i o n s
 *
 *  Following a delete operation, check if we can merge empty regions
 *  together and compact the directory segment. In the worst case there
 *  can be 3 directory entries involved; <UNUSED>, <File>, <UNUSED>. If
 *  <File> is deleted we need to collapse all three <UNUSED> entries to one.
 *  The directory segment is currently in the mount point specific buffer.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      offset          - offset of the directory entry just deleted
 *
 * Outputs:
 *
 *      The mount point specific buffer may be modified
 *
 * Returns:
 *
 *      None
 *
 --*/
static void rt11MergeEmptyRegions(
  struct mountedFS *mount,
  uint16_t offset
)
{
  struct RT11data *data = &mount->rt11data;
  uint16_t entrysz = RT11_DI_SIZE + (le16toh(data->buf[RT11_DH_EXTRA]) >> 1);
  uint16_t src = offset, dest = offset;
  uint16_t free = le16toh(data->buf[offset + RT11_DI_LENGTH]);

  /*
   * Check for previous empty entry.
   */
  if (offset != RT11_DH_SIZE) {
    uint16_t off = offset - entrysz;

    if ((le16toh(data->buf[off + RT11_DI_STATUS]) & RT11_E_MPTY) != 0) {
      free += le16toh(data->buf[off + RT11_DI_LENGTH]);
      dest = off;
    }
  }

  /*
   * Check for following empty entry
   */
  if ((RT11_DS_SIZE - (offset + entrysz)) >= entrysz) {
    uint16_t off = offset + entrysz;

    if ((le16toh(data->buf[off + RT11_DI_STATUS]) & RT11_E_MPTY) != 0) {
      free += le16toh(data->buf[off + RT11_DI_LENGTH]);
      src = off;
    }
  }

  if (src != dest) {
    /*
     * We need to adjust the size of the unused region and slide the rest
     * of the directory down 1 or 2 entries. Since we are collapsing at
     * least 2 directory entries together, we will zero out the file name
     * and type.
     */
    data->buf[dest + RT11_DI_LENGTH] = htole16(free);
    data->buf[dest + RT11_DI_FNAME1] = 0;
    data->buf[dest + RT11_DI_FNAME2] = 0;
    data->buf[dest + RT11_DI_FTYPE] = 0;

    dest += entrysz;
    src += entrysz;

    /*
     * Now slide the directory down
     */
    for (;src < RT11_DS_SIZE;) {
      uint16_t i;

      data->buf[dest + RT11_DI_STATUS] = data->buf[src + RT11_DI_STATUS];

      if (RT11EOS(le16toh(data->buf[dest + RT11_DI_STATUS])) ||
          ((RT11_DS_SIZE - src) < entrysz))
        break;

      for (i = RT11_DI_FNAME1;i < entrysz; i++)
        data->buf[dest + i] = data->buf[src + i];

      dest += entrysz;
      src += entrysz;
    }

    /*
     * Make sure the last entry contains the end-of-segment marker.
     */
    data->buf[dest + RT11_DI_STATUS] = htole16(RT11_E_EOS);
  }
}

/*++
 *      r t 1 1 S p l i t D i r S e g m e n t
 *
 *  Split a directory segment into 2 pieces. The directory segment to be
 *  split is currently in the mount specific buffer.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - partition number
 *      entrysz         - size of directory entries
 *      segment         - the directory segment # to be split
 *      newsegment      - the new directory segment to be added
 *
 * Outputs:
 *
 *      The mount specific buffer will be modified
 *
 * Returns:
 *
 *      1 if split was successful, 0 otherwise
 *
 --*/
static int rt11SplitDirSegment(
  struct mountedFS *mount,
  uint8_t unit,
  uint16_t entrysz,
  uint16_t segment,
  uint16_t newsegment
)
{
  struct RT11data *data = &mount->rt11data;
  uint16_t entries = RT11_DS_DISPACE / entrysz;
  uint16_t nextsegment = data->buf[RT11_DH_NEXT];
  uint16_t entry, count, status, startblk = le16toh(data->buf[RT11_DH_START]);
  uint16_t dest;

  /*
   * Find first permanent or tentative file entry in the middle of the
   * directory segment.
   */
  for (count = 0, entry = RT11_DH_SIZE;;count++, entry += entrysz) {
    status = le16toh(data->buf[entry + RT11_DI_STATUS]);
    if ((count > (entries / 2)) &&
        ((status & (RT11_E_PERM | RT11_E_TENT)) != 0))
      break;
    startblk += le16toh(data->buf[entry + RT11_DI_LENGTH]);
  }

  /*
   * Terminate the current directory segment at this point.
   */
  data->buf[entry + RT11_DI_STATUS] = htole16(RT11_E_EOS);

  /*
   * Link to the new segment and update the directory segment on disk.
   */
  data->buf[RT11_DH_NEXT] = htole16(newsegment);
  if (rt11WriteDirSegment(mount, unit, segment) == 0)
    return 0;

  /*
   * Update the directory header to represent the new directory segment.
   */
  data->buf[RT11_DH_NEXT] = nextsegment;
  data->buf[RT11_DH_START] = htole16(startblk);

  /*
   * Restore the directory entry status word, slide down the directory entries
   * to the beginning of the directory segment and update the new directory
   * segment on disk.
   */
  data->buf[entry + RT11_DI_STATUS] = htole16(status);

  for (dest = RT11_DH_SIZE;entry < RT11_DS_SIZE;) {
    uint16_t i;

    data->buf[dest + RT11_DI_STATUS] = data->buf[entry + RT11_DI_STATUS];
    
    if (RT11EOS(le16toh(data->buf[dest + RT11_DI_STATUS])) ||
        ((RT11_DS_SIZE - entry) < entrysz))
      break;

    for (i = RT11_DI_FNAME1;i < entrysz; i++)
      data->buf[dest + i] = data->buf[entry + i];
    
    dest += entrysz;
    entry += entrysz;
  }

  /*
   * Make sure the last entry contains the end-of-segment marker.
   */
  data->buf[dest + RT11_DI_STATUS] = htole16(RT11_E_EOS);

  if (rt11WriteDirSegment(mount, unit, newsegment) == 0)
    return 0;

  /*
   * Update the highest directory segment in use in the first directory
   * segment.
   */
  if (rt11ReadDirSegment(mount, unit, 1) == 0)
    return 0;

  data->buf[RT11_DH_HIGHEST] = htole16(newsegment);

  if (rt11WriteDirSegment(mount, unit, 1) == 0)
    return 0;

  return 1;
}

/*++
 *      r t 1 1 D a t e
 *
 *  Convert an RT-11 date value into an ASCII string.
 *
 * Inputs:
 *
 *      value           - RT-11 date value
 *      buf             - buffer to receive the string (requires 12 bytes)
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Pointer to the date string
 *
 --*/
char *rt11Date(
  uint16_t value,
  char *buf
)
{
  sprintf(buf, "%02d-%s-%4d",
          (value & RT11_DW_DAY) >> 5,
          month[((value & RT11_DW_MONTH) >> 10) - 1],
          1972 + (value & RT11_DW_YEAR) + ((value & RT11_DW_AGE) >> 14) * 32);

  return buf;
}

/*++
 *      r t 1 1 D i s p l a y D i r
 *
 *  Print a directory entry on stdout.
 *
 * Inputs:
 *
 *      dir             - pointer to the directory entry for the file
 *      full            - if non-zero, print a full directory entry
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
void rt11DisplayDir(
  uint16_t *dir,
  int full
)
{
  char temp[64], creation[16];

  r50Asc(le16toh(dir[RT11_DI_FNAME1]), &temp[0]);
  r50Asc(le16toh(dir[RT11_DI_FNAME2]), &temp[3]);
  temp[6] = '.';
  r50Asc(le16toh(dir[RT11_DI_FTYPE]), &temp[7]);

  if (full)
    sprintf(&temp[10], "  %5u  %s",
            le16toh(dir[RT11_DI_LENGTH]),
            rt11Date(le16toh(dir[RT11_DI_CREATE]), creation));
  else temp[10] ='\0';
  puts(temp);
}

/*++
 *      r t 1 1 C r e a t e F i l e
 *
 *  Create a new file within the RT-11 file system. The new file will be
 *  marked as tentative since the resources allocated to the file may be
 *  in flux.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - partition number
 *      spec            - pointer to the file specification block
 *      file            - pointer to open file descriptor to receive results
 *      size            - file size in 512-byte blocks
 *                        0 means use the largest free space region
 *
 * Outputs:
 *
 *      The mount point specific buffer area will be modified.
 *
 * Returns:
 *
 *      1 if file successfully created, 0 otherwise
 *
 --*/
int rt11CreateFile(
  struct mountedFS *mount,
  uint8_t unit,
  struct rt11FileSpec *spec,
  struct rt11OpenFile *file,
  uint16_t size
)
{
  struct RT11data *data = &mount->rt11data;
  struct tm tm;
  time_t now = time(NULL);
  uint16_t year, today;
  uint16_t entrysz, empty, dstart, dseg, doffset, next;

  /*
   * Check if suitable free space is available
   */
  if (rt11BestFit(mount, unit, size, &dstart, &dseg, &doffset, &next) == 0) {
    ERROR("Free space not available on \"%s%o:\"\n", mount->name, unit);
    return 0;
  }

  /*
   * Create an RT-11 timestamp for today
   */
  localtime_r(&now, &tm);

  year = tm.tm_year - 72;
  today = ((year / 32) << 14) | (year % 32);
  today |= (tm.tm_mday << 5) | ((tm.tm_mon + 1) << 10);

  if (rt11ReadDirSegment(mount, unit, dseg) == 0)
    return 0;

  entrysz = RT11_DI_SIZE + (le16toh(data->buf[RT11_DH_EXTRA]) >> 1);

  /*
   * Try to open up a new directory entry in the current directory segment
   */
  if (rt11MakeDirectoryEntry(mount, entrysz, doffset) == 0) {
    /*
     * No space for a new directory entry. Split the current directory
     * segment and try again.
     */
    if (next == 0) {
      ERROR("No directory segment available for split on \"%s%o:\"\n",
            mount->name, unit);
      return 0;
    }

    if (rt11SplitDirSegment(mount, unit, entrysz, dseg, next) == 0) {
      ERROR("Error splitting directory segment on \"%s%o:\"\n",
            mount->name, unit);
      return 0;
    }

    if (rt11BestFit(mount, unit, size, &dstart, &dseg, &doffset, NULL) == 0) {
      ERROR("Panic: second best fit failed on\"%s%o:\"\n", mount->name, unit);
      exit(1);
    }

    if (rt11ReadDirSegment(mount, unit, dseg) == 0)
      return 0;

    if (rt11MakeDirectoryEntry(mount, entrysz, doffset) == 0) {
      ERROR("Panic: second directory make operation failed on \"%s%o:\"\n",
            mount->name, unit);
      exit(2);
    }
  }

  /*
   * Fill in the newly created directory entry
   */
  empty = doffset + RT11_DI_SIZE + (le16toh(data->buf[RT11_DH_EXTRA]) >> 1);

  data->buf[doffset + RT11_DI_STATUS] = RT11_E_TENT;
  data->buf[doffset + RT11_DI_FNAME1] = htole16(spec->name[0]);
  data->buf[doffset + RT11_DI_FNAME2] = htole16(spec->name[1]);
  data->buf[doffset + RT11_DI_FTYPE] = htole16(spec->type);
  data->buf[doffset + RT11_DI_JOB_CHN] = 0;
  data->buf[doffset + RT11_DI_CREATE] = htole16(today);

  if (size == 0) {
    data->buf[doffset + RT11_DI_LENGTH] = data->buf[empty + RT11_DI_LENGTH];
    data->buf[empty + RT11_DI_LENGTH] = 0;
  } else {
    data->buf[doffset + RT11_DI_LENGTH] = htole16(size);
    data->buf[empty + RT11_DI_LENGTH] =
      htole16(le16toh(data->buf[empty + RT11_DI_LENGTH]) - size);
  }

  if (rt11WriteDirSegment(mount, unit, dseg) == 0)
    return 0;

  /*
   * Fill in the open file descriptor.
   */
  file->status = data->buf[doffset + RT11_DI_STATUS];
  file->name[0] = data->buf[doffset + RT11_DI_FNAME1];
  file->name[1] = data->buf[doffset + RT11_DI_FNAME2];
  file->type = data->buf[doffset + RT11_DI_FTYPE];
  file->length = data->buf[doffset + RT11_DI_LENGTH];
  file->creation = data->buf[doffset + RT11_DI_CREATE];

  file->segment = dseg;
  file->offset = doffset;

  file->mount = mount;
  file->unit = unit;

  file->start = dstart;

  return 1;
}

/*++
 *      r t 1 1 L o o k u p F i l e
 *
 *  Lookup a specific file within the RT-11 file system. This routine fills
 *  in an open file descriptor with information about the file and the
 *  directory entry it resides in.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - partition number
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
int rt11LookupFile(
  struct mountedFS *mount,
  uint8_t unit,
  struct rt11FileSpec *spec,
  struct rt11OpenFile *file
)
{
  struct RT11data *data = &mount->rt11data;

  if (RT11_PARTITIONVALID(data, unit)) {
    uint16_t entrysz, position, dsseg = 1;

    do {
      uint16_t off = RT11_DH_SIZE;

      if (rt11ReadDirSegment(mount, unit, dsseg) == 0)
        return 0;

      entrysz = RT11_DI_SIZE + (le16toh(data->buf[RT11_DH_EXTRA]) >> 1);
      position = le16toh(data->buf[RT11_DH_START]);

      /*
       * Loop until we see and end-of-segment marker or there is no room for
       * another directory entry.
       */
      while (!RT11EOS(le16toh(data->buf[off + RT11_DI_STATUS])) &&
             ((RT11_DS_SIZE - off) >= entrysz)) {
        uint16_t status = le16toh(data->buf[off + RT11_DI_STATUS]);
        
        if ((status & RT11_E_MPTY) == 0) {
          if ((le16toh(data->buf[off + RT11_DI_FNAME1]) == spec->name[0]) &&
              (le16toh(data->buf[off + RT11_DI_FNAME2]) == spec->name[1]) &&
              (le16toh(data->buf[off + RT11_DI_FTYPE]) == spec->type)) {

            /*
             * Save directory entry and it's location in the open file
             * descriptor.
             */
            file->status = data->buf[off + RT11_DI_STATUS];
            file->name[0] = data->buf[off + RT11_DI_FNAME1];
            file->name[1] = data->buf[off + RT11_DI_FNAME2];
            file->type = data->buf[off + RT11_DI_FTYPE];
            file->length = data->buf[off + RT11_DI_LENGTH];
            file->creation = data->buf[off + RT11_DI_CREATE];

            file->segment = dsseg;
            file->offset = off;

            file->mount = mount;
            file->unit = unit;

            file->start = position;

            return 1;
          }
        }
        position += le16toh(data->buf[off + RT11_DI_LENGTH]);

        off += entrysz;
      }
      dsseg = le16toh(data->buf[RT11_DH_NEXT]);
    } while (dsseg != 0);
  }
  return 0;
}

/*++
 *      r t 1 1 U p d a t e F i l e
 *
 *  Update an RT-11 file by writing back the directory entry associated with
 *  the file.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - partition number
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
void rt11UpdateFile(
  struct mountedFS *mount,
  uint8_t unit,
  struct rt11OpenFile *file
)
{
  struct RT11data *data = &mount->rt11data;
  uint16_t offset, length, left, entrysz;

  if (rt11ReadDirSegment(mount, unit, file->segment) == 0)
    return;

  entrysz = RT11_DI_SIZE + (le16toh(data->buf[RT11_DH_EXTRA]) >> 1);
  offset = file->offset;

  length = file->current - file->start + 1;
  left = file->length - length;

  /*
   * Fix up the directory entry and the following empty entry
   */
  data->buf[offset + RT11_DI_STATUS] = htole16(RT11_E_PERM);
  data->buf[offset + RT11_DI_LENGTH] = htole16(length);

  data->buf[offset + entrysz + RT11_DI_LENGTH] =
    htole16(le16toh(data->buf[offset + entrysz + RT11_DI_LENGTH]) + left);

  if (le16toh(data->buf[offset + entrysz + RT11_DI_LENGTH]) == 0) {
    /*
     * Remove the now empty directory entry
     */
    uint16_t dest = offset + entrysz;
    uint16_t src = dest + entrysz;

    for (;src < RT11_DS_SIZE;) {
      uint16_t i;

      data->buf[dest + RT11_DI_STATUS] = data->buf[src + RT11_DI_STATUS];

      if (RT11EOS(le16toh(data->buf[dest + RT11_DI_STATUS])) ||
          ((RT11_DS_SIZE - src) < entrysz))
        break;

      for (i = RT11_DI_FNAME1;i < entrysz; i++)
        data->buf[dest + i] = data->buf[src + i];

      dest += entrysz;
      src += entrysz;
    }

    /*
     * Make sure the last entry contains the end-of-segment marker.
     */
    data->buf[dest + RT11_DI_STATUS] = htole16(RT11_E_EOS);
  }

  rt11WriteDirSegment(mount, file->unit, file->segment);
}

/*++
 *      i n f o
 *
 *  Display information about the internal structure of a single file system.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - partition number
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
  struct RT11data *data = &mount->rt11data;
  uint16_t entrysz, startblk, dsseg = 1;
  char temp[32];

  printf("%s%o:\n\n", mount->name, unit);

  do {
    uint16_t off = RT11_DH_SIZE;

    if (rt11ReadDirSegment(mount, unit, dsseg) == 0)
      return;

    printf("\nDirectory Segment %2d:\n\n", dsseg);
    printf(" File  Type     Date     Status   Class   Length    Disk Region\n\n");

    entrysz = RT11_DI_SIZE + (le16toh(data->buf[RT11_DH_EXTRA]) >> 1);
    startblk = le16toh(data->buf[RT11_DH_START]);

    /*
     * Loop until we see and end-of-segment marker or there is no room for
     * another directory entry.
     */
    while (!RT11EOS(le16toh(data->buf[off + RT11_DI_STATUS])) &&
           ((RT11_DS_SIZE - off) >= entrysz)) {
      uint16_t status = le16toh(data->buf[off + RT11_DI_STATUS]);

      /*
       * Tentative and empty files are reported as "UNUSED"
       */
      if ((status & (RT11_E_TENT | RT11_E_MPTY)) == 0) {
        r50Asc(le16toh(data->buf[off + RT11_DI_FNAME1]), &temp[0]);
        r50Asc(le16toh(data->buf[off + RT11_DI_FNAME2]), &temp[3]);
        temp[6] = '.';
        r50Asc(le16toh(data->buf[off + RT11_DI_FTYPE]), &temp[7]);
        temp[10] = ' ';
        temp[11] = ' ';
        rt11Date(le16toh(data->buf[off + RT11_DI_CREATE]), &temp[12]);

        printf("%s  %07o  %s     %5d     %5d-%5d\n",
               temp, status,
               (status & RT11_E_PRE) != 0 ? "PRE" : "   ",
               le16toh(data->buf[off + RT11_DI_LENGTH]),
               startblk,
               startblk + le16toh(data->buf[off + RT11_DI_LENGTH]) - 1);
      } else {
        printf("< UNUSED >                                %5d     %5d-%5d\n",
               le16toh(data->buf[off + RT11_DI_LENGTH]),
               startblk,
               le16toh(data->buf[off + RT11_DI_LENGTH]) == 0 ? startblk :
                 startblk + le16toh(data->buf[off + RT11_DI_LENGTH]) - 1);
      }
      startblk += le16toh(data->buf[off + RT11_DI_LENGTH]);

      off += entrysz;
    }

    dsseg = le16toh(data->buf[RT11_DH_NEXT]);
  } while (dsseg != 0);
}

/*++
 *      p a r t i t i o n T y p e
 *
 *  Determine the type of the partition whose home block is in the mount
 *  point specific buffer.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - partition number
 *
 * Outputs:
 *
 *      ...
 *
 * Returns:
 *
 *      partition type:
 *
 *        RT11_NOPART   - Not a valid RT11 partition
 *        RT11_SINGLE   - 1 partition supported per disk
 *        RT11_MULTI    - Multiple partitions supported per disk
 *
 --*/
static int partitionType(
  struct mountedFS *mount,
  uint8_t unit
)
{
  struct RT11data *data = &mount->rt11data;

  if (strncmp((char *)&data->buf[RT11_HB_SYSID], RT11_SYSID, strlen(RT11_SYSID)) == 0) {
    uint16_t type = le16toh(data->buf[RT11_HB_SYSVER]);

    if (unit == 0) {
      if ((type == RT11_SYSVER_V3A) || (type == RT11_SYSVER_V04))
        return RT11_SINGLE;
    }

    if (type == RT11_SYSVER_V05)
      return RT11_MULTI;
  }

  if (strncmp((char *)&data->buf[RT11_HB_SYSID], RT11_VMSSYSID, strlen(RT11_VMSSYSID)) == 0)
    return RT11_SINGLE;

  return RT11_NOPART;
}

/*++
 *      v a l i d a t e
 *
 *  Verify that a partition contains a valid RT-11 filesystem. First verify
 *  that the ID fields are correct, then walk the directory structure to
 *  make sure that we are looking at a valid RT-11 partition.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      unit            - partition number
 *      maxblk          - return maximum block # of the partition here
 *      first           - return first directory segment block # here
 *
 * Outputs:
 *
 *      The mount point specific buffer will be overwritten.
 *
 * Returns:
 *
 *      partition type:
 *
 *        RT11_NOPART   - Not a valid RT11 partition
 *        RT11_SINGLE   - 1 partition supported per disk
 *        RT11_MULTI    - Multiple partitions supported per disk
 *
 --*/
static int validate(
  struct mountedFS *mount,
  uint8_t unit,
  uint16_t *maxblk,
  uint16_t *first
)
{
  struct RT11data *data = &mount->rt11data;
  int type = RT11_SINGLE;
  uint16_t entrysz, position, dsseg = 1;
  uint16_t seg_count, seg_highest, highest = 0;
  uint8_t seen[RT11_DS_MAX + 1];

  if (rt11ReadBlock(mount, unit, RT11_HOME, NULL) == 0)
    return RT11_NOPART;

  if (!SWISSET('f')) {
    if ((type = partitionType(mount, unit)) == RT11_NOPART)
      return RT11_NOPART;

    *first = le16toh(data->buf[RT11_HB_FIRST]);
  } else *first = RT11_DSSTART;

  memset(seen, 0, sizeof(seen));

  /*
   * Now walk the directory to validate it's integrity.
   */
  do {
    uint16_t off = RT11_DH_SIZE;

    if (rt11ReadDirSegment(mount, unit, dsseg) == 0)
      return RT11_NOPART;

    /*
     * Make sure we only look at each directory segment once - we want to
     * avoid looping up with invalid input.
     */
    if (seen[dsseg]++ != 0)
      return RT11_NOPART;

    entrysz = RT11_DI_SIZE + (le16toh(data->buf[RT11_DH_EXTRA]) >> 1);
    position = le16toh(data->buf[RT11_DH_START]);
 
    if (dsseg == 1) {
      seg_count = le16toh(data->buf[RT11_DH_COUNT]);
      seg_highest = le16toh(data->buf[RT11_DH_HIGHEST]);
      if ((seg_highest > RT11_DS_MAX) || (seg_highest > seg_count))
        return RT11_NOPART;
    }

    if (seg_count != le16toh(data->buf[RT11_DH_COUNT]))
      return RT11_NOPART;

    /*
     * Loop until we see and end-of-segment marker or there is no room for
     * another directory entry.
     */
    while (!RT11EOS(le16toh(data->buf[off + RT11_DI_STATUS])) &&
           ((RT11_DS_SIZE - off) >= entrysz)) {
      uint16_t status = le16toh(data->buf[off + RT11_DI_STATUS]);

      position += le16toh(data->buf[off + RT11_DI_LENGTH]);

      if ((status & RT11_E_MPTY) != 0)
        break;

      /*
       * Within each directory segment the base address should never
       * decrease.
       */
      if (((position + le16toh(data->buf[off + RT11_DI_LENGTH])) & 0xFFFF) < position)
        return RT11_NOPART;

      off += entrysz;
    }
    if (position > highest)
      highest = position;

    dsseg = le16toh(data->buf[RT11_DH_NEXT]);

    if (dsseg > seg_highest)
      return RT11_NOPART;
   } while (dsseg != 0);

  *maxblk = highest - 1;
  return type;
}

/*++
 *      r t 1 1 R e a d B y t e s
 *
 *  Read a sequence of bytes from an open file.
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
int rt11ReadBytes(
  struct rt11OpenFile *file,
  char *buf,
  int len
)
{
  struct mountedFS *mount = file->mount;
  int count = 0;

  if (file->current == 0) {
    file->current = le16toh(file->start);

    if (rt11ReadBlock(mount, file->unit, file->current, file->buffer) == 0)
      return 0;

    file->count = 0;
    file->last = le16toh(file->start) + le16toh(file->length) - 1;
 }

  while (len) {
    if (file->count == RT11_BLOCKSIZE) {
      if (file->current == file->last)
        break;

      file->current++;

      if (rt11ReadBlock(mount, file->unit, file->current, file->buffer) == 0)
        return 0;

      file->count = 0;
    }
    *buf++ = file->buffer[file->count++];
    len--;
    count++;
  }
  return count;
}

/*++
 *      r t 1 1 W r i t e B y t e s
 *
 *  Write a sequence of bytes to an open file.
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
 *      # of bytes written to the file (may be less than "len"), 0 if error
 *
 --*/
int rt11WriteBytes(
  struct rt11OpenFile *file,
  char *buf,
  int len
)
{
  int count = 0;

  if (file->current == 0) {
    memset(file->buffer, 0, RT11_BLOCKSIZE);

    file->current = le16toh(file->start);
    file->count = 0;
    file->last = le16toh(file->start) + le16toh(file->length) - 1;
  }

  while (len) {
    if (file->count == RT11_BLOCKSIZE) {
      if (file->current == file->last)
        break;

      if (rt11WriteBlock(file->mount, file->unit, file->current, file->buffer) == 0)
        return 0;

      memset(file->buffer, 0, RT11_BLOCKSIZE);

      file->current++;
      file->count = 0;
    }
    file->buffer[file->count++] = *buf++;
    len--;
    count++;
  }
  return count;
}

/*++
 *      r t 1 1 U n g e t B y t e
 *
 *  Move back the internal buffer pointer by 1 byte so that the previous byte
 *  will be read again. This is only used in ASCII mode when we read a ^Z
 *  character indicating EOF.
 *
 * Inputs:
 *
 *      file            - pointer to open file descriptor
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
static void rt11UngetByte(
  struct rt11OpenFile *file
)
{
  if (file->count != 0)
    file->count--;}

/*++
 *      r t 1 1 M o u n t
 *
 *  Verify that the open container file contains 1 or more RT-11 file systems.
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
 *      1 if 1 or more valid RT-11 file systems, 0 otherwise
 *
 --*/
static int rt11Mount(
  struct mountedFS *mount
)
{
  struct RT11data *data = &mount->rt11data;
  struct stat stat;

  data->sectorsz = 0;

  /*
   * Check for device type override.
   */
  if (SWISSET('t')) {
    if (strcmp("rx01", SWGETVAL('t')) == 0) {
      mount->skip = RT11_RX0xNSECT * RT11_RX01SS;
      data->sectorsz = RT11_RX01SS;
    }
    if (strcmp("rx02", SWGETVAL('t')) == 0) {
      mount->skip = RT11_RX0xNSECT * RT11_RX02SS;
      data->sectorsz = RT11_RX02SS;
    }

    if (data->sectorsz == 0)
      fprintf(stderr,
              "mount: Ignoring unknown disk type \"%s\"\n", SWGETVAL('t'));
  }

  memset(&data->valid, 0, sizeof(data->valid));

  if (fstat(fileno(mount->container), &stat) == 0) {
    uint16_t i, count, lastsz, validcount = 0;;

    data->blocks = stat.st_size / RT11_BLOCKSIZE;

    count = data->blocks / RT11_MAXPARTSZ;
    lastsz = data->blocks % RT11_MAXPARTSZ;

    if (lastsz >= RT11_MINPARTSZ)
      count++;

    /*
     * Check each file system for validity.
     */
    for (i = 0; i < count; i++) {
      int type;

      /*
       * Assume valid and maximal size
       */
      data->valid[i / 16] |= 1 << (i % 16);
      data->maxblk[i] = RT11_MAXPARTSZ - 1;

      type = validate(mount, i, &data->maxblk[i], &data->first[i]);

      if (type != RT11_NOPART)
        validcount++;
      else data->valid[i / 16] &= ~(1 << (i % 16));

      if (type == RT11_SINGLE)
        break;
    }

    data->filesystems = validcount;

    if (validcount != 0) {
      if (!quiet)
        printf("%s: successfully mounted (%d partition%s)\n\n",
               mount->name, validcount, validcount == 1 ? "" : "s");

      for (i = 0; (i < 256) && (validcount != 0); i++)
        if (RT11_PARTITIONVALID(data, i)) {
          uint16_t entrysz, freeblks = 0, dsseg = 1;
          uint16_t highest = 0;

          do {
            uint16_t off = RT11_DH_SIZE;

            if (rt11ReadDirSegment(mount, i, dsseg) == 0)
              return 0;

            if (highest == 0)
              highest = le16toh(data->buf[RT11_DH_HIGHEST]);

            entrysz = RT11_DI_SIZE + (le16toh(data->buf[RT11_DH_EXTRA]) >> 1);

            while (!RT11EOS(le16toh(data->buf[off + RT11_DI_STATUS])) &&
                   ((RT11_DS_SIZE - off) >= entrysz)) {
              uint16_t status = le16toh(data->buf[off + RT11_DI_STATUS]);

              if ((status & RT11_E_MPTY) != 0)
                freeblks += le16toh(data->buf[off + RT11_DI_LENGTH]);

              off += entrysz;
            }
            dsseg = le16toh(data->buf[RT11_DH_NEXT]);
          } while (dsseg != 0);

          if (!quiet) {
            char vers[4], *version = NULL;
            uint16_t cnt, extra;

            cnt = le16toh(data->buf[RT11_DH_COUNT]);
            extra = le16toh(data->buf[RT11_DH_EXTRA]);

            if (rt11ReadBlock(mount, i, RT11_HOME, NULL) == 0)
              return 0;

            /*
             * Special handling of volumes created by non-RT-11 systenms
             * (e.g. VMS Exchange).
             */
            if (strncmp((char *)&data->buf[RT11_HB_SYSID], RT11_VMSSYSID, strlen(RT11_VMSSYSID)) == 0) {
              r50Asc(le16toh(data->buf[RT11_HB_SYSVER]), vers);
              version = vers;
            } else {
              switch (le16toh(data->buf[RT11_HB_SYSVER])) {
                case RT11_SYSVER_V3A:
                  version = "V3A";
                  break;

                case RT11_SYSVER_V04:
                  version = "V04";
                  break;

                case RT11_SYSVER_V05:
                  version = "V05";
                  break;
              }
            }

            printf("%s%o:\n", mount->name, i);
            if (version != NULL)
              printf("  Version: %s,        System ID: %12s\n",
                     version, (char *)&data->buf[RT11_HB_SYSID]);
            printf("  Total blocks: %5d, Free blocks: %5d\n"
                   "  Directory segments: %2d (Highest in use: %d)\n"
                   "  Extra bytes/directory entry: %d\n",
                   data->maxblk[i] + 1, freeblks, cnt, highest, extra);
            if (data->sectorsz != 0)
              printf("  Sector size: %d\n", data->sectorsz);
          }
          validcount--;
        }
      return 1;
    }
  }
  return 0;
}

/*++
 *      r t 1 1 U m o u n t
 *
 *  Unmount the RT-11 file system(s), releasing any storage allocated.
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
static void rt11Umount(
  struct mountedFS *UNUSED(mount)
)
{
}

/*++
 *      r t 1 1 S i z e
 *
 *  Return the size of an RT-11 container file.
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
 *      Size of the container file in bytes.
 *
 --*/
static size_t rt11Size(void)
{
  size_t size = (RT11_MAXPARTSZ - 1) * RT11_BLOCKSIZE;

  if (SWISSET('t')) {
    int i = 0;
    char *type = SWGETVAL('t');

    while (rt11DiskSize[i].name != NULL) {
      if (strcmp(rt11DiskSize[i].name, type) == 0) {
        size = rt11DiskSize[i].size;
        break;
      }
      i++;
    }
    if (size == ((RT11_MAXPARTSZ - 1) * RT11_BLOCKSIZE))
      fprintf(stderr,
              "newfs: Invalid device type \"%s\", using default\n", type);
  }
  return size;
}

/*++
 *      r t 1 1 N e w f s
 *
 *  Create an empty RT11 file system.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *                        (not in the mounted file system list)
 *      size            - the size (in bytes) of the file system
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if the file system was successfully created, 0 otherwise
 *
 --*/
static int rt11Newfs(
  struct mountedFS *mount,
  size_t size
)
{
  struct RT11data *data = &mount->rt11data;
  int i;
  uint16_t checksum = 0, extra = 0;;

  /*
   * Check for device type override.
   */
  if (SWISSET('t')) {
    if (strcmp("rx01", SWGETVAL('t')) == 0) {
      mount->skip = RT11_RX0xNSECT * RT11_RX01SS;
      data->sectorsz = RT11_RX01SS;
    }
    if (strcmp("rx02", SWGETVAL('t')) == 0) {
      mount->skip = RT11_RX0xNSECT * RT11_RX02SS;
      data->sectorsz = RT11_RX02SS;
    }
  }

  /*
   * Check for extra bytes on each directory entry, rounded up to nearest
   * even value.
   */
  if (SWISSET('e')) {
    char *endptr;

    extra = strtoul(SWGETVAL('e'), &endptr, 10);
    extra = (extra + 1) & ~1;

    if ((extra > 63) || (*endptr != '\0')) {
      fprintf(stderr, "newfs: bad -e switch value \"%s\" - ignored\n",
              SWGETVAL('e'));
      extra = 0;
    }
  }

  /*
   * Remove possible first track
   */
  size = (size - mount->skip) / RT11_BLOCKSIZE;
  //  size = ((size * RT11_BLOCKSIZE) - mount->skip) / RT11_BLOCKSIZE;

  /*
   * Mark partition 0 as valid
   */
  memset(data->valid, 0, sizeof(data->valid));
  data->valid[0] = 1;
  data->maxblk[0] = size;
  data->first[0] = RT11_DSSTART;
  data->filesystems = 1;

  /*
   * Build and write the Home Block.
   */
  memset(&data->buf[0], 0, RT11_BLOCKSIZE);

  data->buf[RT11_HB_PCS] = htole16(1);
  data->buf[RT11_HB_FIRST] = htole16(RT11_DSSTART);
  data->buf[RT11_HB_SYSVER] = htole16(RT11_SYSVER_V05);

  memcpy((char *)&data->buf[RT11_HB_VOLID], RT11_VOLID, strlen(RT11_VOLID));
  memcpy((char *)&data->buf[RT11_HB_OWNER], RT11_OWNER, strlen(RT11_OWNER));
  memcpy((char *)&data->buf[RT11_HB_SYSID], RT11_SYSID, strlen(RT11_SYSID));

  for (i = 0; i < 255; i++)
    checksum += le16toh(data->buf[i]);

  data->buf[RT11_HB_CHKSUM] = htole16(checksum);

  if (rt11WriteBlock(mount, 0, RT11_HOME, NULL) == 0)
    return 0;

  /*
   * Build the maximum # of directory segments
   */
  for (i = 1; i <= RT11_DS_MAX; i++) {
    memset(data->buf, 0, sizeof(data->buf));

    data->buf[RT11_DH_COUNT] = htole16(RT11_DS_MAX);
    data->buf[RT11_DH_HIGHEST] = htole16(1);
    data->buf[RT11_DH_EXTRA] = htole16(extra);
    data->buf[RT11_DH_START] = htole16(RT11_DSSTART + (2 * RT11_DS_MAX));

    if (i == 1) {
      data->buf[RT11_DH_SIZE + RT11_DI_STATUS] = htole16(RT11_E_MPTY);
      data->buf[RT11_DH_SIZE + RT11_DI_LENGTH] =
        htole16(size - (RT11_DSSTART + (2 * RT11_DS_MAX)));
      data->buf[RT11_DH_SIZE + RT11_DI_SIZE + RT11_DI_STATUS] =
        htole16(RT11_E_EOS);
    } else data->buf[RT11_DH_SIZE + RT11_DI_STATUS] = htole16(RT11_E_EOS);

    if (rt11WriteDirSegment(mount, 0, i) == 0)
      return 0;
  }
  return 1;
}

/*++
 *      r t 1 1 I n f o
 *
 *  Display information about the internal structure of the RT-11 file
 *  system(s).
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - partition number
 *      present         - partition number present
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
static void rt11Info(
  struct mountedFS *mount,
  uint8_t unit,
  uint8_t present
)
{
  struct RT11data *data = &mount->rt11data;

  if (present) {
    if (RT11_PARTITIONVALID(data, unit))
      info(mount, unit);
  } else {
    uint16_t i, count = data->filesystems;

    /*
     * Display information about all valid partitions
     */
    for (i = 0; (i < 256) && (count != 0); i++)
      if (RT11_PARTITIONVALID(data, i)) {
        info(mount, i);
        count--;
      }
  }
}

/*++
 *      r t 1 1 D i r
 *
 *  Produce a full or brief directory listing.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - partition number
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
static void rt11Dir(
  struct mountedFS *mount,
  uint8_t unit,
  char *fname
)
{
  struct RT11data *data = &mount->rt11data;
  struct rt11FileSpec spec;

  if (rt11ParseFilespec(fname, &spec, RT11_M_NONAME) == 0) {
    fprintf(stderr, "dir: syntax error in file spec \"%s\"\n", fname);
    return;
  }

  if (RT11_PARTITIONVALID(data, unit)) {
    uint16_t entrysz, dsseg = 1;
    regex_t reg;

    if ((spec.flags & (RT11_WC_NAME | RT11_WC_TYPE)) != 0)
      if (rt11BuildRegex(&spec, &reg) == 0) {
        fprintf(stderr, "dir: regcomp() failed\n");
        return;
      }

    do {
      uint16_t off = RT11_DH_SIZE;

      if (rt11ReadDirSegment(mount, unit, dsseg) == 0) {
        if ((spec.flags & (RT11_WC_NAME | RT11_WC_TYPE)) != 0)
          regfree(&reg);
        return;
      }

      entrysz = RT11_DI_SIZE + (le16toh(data->buf[RT11_DH_EXTRA]) >> 1);

      /*
       * Loop until we see and end-of-segment marker or there is no room for
       * another directory entry.
       */
      while (!RT11EOS(le16toh(data->buf[off + RT11_DI_STATUS])) &&
             ((RT11_DS_SIZE - off) >= entrysz)) {
        uint16_t status = le16toh(data->buf[off + RT11_DI_STATUS]);
        
        if ((status & (RT11_E_TENT | RT11_E_MPTY)) == 0) {
          uint16_t fname1 = le16toh(data->buf[off + RT11_DI_FNAME1]);
          uint16_t fname2 = le16toh(data->buf[off + RT11_DI_FNAME2]);
          uint16_t ftype = le16toh(data->buf[off + RT11_DI_FTYPE]);

          if ((spec.flags & (RT11_WC_NAME | RT11_WC_TYPE)) == 0) {
            if ((fname1 != spec.name[0]) ||
                (fname2 != spec.name[1]) ||
                (ftype != spec.type))
              goto nomatch;
          } else {
            if (rt11MatchRegex(&reg, fname1, fname2, ftype) == 0)
              goto nomatch;
          }

          rt11DisplayDir(&data->buf[off], SWISSET('f'));
        }

      nomatch:
        off += entrysz;
      }

      dsseg = le16toh(data->buf[RT11_DH_NEXT]);
    } while (dsseg != 0);

    if ((spec.flags & (RT11_WC_NAME | RT11_WC_TYPE)) != 0)
      regfree(&reg);
  }
}

/*++
 *      r t 1 1 O p e n F i l e R
 *
 *  Open an RT-11 file for reading.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - partition number
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
static void *rt11OpenFileR(
  struct mountedFS *mount,
  uint8_t unit,
  char *fname
)
{
  struct rt11OpenFile *file;
  struct rt11FileSpec spec;

  if (rt11ParseFilespec(fname, &spec, RT11_M_NONE) == 0) {
    fprintf(stderr, "Failed to parse filename \"%s\"\n", fname);
    return NULL;
  }

  if ((file = malloc(sizeof(struct rt11OpenFile))) != NULL) {
    memset(file, 0, sizeof(struct rt11OpenFile));

    if (rt11LookupFile(mount, unit, &spec, file) != 0) {
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
 *      r t 1 1 O p e n F i l e W
 *
 *  Open an RT-11 file for writing.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - partition number
 *      fname           - pointer to filename string
 *      size            - estimated file size (in bytes)
 *                        0 means allocate as much space as possible
 *
 * Outputs:
 *
 *      %None
 *
 * Returns:
 *
 *      Pointer to open file descriptor, NULL if open fails
 *
 --*/
void *rt11OpenFileW(
  struct mountedFS *mount,
  uint8_t unit,
  char *fname,
  off_t size
)
{
  struct rt11OpenFile *file;
  struct rt11FileSpec spec;
  uint16_t blocks = 0;

  if (size != 0)
    blocks = (size + (RT11_BLOCKSIZE - 1)) / RT11_BLOCKSIZE;

  if (rt11ParseFilespec(fname, &spec, RT11_M_NONE) == 0) {
    fprintf(stderr, "Failed to parse filename \"%s\"\n", fname);
    return NULL;
  }

  if ((file = malloc(sizeof(struct rt11OpenFile))) != NULL) {
    memset(file, 0, sizeof(struct rt11OpenFile));

    if (rt11LookupFile(mount, unit, &spec, file) == 0) {
      /*
       * Allocate local buffer space for the file.
       */
      if ((file->buffer = malloc(mount->blocksz)) != NULL) {
        if (rt11CreateFile(mount, unit, &spec, file, blocks) == 0) {
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
      ERROR("File \"%s\" already exists\n", fname);
      free(file);
      return NULL;
    }
  } else ERROR("Memory allocation failure\n");
  return file;
}

/*++
 *      r t 1 1 F i l e S i z e
 *
 *  Return an estimate of the size of a currently open file. This routine
 *  bases the file size on the number of blocks allocated to the file and
 *  may over-report the actual size of the file.
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
static off_t rt11FileSize(
  void *filep
)
{
  struct rt11OpenFile *file = filep;

  return le16toh(file->length) * RT11_BLOCKSIZE;
}

/*++
 *      r t 1 1 D e l e t e F i l e
 *
 *  Delete a file from an RT-11 file system.
 *
 * Inputs:
 *
 *      filep           - pointer to open file descriptor
 *      fname           - pointer to filename string
 *
 * Outputs:
 *
 *      The mount point specific buffer will be modified.
 *
 * Returns:
 *
 *      None
 *
 --*/
static void rt11DeleteFile(
  void *filep,
  char *UNUSED(fname)
)
{
  struct rt11OpenFile *file = filep;
  struct mountedFS *mount = file->mount;
  struct RT11data *data = &mount->rt11data;

  if (rt11ReadDirSegment(mount, file->unit, file->segment) == 0)
    return;

  data->buf[file->offset + RT11_DI_STATUS] = le16toh(RT11_E_MPTY);

  rt11MergeEmptyRegions(mount, file->offset);

  if (rt11WriteDirSegment(mount, file->unit, file->segment) == 0)
    return;

  rt11CloseFile(file);
}

/*++
 *      r t 1 1 C l o s e F i l e
 *
 *  Close an open RT-11 file.
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
static void rt11CloseFile(
  void *filep
)
{
  struct rt11OpenFile *file = filep;
  
  if (file->mode == M_WR) {
    if (SWISSET('a') &&
        !SWISSET('p') &&
        (file->count != RT11_BLOCKSIZE)) {
      char ch = '\032';

      rt11WriteBytes(file, &ch, 1);
    }

    if (file->current == 0)
      file->current = le16toh(file->start);

    /*
     * Flush the current buffer
     */
    rt11WriteBlock(file->mount, file->unit, file->current, file->buffer);
    rt11UpdateFile(file->mount, file->unit, file);
  }

  if (file != NULL) {
    if (file->buffer != NULL)
      free(file->buffer);
    free(file);
  }
}

/*++
 *      r t 1 1 R e a d F i l e
 *
 *  Read data from an RT-11 file to a supplied buffer.
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
static size_t rt11ReadFile(
  void *filep,
  void *buf,
  size_t buflen
)
{
  struct rt11OpenFile *file = filep;
  char *bufr = buf;

  if (SWISSET('a')) {
    char ch;
    size_t count = 0;

    /*
     * Read a full or partial line from the open file.
     */
    while ((buflen != 0) && (rt11ReadBytes(file, &ch, 1) == 1)) {
      if (ch == '\032') {
        /*
         * ^Z indicating EOF
         */
        rt11UngetByte(file);
        break;
      }
      bufr[count++] = ch;
      buflen--;
      if (ch == '\n')
        break;
    }
    return count;
  }

  return rt11ReadBytes(file, bufr, buflen);
}

/*++
 *      r t 1 1 W r i t e F i l e
 *
 *  Write data to an RT-11 file from a supplied buffer.
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
static size_t rt11WriteFile(
  void *filep,
  void *buf,
  size_t buflen
)
{
  struct rt11OpenFile *file = filep;
  char *bufw = buf;

  return rt11WriteBytes(file, bufw, buflen);
}

/*++
 *      r t 1 1 F S
 *
 *  Descriptor for accessing RT-11 file systems.
 *
 --*/
struct FSdef rt11FS = {
  NULL,
  "rt11",
  "rt11             PDP-11 RT-11 file system\n",
  FS_UNITVALID,
  RT11_BLOCKSIZE,
  rt11Mount,
  rt11Umount,
  rt11Size,
  rt11Newfs,
  NULL,
  rt11Info,
  rt11Dir,
  rt11OpenFileR,
  rt11OpenFileW,
  rt11FileSize,
  rt11CloseFile,
  rt11ReadFile,
  rt11WriteFile,
  rt11DeleteFile,
  NULL,                                 /* No tape support functions */
  NULL,
  NULL,
  NULL
};
