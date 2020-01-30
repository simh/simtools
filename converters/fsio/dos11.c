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
 * Support routines for handling DOS/BATCH-11 file systems under fsio
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>

#include "fsio.h"

extern uint16_t bits[], lowbits[], highbits[];
extern uint8_t zeroes[];

/*
 * Table of "set" commands
 */
static char *setCmds[] = {
  "uic",
  "ufd",
  NULL
};
#define DOS11SET_UIC    0
#define DOS11SET_UFD    1

static uint16_t bitmapAllocBlock(struct mountedFS *);
static uint16_t bitmapAllocContiguous(struct mountedFS *, uint16_t);
static int bitmapFlush(struct mountedFS *);
static int bitmapLoad(struct mountedFS *, uint16_t);
static int bitmapReleaseBlock(struct mountedFS *, uint16_t);
static int bitmapScan(struct mountedFS *, uint16_t, uint16_t, uint16_t *);
static int bitmapClrBit(struct mountedFS *, uint16_t);
static int bitmapSetBit(struct mountedFS *, uint16_t);
static int bitmapGetWord(struct mountedFS *, uint16_t, uint16_t *);

int dos11CreateFile(struct mountedFS *, struct dos11FileSpec *, struct dos11OpenFile *, unsigned long);
int dos11LookupFile(struct mountedFS *, struct dos11FileSpec *, struct dos11OpenFile *);
void dos11UpdateFile(struct mountedFS *, struct dos11OpenFile *);
uint16_t dos11XtndDirectory(struct mountedFS *);

static void dos11CloseFile(void *);

int dos11ReadBlock(struct mountedFS *, unsigned int, void *);
int dos11WriteBlock(struct mountedFS *, unsigned int, void *);

extern int args;
extern char **words;
extern int quiet;

/*++
 *      b i t m a p A l l o c B l o c k
 *
 *  Find an unsed block and allocate it. Ths scan will start at the first
 *  known location of a free block (or 0 if this is the first scan) and will
 *  will remember this location to reduce the amount of scanning required
 *  for subsequent allocations.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *
 * Outputs:
 *
 *      If the allocation is successful, the bitmap will be updated in
 *      memory and the bitmap buffer marked as dirty.
 *
 * Returns:
 *
 *      Allocated block # if successful, 0 otherwise
 *
 --*/
static uint16_t bitmapAllocBlock(
  struct mountedFS *mount
)
{
  struct DOS11data *data = &mount->dos11data;
  uint16_t map = data->bmscan / MAP_BLOCKS;
  uint16_t offset = data->bmscan % MAP_BLOCKS;

  /*
   * Iterate across all available bitmaps.
   */
  while (map < data->bitmaps) {
    uint16_t wrd = offset / 16;
    uint16_t bit = offset % 16;

    if (bitmapLoad(mount, map) == 0)
      return 0;

    /*
     * Iterate across a single bitmap.
     */
    while (wrd < MAP_LEN) {
      uint16_t val = le16toh(data->bmbuf[MAP_BMSTART + wrd]);

      if (val != 0177777) {
        data->bmscan = (map * MAP_BLOCKS) + (wrd * 16);

        while (bit < 16) {
          if ((val & bits[bit]) == 0) {
            val |= bits[bit];
            data->bmbuf[MAP_BMSTART + wrd] = htole16(val);
            data->bmdirty = 1;
            return (map * MAP_BLOCKS) + (wrd * 16) + bit;
          }
          bit++;
        }
      }
      wrd++;
      bit = 0;
    }
    map++;
    offset = 0;
  }
  return 0;
}

/*++
 *      b i t m a p A l l o c C o n t i g u o u s
 *
 *  Find a sequence of contiguous bits in the bitmaps and allocate them.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      count           - # of contiguous blocks to allocate
 *
 * Outputs:
 *
 *      If the allocation is successful, the bitmap will be updated in
 *      memory and possibly on disk. The last referenced bitmap will be
 *      marked as dirty.
 *
 * Returns:
 *
 *      First allocated block # if successful, 0 otherwise
 *
 --*/
static uint16_t bitmapAllocContiguous(
  struct mountedFS *mount,
  uint16_t count
)
{
  struct DOS11data *data = &mount->dos11data;
  uint16_t map, offset, scan = data->bmscan;
  uint16_t i;
  uint8_t first = 1;

  /*
   * Check that the request is reasonable
   */
  if (count >= data->blocks)
    return 0;

 restart:
  /*
   * Make sure that there may be sufficient space to satisfy the request.
   */
  if (((data->blocks - 1) - scan) < count)
    return 0;

  map = scan / MAP_BLOCKS;
  offset = scan % MAP_BLOCKS;

  /*
   * Iterate across all available bitmaps.
   */
  while (map < data->bitmaps) {
    uint16_t wrd = offset / 16;

    if (bitmapLoad(mount, map) == 0)
      return 0;

    /*
     * Iterate across a single bitmap.
     */
    while (wrd < MAP_LEN) {
      uint16_t val = le16toh(data->bmbuf[MAP_BMSTART + wrd]);

      if (val != 0177777) {
        if (first) {
          /*
           * Update cache of where to start scanning
           */
          data->bmscan = (map * MAP_BLOCKS) + (wrd * 16);
          first = 0;
        }

        if (count <= 16) {
          /*
           * The required blocks could fit into a single. See if that is the
           * case.
           */
          uint16_t mask = lowbits[count - 1];

          for (i = 0; i < (17 - count); i++)
            if ((val & (mask << i)) == 0) {
              /*
               * We have a fit. Allocate the space.
               */
              val |= (mask << i);
              data->bmbuf[MAP_BMSTART + wrd] = htole16(val);
              data->bmdirty = 1;
              return (map * MAP_BLOCKS) + (wrd * 16) + i;
            }
        }

        /*
         * Let's see if we can make use of the last blocks described by
         * this word.
         */
        if ((val & 0100000) == 0) {
          uint16_t base = (map * MAP_BLOCKS) + (wrd * 16);

          /*
           * See how many bits we can use
           */
          for (i = 1; i <= 16; i++)
            if ((val & highbits[i]) != 0)
              break;

          if (bitmapScan(mount, count - i, base + 16, &scan) == 0)
            goto restart;

          base = base + 16 - i;

          /*
           * Allocate the blocks
           */
          for (i = 0; i < count; i++)
            if (bitmapSetBit(mount, base + i) == 0)
              return 0;
          
          return base;
        }
      }
      wrd++;
    }
    map++;
    offset = 0;
  }
  return 0;
}

/*++
 *      b i t m a p F l u s h
 *
 *  If the currently loaded bitmap is dirty, write it back to disk.
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
 *      1 if successful, 0 otherwise
 *
 --*/
static int bitmapFlush(
  struct mountedFS *mount
)
{
  struct DOS11data *data = &mount->dos11data;

  if (data->bmdirty != 0) {
    if (dos11WriteBlock(mount, data->bmblk[data->bmindex], data->bmbuf) == 0)
      return 0;
    data->bmdirty = 0;
  }
  return 1;
}

/*++
 *      b i t m a p L o a d
 *
 *  Load the specified bitmap into the bitmap buffer. If the buffer is dirty,
 *  write the current contents of the buffer back to disk before loading the
 *  new bitmap.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      map             - logical bitmap # in the range 0 - N
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
static int bitmapLoad(
  struct mountedFS *mount,
  uint16_t map
)
{
  struct DOS11data *data = &mount->dos11data;

  if (map >= data->bitmaps) {
    ERROR("Invalid bitmap # (%d), max is %d\n", map, data->bitmaps);
    return 0;
  }

  /*
   * If the map is already loaded, nothing to do.
   */
  if (map != data->bmindex) {
    if (data->bmdirty != 0) {
      if (dos11WriteBlock(mount, data->bmblk[data->bmindex], data->bmbuf) == 0)
        return 0;
      data->bmdirty = 0;
    }

    data->bmindex = map;
    if (dos11ReadBlock(mount, data->bmblk[map], data->bmbuf) == 0)
      return 0;
  }

  return 1;
}

/*++
 *      b i t m a p R e l e a s e B l o c k
 *
 *  Release a specified block in the bitmap.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      block           - block # to be released
 *
 * Outputs:
 *
 *      The bitmap will be updated by releasing the block and the bitmap
 *      will be marked as dirty.
 *
 * Returns:
 *
 *      1 if successful, 0 otherwise
 *
 --*/
static int bitmapReleaseBlock(
  struct mountedFS *mount,
  uint16_t block
)
{
  return bitmapClrBit(mount, block);
}

/*++
 *      b i t m a p S c a n
 *
 *  Scan forward from a word boundary to check whether a sequence of blocks
 *  is free.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      count           - # of contiguous blocks to allocate
 *      start           - starting block # (always aligned on a 16 block
 *                        boundary)
 *      restart         - return restart point here on failure
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 on success, 0 otherwise
 *
 --*/
static int bitmapScan(
  struct mountedFS *mount,
  uint16_t count,
  uint16_t start,
  uint16_t *restart
)
{
  uint16_t value;

  while (count >= 16) {
    if ((bitmapGetWord(mount, start, &value) == 0) ||
        (value != 0)) {
      *restart = start;
      return 0;
    }
    start += 16;
    count -= 16;
  }

  if (count != 0)
    if ((bitmapGetWord(mount, start, &value) == 0) ||
        ((value & lowbits[count - 1]) != 0)) {
      *restart = start;
      return 0;
    }

  return 1;
}

/*++
 *      b i t m a p C l r B i t
 *
 *  Modify the bitmap by clearing the bit associated with a specific block.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      block           - block # whose associated bit is to be cleared
 *
 * Outputs:
 *
 *      The bitmap will be updated by clearing the bit and the bitmap will
 *      be marked as dirty.
 *
 * Returns:
 *
 *      1 if successful, 0 otherwise
 *
 --*/
static int bitmapClrBit(
  struct mountedFS *mount,
  uint16_t block
)
{
  struct DOS11data *data = &mount->dos11data;
  uint16_t map = block / MAP_BLOCKS;
  uint16_t offset = block % MAP_BLOCKS;
  uint16_t wrd = offset / 16;
  uint16_t bit = offset % 16;

  if (bitmapLoad(mount, map) == 0)
    return 0;

  data->bmbuf[MAP_BMSTART + wrd] =
    htole16(le16toh(data->bmbuf[MAP_BMSTART + wrd]) & ~bits[bit]);
  data->bmdirty = 1;
  return 1;
}

/*++
 *      b i t m a p S e t B i t
 *
 *  Modify the bitmap by setting the bit associated with a specific block.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      block           - block # whose associated bit is to be set
 *
 * Outputs:
 *
 *      The bitmap will be updated by setting the bit and the bitmap will
 *      be marked as dirty.
 *
 * Returns:
 *
 *      1 if successful, 0 otherwise
 *
 --*/
static int bitmapSetBit(
  struct mountedFS *mount,
  uint16_t block
)
{
  struct DOS11data *data = &mount->dos11data;
  uint16_t map = block / MAP_BLOCKS;
  uint16_t offset = block % MAP_BLOCKS;
  uint16_t wrd = offset / 16;
  uint16_t bit = offset % 16;

  if (bitmapLoad(mount, map) == 0)
    return 0;

  data->bmbuf[MAP_BMSTART + wrd] =
    htole16(le16toh(data->bmbuf[MAP_BMSTART + wrd]) | bits[bit]);
  data->bmdirty = 1;
  return 1;
}

/*++
 *      b i t m a p G e t W o r d
 *
 *  Get the bitmap word which contains the bit associated with a specified
 *  block.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      block           - block # associated with the bitmap word
 *      value           - bitmap word is returned here
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
static int bitmapGetWord(
  struct mountedFS *mount,
  uint16_t block,
  uint16_t *value
)
{
  struct DOS11data *data = &mount->dos11data;
  uint16_t map = block / MAP_BLOCKS;
  uint16_t offset = block % MAP_BLOCKS;
  uint16_t wrd = offset / 16;

  if (bitmapLoad(mount, map) == 0)
    return 0;

  *value = le16toh(data->bmbuf[MAP_BMSTART + wrd]);
  return 1;
}

/*++
 *      d o s 1 1 C r e a t e U F D
 *
 *  Create a UFD.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      group           - group number
 *      user            - user number      
 *
 * Outputs:
 *
 *      The mount specific buffer will be modified.
 *
 * Returns:
 *
 *      1 if UFD alread exists or successfully created, 0 otherwise
 *
 --*/
static int dos11CreateUFD(
  struct mountedFS *mount,
  uint8_t group,
  uint8_t user
)
{
  struct DOS11data *data = &mount->dos11data;
  uint16_t mfdblk, mfdsav, uic = (group << 8) | user;
  unsigned int i;

  if (dos11ReadBlock(mount, MFD1_BLOCK, NULL) == 0)
    return 0;

  /*
   * Search the MFD to see if the UFD already exists
   */
  mfdblk = mfdsav = le16toh(data->buf[MFD1_MFD2BLOCK]);
  do {
    if (dos11ReadBlock(mount, mfdblk, NULL) == 0)
      return 0;

    for (i = MFD2_HEADER; i < EODIR(mount, MFD2_SIZE); i += MFD2_SIZE) {
      if (le16toh(data->buf[i + MFD2_UFDUIC]) != 0)
        if (le16toh(data->buf[i + MFD2_UFDUIC] == uic))
          return 1;
    }
    mfdblk = le16toh(data->buf[MFD2_LINK]);
  } while (mfdblk != 0);

  /*
   * The UFD does not currently exist, create a new one without a data
   * block assigned. The first file creation operation will allocate a
   * data block.
   */
  mfdblk = mfdsav;
  for (;;) {
    if (dos11ReadBlock(mount, mfdblk, NULL) == 0)
      return 0;

    for (i = MFD2_HEADER; i < EODIR(mount, MFD2_SIZE); i += MFD2_SIZE) {
      if (le16toh(data->buf[i + MFD2_UFDUIC]) == 0) {
        data->buf[i + MFD2_UFDUIC] = htole16(uic);
        data->buf[i + MFD2_UFDSTART] = 0;
        data->buf[i + MFD2_UFDSIZE] = htole16(UFD_LEN);
        data->buf[i + MFD2_UFDZERO] = 0;

        dos11WriteBlock(mount, mfdblk, NULL);
        return 1;
      }
    }

    if (le16toh(data->buf[MFD2_LINK]) == 0) {
      /*
       * We have reached the end of the MFD, append a new block if available.
       */
      uint16_t buf[512], newblk;

      if ((newblk = bitmapAllocBlock(mount)) == 0) {
        ERROR("No space available to extend MFD on \"%s:\"\n", mount->name);
        return 0;
      }

      /*
       * The newly created block consists only of unused entries.
       */
      memset(buf, 0, sizeof(buf));
      if (dos11WriteBlock(mount, newblk, buf) == 0)
        return 0;

      data->buf[MFD2_LINK] = htole16(newblk);

      if (dos11WriteBlock(mount, mfdblk, NULL) == 0)
        return 0;
    }
    mfdblk = le16toh(data->buf[MFD2_LINK]);
  }
  return 0;
}

/*++
 *      d o s 1 1 D i s p l a y D i r
 *
 *  Print a directory entry on stdout.
 *
 * Inputs:
 *
 *      dir             - pointer to the UFD entry for the file
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
void dos11DisplayDir(
  uint16_t *dir,
  int full
)
{
  char temp[64], creation[16];

  r50Asc(le16toh(dir[UFD_FILENAME]), &temp[0]);
  r50Asc(le16toh(dir[UFD_FILENAME + 1]), &temp[3]);
  if (le16toh(dir[UFD_EXTENSION]) != 0) {
    temp[6] = '.';
    r50Asc(le16toh(dir[UFD_EXTENSION]), &temp[7]);
  } else {
    temp[6] = ' ';
    temp[7] = ' ';
    temp[8] = ' ';
    temp[9] = ' ';
  }

  if (full) {
    sprintf(&temp[10], "  %5u%c    %s   <%03o>",
            le16toh(dir[UFD_FILELENGTH]),
            (le16toh(dir[UFD_CREATION]) & UFD_TYPE) ? 'C' : ' ',
            dos11Date(le16toh(dir[UFD_CREATION]) & UFD_DATE, creation),
            le16toh(dir[UFD_LUP]) & UFD_PROT);
  } else temp[10] ='\0';
  puts(temp);
}

/*++
 *      d o s 1 1 C r e a t e F i l e
 *
 *  Create a new file within the DOS-11 file system. The new file will be
 *  marked as "locked" since the resources allocated to the file may be
 *  in flux. The caller must check that the file does not already exist
 *  before calling this routine.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      spec            - pointer to the file specification block
 *      file            - pointer to open file descriptor to receive results
 *      contig          - # of contiguous blocks
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
int dos11CreateFile(
  struct mountedFS *mount,
  struct dos11FileSpec *spec,
  struct dos11OpenFile *file,
  unsigned long contig
)
{
  struct DOS11data *data = &mount->dos11data;
  uint16_t mfdblk, ufdblk, ufdblk2;
  unsigned int i, j;
  struct tm tm;
  time_t now = time(NULL);
  uint16_t today;

  /*
   * Compute a suitable year for file creation date. This year will have
   * the same calendar as the current year but will be in the 20th century
   * so that DOS/BATCH-11 will be able to enter the date successfully (It is
   * not Y2K compliant!).
   */
  localtime_r(&now, &tm);
  tm.tm_year -= 28;

  today = ((tm.tm_year - 70) * 1000) + tm.tm_yday + 1;
  
  if (dos11ReadBlock(mount, MFD1_BLOCK, NULL) == 0)
    return 0;

  /*
   * Search the MFD for the specified UIC.
   */
  mfdblk = le16toh(data->buf[MFD1_MFD2BLOCK]);
  do {
    if (dos11ReadBlock(mount, mfdblk, NULL) == 0)
      return 0;

    for (i = MFD2_HEADER; i < EODIR(mount, MFD2_SIZE); i += MFD2_SIZE) {
      uint16_t uic = le16toh(data->buf[i + MFD2_UFDUIC]);
      uint16_t entrysz = le16toh(data->buf[i + MFD2_UFDSIZE]);

      if (uic != 0) {
        if (uic == ((spec->group << 8) | spec->user)) {
          ufdblk = le16toh(data->buf[i + MFD2_UFDSTART]);

          if (ufdblk == 0) {
            if ((ufdblk = dos11XtndDirectory(mount)) == 0) {
              ERROR("%s: Unable to create initial directory block\n",
                    mount->name);
              return 0;
            }

            data->buf[i + MFD2_UFDSTART] = htole16(ufdblk);
            if (dos11WriteBlock(mount, mfdblk, NULL) == 0)
              return 0;
          }
          for (;;) {
            if (dos11ReadBlock(mount, ufdblk, NULL) == 0)
              return 0;
            
            for (j = 1; j < EODIR(mount, entrysz); j += entrysz) {
              if ((le16toh(data->buf[j + UFD_FILENAME]) == 0) &&
                  (le16toh(data->buf[j + UFD_FILENAME + 1]) == 0) &&
                  (le16toh(data->buf[j + UFD_EXTENSION]) == 0)) {
                data->buf[j + UFD_FILENAME] =
                  file->name[0] = htole16(spec->name[0]);
                data->buf[j + UFD_FILENAME + 1] =
                  file->name[1] = htole16(spec->name[1]);
                data->buf[j + UFD_EXTENSION] = file->ext = htole16(spec->ext);
                data->buf[j + UFD_CREATION] =
                  file->creation =
                    htole16(today | (SWISSET('c') ? UFD_TYPECONTIGUOUS : 0));
                data->buf[j + UFD_NEXTFREEBYTE] = file->nfb = 0;
                data->buf[j + UFD_FILESTART] = file->start = 0;
                data->buf[j + UFD_FILELENGTH] = file->length = 0;
                data->buf[j + UFD_LASTBLOCKWRITTEN] = file->last = 0;
                data->buf[j + UFD_LUP] = file->lup = htole16(UFD_LOCK + 0233);

                file->ufdblk = ufdblk;
                file->ufdoffset = j;

                file-> mount = mount;

                if (SWISSET('c')) {
                  /*
                   * Allocate contiguous disk space for the file.
                   */
                  uint16_t base = bitmapAllocContiguous(mount, contig);

                  if (base == 0) {
                    ERROR("%s: Unable to allocate contiguous space\n",
                          mount->name);
                    return 0;
                  }

                  data->buf[j + UFD_FILESTART] = 
                    file->start = htole16(base);
                  data->buf[j + UFD_FILELENGTH] = 
                    file->length = htole16(contig);
                }

                /*
                 * Write the UFD block back to disk. The file will be marked
                 * as "locked" indicating that it is being written and the
                 * directory entry may not be accurate.
                 */
                if (dos11WriteBlock(mount, ufdblk, NULL) == 0)
                  return 0;

                return 1;
              }
            }

            if ((ufdblk = le16toh(data->buf[UFD_LINK])) == 0) {
              if ((ufdblk2 = dos11XtndDirectory(mount)) == 0) {
                ERROR("%s: Unable to extend UFD\n", mount->name);
                return 0;
              }

              data->buf[UFD_LINK] = htole16(ufdblk2);
              if (dos11WriteBlock(mount, ufdblk, NULL) == 0)
                return 0;
              ufdblk = ufdblk2;
            }
          }
        }
      }
    }
    mfdblk = le16toh(data->buf[MFD2_LINK]);
  } while (mfdblk != 0);

  return 0;
}

/*++
 *      d o s 1 1 L o o k u p F i l e
 *
 *  Lookup a specific file within the DOS-11 file system. This routine
 *  fills in an open file descriptor with information about the file and
 *  the user directory it resides in.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      spec            - pointer to the file specification block
 *      file            - pointer to open file descriptor to receive results
 *
 * Outputs:
 *
 *      The mount point specific buffer area will be modified.
 *
 * Returns:
 *
 *      1 if file found, 0 otherwise
 *
 --*/
int dos11LookupFile(
  struct mountedFS *mount,
  struct dos11FileSpec *spec,
  struct dos11OpenFile *file
)
{
  struct DOS11data *data = &mount->dos11data;
  unsigned int mfdblk, ufdblk, i, j;

  if (dos11ReadBlock(mount, MFD1_BLOCK, NULL) == 0)
    return 0;

  /*
   * Search the MFD for the specified UIC.
   */
  mfdblk = le16toh(data->buf[MFD1_MFD2BLOCK]);
  do {
    if (dos11ReadBlock(mount, mfdblk, NULL) == 0)
      return 0;

    for (i = MFD2_HEADER; i < EODIR(mount, MFD2_SIZE); i += MFD2_SIZE) {
      uint16_t uic = le16toh(data->buf[i + MFD2_UFDUIC]);
      uint16_t entrysz = le16toh(data->buf[i + MFD2_UFDSIZE]);

      if (uic != 0) {
        if (uic == ((spec->group << 8) | spec->user)) {
          ufdblk = le16toh(data->buf[i + MFD2_UFDSTART]);

          if (ufdblk != 0) {
            do {
              if (dos11ReadBlock(mount, ufdblk, NULL) == 0)
                return 0;

              for (j = UFD_HEADER; j < EODIR(mount, entrysz); j += entrysz) {
                if ((le16toh(data->buf[j + UFD_FILENAME]) == 0) &&
                    (le16toh(data->buf[j + UFD_FILENAME + 1]) == 0) &&
                    (le16toh(data->buf[j + UFD_EXTENSION]) == 0))
                  continue;

                if ((le16toh(data->buf[j + UFD_FILENAME]) != spec->name[0]) ||
                    (le16toh(data->buf[j + UFD_FILENAME + 1]) != spec->name[1]) ||
                    (le16toh(data->buf[j + UFD_EXTENSION]) != spec->ext))
                  continue;
                /*
                 * Save the directory entry and it's location in the open
                 * file descriptor.
                 */
                file->name[0] = data->buf[j + UFD_FILENAME];
                file->name[1] = data->buf[j + UFD_FILENAME + 1];
                file->ext = data->buf[j + UFD_EXTENSION];
                file->creation = data->buf[j + UFD_CREATION];
                file->nfb = data->buf[j + UFD_NEXTFREEBYTE];
                file->start = data->buf[j + UFD_FILESTART];
                file->length = data->buf[j + UFD_FILELENGTH];
                file->last = data->buf[j + UFD_LASTBLOCKWRITTEN];
                file->lup = data->buf[j + UFD_LUP];

                file->ufdblk = ufdblk;
                file->ufdoffset = j;

                file->mount = mount;
                return 1;
              }
              ufdblk = le16toh(data->buf[UFD_LINK]);
            } while (ufdblk != 0);
            return 0;
          }
        }
      }
    }
    mfdblk = le16toh(data->buf[MFD2_LINK]);
  } while (mfdblk != 0);

  return 0;
}

/*++
 *      d o s 1 1 U p d a t e F i l e
 *
 *  Update a DOS-11 file by writing back the UFD entry associated with the
 *  file.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      file            - pointer to open file descriptor
 *
 * Outputs:
 *
 *      The mount point specific buffer area will be modified.
 *
 * Returns:
 *
 *      None
 *
 --*/
void dos11UpdateFile(
  struct mountedFS *mount,
  struct dos11OpenFile *file
)
{
  struct DOS11data *data = &mount->dos11data;

  if (dos11ReadBlock(mount, file->ufdblk, NULL) == 0)
    return;

  /*
   * Update the directory entry.
   */
  data->buf[file->ufdoffset + UFD_FILENAME] = file->name[0];
  data->buf[file->ufdoffset + UFD_FILENAME + 1] = file->name[1];
  data->buf[file->ufdoffset + UFD_EXTENSION] = file->ext;
  data->buf[file->ufdoffset + UFD_CREATION] = file->creation;
  data->buf[file->ufdoffset + UFD_NEXTFREEBYTE] = file->nfb;
  data->buf[file->ufdoffset + UFD_FILESTART] = file->start;
  data->buf[file->ufdoffset + UFD_FILELENGTH] = file->length;
  data->buf[file->ufdoffset + UFD_LASTBLOCKWRITTEN] = file->last;
  data->buf[file->ufdoffset + UFD_LUP] = file->lup & ~htole16(UFD_LOCK);;

#ifdef DEBUG
  if ((mount->flags & FS_DEBUG) != 0) {
    if (file->ufdoffset != 1)
      dos11DisplayDir(&data->buf[file->ufdoffset - UFD_LEN], 1);
    dos11DisplayDir(&data->buf[file->ufdoffset], 1);
  }
#endif

  dos11WriteBlock(mount, file->ufdblk, NULL);
}

/*++
 *      d o s 1 1 X t n d D i r e c t o r y
 *
 *  Extend a directory by one block. The new directory block will be zeroed
 *  which indicates that all directory entries are empty and this is the last
 *  block in the directory chain.
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
 *      Block # allocated and written for the directory, 0 if failure
 *
 --*/
uint16_t dos11XtndDirectory(
  struct mountedFS *mount
)
{
  uint16_t buf[512], ufdblk;

  memset(buf, 0, sizeof(buf));

  if ((ufdblk = bitmapAllocBlock(mount)) != 0) {
    if (dos11WriteBlock(mount, ufdblk, buf) == 0)
      return 0;

    if (bitmapFlush(mount) == 0)
      return 0;
  }
  return ufdblk;
}

/*++
 *      d o s 1 1 P a r s e F i l e s p e c
 *
 *  Parse a character string representing a DOS-11 file specification.
 *
 * Inputs:
 *
 *      ptr             - pointer to the file specification string
 *      spec            - pointer to the file specification block
 *      user            - default user number
 *      group           - default group number
 *      wildcard        - wildcard processing options:
 *                        0 (DOS11_M_NONE)      - wildcards not allowed
 *                        1 (DOS11_M_ALLOW)     - wildcards allowed
 *                        2 (DOS11_M_NONAME)    - wildcards allowed
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
#define P_GROUP         3
#define P_USER          4

int dos11ParseFilespec(
  char *ptr,
  struct dos11FileSpec *spec,
  unsigned char user,
  unsigned char group,
  int wildcard
)
{
  char term[] = { '\0', '.', '[', ',', ']' };
  char flags[] =
    { 0, DOS11_WC_NAME, DOS11_WC_EXT, DOS11_WC_GROUP, DOS11_WC_USER };
  int state = P_NAME;
  unsigned int uic, i;
  char filename[6], ext[3];

  memset(spec, 0, sizeof(struct dos11FileSpec));
  spec->user = user;
  spec->group = group;

  memset(&filename, ' ', sizeof(filename));
  memset(&ext, ' ', sizeof(ext));

  if (wildcard == DOS11_M_NONAME)
    if ((*ptr == '\0') || (*ptr == '['))
      spec->flags = DOS11_WC_NAME | DOS11_WC_EXT;

  while (state != P_DONE) {
    if (wildcard) {
      if (*ptr == '*') {
        spec->flags |= flags[state];
        ptr++;
        if (*ptr == '\0')
          state = P_DONE;
        else if (*ptr == term[state]) {
          if (state == P_USER)
            state = P_DONE;
          else state++;
        } else return 0;
        ptr++;
        continue;
      }
    }

    i = 0;

    switch (state) {
      case P_NAME:
        while ((*ptr != '\0') &&
               (*ptr != '.') &&
               (strchr(rad50, toupper(*ptr)) != NULL) &&
               (i < sizeof(filename)))
          filename[i++] = toupper(*ptr++);

        switch (*ptr++) {
          case '\0':
            state = P_DONE;
            break;

          case '.':
            state = P_EXT;
            break;

          case '[':
            state = P_GROUP;
            break;

          default:
            return 0;
        }
        break;

      case P_EXT:
        while ((*ptr != '\0') &&
               (strchr(rad50, toupper(*ptr)) != NULL) &&
               (i < sizeof(ext)))
          ext[i++] = toupper(*ptr++);
        
        switch (*ptr++) {
          case '\0':
            state = P_DONE;
            break;

          case '[':
            state = P_GROUP;
            break;

          default:
            return 0;
        }
        break;

      case P_GROUP:
        uic = 0;
        while ((strchr("01234567", *ptr) != NULL) && (i < 3)) {
          uic = (uic << 3) | (*ptr++ - '0');
          i++;
        }
        if ((uic == 0) || (uic > 0377))
          return 0;
        spec->group = uic & 0377;

        if (*ptr++ != ',')
          return 0;

        state = P_USER;
        break;

      case P_USER:
        uic = 0;
        while ((strchr("01234567", *ptr) != NULL) && (i < 3)) {
          uic = (uic << 3) | (*ptr++ - '0');
          i++;
        }
        if ((uic == 0) || (uic > 0377))
          return 0;
        spec->user = uic & 0377;

        if (*ptr++ != ']')
          return 0;

        state = P_DONE;
        break;
    }
  }

  spec->name[0] = ascR50(&filename[0]);
  spec->name[1] = ascR50(&filename[3]);
  spec->ext = ascR50(&ext[0]);
  return 1;
}

/*++
 *      d o s 1 1 R e a d B l o c k
 *
 *  Read a block from a DOS-11 file system.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      block           - logical block # in the range 0 - N
 *      buf             - buffer to receive data, if NULL use the mount
 *                        point specific buffer
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
int dos11ReadBlock(
  struct mountedFS *mount,
  unsigned int block,
  void *buf
)
{
  struct DOS11data *data = &mount->dos11data;
  void *buffer = buf == NULL ? data->buf : buf;
  int status;

  if (block >= data->blocks) {
    ERROR("Attempt to read block (%u) outside file system \"%s\"\n",
          block, mount->name);
    return 0;
  }

#ifdef DEBUG
  if ((mount->flags & FS_DEBUG) != 0)
    fprintf(DEBUGout, ">> %s: (dos11) Reading logical block %o\n",
            mount->name, block);
#endif

  status = FSioReadBlock(mount, block, buffer);

  if (status == 0)
    ERROR("I/O error on \"%s\"\n", mount->name);

  return status;
}

/*++
 *      d o s 1 1 W r i t e B l o c k
 *
 *  Write a block to a DOS-11 file system.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      block           - logical block # in the range 0 - N
 *      buf             - buffer to containing data, if NULL use the mount
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
int dos11WriteBlock(
  struct mountedFS *mount,
  unsigned int block,
  void *buf
)
{
  struct DOS11data *data = &mount->dos11data;
  void *buffer = buf == NULL ? data->buf : buf;
  int status;

  if (block >= data->blocks) {
    ERROR("Attempt to write block (%u) outside file system \"%s\"\n",
          block, mount->name);
    return 0;
  }

#ifdef DEBUG
  if ((mount->flags & FS_DEBUG) != 0)
    fprintf(DEBUGout, ">> %s: (dos11) Writing logical block %o\n",
            mount->name, block);
#endif

  status = FSioWriteBlock(mount, block, buffer);

  if (status == 0)
    ERROR("I/O error on \"%s\"\n", mount->name);

  return status;
}

/*++
 *      d o s 1 1 R e a d B y t e s
 *
 *  Read a sequence of bytes from an open file. The file may be either
 *  "Linked" or "Contiguous".
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
int dos11ReadBytes(
  struct dos11OpenFile *file,
  char *buf,
  int len
)
{
  int count = 0;

  if (file->current == 0) {
    file->current = le16toh(file->start);

    if (dos11ReadBlock(file->mount, file->current, file->buffer) == 0)
      return 0;

    file->nab = (le16toh(file->creation) & UFD_TYPE) == 0 ? 2 : 0;
    file->eob = file->current == le16toh(file->last) ?
      le16toh(file->nfb) : file->mount->blocksz;
  }

  while (len) {
    if (file->nab == file->eob) {
      if (file->current == le16toh(file->last))
        break;

      if ((le16toh(file->creation) & UFD_TYPE) == 0)
        file->current = le16toh(*((uint16_t *)(file->buffer)));
      else file->current++;

      if (dos11ReadBlock(file->mount, file->current, file->buffer) == 0)
        return 0;

      file->nab = (le16toh(file->creation) & UFD_TYPE) == 0 ? 2 : 0;
      file->eob = file->current == le16toh(file->last) ?
        le16toh(file->nfb) : file->mount->blocksz;
      continue;
    }
    *buf++ = file->buffer[file->nab++];
    len--;
    count++;
  }
  return count;
}

/*++
 *      d o s 1 1 W r i t e B y t e s
 *
 *  Write a sequence of bytes to an open file. The file may be either
 *  "Linked" or "Contiguous". Linked files will be automatically extended
 *  as new data is written.
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
int dos11WriteBytes(
  struct dos11OpenFile *file,
  char *buf,
  int len
)
{
  int count = 0;
  uint16_t next;

  if (file->current == 0) {
    file->eob = file->mount->blocksz;

    if ((le16toh(file->creation) & UFD_TYPE) == 0) {
      /*
       * Linked file - allocate initial block
       */
      if ((next = bitmapAllocBlock(file->mount)) == 0) {
        ERROR("Free disk space exhausted\n");
        return 0;
      }
      file->current = next;
      file->start = file->last = htole16(next);
      file->length = htole16(1);
      *((uint16_t *)(file->buffer)) = 0;
      file->nab = 2;
    } else {
      file->current = le16toh(file->start);
      file->last = file->start;
      file->nab = 0;
    }
  }

  while (len) {
    if (file->nab == file->eob) {
      if ((le16toh(file->creation) & UFD_TYPE) == 0) {
        /*
         * Linked file - extend it by 1 block
         */
        if ((next = bitmapAllocBlock(file->mount)) == 0) {
          ERROR("Free disk space exhausted extending file\n");
          return count;
        }
        *((uint16_t *)(file->buffer)) = htole16(next);

        if (dos11WriteBlock(file->mount, file->current, file->buffer) == 0)
          return 0;

        file->current = next;
        file->last = htole16(next);
        file->length = htole16(le16toh(file->length) + 1);
        file->nab = 2;
      } else {
        if (dos11WriteBlock(file->mount, file->current, file->buffer) == 0)
          return 0;

        if (++file->current == (le16toh(file->last) + le16toh(file->length))) {
          ERROR("Contiguous disk space allocation exceeded\n");
          return 0;
        }
        file->nab = 0;
        file->last = htole16(file->current);
      }
    }

    file->buffer[file->nab++] = *buf++;
    len--;
    count++;
  }
  return count;
}

/*++
 *      d o s 1 1 M o u n t
 *
 *  Verify that the open container file is a DOS-11 file system. We check that
 *  we can read all MFD entries and the UFD entries have the correct size
 *  (9 words). We also verify that we can read all bitmaps and they have the
 *  correct size (60 words).
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
 *      1 if a valid DOS-11 file system, 0 otherwise
 *
 --*/
static int dos11Mount(
  struct mountedFS *mount
)
{
  struct DOS11data *data = &mount->dos11data;
  struct stat stat;
  uint16_t mfdblk, mapblk, interleave;
  unsigned int i, freeblocks = 0;

  if (fstat(fileno(mount->container), &stat) == 0) {
    if (stat.st_blocks < DISKSIZE_RK05)
      mount->blocksz = BLOCKSIZE_RF11 * 2;
    if (stat.st_blocks > DISKSIZE_RK05)
      mount->blocksz = BLOCKSIZE_RP03 * 2;

    data->blocks = stat.st_size / mount->blocksz;

    /*
     * Build a list of the bitmap blocks.
     */
    if (dos11ReadBlock(mount, MFD1_BLOCK, NULL) == 0)
      return 0;

    mfdblk = le16toh(data->buf[MFD1_MFD2BLOCK]);
    interleave = le16toh(data->buf[MFD1_INTERLEAVE]);

    data->bitmaps = 0;
    mapblk = le16toh(data->buf[MFD1_BMSTART]);

    do {
      if (dos11ReadBlock(mount, mapblk, NULL) == 0)
        return 0;

      if (le16toh(data->buf[MAP_WORDS]) != MAP_LEN) {
        ERROR("mount: wrong bitmap size (%d) for bitmap %d\n",
              le16toh(data->buf[MAP_WORDS]),
              data->bitmaps + 1);
        return 0;
      }

      /*
       * Compute the # of free blocks in the bitmap.
       */
      for (i = 0; i < MAP_LEN; i++) {
        uint16_t bmentry = le16toh(data->buf[i + MAP_BMSTART]);

        if (bmentry != 0177777) {
          freeblocks += zeroes[(bmentry >> 8) & 0377];
          freeblocks += zeroes[bmentry & 0377];
        }
      }
      data->bmblk[data->bitmaps++] = mapblk;

      mapblk = le16toh(data->buf[MAP_LINK]);
    } while (mapblk != 0);

    do {
      if (dos11ReadBlock(mount, mfdblk, NULL) == 0)
        return 0;

      for (i = MFD2_HEADER; i < EODIR(mount, MFD2_SIZE); i += MFD2_SIZE)
        if (le16toh(data->buf[i + MFD2_UFDUIC]) != 0)
          if (le16toh(data->buf[i + MFD2_UFDSIZE]) != UFD_LEN) {
            ERROR("mount: wrong directory size (%d) for [%3o,%3o]\n",
                  le16toh(data->buf[i + MFD2_UFDSIZE]),
                  (le16toh(data->buf[i + MFD2_UFDUIC]) >> 8) & 0377,
                  le16toh(data->buf[i + MFD2_UFDUIC]) & 0377);
            return 0;
          }
      mfdblk = le16toh(data->buf[MFD2_LINK]);
    } while (mfdblk != 0);

    /*
     * Preload bitmap 0
     */
    data->bmindex = 0;
    data->bmscan = 0;
    data->bmdirty = 0;

    if (dos11ReadBlock(mount, data->bmblk[0], data->bmbuf) == 0)
      return 0;

    if (!quiet) {
      printf("%s: successfully mounted\n", mount->name);
      printf("Total blocks: %d, Free blocks: %d, Interleave: %d\n",
             data->blocks, freeblocks, interleave);
    }

    /*
     * Set up default parameters
     */
    data->group = 1;
    data->user = 1;
    return 1;
  }
  return 0;
}

/*++
 *      d o s 1 1 U m o u n t
 *
 *  Unmount the DOS-11 file system, releasing any storage allocated.
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
static void dos11Umount(
  struct mountedFS *UNUSED(mount)
)
{
}

/*++
 *      d o s 1 1 S i z e
 *
 *  Return the size of a DOS-11 container file (RK05).
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
 *      Default size of the container file in bytes (RK05).
 *
 --*/
static size_t dos11Size(void)
{
  return DISKSIZE_RK05 * BLOCKSIZE_RK11;
}

/*++
 *      d o s 1 1 N e w f s
 *
 *  Create an empty DOS11 file system.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *                        (not in the mounted file system list)
 *      size            - the size (in blocks) of the file system
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
static int dos11Newfs(
  struct mountedFS *mount,
  size_t size
)
{
  struct DOS11data *data = &mount->dos11data;
  int i;

#define MFD2_BLOCK      2
#define MAP_BLOCK       3

  memset(data, 0, sizeof(*data));

  data->blocks = size / BLOCKSIZE_RK11;
  data->bitmaps = 5;
  data->bmblk[0] = MAP_BLOCK;
  data->bmblk[1] = MAP_BLOCK + 1;
  data->bmblk[2] = MAP_BLOCK + 2;
  data->bmblk[3] = MAP_BLOCK + 3;
  data->bmblk[4] = MAP_BLOCK + 4;
  data->bmindex = 0177777;

  /*
   * Build and write MFD Block #1:
   *
   *   - second MFD block is at block 2
   *   - 5 bitmaps starting at block (filesys->blocks - 5).
   */
  memset(data->buf, 0, mount->blocksz);
  data->buf[MFD1_MFD2BLOCK] = htole16(MFD2_BLOCK);
  data->buf[MFD1_INTERLEAVE] = htole16(1);
  data->buf[MFD1_BMSTART] = htole16(data->bmblk[0]);
  data->buf[MFD1_BMSTART + 1] = htole16(data->bmblk[1]);
  data->buf[MFD1_BMSTART + 2] = htole16(data->bmblk[2]);
  data->buf[MFD1_BMSTART + 3] = htole16(data->bmblk[3]);
  data->buf[MFD1_BMSTART + 4] = htole16(data->bmblk[4]);
  data->buf[MFD1_BMSTART + 5] = htole16(0);

  if (dos11WriteBlock(mount, MFD1_BLOCK, NULL) == 0)
    return 0;

  /*
   * Build and write MFD Block #2:
   *
   *   - no UFDs present
   */
  memset(data->buf, 0, mount->blocksz);

  if (dos11WriteBlock(mount, MFD2_BLOCK, NULL) == 0)
    return 0;

  /*
   * Build and write 5 bitmap blocks.
   */
  memset(data->buf, 0, mount->blocksz);

  for (i = 1; i < 6; i++) {
    data->buf[MAP_LINK] =
      i == 5 ? 0 : htole16(data->bmblk[i]);
    data->buf[MAP_MAP] = htole16(i);
    data->buf[MAP_WORDS] = htole16(MAP_LEN);
    data->buf[MAP_FIRST] = htole16(MAP_BLOCK);

    if (dos11WriteBlock(mount, data->bmblk[i - 1], NULL) == 0)
      return 0;
  }

  /*
   * Reserve used blocks in the bitmap(s).
   */
  if (bitmapSetBit(mount, BOOT_BLOCK) == 0)
    return 0;
  if (bitmapSetBit(mount, MFD1_BLOCK) == 0)
    return 0;
  if (bitmapSetBit(mount, MFD2_BLOCK) == 0)
    return 0;

  for (i = 1; i < 6; i++)
    if (bitmapSetBit(mount, MAP_BLOCK + i - 1) == 0)
      return 0;

  /*
   * Reserve all blocks past the end of the disk.
   */
  for (i = 4800; i < (5 * MAP_BLOCKS); i++)
    if (bitmapSetBit(mount, i) == 0)
    return 0;

  return bitmapFlush(mount);
}

/*++
 *      d o s 1 1 S e t
 *
 *  Set mount point specific values.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - device unit number (unused)
 *      present         - device unit number present (unused)
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
static void dos11Set(
  struct mountedFS *mount,
  uint8_t UNUSED(unit),
  uint8_t UNUSED(present)
)
{
  struct DOS11data *data = &mount->dos11data;
  int idx = 0;
  uint8_t user, group;

  while (setCmds[idx] != NULL) {
    if (strcmp(words[1], setCmds[idx]) == 0) {
      switch (idx) {
        case DOS11SET_UIC:
          if (args == 3) {
            if (sscanf(words[2], "[%hho,%hho]", &group, &user) == 2) {
              data->group = group;
              data->user = user;
            } else fprintf(stderr,
                           "dos11: UIC syntax error \"%s\"\n", words[2]);
          } else fprintf(stderr, "dos11: Invalid syntax for \"set uic\"\n");
          return;

        case DOS11SET_UFD:
          if (args == 3) {
            if (sscanf(words[2], "[%hho,%hho]", &group, &user) == 2) {
              if (dos11CreateUFD(mount, group, user) != 0) {
                data->group = group;
                data->user = user;
              }
            } else fprintf(stderr,
                           "dos11: UIC syntax error \"%s\"\n", words[2]);
          } else fprintf(stderr, "dos11: Invalid syntax for \"set ufd\"\n");
          return;

        default:
          fprintf(stderr, "dos11: \"%s\" not implemented\n", words[1]);
          return;
      }
    }
    idx++;
  }
  fprintf(stderr, "dos11: Unknown set command \"%s\"\n", words[1]);
}

/*++
 *      d o s 1 1 I n f o
 *
 *  Display infomation about the internal structure of the DOS-11 file system.
 *  This functions generates a display similar to the output provided by the
 *  LIST operation of DOS/BATCH-11 V09.20C
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - device unit number (unused)
 *      present         - device unit number present (unused)
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
static void dos11Info(
  struct mountedFS *mount,
  uint8_t UNUSED(unit),
  uint8_t UNUSED(present)
)
{
  struct DOS11data *data = &mount->dos11data;
  struct tm tm;
  char datetime[32], temp[32];
  time_t now = time(NULL);
  unsigned int mfd2blk, mfdblk, ufdblk, mapblk, i, j;
  uint16_t buf[512];

  localtime_r(&now, &tm);
  strftime(datetime, sizeof(datetime), "%d-%b-%C at %I:%M:%S", &tm);

  printf("* * * * * * Listing of MFD for %s * * * * * * on %s\n",
         mount->name, datetime);
  printf("      UIC  First UFD Block  UFD Entry Size\n\n");

  if (dos11ReadBlock(mount, MFD1_BLOCK, NULL) == 0)
    return;

  /*
   * Get some useful block addresses
   */
  mfd2blk = le16toh(data->buf[MFD1_MFD2BLOCK]);
  mapblk = le16toh(data->buf[MFD1_BMSTART]);

  /*
   * Display the MFD entries
   */
  mfdblk = mfd2blk;
  do {
    if (dos11ReadBlock(mount, mfdblk, NULL) == 0)
      return;

    for (i = MFD2_HEADER; i < EODIR(mount, MFD2_SIZE); i += MFD2_SIZE)
      if (le16toh(data->buf[i + MFD2_UFDUIC]) != 0) {
        printf("[%3o,%3o]            %5o          %5d.\n",
               (le16toh(data->buf[i + MFD2_UFDUIC]) >> 8) & 0377,
               le16toh(data->buf[i + MFD2_UFDUIC]) & 0377,
               le16toh(data->buf[i + MFD2_UFDSTART]),
               le16toh(data->buf[i + MFD2_UFDSIZE]));
      }
    mfdblk = le16toh(data->buf[MFD2_LINK]);
  } while (mfdblk != 0);

  printf("\n");

  /*
   * Display the individual UFD entries
   */
  mfdblk = mfd2blk;
  do {
    if (dos11ReadBlock(mount, mfdblk, NULL) == 0)
      return;

    for (i = MFD2_HEADER; i < EODIR(mount, MFD2_SIZE); i += MFD2_SIZE)
      if (le16toh(data->buf[i + MFD2_UFDUIC]) != 0) {
        unsigned int entrysz = le16toh(data->buf[i + MFD2_UFDSIZE]);

        printf("* * * Listing of [%3o,%3o] User Directory * * *\n\n",
               (le16toh(data->buf[i + MFD2_UFDUIC]) >> 8) & 0377,
               le16toh(data->buf[i + MFD2_UFDUIC]) & 0377);
        printf("  File Ext         Date  Type  Usage  Lock   Start  Length      End  Prot    EBP\n\n");

        if ((ufdblk = le16toh(data->buf[i + MFD2_UFDSTART])) != 0) {
          do {
            if (dos11ReadBlock(mount, ufdblk, buf) == 0)
              return;

            for (j = UFD_HEADER; j < EODIR(mount, entrysz); j += entrysz) {
              /*
               * Skip over deleted files
               */
              if ((le16toh(buf[j + UFD_FILENAME]) == 0) &&
                  (le16toh(buf[j + UFD_FILENAME + 1]) == 0) &&
                  (le16toh(buf[j + UFD_EXTENSION]) == 0))
                continue;

              r50Asc(le16toh(buf[j + UFD_FILENAME]), &temp[0]);
              r50Asc(le16toh(buf[j + UFD_FILENAME + 1]), &temp[3]);
              temp[6] = '.';
              r50Asc(le16toh(buf[j + UFD_EXTENSION]), &temp[7]);
              temp[10] = ' ';
              temp[11] = ' ';
              dos11Date(le16toh(buf[j + UFD_CREATION]) & UFD_DATE, &temp[12]);
              temp[25] = '\0';

              printf("%s     %c    %3o     %c  %6o  %5u.   %6o   %3o %6o\n",
                     temp,
                     (le16toh(buf[j + UFD_CREATION]) & UFD_TYPE) ? 'C' : 'L',
                     (le16toh(buf[j + UFD_LUP]) & UFD_USAGE) >> 8,
                     (le16toh(buf[j + UFD_LUP]) & UFD_LOCK) ? '1' : '0',
                     le16toh(buf[j + UFD_FILESTART]),
                     le16toh(buf[j + UFD_FILELENGTH]),
                     le16toh(buf[j + UFD_LASTBLOCKWRITTEN]),
                     le16toh(buf[j + UFD_LUP]) & UFD_PROT,
                     le16toh(buf[j + UFD_NEXTFREEBYTE]));
            }
            ufdblk = le16toh(buf[UFD_LINK]);
          } while (ufdblk != 0);
          printf("\n");
        }
      }
    mfdblk = le16toh(data->buf[MFD2_LINK]);
  } while (mfdblk != 0);

  /*
   * Display bitmap information
   */
  printf("* * * * * * Map Verification * * * * * *\n\n");
  do {
    if (dos11ReadBlock(mount, mapblk, buf) == 0)
      return;

    printf("* * * * * * Map Header Information * * * * * *\n");
    printf("Link = %6o\n", le16toh(buf[MAP_LINK]));
    printf("Map Number = %2u.\n", le16toh(buf[MAP_MAP]));
    printf("Words In Map = %3u.\n", le16toh(buf[MAP_WORDS]));
    printf("Link To First Map = %6o\n\n", le16toh(buf[MAP_FIRST]));

    for (i = 0; i < le16toh(buf[MAP_WORDS]); i++)
      printf(" %6o%c",
             le16toh(buf[MAP_BMSTART + i]),
             ((i + 1) % 8) == 0 ? '\n' : ' ');
    printf("\n\n");

    mapblk = le16toh(buf[MAP_LINK]);
  } while (mapblk != 0);
}

/*++
 *      d o s 1 1 D i r
 *
 *  Produce a full or brief directory listing.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - device unit number (unused)
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
static void dos11Dir(
  struct mountedFS *mount,
  uint8_t UNUSED(unit),
  char *fname
)
{
  struct DOS11data *data = &mount->dos11data;
  uint8_t user = data->user;
  uint8_t group = data->group;
  struct dos11FileSpec spec;
  unsigned int mfdblk, ufdblk, i, j;
  uint16_t buf[512];
  uint8_t found = 0;

  if (dos11ParseFilespec(fname, &spec, user, group, DOS11_M_NONAME) == 0) {
    fprintf(stderr, "dir: syntax error in file spec \"%s\"\n", fname);
    return;
  }

  if (dos11ReadBlock(mount, MFD1_BLOCK, NULL) == 0)
    return;

  /*
   * Search the MFD for matching UIC entries.
   */
  mfdblk = le16toh(data->buf[MFD1_MFD2BLOCK]);
  do {
    if (dos11ReadBlock(mount, mfdblk, NULL) == 0)
      return;

    for (i = MFD2_HEADER; i < EODIR(mount, MFD2_SIZE); i += MFD2_SIZE) {
      uint16_t uic = le16toh(data->buf[i + MFD2_UFDUIC]);
      uint16_t entrysz = le16toh(data->buf[i + MFD2_UFDSIZE]);

      if (uic == 0)
        continue;

      if ((spec.flags & DOS11_WC_GROUP) == 0)
        if (spec.group != ((uic >> 8) & 0377))
          continue;

      if ((spec.flags & DOS11_WC_USER) == 0)
        if (spec.user != (uic & 0377))
          continue;

      found = 1;

      if ((ufdblk = le16toh(data->buf[i + MFD2_UFDSTART])) != 0) {
        unsigned int header = 0;

        do {
          if (dos11ReadBlock(mount, ufdblk, buf) == 0)
            return;

          for (j = UFD_HEADER; j < EODIR(mount, entrysz); j += entrysz) {
            if ((le16toh(buf[j + UFD_FILENAME]) == 0) &&
                (le16toh(buf[j + UFD_FILENAME + 1]) == 0) &&
                (le16toh(buf[j + UFD_EXTENSION]) == 0))
              continue;

            if ((spec.flags & DOS11_WC_NAME) == 0)
              if ((le16toh(buf[j + UFD_FILENAME]) != spec.name[0]) ||
                  (le16toh(buf[j + UFD_FILENAME + 1]) != spec.name[1]))
                continue;

            if ((spec.flags & DOS11_WC_EXT) == 0)
              if (le16toh(buf[j + UFD_EXTENSION]) != spec.ext)
                  continue;

            if (!header) {
              printf("%s:[%u, %u]\n\n",
                     mount->name, (uic >> 8) & 0377, uic & 0377);
              header = 1;
            }

            dos11DisplayDir(&buf[j], SWISSET('f'));
          }
          ufdblk = le16toh(buf[UFD_LINK]);
        } while (ufdblk != 0);

        if (header)
          printf("\n");
      }
    }
    mfdblk = le16toh(data->buf[MFD2_LINK]);
  } while (mfdblk != 0);

  if (!found && ((spec.flags & (DOS11_WC_GROUP | DOS11_WC_USER)) == 0))
    ERROR("dir: Directory [%o,%o] not found\n", spec.group, spec.user);
}

/*++
 *      d o s 1 1 O p e n F i l e R
 *
 *  Open a DOS-11 file for reading.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - device unit number (unused)
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
static void *dos11OpenFileR(
  struct mountedFS *mount,
  uint8_t UNUSED(unit),
  char *fname
)
{
  struct DOS11data *data = &mount->dos11data;
  struct dos11OpenFile *file;
  struct dos11FileSpec spec;
  uint8_t user = data->user;
  uint8_t group = data->group;

  if (dos11ParseFilespec(fname, &spec, user, group, DOS11_M_NONE) == 0) {
    fprintf(stderr, "Failed to parse filename \"%s\"\n", fname);
    return NULL;
  }

  if ((file = malloc(sizeof(struct dos11OpenFile))) != NULL) {
    memset(file, 0, sizeof(struct dos11OpenFile));

    if (dos11LookupFile(mount, &spec, file) != 0) {
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
 *      d o s 1 1 O p e n F i l e W
 *
 *  Open a DOS-11 file for writing.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - device unit number (unused)
 *      fname           - pointer to filename string
 *      size            - estimated file size (in bytes)
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
static void *dos11OpenFileW(
  struct mountedFS *mount,
  uint8_t UNUSED(unit),
  char *fname,
  off_t size
)
{
  struct DOS11data *data = &mount->dos11data;
  struct dos11OpenFile *file;
  struct dos11FileSpec spec;
  unsigned long contig = (size + mount->blocksz - 1) / mount->blocksz;
  uint8_t user = data->user;
  uint8_t group = data->group;

  if (SWISSET('c') && (contig == 0)) {
      fprintf(stderr,"DOS-11 contiguous files must be at least 1 block\n");
      return NULL;
  }

  if (dos11ParseFilespec(fname, &spec, user, group, DOS11_M_NONE) == 0) {
    fprintf(stderr, "Failed to parse filename \"%s\"\n", fname);
    return NULL;
  }

  if ((file = malloc(sizeof(struct dos11OpenFile))) != NULL) {
    memset(file, 0, sizeof(struct dos11OpenFile));

    if (dos11LookupFile(mount, &spec, file) == 0) {
      /*
       * Allocate local bufferspace for the file.
       */
      if ((file->buffer = malloc(mount->blocksz)) != NULL) {
        if (dos11CreateFile(mount, &spec, file, contig) == 0) {
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
  } else fprintf(stderr, "Memory allocation failure\n");
  return file;
}

/*++
 *      d o s 1 1 F i l e S i z e
 *
 *  Return an estimate of the size of an open file. This routine bases the
 *  size on the number of blocks allocated to the file. The size of "Linked"
 *  files will be over-reported by 2 bytes for each block allocated.
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
 *      Estimated size of the file
 *
 --*/
static off_t dos11FileSize(
  void *filep
)
{
  struct dos11OpenFile *file = filep;
  struct mountedFS *mount = file->mount;

  return le16toh(file->length) * mount->blocksz;
}

/*++
 *      d o s 1 1 D e l e t e F i l e
 *
 *  Delete a file which has been opened via dos11LookupFile().
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
static void dos11DeleteFile(
  void *filep,
  char *UNUSED(fname)
)
{
  struct dos11OpenFile *file = filep;
  struct mountedFS *mount = file->mount;
  struct DOS11data *data = &mount->dos11data;
  uint16_t i, block;

  file->name[0] = 0;
  file->name[1] = 0;
  file->ext = 0;

  dos11UpdateFile(mount, file);

  /*
   * The file has been removed from its UFD, release all disk blocks
   * allocated to the file.
   */
  block = le16toh(file->start);

  if ((le16toh(file->creation) & UFD_TYPE) != 0) {
    /*
     * Contiguous file
     */
    for (i = 0; i < le16toh(file->length); i++)
      if (bitmapReleaseBlock(mount, block + i) == 0)
        return;
  } else {
    /*
     * Linked file
     */
    while (block != 0) {
      if (dos11ReadBlock(mount, block, NULL) == 0)
        return;
      if (bitmapReleaseBlock(mount, block) == 0)
        return;

      block = le16toh(data->buf[FILE_LINK]);
    }
  }
  /*
   * Make sure the bitmaps on disk are up to date.
   */
  bitmapFlush(mount);

  dos11CloseFile(file);
}

/*++
 *      d o s 1 1 C l o s e F i l e
 *
 *  Close an open DOS-11 file.
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
static void dos11CloseFile(
  void *filep
)
{
  struct dos11OpenFile *file = filep;
  struct mountedFS *mount = file->mount;

  if (file->mode == M_WR) {
    uint16_t start = (le16toh(file->creation) & UFD_TYPE) == 0 ? 2 : 0;

    /*
     * Flush any partial buffer, pending bitmap updates and update the
     * directory entry.
     */
    if (((le16toh(file->creation) & UFD_TYPE) == 0) ||
        (file->current == le16toh(file->last)))
      file->nfb = htole16(file->nab);
    else file->nfb = htole16(mount->blocksz);

    if (file->nab != start)
      dos11WriteBlock(mount, file->current, file->buffer);
    bitmapFlush(mount);
    dos11UpdateFile(mount, file);
  }

  if (file != NULL) {
    if (file->buffer != NULL)
      free(file->buffer);
    free(file);
  }
}

/*++
 *      d o s 1 1 R e a d F i l e
 *
 *  Read data from a DOS-11 file to a supplied buffer.
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
static size_t dos11ReadFile(
  void *filep,
  void *buf,
  size_t buflen
)
{
  struct dos11OpenFile *file = filep;
  char *bufr = buf;

  if (SWISSET('a')) {
    char ch;
    size_t count = 0;

    /*
     * Read a full or partial line from the open file.
     */
    while ((buflen != 0) && (dos11ReadBytes(file, &ch, 1) == 1)) {
      ch &= 0177;
      bufr[count++] = ch;
      buflen--;
      if (ch == '\n')
        break;
    }
    return count;
  }

  return dos11ReadBytes(file, bufr, buflen);
}

/*++
 *      d o s 1 1 W r i t e F i l e
 *
 *  Write data to a DOS-11 file from a supplied buffer.
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
 *      # of bytes of data written, 0 means error
 *
 --*/
size_t dos11WriteFile(
  void *filep,
  void *buf,
  size_t buflen
)
{
  struct dos11OpenFile *file = filep;
  char *bufw = buf;

  if (SWISSET('a')) {
    size_t count = 0;

    /*
     * Write a line, terminated by <CRLF>, to the open file.
     */
    while (buflen != 0) {
      char ch;

      ch = bufw[count++] & 0177;
      buflen--;
      if (dos11WriteBytes(file, &ch, 1) == 0)
        break;
    }
    return count;
  }
  return dos11WriteBytes(file, bufw, buflen);
}

/*++
 *      d o s 1 1 F S
 *
 *  Descriptor for accessing DOS-11 file systems.
 *
 --*/
struct FSdef dos11FS = {
  NULL,
  "dos11",
  "dos11            PDP-11 DOS/BATCH-11 file system (RF11, RK05 or RP03 disks)\n",
  0,
  BLOCKSIZE_RK11 * 2,                   /* Default for RK05 */
  dos11Mount,
  dos11Umount,
  dos11Size,
  dos11Newfs,
  dos11Set,
  dos11Info,
  dos11Dir,
  dos11OpenFileR,
  dos11OpenFileW,
  dos11FileSize,
  dos11CloseFile,
  dos11ReadFile,
  dos11WriteFile,
  dos11DeleteFile,
  NULL,                                 /* No tape support functions */
  NULL,
  NULL,
  NULL
};
