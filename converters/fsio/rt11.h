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
#ifndef __RT11_H__
#define __RT11_H__

/*
 * General disk layout:
 *
 * Block
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0     |                            Reserved                           |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 1     |                     Home Block (Reserved)                     |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 2     |                            Reserved                           |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 3     |                            Reserved                           |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 4     |                            Reserved                           |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 5     |                            Reserved                           |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 6     |                     Directory Segment 1                       |
 * 7     |                                                               |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 10    |                     Directory Segment 2                       |
 * 11    |                                                               |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                              ...                              |
 *       |                              ...                              |
 *       |                              ...                              |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * x     |                     Directory Segment n                       |
 * x+1   |                                                               |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * Files |                              ...                              |
 *       |                              ...                              |
 *       |                              ...                              |
 *       |                              ...                              |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *                                 End of Volume
 *
 * Home Block:
 *
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0000  |                   Bad block replacement table                 |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0202  |                            Unused                             |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0204  |                  INITIALIZE/RESTORE data area                 |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0252  |                     BUP Information area                      |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0274  |                            Unused                             |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0700  |                 Reserved for Digital - 000000                 |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0702  |                 Reserved for Digital - 000000                 |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0704  |                            Unused                             |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0722  |                  Pack cluster size - 000001                   |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0724  |          Block # of first directory segment - 000006          |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0726  |               System version - Radix-50 "V3A"?                |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0730  |            Volume identification - "RT11A       "             |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0744  |                  Owner name - "            "                  |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0760  |            System identification - "DECRT11A    "             |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * 0776  |                           Checksum                            |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *
 * Directory Segment Header:
 *
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |            Total # of directory segments (1 - 31)             |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |               Next logical directory segment #                |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |               Highest directory segment in use                |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                Extra bytes per directory entry                |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |               Block # for start of this segment               |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *
 * Directory Entry:
 *
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                          Status Word                          |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |               Radix-50 File Name (chars 1 - 3)                |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |               Radix-50 File Name (chars 4 - 6)                |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |               Radix-50 File Type (1 - 3 chars)                |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |             Job #             |           Channel #           |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                         Creation Date                         |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |                     Optional Extra Words                      |
 *       |                              ...                              |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *
 * Status Word:
 *
 *         15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *         |   |           |   |   |   |           |
 *         |   |           |   |   |   |            - Prefix block indicator
 *         |   |           |   |   |    - Tentative file
 *         |   |           |   |    - Empty area
 *         |   |           |    - Permanent file
 *         |   |            - End of segment marker
 *         |    - Protected from .WRITE requests
 *          - Protected permanent file
 *
 * Date Word:
 *
 *         15  14  13          10  9               5   4               0
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *       |       |               |                   |                   |
 *       +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *         |   |   |           |   |               |   |               |
 *         +---+   +-----------+   +---------------+   +---------------+
 *           |           |                 |                   |
 *           |           |                 |            Year - 1972
 *           |           |                 |               - 32 x Age
 *           |           |                  - Day (1 - 31)
 *           |            - Month (1 - 12)
 *            - Age (0 - 3)
 */

#define RT11_HOME       1               /* Home block is always 1 */
#define RT11_DSSTART    6               /* Start of directory segs */
#define RT11_BLOCKSIZE  512             /* Size of a data block on disk */
#define RT11_RX01SS     128             /* Sector size of RX01 floppy */
#define RT11_RX02SS     256             /* Sector size of RX02 floppy */
#define RT11_RX0xNSECT  26              /* Sectors/track on RX01/RX02 */

#define RT11_SYSVER_V3A 36521
#define RT11_SYSVER_V04 36434
#define RT11_SYSVER_V05 36435

#define RT11_NOPART     0               /* Not a valid partition */
#define RT11_SINGLE     1               /* Single partition per disk */
#define RT11_MULTI      2               /* Multiple partitions per disk */

#define RT11_VOLID      "RT11A       "
#define RT11_OWNER      "            "
#define RT11_SYSID      "DECRT11A    "
#define RT11_VMSSYSID   "DECVMSEXCHNG"  /* VMS exchange created volume */

/*
 * Partition sizes. The last block of a maximum sized partition (32MB) is
 * unused. The minimum size is based on a file system having 1 directory
 * segment and 1 data block! Is this reasonable?
 */
#define RT11_MAXPARTSZ  0200000         /* Max partition size */
#define RT11_MINPARTSZ  0000010         /* Min partition size */

#define RT11_RK05SZ     4800            /* Blocks on an RK05 drive */
#define RT11_RL01SZ     10240           /* Blocks on an RL01 drive */
#define RT11_RL02SZ     20480           /* Blocks on an RL02 drive */
#define RT11_RX0xSZ     2002            /* Sectors on an RX01/RX02 */

#define RT11_HB_BBLOCK  0000            /* Bad block replacement tbl */
#define RT11_HB_RESTORE 0102            /* INIT/RESTORE data area */
#define RT11_HB_BUP     0125            /* BUP info area */
#define RT11_HB_PCS     0351            /* Pack cluster size */
#define RT11_HB_FIRST   0352            /* First directory segment */
#define RT11_HB_SYSVER  0353            /* System version */
#define RT11_HB_VOLID   0354            /* Volume identification */
#define RT11_HB_OWNER   0362            /* Owner name */
#define RT11_HB_SYSID   0370            /* System identification */
#define RT11_HB_CHKSUM  0377            /* Checksum */

#define RT11_DH_COUNT   0000            /* # of directory segments */
#define RT11_DH_NEXT    0001            /* Next logical segment # */
#define RT11_DH_HIGHEST 0002            /* Highest segment # in use */
#define RT11_DH_EXTRA   0003            /* Extra bytes/dir. entry */
#define RT11_DH_START   0004            /* Block # for segment start */
#define RT11_DH_SIZE    0005            /* Size of header */

#define RT11_DS_SIZE    512             /* Directory segment size */
#define RT11_DS_DISPACE (RT11_DS_SIZE - RT11_DH_SIZE)
#define RT11_DS_MAX     31              /* Max # of directory segments */

#define RT11_DI_STATUS  0000            /* Status word */
#define RT11_DI_FNAME1  0001            /* File name (chars 1 - 3) */
#define RT11_DI_FNAME2  0002            /* File name (chars 4 - 6) */
#define RT11_DI_FTYPE   0003            /* File type (1 - 3 chars) */
#define RT11_DI_LENGTH  0004            /* Total file length */
#define RT11_DI_JOB_CHN 0005            /* Channel # */
#define RT11_DI_CREATE  0006            /* Date of creation */
#define RT11_DI_SIZE    0007            /* Default entry size */

#define RT11_E_PRE      000020          /* Prefix block indicator */
#define RT11_E_TENT     000400          /* Tentative file */
#define RT11_E_MPTY     001000          /* Empty area */
#define RT11_E_PERM     002000          /* Permanent file */
#define RT11_E_EOS      004000          /* End of segment marker */
#define RT11_E_READ     040000          /* Protected from .WRITE */
#define RT11_E_PROT     100000          /* Protected permanent file */

#define RT11EOS(v)              (((v) & RT11_E_EOS) != 0)

#define RT11_DW_YEAR    0000037         /* Year */
#define RT11_DW_DAY     0001740         /* Day */
#define RT11_DW_MONTH   0036000         /* Month */
#define RT11_DW_AGE     0140000         /* Age */

/*
 * Structure to describe a filename. Asterisks may be used as wild card
 * characters for the 2 components of a filename; name and type.
 */
struct rt11FileSpec {
  uint8_t               flags;          /* Wild card indicators */
  uint16_t              name[2];        /* File name (RAD50) */
  uint16_t              type;           /* File type (RAD50) */
  char                  fname[6];       /* File name (ASCII) */
  char                  ftype[3];       /* File type (ASCII) */
};
#define RT11_WC_NAME    0001            /* Wild card in name */
#define RT11_WC_TYPE    0002            /* Wild card in type */

#define RT11_M_NONE     0000            /* Wild cards not allowed */
#define RT11_M_ALLOW    0001            /* Wild cards allowed */
#define RT11_M_NONAME   0002            /* Wild cards allowed */
                                        /* If no filename + extension */
                                        /* present, default to *.* */
/*
 * Structure to define an open file. This is an RT-11 directory entry along
 * with sufficient information to be able to write the directory entry back
 * to disk. Some of the directory information can be created on-the-fly so
 * will not be stored here.
 */
struct rt11OpenFile {
  uint16_t              status;         /* File status */
  uint16_t              name[2];        /* File name */
  uint16_t              type;           /* File type */
  uint16_t              length;         /* Blocks written */
  uint16_t              creation;       /* Creation date */
                                        /* End of directory entry */
  uint8_t               segment;        /* Directory segment # */
  uint16_t              offset;         /* Directory offset */
                                        /* Start of read/write info */
  enum openMode         mode;           /* Open mode (read/write) */
  struct mountedFS      *mount;         /* Mounted file system descriptor */
  uint8_t               unit;           /* Partition number */
  char                  *buffer;        /* Private buffer for file I/O */
  uint16_t              current;        /* Current working block # */
  uint16_t              last;           /* Last usable block */
  uint16_t              count;          /* Bytes used in current block */
  uint16_t              start;          /* Starting block # */
};

/*
 * RT-11 specific data area.
 */
struct RT11data {
  unsigned int          blocks;         /* Size of container */
  unsigned int          sectorsz;       /* Interleave sector size */
                                        /* 0 if no interleave */
  uint16_t              filesystems;    /* Max # of filesystems */
  uint16_t              valid[16];      /* Valid partitions */
  uint16_t              maxblk[256];    /* Max block address */
  uint16_t              first[256];     /* First directory block */
  uint16_t              buf[512];       /* Disk buffer - enough for a */
                                        /*   directory segment */
};
#define RT11_PARTITIONVALID(d, u) ((d->valid[u / 16] & (1 << (u % 16))) != 0)

#endif
