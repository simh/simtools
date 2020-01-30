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

#ifndef __FSIO_H__
#define __FSIO_H__
#include <stdio.h>
#include <stdint.h>
#include "declib.h"

/*
 * Mode for open files
 */
enum openMode           { M_RD, M_WR };

#include "dos11.h"
#include "rt11.h"
#include "dosmt.h"
#include "os8.h"

/*
 * All of the supported file systems are natively little endian so we only
 * need a subset of the endian support macros/routines.
 */
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>

#define htole16(x)      OSSwapHostToLittleInt16(x)
#define le16toh(x)      OSSwapLittleToHostInt16(x)

#define htole32(x)      OSSwapHostToLittleInt32(x)
#define le32toh(x)      OSSwapLittleToHostInt32(x)
#elif defined(__linux__)
#include <endian.h>
#elif defined(__NetBSD__)
#include <sys/endian.h>

#define le16toh(x)      letoh16(x)

#define le32toh(x)      letoh32(x)
#elif defined(__FreeBSD__) || defined(__OpenBSD__)
#include <sys/endian.h>
#endif

#ifdef __GNUC__
#define UNUSED(x)       UNUSED_ ## x __attribute__((__unused__))
#else
#define UNUSED(x)       UNUSED_ ## x
#endif

#define MAX_DEVLEN      16

#if defined(__linux__)
#define OPTIONS(s)      "+" s
#else
#define OPTIONS(s)      s
#endif

extern uint32_t swPresent;
extern char *swValue[];

#define SWISSET(c)      ((swPresent & (1 << (c - 'a'))) != 0)
#define SWSET(c)        swPresent |= (1 << (c - 'a'))
#define SWGETVAL(c)     (swValue[c - 'a'])
#define SWSETVAL(c, v)  swValue[c - 'a'] = v

#ifdef DEBUG
extern FILE *DEBUGout;
#define ERROR(...) { \
    fprintf(stderr, __VA_ARGS__); \
    if ((DEBUGout != NULL) && (DEBUGout != stdout)) \
      fprintf(DEBUGout, __VA_ARGS__); \
  }
#else
#define ERROR(...)     fprintf(stderr, __VA_ARGS__)
#endif

struct mountedFS;

/*
 * File system definition
 */
struct FSdef {
  struct FSdef          *next;          /* Pointer to next file system */
  char                  *fstype;        /* File system type name */
  char                  *descr;         /* File system description */
  uint16_t              flags;          /* Flags */
  size_t                blocksz;        /* Default block size */
  int                   (*mount)(struct mountedFS *);
  void                  (*umount)(struct mountedFS *);
  size_t                (*size)(void);
  int                   (*newfs)(struct mountedFS *, size_t);
  void                  (*set)(struct mountedFS *, uint8_t, uint8_t);
  void                  (*info)(struct mountedFS *, uint8_t, uint8_t);
  void                  (*dir)(struct mountedFS *, uint8_t, char *);
  void                  *(*openFileR)(struct mountedFS *, uint8_t, char *);
  void                  *(*openFileW)(struct mountedFS *, uint8_t, char *, off_t);
  off_t                 (*fileSize)(void *);
  void                  (*closeFile)(void *);
  size_t                (*readFile)(void *, void *, size_t);
  size_t                (*writeFile)(void *, void *, size_t);
  void                  (*deleteFile)(void *, char *);
  /*
   * The following functions are only supported by magtape file systems.
   */
  void                  (*rewind)(struct mountedFS *);
  void                  (*eom)(struct mountedFS *);
  void                  (*skipforw)(struct mountedFS *, unsigned long);
  void                  (*skiprev)(struct mountedFS *, unsigned long);
};
#define FS_UNITVALID    0x0001          /* Unit # valid in device name */
#define FS_TAPE         0x0002          /* File system for magtapes */
#define FS_EMPTYFILE    0x0004          /* Empty file is OK */
#define FS_1OPENFILE    0x0008          /* Only support a single open file */

/*
 * Mounted file system descriptor
 */
struct mountedFS {
  struct mountedFS      *next;          /* Pointer to next mounted file sys */
  char                  name[MAX_DEVLEN + 1];
  struct FSdef          *filesys;       /* File system */
  size_t                blocksz;        /* Active block size */
  uint16_t              flags;
#define FS_READONLY     0x0001          /* Mounted read-only */
#define FS_DEBUG        0x0002          /* Debug output */
                                        /* Bits after 0x0080 reserved for */
                                        /* file system use */
  FILE                  *container;     /* Container file access */
  off_t                 skip;           /* Data to skip in container file */
  union {
    struct DOS11data    _dos11;
    struct RT11data     _rt11;
    struct DOSMTdata    _dosmt;
    struct OS8data      _os8;
  }                     FSdata;
#define dos11data       FSdata._dos11
#define rt11data        FSdata._rt11
#define dosmtdata       FSdata._dosmt
#define os8data         FSdata._os8
};

extern int FSioReadBlob(struct mountedFS *, off_t, unsigned int, void *);
extern int FSioWriteBlob(struct mountedFS *, off_t, unsigned int, void *);
extern int FSioReadBlock(struct mountedFS *, unsigned int, void *);
extern int FSioWriteBlock(struct mountedFS *, unsigned int, void *);
extern int FSioReadSector(struct mountedFS *, unsigned int, unsigned int, void *);
extern int FSioWriteSector(struct mountedFS *, unsigned int, unsigned int, void *);

#endif
