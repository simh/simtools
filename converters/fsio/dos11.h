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
#ifndef __DOS11_H__
#define __DOS11_H__

/*
 * General disk layout:
 *
 * Block
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0     |                  Reserved for Bootstrap                       |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 1     |                       MFD Block #1                            |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 2     |                       UFD Block #1                            |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * ...   |                                                               |
 *       |                     User linked files                         |
 *       |                             &                                 |
 *       |                     other UFD blocks                          |
 *       |                                                               |
 *       |--                                                           --|
 *       |                  User contiguous files                        |
 *       |                                                               |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * X-n   |                       MFD Block #2                            |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * X-n-1 |                      Bitmap Block #1                          |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * ...   |                                                               |
 *       |                                                               |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * X     |                      Bitmap Block #n                          |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *
 * MFD Block 1:
 *
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                   Block # of MFD Block 2                      |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                      Interleave factor                        |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                    Bitmap start block #                       |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                      Bitmap #1 block #                        |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                      Bitmap #2 block #                        |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                                                               |
 *       |                              ...                              |
 *       |                                                               |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                      Bitmap #N block #                        |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                               0                               |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                                                               |
 *
 * MFD Block 2 - N:
 *
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                  Link to next MFD block or 0                  |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |            Group code         |           User code           |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                      UFD start block #                        |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                   # of words in UFD entry                     |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                               0                               |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                     Repeat above 4 words                      |
 *       |                          for each UFD                         |
 *       |                                                               |
 *
 * UFD Block:
 *
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                  Link to next UFD block or 0                  |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                             File                              |
 *       +-                                                             -+
 *       |                             name                              |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                          Extension                            |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |Typ|    Rsvd   |                 Creation Date                 |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                        Next free byte                         |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                        Start block #                          |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                    Length (in # of blocks)                    |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                      Last block written                       |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |LCK|        Usage count        |        Protection code        |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *
 * Bitmap Block:
 *
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                   Link to next bitmap block                   |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                             Map #                             |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                       # of words of map                       |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                   Link to first bitmap block                  |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                     Map for blocks 0 - 17                     |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                     Map for blocks 20 - 37                    |
 *       |                              ...                              |
 *       |                                                               |
 */

/*
 * The MFD and UFD directories can be treated as an array of 16-bit values.
 * The following offsets describe each structure.
 */
#define BOOT_BLOCK              0               /* Reserved for bootstrap */
#define MFD1_BLOCK              1               /* MFD always at block 1*/

#define MFD1_MFD2BLOCK          0
#define MFD1_INTERLEAVE         1
#define MFD1_BMSTART            2
#define MFD1_BM1                3

#define MFD2_LINK               0
#define MFD2_HEADER             1

#define MFD2_UFDUIC             0
#define MFD2_UFDSTART           1
#define MFD2_UFDSIZE            2
#define MFD2_UFDZERO            3
#define MFD2_SIZE               4

#define UFD_LINK                0
#define UFD_HEADER              1

#define UFD_FILENAME            0
#define UFD_EXTENSION           2
#define UFD_CREATION            3
#define UFD_TYPE                0100000
#define UFD_TYPELINKED          0000000
#define UFD_TYPECONTIGUOUS      0100000
#define UFD_DATE                0077777
#define UFD_NEXTFREEBYTE        4
#define UFD_FILESTART           5
#define UFD_FILELENGTH          6
#define UFD_LASTBLOCKWRITTEN    7
#define UFD_LUP                 8
#define UFD_LOCK                0100000
#define UFD_USAGE               0077400
#define UFD_PROT                0000377
#define UFD_LEN                 9               /* 9 word in each entry */

#define MAP_LINK                0
#define MAP_MAP                 1
#define MAP_WORDS               2
#define MAP_FIRST               3
#define MAP_BMSTART             4
#define MAP_LEN                 60              /* 60 words in each entry */
#define MAP_BLOCKS              (MAP_LEN * 16)

#define FILE_LINK               0

/*
 * Block sizes for each supported disk drive
 */
#define BLOCKSIZE_RC11          64
#define BLOCKSIZE_RF11          64
#define BLOCKSIZE_RK11          256
#define BLOCKSIZE_RP03          512

/*
 * Max # of UIC's (UFDs) for each supported disk drive
 */
#define UICCOUNT_RC11           15
#define UICCOUNT_RF11           15
#define UICCOUNT_RK11           63
#define UICCOUNT_RP03           127

/*
 * Compute end of directory block, given the size of each directory entry.
 * This macro is good for both MFDs and UFDs.
 */
#define EODIR(m, sz)            ((m->blocksz / 2) - (sz - 1))

/*
 * The logical block size will depend on the type of the disks. Disks smaller
 * than a RK05 (RS11/RS64) will use 64 word blocks while disks larger than
 * a RK05 (RP03) will use 512 word blocks. The default will be for an 256
 * word blocks for a RK05.
 */
#define DISKSIZE_RK05           4800

/*
 * Structure to describe a filename and associated UIC. Asterisks may be used
 * as wild card characters for the 4 components of a filename; name, extension,
 * group number and user number.
 */
struct dos11FileSpec {
  uint8_t               flags;          /* Wild card indicators */
  uint16_t              name[2];        /* File name */
  uint16_t              ext;            /* Extension */
  unsigned char         user;           /* User number */
  unsigned char         group;          /* Group number */
};
#define DOS11_WC_NAME   0001            /* Wild card name */
#define DOS11_WC_EXT    0002            /* Wild card extension */
#define DOS11_WC_GROUP  0004            /* Wild card group number */
#define DOS11_WC_USER   0010            /* Wild card user number */

#define DOS11_M_NONE    0000            /* Wild cards not allowed */
#define DOS11_M_ALLOW   0001            /* Wild cards allowed */
#define DOS11_M_NONAME  0002            /* Wild cards allowed */
                                        /* If no filename + extension */
                                        /* present, default to *.* */
/*
 * Structure to define an open file. This is a DOS-11 UFD entry along with
 * sufficient information to be able to write the directory entry back
 * to disk.
 */
struct dos11OpenFile {
  uint16_t              name[2];        /* File name */
  uint16_t              ext;            /* Extension */
  uint16_t              creation;       /* Type + creation date */
  uint16_t              nfb;            /* Next free byte */
  uint16_t              start;          /* Start block # */
  uint16_t              length;         /* Length in blocks */
  uint16_t              last;           /* Last block written */
  uint16_t              lup;            /* Lock, usage + protection */
                                        /* End of directory entry */
  uint16_t              ufdblk;         /* UFD block # */
  uint16_t              ufdoffset;      /* Offset within UFD */
                                        /* Start of read/write info */
  enum openMode         mode;           /* Open mode (read/write) */
  struct mountedFS      *mount;         /* Mounted file system descriptor */
  char                  *buffer;        /* Private buffer for file I/O */
  uint16_t              current;        /* Current block */
  uint16_t              nab;            /* Next available byte */
  uint16_t              eob;            /* End of buffer */
};

/*
 * DOS-11 specific data area. Some fields are sized for the worst case -
 * RP03 disk pack with 65535 blocks of 1024 bytes each.
 */
struct DOS11data {
  unsigned int          blocks;         /* # of blocks in file system */
  uint16_t              bitmaps;        /* # of bitmaps */
  uint16_t              bmblk[128];     /* Bitmap block addresses */
  uint16_t              bmindex;        /* Current bitmap in buffer */
  uint16_t              bmscan;         /* Start bitmap scans here */
  uint8_t               bmdirty;        /* 0 => clean, 1 => dirty */
  uint16_t              bmbuf[512];     /* Buffer for bitmap access */
  uint16_t              buf[512];       /* Disk buffer */
  /*
   * Settable parameters
   */
  uint8_t               group;          /* Default group # */
  uint8_t               user;           /* Default user # */
};

#endif
