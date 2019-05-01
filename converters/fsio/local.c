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
 * Support routines for local file access under fsio.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include "fsio.h"

/*++
 *      l o c a l I n f o
 *
 *  Display information about the local file system. This is not supported
 *  by the "local" device.
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
static void localInfo(
  struct mountedFS *UNUSED(mount),
  uint8_t UNUSED(unit),
  uint8_t UNUSED(present)
)
{
  fprintf(stderr, "info: The \"local:\" device does not support this command\n");
}

/*++
 *      l o c a l D i r
 *
 *  Produce a full or brief directory listing.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - device unit number
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
static void localDir(
  struct mountedFS *UNUSED(mount),
  uint8_t UNUSED(unit),
  char *fname
)
{
  char cmd[64];

  sprintf(cmd, "/bin/ls %s%s\n",
          SWISSET('f') ? "-l " : "", fname);
  system(cmd);
}

/*++
 *      l o c a l O p e n F i l e R
 *
 *  Open a local file for reading.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - device unit number
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
static void *localOpenFileR(
  struct mountedFS *UNUSED(mount),
  uint8_t UNUSED(unit),
  char *fname
)
{
  return fopen(fname, "r");
}

/*++
 *      l o c a l O p e n F i l e W
 *
 *  Open a local file for writing.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      unit            - device unit number
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
static void *localOpenFileW(
  struct mountedFS *UNUSED(mount),
  uint8_t UNUSED(unit),
  char *fname,
  off_t UNUSED(size)
)
{
  return fopen(fname, "w");
}

/*++
 *      l o c a l F i l e S i z e
 *
 *  Return the size of a currently open file. If the file is open in ASCII
 *  mode we do not know how many lines are present in the file and so cannot
 *  calculate how much additional space will be needed for a possible <LF>
 *  to <CRLF> translation. In this case we return 0.
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
 *      Current size of the file, 0 on error
 *
 --*/
static off_t localFileSize(
  void *filep
)
{
  FILE *file = filep;
  struct stat stat;

  if (!SWISSET('a'))
    if (fstat(fileno(file), &stat) == 0)
      return stat.st_size;

  return 0;
}

/*++
 *      l o c a l D e l e t e F i l e
 *
 *  Delete a local file.
 *
 * Inputs:
 *
 *      file            - pointer to open file descriptor
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
static void localDeleteFile(
  void *file,
  char *fname
)
{
  fclose(file);

  if (unlink(fname) != 0)
    fprintf(stderr, "delete: failed to delete \"%s\"\n", fname);
}

/*++
 *      l o c a l C l o s e F i l e
 *
 *  Close an open local file.
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
static void localCloseFile(
  void *file
)
{
  fclose(file);
}

/*++
 *      l o c a l R e a d F i l e
 *
 *  Read from a local file into a supplied buffer. If ASCII mode is active,
 *  each read will return at most 1 line of data and any terminating <LF> will
 *  be translated into <CRLF>.
 *
 * Inputs:
 *
 *      file            - pointer to open file descriptor
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
static size_t localReadFile(
  void *file,
  void *buf,
  size_t buflen
)
{
  if (SWISSET('a')) {
    char *bufr = buf;
    int readlen;

    if (fgets(bufr, buflen - 1, file) == NULL)
      return 0;

    /*
     * Translate terminating \n into \r\n unless it is already there
     */
    if ((readlen = strlen(bufr)) != 0) {
      if (bufr[readlen - 1] == '\n') {
        if ((readlen == 1) || (bufr[readlen - 2] != '\r'))
          strcpy(&bufr[readlen - 1], "\r\n");
      }
    }
    return strlen(bufr);
  }
  return fread(buf, sizeof(char), buflen, file);
}

/*++
 *      l o c a l W r i t e F i l e
 *
 *  Write to a local file from a supplied buffer. If ASCII mode is active,
 *  if the buffer is terminated with <CRLF> translate it into <LF>.
 *
 * Inputs:
 *
 *      file            - pointer to open file descriptor
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
static size_t localWriteFile(
  void *file,
  void *buf,
  size_t buflen
)
{
  if (SWISSET('a')) {
    char *bufw = buf;

    if (buflen >= 2)
      if ((bufw[buflen - 2] == '\r') && (bufw[buflen - 1] == '\n')) {
        bufw[buflen - 2] = '\n';
        buflen--;
      }
  }
  return fwrite(buf, sizeof(char), buflen, file);
}

/*++
 *      l o c a l F S
 *
 *  Descriptor for accessing local files. Note that none of command routines
 *  are present since this device never appears to be mounted.
 --*/
struct FSdef localFS = {
  NULL,
  "local",
  "local            Local file access\n",
  0,
  0,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  localInfo,
  localDir,
  localOpenFileR,
  localOpenFileW,
  localFileSize,
  localCloseFile,
  localReadFile,
  localWriteFile,
  localDeleteFile,
  NULL,                                 /* No tape support functions */
  NULL,
  NULL,
  NULL
};

/*
 * Statically allocated mounted file system for local file access
 */
struct mountedFS localMount;
