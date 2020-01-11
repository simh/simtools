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

#ifndef __OS8_H__
#define __OS8_H__

/*
 * General disk layout (each block is 256 12-bit words).
 *
 * Block
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *  0     |                   Reserved                    |
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *  1-6   |              Directory segments               |
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *  7-12  |               Keyboard Monitor                | **
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 * 13-15  |              User Service Routine             | **
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 * 16-25  |                Device Handlers                | **
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 * 26     |            Enter Processor For USR            | **
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 * 27-50  |             System Scratch Blocks             | **
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 * 51-53  |                Command Decoder                | **
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 * 54-55  |            SAVE And DATE Overlays             | **
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 * 56     |             Monitor Error Routine             | **
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 * 57     |            CHAIN Processor For USR            | **
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 * 60-63  |                  System ODT                   | **
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 * 64     |         Reserved For System Expansion         | **
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 * 65     |               CCL Reminiscences               | **
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 * 66     |            12K TD8E Resident Code             | **
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 * 67     |                  CCL Overlay                  | **
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 * Files  |                      ...                      |
 *        |                      ...                      |
 *        |                      ...                      |
 *        |                      ...                      |
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *                          End Of Volume
 *
 * Blocks marked "**" are only reserved on System Devices, on non-System
 * Devices, file storage starts at block 7.
 *
 * Directory Segment Header:
 *
 * Word
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *  0     |       Minus # of entries in this segment      |
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *  1     | Starting block # of first file in this segment|
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *  2     |         Link to next directory segment        |
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *  3     |                   Flag Word                   |
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *  4     |    Minus # of additional information words    |
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *
 * Directory Entry:
 *
 * Word
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *  0     |     Sixbit ASCII File Name (chars 1 - 2)      |
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *  1     |     Sixbit ASCII File Name (chars 3 - 4)      |
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *  2     |     Sixbit ASCII File Name (chars 5 - 6)      |
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *  3     |   Sixbit ASCII File Extension (chars 1 - 2)   |
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *        |        N additional information words         |
 *        |                      ...                      |
 *        |                      ...                      |
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 * N + 4  |          Minus file length in blocks          |
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *
 * If there are additional informations words in the directory, the file
 * creation time is stored in word 4 (zero means not available):
 *
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *        |               |                   |           |
 *        +---+---+---+---+---+---+---+---+---+---+---+---+
 *          |           |   |               |   |       |
 *          |           |   |               |   +-------+
 *          |           |   +---------------+       |
 *          +-----------+           |               Year - 1970
 *                |                 Day of Month (1 - 31)
 *                Month (1 - 12)
 */

#define OS8_BLOCKSIZE   256             /* Size of a block on disk (words) */

#define OS8_DSSTART     1               /* Start of directory segments */
#define OS8_DSLAST      6               /* Last directory segment */
#define OS8_DATA        7               /* Start of data (non-system device) */

#define OS8_DH_ENTRIES  0               /* -(# of entries in segment) */
#define OS8_DH_START    1               /* Block # for segment start */
#define OS8_DH_NEXT     2               /* Next directory segment */
#define OS8_DH_FLAGWD   3               /* Flag word */
#define OS8_DH_EXTRA    4               /* -(# of additional info. words) */
#define OS8_DH_SIZE     5               /* Size of directory header */

#define OS8_DI_FNAME1   0               /* File name (chars 1 - 2) */
#define OS8_DI_FNAME2   1               /* File name (chars 3 - 4) */
#define OS8_DI_FNAME3   2               /* File name (chars 5 - 6) */
#define OS8_DI_EXT      3               /* File extension (1 - 2 chars) */
                                        /* Additional words go here */
#define OS8_DI_DATE     4               /* Optional creation date */
#define OS8_DI_LENGTH   4               /* -(File length in blocks) */
#define OS8_DI_SIZE     5               /* Default entry size */

#define OS8_ED_IND      0               /* Empty directory entry */
#define OS8_ED_LENGTH   1               /* -(Empty file length in blocks) */
#define OS8_ED_SIZE     2               /* Entry size */

/*
 * Device specific definitions
 */
#define OS8_RK05FS_BLKS 3248            /* Blocks in an RK05 file system */

#define OS8_RX01SS      128             /* Byte sector size of RX01 floppy */
#define OS8_RX02SS      256             /* Byte sector size of RX02 floppy */
#define OS8_RX01SS_W    64              /* Word sector size of RX01 floppy */
#define OS8_RX02SS_W    128             /* Word sector size of RX02 floppy */
#define OS8_RX0xNSECT   26              /* Sectors/track on RX01/RX02 */
#define OS8_RX0xSZ      2002            /* Sectors on an RX01/RX02 */

/*
 * Structure to describe a filename. Asterisks may be used as wild card
 * characters for the 2 components of a filename; namd and extension
 */
struct os8FileSpec {
  uint8_t               flags;          /* Wild card indicators */
  uint16_t              name[3];        /* File name (sixbit) */
  uint16_t              ext;            /* File extension (sixbit) */
  char                  fname[6];       /* File name (ASCII) */
  char                  fext[2];        /* File extension (ASCII) */
};
#define OS8_WC_NAME     001             /* Wild card in name */
#define OS8_WC_EXT      002             /* Wild card in extension */

#define OS8_M_NONE      0000            /* Wild cards not allowed */
#define OS8_M_ALLOW     0001            /* Wild cards allowed */
#define OS8_M_NONAME    0002            /* Wild cards allowed */
                                        /* If no filename + extension */
                                        /* present, default to *.* */
/*
 * OS/8 files pack 3 bytes into a pair of 12-bit words. The following states
 * are used to pack/unpack bytes.
 */
#define OS8_BYTE0       0               /* Pack/unpack byte 0 */
#define OS8_BYTE1       1               /* Pack/unpack byte 1 */
#define OS8_BYTE2       2               /* Pack/unpack byte 2 */
#define OS8_CHECK       3               /* Check for end of block/EOF */

/*
 * Structure to define an open file. This is an OS/8 directory entry along
 * with sufficient information to be able to write the directory entry back
 * to disk.
 */
struct os8OpenFile {
  uint16_t              name[3];        /* File name */
  uint16_t              ext;            /* File extension */
  uint16_t              creation;       /* Creation date */
  uint16_t              length;         /* File length (blocks) */
                                        /* End of directory entry */
  uint8_t               segment;        /* Directory segment */
  uint16_t              offset;         /* Directory offset */
  uint16_t              entrysz;        /* Size of directory entries */
  uint16_t              extra;          /* Extra words in directory entries */
  uint16_t              remain;         /* Remaining directory entries */
                                        /* Start of read/write info */
  enum openMode         mode;           /* Open mode (read/write) */
  struct mountedFS      *mount;         /* Mounted file system descriptor */
  uint8_t               unit;           /* File system # */
  uint16_t              *buffer;        /* Private buffer for file I/O */
  uint16_t              start;          /* Starting block # */
  uint16_t              current;        /* Current block # */
  uint16_t              last;           /* Last block # */
  uint16_t              wordpos;        /* Current word offset */
  uint8_t               bytepos;        /* Current byte position */
  off_t                 written;        /* # of bytes written to the file */
};

/*
 * Device descriptor
 */
struct OS8device {
  char                  *name;          /* Device name */
  uint8_t               filesys;        /* # of file systems on device */
  size_t                diskSize;       /* # of blocks on device */
  off_t                 skip;           /* Reserved space at start of disk */
  uint16_t              blocks[8];      /* File system sizes */
  int                   (*blockPresent)(struct mountedFS *, uint8_t, unsigned int);
  int                   (*readBlock)(struct mountedFS *, uint8_t, unsigned int, void *);
  int                   (*writeBlock)(struct mountedFS *, uint8_t, unsigned int, void *);
};

/*
 * OS/8 specific data area
 */
struct OS8data {
  unsigned int          blocks;         /* Size of container */
  struct OS8device      *device;        /* Device type */
  uint8_t               devices;        /* # of "devices" present */
  uint8_t               valid;          /* Bitmap of valid devices */
  uint16_t              date;           /* Date for creating files */
  uint16_t              buf[OS8_BLOCKSIZE]; /* Disk buffer */
};

#endif
