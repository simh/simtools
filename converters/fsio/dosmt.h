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
#ifndef __DOSMT_H__
#define __DOSMT_H__
#include "tape.h"

/*
 * DOS-11 magtapes contain data records of 512 bytes each.
 */
#define DOSMTRCLNT      512

/*
 * DOS/BATCH-11 magtape file header.
 */
struct dosmthdr {
  uint16_t              fname[2];       /* first 6 chars of filename (RAD50) */
  uint16_t              ext;            /* 3 char file extension (RAD50) */
  uint8_t               prog;           /* programmer # (octal) */
  uint8_t               proj;           /* project # (octal) */
  uint16_t              prot;           /* protection code (octal) */
  uint16_t              date;           /* (year-1970) * 1000 + day of year */
  uint16_t              fname3;         /* optional, char 7 - 9 of name */
};

/*
 * Structure to describe a filename and associated UIC. Asterisks may be used
 * as wild card characters for the 4 components of a filename; name, extension,
 * group number and user number.
 */
struct dosmtFileSpec {
  uint8_t               flags;          /* Wild card indicators */
  uint16_t              name[3];        /* File name */
  uint16_t              ext;            /* Extension */
  unsigned char         proj;           /* Project number */
  unsigned char         prog;           /* Programmer number */
};
#define DOSMT_WC_NAME   0001            /* Wild card name */
#define DOSMT_WC_EXT    0002            /* Wild card extension */
#define DOSMT_WC_PROJ   0004            /* Wild card project number */
#define DOSMT_WC_PROG   0010            /* Wild card programmer number */

#define DOSMT_M_NONE    0000            /* Wild cards not allowed */
#define DOSMT_M_ALLOW   0001            /* Wild cards allowed */
#define DOSMT_M_NONAME  0002            /* Wild cards allowed */
                                        /* If no filename + extension */
                                        /* present, default to *.*[*,*] */

/*
 * Structure to define an open file.
 */
struct dosmtOpenFile {
  enum openMode         mode;           /* Open mode (read/write) */
  struct mountedFS      *mount;         /* Mounted file system descriptor */
  char                  buf[DOSMTRCLNT];/* Private buffer for file I/O */
  uint16_t              nextb;          /* Next byte to use */
  uint8_t               tm;             /* Tape mark has been read */
  uint8_t               error;          /* Error has been detected */
};

/*
 * DOS-11 magtape specific data area.
 */
struct DOSMTdata {
  uint8_t               buf[DOSMTRCLNT];
  off_t                 eot;            /* Logical end-of-tape */

  /*
   * Settable parameters
   */
  uint8_t               proj;           /* project # */
  uint8_t               prog;           /* programmer # */
  uint8_t               prot;           /* protection code */
#define FS_DOSMTEXT     0x0100          /* Write extended file headers */
};

#endif
