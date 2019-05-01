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
 * Support routines for handling DOS/BATCH-11 magtapes under fsio
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>

#include "fsio.h"

/*
 * Table of "set" commands
 */
static char *setCmds[] = {
  "uic",
  "prot",
  NULL
};
#define DOSMTSET_UIC    0
#define DOSMTSET_PROT   1

extern int args;
extern char **words;

/*++
 *      d o s m t P a r s e F i l e s p e c
 *
 *  Parse a character string representing a DOS-11 magtape file specification.
 *
 * Inputs:
 *
 *      mount           - pointer to a mounted file system descriptor
 *      ptr             - pointer to the file specification string
 *      spec            - pointer to the file specification block
 *      wildcard        - wildcard processing options:
 *                        0 (DOSMT_M_NONE)      - wildcards not allowed
 *                        1 (DOSMT_M_ALLOW)     - wildcards allowed
 *                        2 (DOSMT_M_NONAME)    - wildcards allowed
 *                                                if filename + ext not
 *                                                present default to *.*[*,*]
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
#define P_PROJ          3
#define P_PROG          4

int dosmtParseFilespec(
  struct mountedFS *mount,
  char *ptr,
  struct dosmtFileSpec *spec,
  int wildcard
)
{
  struct DOSMTdata *data = &mount->dosmtdata;
  char term[] = { '\0', '.', '[', ',', ']' };
  char flags[] =
    { 0, DOSMT_WC_NAME, DOSMT_WC_EXT, DOSMT_WC_PROJ, DOSMT_WC_PROG };
  int state = P_NAME;
  unsigned int uic, i, namemax = ((mount->flags & FS_DOSMTEXT) != 0) ? 9 : 6;
  char filename[9], ext[3];

  memset(spec, 0, sizeof(struct dosmtFileSpec));
  spec->proj = data->proj;
  spec->prog = data->prog;

  memset(&filename, ' ', sizeof(filename));
  memset(&ext, ' ', sizeof(ext));

  if (wildcard == DOSMT_M_NONAME)
    spec->flags = DOSMT_WC_NAME | DOSMT_WC_EXT | DOSMT_WC_PROJ | DOSMT_WC_PROG;

  while (state != P_DONE) {
    if (wildcard) {
      if (*ptr == '*') {
        spec->flags |= flags[state];
        ptr++;
        if (*ptr == '\0')
          state = P_DONE;
        else if (*ptr == term[state]) {
          if (state == P_PROG)
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
               (i < namemax)) {
          spec->flags &= ~DOSMT_WC_NAME;
          filename[i++] = toupper(*ptr++);
        }

        switch (*ptr++) {
          case '\0':
            state = P_DONE;
            break;

          case '.':
            state = P_EXT;
            break;

          case '[':
            state = P_PROJ;
            break;

          default:
            return 0;
        }
        break;

      case P_EXT:
        while ((*ptr != '\0') &&
               (strchr(rad50, toupper(*ptr)) != NULL) &&
               (i < sizeof(ext))) {
          spec->flags &= ~DOSMT_WC_EXT;
          ext[i++] = toupper(*ptr++);
        }

        switch (*ptr++) {
          case '\0':
            state = P_DONE;
            break;

          case '[':
            state = P_PROJ;
            break;

          default:
            return 0;
        }
        break;

      case P_PROJ:
        spec->flags &= ~(DOSMT_WC_PROJ | DOSMT_WC_PROG);
        uic = 0;
        while ((strchr("01234567", *ptr) != NULL) && (i < 3)) {
          uic = (uic << 3) | (*ptr++ - '0');
          i++;
        }
        if ((uic == 0) || (uic > 0377))
          return 0;
        spec->proj = uic & 0377;

        if (*ptr++ != ',')
          return 0;

        state = P_PROG;
        break;

      case P_PROG:
        uic = 0;
        while ((strchr("01234567", *ptr) != NULL) && (i < 3)) {
          uic = (uic << 3) | (*ptr++ - '0');
          i++;
        }
        if ((uic == 0) || (uic > 0377))
          return 0;
        spec->prog = uic & 0377;

        if (*ptr++ != ']')
          return 0;

        state = P_DONE;
        break;
    }
  }

  spec->name[0] = ascR50(&filename[0]);
  spec->name[1] = ascR50(&filename[3]);
  spec->name[2] = ascR50(&filename[6]);
  spec->ext = ascR50(&ext[0]);
  return 1;
}

/*++
 *      d o s m t L o o k u p F i l e
 *
 *  Lookup up a specific file on the tape. If the "-n" switch is set, the
 *  tape will not be rewound before starting the lookup.
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
int dosmtLookupFile(
  struct mountedFS *mount,
  struct dosmtFileSpec *spec,
  struct dosmtOpenFile *file
)
{
  struct DOSMTdata *data = &mount->dosmtdata;
  uint32_t status;

  if (!SWISSET('n'))
    tapeRewind(mount->container);

  do {
    switch (status = tapeReadRecord(mount->container, data->buf, DOSMTRCLNT)) {
      case ST_FAIL:
        goto error;

      case ST_EOM:
        break;

      case ST_TM:
        /* Second tape mark in a row - treat as end-of-media */
        status = ST_EOM;
        break;

      default:
        if ((status & ST_ERROR) == 0) {
          if (status == sizeof(struct dosmthdr)) {
            struct dosmthdr *hdr = (struct dosmthdr *)data->buf;

            /*
             * Determine if this file matches the filespec.
             */
            if ((spec->flags & DOSMT_WC_PROJ) == 0)
              if (spec->proj != hdr->proj)
                goto nomatch;

            if ((spec->flags & DOSMT_WC_PROG) == 0)
              if (spec->prog != hdr->prog)
                goto nomatch;

            if ((spec->flags & DOSMT_WC_NAME) == 0) {
              if ((spec->name[0] != le16toh(hdr->fname[0])) ||
                  (spec->name[1] != le16toh(hdr->fname[1])))
                goto nomatch;

              if ((mount->flags & FS_DOSMTEXT) != 0)
                if (spec->name[2] != le16toh(hdr->fname3))
                  goto nomatch;
            }

            if ((spec->flags & DOSMT_WC_EXT) == 0)
              if (spec->ext != le16toh(hdr->ext))
                goto nomatch;

            /*
             * Found a matching file.
             */
            file->mount = mount;

            return 1;
          } else goto error;
        }

    nomatch:
        /*
         * Skip to next file.
         */
        do {
          if ((status = tapeReadRecordLength(mount->container)) == ST_FAIL)
            goto error;
        } while ((status != ST_EOM) && (status != ST_TM));

        if (status == ST_EOM)
          tapeSetPosition(mount->container, data->eot);
    }
  } while (status != ST_EOM);

 error:
  /*
   * Make sure we leave the tape at a valid position.
   */
  tapeSetPosition(mount->container, data->eot);
  return 0;
}

/*++
 *      d o s m t R e a d B y t e s
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
static int dosmtReadBytes(
  struct dosmtOpenFile *file,
  char *buf,
  int len
)
{
  struct mountedFS *mount = file->mount;
  int count = 0;
  uint32_t status;
  off_t pos;

  while (len) {
    if (file->nextb == DOSMTRCLNT) {
      if (file->tm != 0)
        return count;

      pos = tapeGetPosition(mount->container);

      switch (status = tapeReadRecord(mount->container, file->buf, DOSMTRCLNT)) {
        case ST_FAIL:
          return 0;

        case ST_TM:
        case ST_EOM:
          file->tm = 1;
          return count;

        default:
          if ((status & ST_ERROR) || (status != DOSMTRCLNT)) {
            tapeSetPosition(mount->container, pos);
            return count;
          }
          file->nextb = 0;
          break;
      }
    }
    *buf++ = file->buf[file->nextb++];
    len--;
    count++;
  }
  return count;
}

/*++
 *      d o s m t W r i t e B y t e s
 *
 *  Write a sequence of bytes to an open DOS-11 magtape file.
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
static int dosmtWriteBytes(
  struct dosmtOpenFile *file,
  char *buf,
  int len
)
{
  struct mountedFS *mount = file->mount;
  int count = 0;

  if (file->error != 0)
    return 0;

  while (len) {
    file->buf[file->nextb++] = *buf++;
    len--;
    count++;

    if (file->nextb == DOSMTRCLNT) {
      if (tapeWriteRecord(mount->container, file->buf, DOSMTRCLNT) == 0) {
        file->error = 1;
        return count;
      }
      memset(file->buf, 0, DOSMTRCLNT);
      file->nextb = 0;
    }
  }
  return count;
}

/*++
 *      d o s m t M o u n t
 *
 *  Verify that the open container file is in valid .tap format and that it
 *  consists of a number of files each preceeded by a file header. Strict
 *  DOS-11 tapes will have a 12 byte file header but there are some tapes
 *  will use an extended, 14 byte header with an extra 3 characters in the
 *  filename.
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
 *      1 if a valid DOS-11 magtape, 0 otherwise
 *
 --*/
static int dosmtMount(
  struct mountedFS *mount
)
{
  struct DOSMTdata *data = &mount->dosmtdata;

  if (tapeVerify(mount->container, &data->eot)) {
    unsigned char buf[DOSMTRCLNT];
    uint32_t status;

    /*
     * Scan each file making sure it is preceeded by a dos11 header (either
     * version is valid).
     */
    do {
      /*
       * If we are at end-of-tape, everything is OK
       */
      if (tapeGetPosition(mount->container) == data->eot)
        break;

      switch (status = tapeReadRecord(mount->container, buf, sizeof(buf))) {
        case ST_FAIL:
          return 0;

        case ST_EOM:
          break;

        case ST_TM:
          /* Second tape mark in a row - treat as end of media */
          status = ST_EOM;
          break;

        default:
          /*
           * If the header has an error, unable to use this tape.
           */
          if ((status & ST_ERROR) != 0)
            return 0;

          status &= ST_LENGTH;

          if (status != sizeof(struct dosmthdr))
            return 0;

          /*
           * Scan forward to the end of this file.
           */
          do {
            switch (status = tapeReadRecordLength(mount->container)) {
              case ST_FAIL:
                return 0;

              case ST_EOM:
              case ST_TM:
                break;

              default:
                if ((status & ST_ERROR) == 0)
                  if ((status & ST_LENGTH) != DOSMTRCLNT)
                    return 0;
            }
          } while ((status != ST_EOM) && (status != ST_TM));
      }
    } while (status != ST_EOM);

    data->proj = 01;
    data->prog = 01;
    data->prot = 0233;

    if (SWISSET('x'))
      mount->flags |= FS_DOSMTEXT;

    /*
     * Position the tape at the beginning
     */
    tapeRewind(mount->container);

    return 1;
  } else fprintf(stderr, "mount: Invalid tape format\n");
  return 0;
}

/*++
 *      d o s m t U m o u n t
 *
 *  Unmount the DOS-11 magtape file system, releasing any storage allocated.
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
static void dosmtUmount(
  struct mountedFS *UNUSED(mount)
)
{
}

/*++
 *      d o s m t S e t
 *
 *  Set mount point/filesystem specific values.
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
static void dosmtSet(
  struct mountedFS *mount,
  uint8_t UNUSED(unit),
  uint8_t UNUSED(present)
)
{
  struct DOSMTdata *data = &mount->dosmtdata;
  int idx = 0;
  uint8_t prog, proj, prot;

  while (setCmds[idx] != NULL) {
    if (strcmp(words[1], setCmds[idx]) == 0) {
      switch (idx) {
        case DOSMTSET_UIC:
          if (args == 3) {
            if (sscanf(words[2], "[%hho,%hho]", &proj, &prog) == 2) {
              data->proj = proj;
              data->prog = prog;
            } else fprintf(stderr,
                           "dosmt: UIC syntax error \"%s\"\n", words[2]);
          } else fprintf(stderr, "dosmt: Invalid syntax for \"set uic\"\n");
          return;

        case DOSMTSET_PROT:
          if (args == 3) {
            if (sscanf(words[2], "<%hho>", &prot) == 1) {
              data->prot = prot;
            } else fprintf(stderr,
                           "dosmt: Protection syntax error \"%s\"\n", words[2]);
          } else fprintf(stderr, "dosmt: Invalid syntax for \"set prot\"\n");
          return;
      }
    }
    idx++;
  }
  fprintf(stderr, "dosmt: Unknown set command \"%s\"\n", words[1]);
}

/*++
 *      d o s m t I n f o
 *
 *  Display information about the current position of the tape.
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
static void dosmtInfo(
  struct mountedFS *mount,
  uint8_t UNUSED(unit),
  uint8_t UNUSED(present)
)
{
  struct DOSMTdata *data = &mount->dosmtdata;
  off_t pos = tapeGetPosition(mount->container);
  int eot = 0, bot = pos == 0;
  char temp[32], filename[14];
  struct dosmthdr *hdr = (struct dosmthdr *)data->buf;
  struct stat stat;

  fstat(fileno(mount->container), &stat);

  /*
   * If we are positioned at the end-of-file, there is a tape mark or
   * end-of-media marker missing. Treat it as though the missing marker
   * is present.
   */
  if (pos != stat.st_size) {
    uint32_t bc;

    switch (bc = tapeReadRecord(mount->container, data->buf, DOSMTRCLNT)) {
      case ST_FAIL:
        goto error;

      case ST_TM:
      case ST_EOM:
        eot = 1;
        break;

      default:
        if ((bc & ST_ERROR) == 0) {
          if (bc == sizeof(struct dosmthdr)) {

            memset(filename, ' ', sizeof(filename));
            filename[13]='\0';

            r50Asc(le16toh(hdr->fname[0]), &filename[0]);
            r50Asc(le16toh(hdr->fname[1]), &filename[3]);
            if ((mount->flags & FS_DOSMTEXT) != 0)
              r50Asc(le16toh(hdr->fname3), &filename[6]);
            filename[9] = '.';
            r50Asc(le16toh(hdr->ext), &filename[10]);
            break;
          }
        }
        strcpy(filename, "Unknown");
        break;
    }

    /*
     * Restore the tape position
     */
    if (tapeSetPosition(mount->container, pos) != 0)
      goto error;

  } else eot = 1;

  if ((bot == 0) && (eot == 0)) {
    unsigned long long position = pos;

    sprintf(temp, "Offset %llu", position);
  }

  printf("%s:\n", mount->name);
  printf("  Position: %s\n",
         bot ? "Beginning of tape" :
           (eot ? "End of tape" : temp));
  if (!eot)
    printf("  Next file: %s[%3o,%3o]\n",
           filename, hdr->proj, hdr->prog);
  return;

 error:
  fprintf(stderr, "info: container file I/O error\n");
  tapeRewind(mount->container);
}

/*++
 *      d o s m t D i r
 *
 *  Produce a full or brief directory listing. After listing the tape will
 *  be positioned at beginning-of-tape. If the '-n' switch is specified,
 *  the directory will be listed from the current tape position.
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
static void dosmtDir(
  struct mountedFS *mount,
  uint8_t UNUSED(unit),
  char *fname
)
{
  struct DOSMTdata *data = &mount->dosmtdata;
  struct dosmtFileSpec spec;
  uint32_t status;

  if (dosmtParseFilespec(mount, fname, &spec, DOSMT_M_NONAME) == 0) {
    fprintf(stderr, "dir: syntax error in file spec \"%s\"\n", fname);
    return;
  }

  /*
   * Position to beginning-of-tape unless the '-n' switch is specified.
   */
  if (!SWISSET('n'))
    tapeRewind(mount->container);

  do {
    switch (status = tapeReadRecord(mount->container, data->buf, DOSMTRCLNT)) {
      case ST_FAIL:
        return;

      case ST_EOM:
        break;

      case ST_TM:
        /* Second tape mark in a row - treat as end-of-media */
        status = ST_EOM;
        break;

      default:
        if ((status & ST_ERROR) == 0) {
          if (status == sizeof(struct dosmthdr)) {
            char filename[14], sdate[12];
            struct dosmthdr *hdr = (struct dosmthdr *)data->buf;
            uint16_t blocks = 0;
            uint32_t length;

            /*
             * Compute the number of blocks in this file.
             */
            do {
              switch (length = tapeReadRecordLength(mount->container)) {
                case ST_FAIL:
                  return;

                case ST_EOM:
                case ST_TM:
                  break;

                default:
                  if ((length & ST_ERROR) == 0)
                    blocks++;
                  break;
              }
            } while ((length != ST_EOM) && (length != ST_TM));

            /*
             * Determine if this file matches the filespec.
             */
            if ((spec.flags & DOSMT_WC_PROJ) == 0)
              if (spec.proj != hdr->proj)
                continue;

            if ((spec.flags & DOSMT_WC_PROG) == 0)
              if (spec.prog != hdr->prog)
                continue;

            if ((spec.flags & DOSMT_WC_NAME) == 0) {
              if ((spec.name[0] != le16toh(hdr->fname[0])) ||
                  (spec.name[1] != le16toh(hdr->fname[1])))
                continue;

              if ((mount->flags & FS_DOSMTEXT) != 0)
                if (spec.name[2] != le16toh(hdr->fname3))
                  continue;
            }

            if ((spec.flags & DOSMT_WC_EXT) == 0)
              if (spec.ext != le16toh(hdr->ext))
                continue;
              
            memset(filename, ' ', sizeof(filename));
            filename[13] = '\0';

            r50Asc(le16toh(hdr->fname[0]), &filename[0]);
            r50Asc(le16toh(hdr->fname[1]), &filename[3]);
            if ((mount->flags & FS_DOSMTEXT) != 0)
              r50Asc(le16toh(hdr->fname3), &filename[6]);
            filename[9] = '.';
            r50Asc(le16toh(hdr->ext), &filename[10]);

            if (SWISSET('f')) {
              dos11Date(le16toh(hdr->date), sdate);

              printf("%s %5d. %s <%03o> [%3o,%3o]\n",
                     filename, blocks, sdate,
                     le16toh(hdr->prot), hdr->proj, hdr->prog);
            } else printf("%s [%3o,%3o]\n", filename, hdr->proj, hdr->prog);
          } else fprintf(stderr, "   *** Unexpected record size\n");
        } else fprintf(stderr, "  *** Directory entry contains error\n");
    }
  } while (status != ST_EOM);

  tapeRewind(mount->container);
}

/*++
 *      d o s m t O p e n F i l e R
 *
 *  Open a DOS-11 magtape file for reading.
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
static void *dosmtOpenFileR(
  struct mountedFS *mount,
  uint8_t UNUSED(unit),
  char *fname
)
{
  struct dosmtOpenFile *file;
  struct dosmtFileSpec spec;

  if (dosmtParseFilespec(mount, fname, &spec, DOSMT_M_NONAME) == 0) {
    fprintf(stderr, "Failed to parse filename \"%s\"\n", fname);
    return NULL;
  }

  if ((file = malloc(sizeof(struct dosmtOpenFile))) != NULL) {
    if (dosmtLookupFile(mount, &spec, file) != 0) {
      file->mode = M_RD;
      file->nextb = DOSMTRCLNT;
      file->tm = 0;
      file->error = 0;
    } else {
      free(file);
      return NULL;
    }
  }
  return file;
}

/*++
 *      d o s m t O p e n F i l e W
 *
 *  Open a DOS-11 magtape file for writing.
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
static void *dosmtOpenFileW(
  struct mountedFS *mount,
  uint8_t UNUSED(unit),
  char *fname,
  off_t UNUSED(size)
)
{
  struct dosmtOpenFile *file;
  struct dosmtFileSpec spec;
  struct dosmthdr hdr;
  struct tm tm;
  time_t now = time(NULL);
  uint16_t today;

  if (dosmtParseFilespec(mount, fname, &spec, DOSMT_M_NONE) == 0) {
    fprintf(stderr, "Failed to parse filename \"%s\"\n", fname);
    return NULL;
  }

  if ((file = malloc(sizeof(struct dosmtOpenFile))) != NULL) {
    /*
     * Compute a suitable year for file creation date. This year will have
     * the same calendar as the current year but will be in the 20th century
     * so that DOS/BATCH-11 will be able to interpret it correctly.
     */
    localtime_r(&now, &tm);
    tm.tm_year -= 28;

    today = ((tm.tm_year - 70) * 1000) + tm.tm_yday + 1;

    hdr.fname[0] = htole16(spec.name[0]);
    hdr.fname[1] = htole16(spec.name[1]);
    if ((mount->flags & FS_DOSMTEXT) != 0)
      hdr.fname3 = htole16(spec.name[2]);
    else hdr.fname3 = 0;
    hdr.ext = htole16(spec.ext);
    hdr.prog = spec.prog;
    hdr.proj = spec.proj;
    hdr.prot = htole16(mount->dosmtdata.prot);
    hdr.date = htole16(today);

    if (tapeWriteRecord(mount->container, &hdr, sizeof(hdr)) == 0) {
      free(file);
      return NULL;
    }

    file->mode = M_WR;
    file->mount = mount;
    file->nextb = 0;
    file->tm = 0;
    file->error = 0;
  }
  return file;
}

/*++
 *      d o s m t F i l e S i z e
 *
 *  Return an estimate of the size of an open DOS-11 magtape file.
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
static off_t dosmtFileSize(
  void *filep
)
{
  struct dosmtOpenFile *file = filep;
  struct mountedFS *mount = file->mount;
  off_t pos = tapeGetPosition(mount->container);
  off_t size = 0;
  uint32_t length;

  /*
   * Compute the length of this file. The estimate may larger than the
   * actual size of the file size don't know the actual EOF position
   * within the last block of the file.
   */
  do {
    switch (length = tapeReadRecordLength(mount->container)) {
      case ST_FAIL:
        size = 0;
        length = ST_EOM;
        /* FALLTHROUGH */

      case ST_EOM:
      case ST_TM:
        break;

      default:
        size += DOSMTRCLNT;
        break;
    }
  } while((length != ST_EOM) && (length != ST_TM));

  tapeSetPosition(mount->container, pos);
  return size;
}

/*++
 *      d o s m t C l o s e F i l e
 *
 *  Close an open DOS-11 magtape file.
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
static void dosmtCloseFile(
  void *filep
)
{
  struct dosmtOpenFile *file = filep;
  struct mountedFS *mount = file->mount;
  struct DOSMTdata *data = &mount->dosmtdata;
  uint32_t status;

  if (file->mode == M_RD) {
    if (file->tm == 0) {
      /*
       * Tape mark not seen, skip to end of current file.
       */
      do {
        if ((status = tapeReadRecordLength(mount->container)) == ST_FAIL) {
          fprintf(stderr,
                  "Error positioning \"%s\", rewinding\n", mount->name);
          tapeRewind(mount->container);
          status = ST_TM;
        }
      } while ((status != ST_EOM) && (status != ST_TM));

      if (status == ST_EOM)
        tapeSkipRecordR(mount->container);
    }
  } else {
    if (file->nextb != 0) {
      /*
       * The are some pending output byte(s), flush a final record to the tape.
       */
      if (tapeWriteRecord(mount->container, file->buf, DOSMTRCLNT) == 0)
        file->error = 1;
    }
    if (tapeWriteTM(mount->container) == 0)
      file->error = 1;

    data->eot = tapeGetPosition(mount->container);

    if (tapeWriteEOM(mount->container, 1) == 0)
      file->error = 1;

    if (file->error != 0) {
      ERROR("Panic: Error writing on \"%s\"\n", mount->name);
      exit(3);
    }
  }

  free(file);
}

/*++
 *      d o s m t R e a d F i l e
 *
 *  Read data from a DOS-11 magtape to a supplied buffer.
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
static size_t dosmtReadFile(
  void *filep,
  void *buf,
  size_t buflen
)
{
  struct dosmtOpenFile *file = filep;
  char *bufr = buf;

  return dosmtReadBytes(file, bufr, buflen);
}

/*++
 *      d o s m t W r i t e F i l e
 *
 *  Write data to a DOS-11 magtape file from a supplied buffer.
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
size_t dosmtWriteFile(
  void *filep,
  void *buf,
  size_t buflen
)
{
  struct dosmtOpenFile *file = filep;
  char *bufw = buf;

  return dosmtWriteBytes(file, bufw, buflen);
}

/*++
 *      d o s m t R e w i n d
 *
 *  Rewind the tape.
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
static void dosmtRewind(
  struct mountedFS *mount
)
{
  tapeRewind(mount->container);
}

/*++
 *      d o s m t E O M
 *
 *  Position the tape just before the final tape mark or end of media marker.
 *  A subsequent write operation will append to the tape. Note that we have
 *  special case an empty container file containg <TM><TM> or <TM><EOM>
 *  where we need to position to the beginning of the container rather than
 *  after the first <TM>.
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
static void dosmtEOM(
  struct mountedFS *mount
)
{
  struct DOSMTdata *data = &mount->dosmtdata;

  if (tapeEOM(mount->container, &data->eot) == 0)
    fprintf(stderr, "eom: Failed to position device\n");
}

/*++
 *      d o s m t S k i p F
 *
 *  Skip forward over a specified number of files. If end-of-media is reached,
 *  the skip operation will terminate early.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      count           - # of files to skip
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
static void dosmtSkipF(
  struct mountedFS *mount,
  unsigned long count
)
{
  if (tapeSkipForward(mount->container, count) == 0)
    fprintf(stderr, "skipf: Failed to position device\n");
}

/*++
 *      d o s m t S k i p R
 *
 *  Skip backwards over a specified number of files. If end-of-media is
 *  reached, the skip operation will terminate early.
 *
 * Inputs:
 *
 *      mount           - pointer to the mounted file system descriptor
 *      count           - # of files to skip
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
static void dosmtSkipR(
  struct mountedFS *mount,
  unsigned long count
)
{
  if (tapeSkipReverse(mount->container, count) == 0)
    fprintf(stderr, "skipf: Failed to position device\n");
}

/*++
 *      d o s m t F S
 *
 *  Descriptor for access DOS/BATCH-11 magtapes.
 *
 --*/
struct FSdef dosmtFS = {
  NULL,
  "dosmt",
  "dosmt            PDP-11 DOS/BATCH-11 magtape access\n",
  FS_TAPE | FS_EMPTYFILE | FS_1OPENFILE,
  0,
  dosmtMount,
  dosmtUmount,
  NULL,
  NULL,
  dosmtSet,
  dosmtInfo,
  dosmtDir,
  dosmtOpenFileR,
  dosmtOpenFileW,
  dosmtFileSize,
  dosmtCloseFile,
  dosmtReadFile,
  dosmtWriteFile,
  NULL,
  dosmtRewind,
  dosmtEOM,
  dosmtSkipF,
  dosmtSkipR
};
