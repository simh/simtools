/* dos11.c: Handle DOS/BATCH-11 tape file structure operations

   Copyright (c) 2015, John Forecast

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   JOHN FORECAST BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of John Forecast shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from John Forecast.

*/

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "dos11.h"
#include "tapeio.h"
/*
 * Record buffer.
 */
char record[MAXRCLNT];

FILE *file = NULL;

int CRpending = 0;

char rad50[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789";

/*
 * Table of days/month for both normal and leap years.
 */
unsigned short mnth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
unsigned short mnthl[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

char *month[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/*++
 *      r50Asc
 *
 *  Convert 1 16-bit rad50 value into 3 ASCII characters.
 *
 * Inputs:
 *
 *      value           - rad50 value to be converted
 *      outbuf          - pointer to buffer to receive the characters
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
static void r50Asc(
  uint16 value,
  char *outbuf
)
{
  outbuf[2] = rad50[value % 050];
  value /= 050;
  outbuf[1] = rad50[value % 050];
  outbuf[0] = rad50[value / 050];
}

/*++
 *      r50AscNoSpace
 *
 *  Convert 1 16-bit value into up to 3 ASCII characters, spaces are dropped
 *  from the conversion.
 *
 * Inputs:
 *
 *      value           - rad50 value to be converted
 *      outbuf          - pointer to the buffer to receive the characters
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      # of none space characters converted
 *
 --*/
static int r50AscNoSpace(
  uint16 value,
  char *outbuf
)
{
  int count = 0;
  int value2 = value / 050;

  /*
   * The rad50 representation of ' ' is zero.
   */
  if ((value2 / 050) != 0)
    outbuf[count++] = rad50[value2 / 050];
  if ((value2 % 050) != 0)
    outbuf[count++] = rad50[value2 % 050];
  if ((value % 050) != 0)
    outbuf[count++] = rad50[value % 050];

  return count;
}

/*++
 *      AscR50
 *
 *  Converts 3 characters into a single 16-bit rad50 value. If an input
 *  character is not in the rad50 character set it is converted to '%'.
 *
 * Inputs:
 *
 *      inbuf           - pointer to the buffer with the 3 characters
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      rad50 value for the 3 characters
 *
 --*/
static uint16 AscR50(
  char *inbuf
)
{
  uint16 value;
  char *ptr;

  if ((ptr = strchr(rad50, toupper(*inbuf++))) == NULL)
    ptr = strchr(rad50, '%');

  value = (ptr - rad50) * 03100;

  if ((ptr = strchr(rad50, toupper(*inbuf++))) == NULL)
    ptr = strchr(rad50, '%');

  value += (ptr - rad50) * 050;

  if ((ptr = strchr(rad50, toupper(*inbuf))) == NULL)
    ptr = strchr(rad50, '%');

  value += ptr - rad50;

  return value;
}

/*++
 *      dos11Date
 *
 *  Convert a DOS/BATCH-11 date value into an ASCII string.
 *
 * Inputs:
 *
 *      value           - DOS/BATCH-11 date value
 *      buf             - buffer to receive the string (requires 12 bytes)
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
static void dos11Date(
  uint16 value,
  char *buf
)
{
  unsigned short year, doyr, leapyr;
  unsigned short *table;

  /*
   * The DOS/BATCH-11 date format covers a range of 1970 - 2035.
   */
  year = 1970 + value / 1000;
  doyr = value % 1000;
  leapyr = ((year % 4) == 0) && (year != 2000);

  table = leapyr ? mnthl : mnth;

  /*
   * Check for valid day of year.
   */
  if (doyr < (leapyr ? 366 : 365)) {
    int i = 0;

    while (doyr > table[i])
      doyr -= table[i++];

    sprintf(buf, "%2d-%s-%4d", doyr, month[i], year);
  } else strcpy(buf, "xx-yyy-xxxx");
}

/*++
 *      initAscii
 *
 *  Initialize variables for ASCII mode transfers (translates CRLF -> LF).
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
 *      None
 *
 --*/
static void initAscii(void)
{
  CRpending = 0;
}

/*++
 *      xferAscii
 *
 *  Write data to the current output file while performing CRLF translation.
 *
 * Inputs:
 *
 *      ch              - the next character to be output to the file
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      # of errors returned by fwrite
 *
 --*/
static char cr = '\r';

static int xferAscii(
  char ch
)
{
  int count = 0;

  if (ch == '\r') {
    if (CRpending != 0)
      if (fwrite(&cr, sizeof(char), 1, file) != 1)
        count++;
    CRpending = 1;
    return count;
  }

  if (CRpending != 0) {
    CRpending = 0;
    if (ch != '\n')
      if (fwrite(&cr, sizeof(char), 1,  file) != 1)
        count++;
  }
  if (fwrite(&ch, sizeof(ch), 1, file) != 1)
    count++;

  return count;
}

/*++
 *      flushAscii
 *
 *  Flush any pending CR to the output file
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
 *      # of errors returned by fwrite (0 or 1)
 *
 --*/
static int flushAscii(void)
{
  if (CRpending != 0)
    if (fwrite(&cr, sizeof(char), 1, file) != 1)
      return 1;

  return 0;
}

/*++
 *      appendFile
 *
 *  Append a file to the current open tape.
 *
 * Inputs:
 *
 *      name            - pointer to the filename string to append
 *      ascii           - pointer to string containing extensions whose files
 *                        are to be appended in ASCII mode.
 *                          e.g. ".MAC:.BAT" or ".MAC.BAT"
 *      prog            - programmer number for the directory entry
 *      proj            - project number for the directory entry
 *      reclen          - record length to use on the 'tape'
 *      strict          - if 1, use strict DOS/BATCH-11 format file headers
 *                        i.e. dos11hdr1
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *       0 if file successfully appended,
 *       1 if failed to open the input file
 *      -1 if some data may have been written to the tape which is now corrupt
 *
 --*/
int appendFile(
  char *name,
  char *ascii,
  uint8 prog,
  uint8 proj,
  int reclen,
  int strict
)
{
  size_t hdrSz = strict ? sizeof(struct dos11hdr1) : sizeof(struct dos11hdr2);
  struct dos11hdr2 hdr;
  char *fname, *ext, *useAscii;
  time_t now;
  struct tm *tm;

  memset(&hdr, 0, sizeof(hdr));

  if ((file = fopen(name, "r")) != NULL) {
    char filename[16], extension[8], exten[8];
    int i, len;
    size_t datalen;

    /*
     * We now need to convert the supplied filename into something that
     * fits into the DOS/BATCH-11 file header structure(s).
     */
    if ((fname = strrchr(name, '/')) == NULL)
      fname = name;

    ext = strrchr(fname, '.');

    /*
     * Fill int the first 'n' bytes of the filename and extension with the
     * remaining bytes set to ' '.
     */
    memset(&filename, ' ', sizeof(filename));
    memset(&extension, ' ', sizeof(extension));
    memset(&exten, 0, sizeof(exten));

    if (ext != NULL)
      len = ext - fname;
    else len = strlen(fname);

    if (len > (strict ? 6 : 9))
      len = strict ? 6 : 9;

    for (i = 0; i < len; i++)
      filename[i] = toupper(fname[i]);

    if (ext != NULL) {
      len = strlen(&ext[1]);
      if (len > 3)
        len = 3;

      for (i = 0; i < len + 1; i++) {
        extension[i] = toupper(ext[i]);
        exten[i] = toupper(ext[i]);
      }
    }

    /*
     * Construct the directory entry
     */
    hdr.fname[0] = AscR50(&filename[0]);
    hdr.fname[1] = AscR50(&filename[3]);
    if (!strict)
      hdr.fname3 = AscR50(&filename[6]);
    if (ext != NULL)
      hdr.ext = AscR50(&extension[1]);

    hdr.proj = proj;
    hdr.prog = prog;
    hdr.prot = 0233;

    now = time(NULL);
    tm = localtime(&now);

    hdr.date = ((tm->tm_year - 70) * 1000) + tm->tm_yday + 1;

    useAscii = strstr(ascii, exten);

    if (WriteTapeRecord(&hdr, hdrSz) == 0) {
      initTapeBuffering(reclen);
      while ((datalen = fread(record, sizeof(char), reclen, file)) != 0) {
        if (ferror(file))
          goto failed;

        if (useAscii != NULL) {
          size_t j;

          for (j = 0; j < datalen; i++) {
            if (record[j] == '\n')
              if (writeTapeBuffering('\r') != 0)
                goto failed;
            if (writeTapeBuffering(record[j]) != 0)
              goto failed;
          }
        } else {
          if (WriteTapeRecord(record, datalen) != 0)
            goto failed;
        }
      }
      if (flushTapeBuffering() != 0)
        goto failed;

      if ((WriteTapeMark(0) == 0) && (WriteTapeMark(1) == 0)) {
        fclose(file);
        return 0;
      }
    }
  failed:
    fclose(file);
    return -1;
  }
  return 1;
}

/*++
 *      extractFiles
 *
 *  Extract files from the current open tape to the current working
 *  directory.
 *
 * Inputs:
 *
 *      ascii           - pointer to string containing extensions whose files
 *                        are to be extracted in ASCII mode.
 *                          e.g. ".MAC:.BAT" or ".MAC.BAT"
 *      ppn             - if non-zero, append proj, prog numbers to the name:
 *                              filename_prog_proj.ext
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
void extractFiles(
  char *ascii,
  int ppn
)
{
  unsigned int status;

  do {
    switch (status = ReadTapeRecord(record, sizeof(record))) {
      case ST_EOM:
        break;

      case ST_TM:
        /* Second tape mark in a row - treat as end of medium */
        status = ST_EOM;
        break;

      default:
        if ((status & ST_ERROR) == 0) {
          status &= ST_LENGTH;

          if ((status == sizeof(struct dos11hdr1)) ||
              (status == sizeof(struct dos11hdr2))) {
            char filename[32];
            struct dos11hdr2 *hdr = (struct dos11hdr2 *)record;
            int offset = 0, extension;
            char *useAscii;
            uint32 errorCount = 0, length, i;

            /*
             * Construct the output filename
             */
            offset += r50AscNoSpace(hdr->fname[0], &filename[offset]);
            offset += r50AscNoSpace(hdr->fname[1], &filename[offset]);
            if (status == sizeof(struct dos11hdr2))
              offset += r50AscNoSpace(hdr->fname3, &filename[offset]);
            if (ppn) {
              sprintf(&filename[offset], "_%o_%o", hdr->prog, hdr->proj);
              offset = strlen(filename);
            }
            extension = offset;
            filename[offset++] = '.';
            offset += r50AscNoSpace(hdr->ext, &filename[offset]);
            filename[offset++] = '\0';

            useAscii = strstr(ascii, &filename[extension]);

            printf("   Extracting: %s ", filename);

            initAscii();

            if ((file = fopen(filename, "w")) != NULL) {
              do {
                switch (status = ReadTapeRecord(record, sizeof(record))) {
                  case ST_EOM:
                  case ST_TM:
                    break;

                  default:
                    if ((status & ST_ERROR) != 0)
                      errorCount++;
                    length = status & ST_LENGTH;

                    if (useAscii != NULL) {
                      /*
                       * Process each character separately in order to map
                       * CRLF -> LF.
                       */
                      for (i = 0; i < length; i++)
                        xferAscii(record[i]);
                    } else {
                      if (fwrite(record, sizeof(char), length, file) != length)
                        errorCount++;
                    }
                }
              } while ((status != ST_EOM) && (status != ST_TM));
              flushAscii();
              printf("%s \n", errorCount ? "Errors detected" : "Done");
              fclose(file);
              break;
            } else printf("*** File open failure\n");
          } else fprintf(stderr, "   *** Unexpected record size\n");
        } else fprintf(stderr, "   *** Directory entry contains error\n");

        /*
         * Scan forward to the end of this file
         */
        do {
          status = ReadTapeRecordLength();
        } while ((status != ST_EOM) && (status != ST_TM));
    }
  } while (status != ST_EOM);
}

/*++
 *      listDirectory
 *
 *  List the directory from the current open tape to stdout.
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
 *      None
 *
 --*/
void listDirectory(void)
{
  unsigned int status;

  do {
    switch (status = ReadTapeRecord(record, sizeof(record))) {
      case ST_EOM:
        break;

      case ST_TM:
        /* Second tape mark in a row - treat as end of medium */
        status = ST_EOM;
        break;

      default:
        if ((status & ST_ERROR) == 0) {
          status &= ST_LENGTH;

          if ((status == sizeof(struct dos11hdr1)) ||
              (status == sizeof(struct dos11hdr2))) {
            char filename[14], sdate[12];
            struct dos11hdr2 *hdr = (struct dos11hdr2 *)record;
            uint32 errorCount = 0, length = 0;

            memset(filename, ' ', sizeof(filename));
            filename[13] = '\0';

            r50Asc(hdr->fname[0], &filename[0]);
            r50Asc(hdr->fname[1], &filename[3]);
            if (status == sizeof(struct dos11hdr2))
              r50Asc(hdr->fname3, &filename[6]);
            filename[9] = '.';
            r50Asc(hdr->ext, &filename[10]);

            dos11Date(hdr->date, sdate);

            printf("%s %s <%03o> [%3o,%3o] ",
                   filename, sdate, hdr->prot, hdr->prog, hdr->proj);

            do {
              switch (status = ReadTapeRecordLength()) {
                case ST_EOM:
                case ST_TM:
                  printf("%u bytes%s\n", length, errorCount ? "(E)" : "");
                  break;

                default:
                  if ((status & ST_ERROR) != 0)
                    errorCount++;
                  length += status & ST_LENGTH;
                  break;
              }
            } while ((status != ST_EOM) && (status != ST_TM));
            break;
          } else fprintf(stderr, "   *** Unexpected record size\n");
        } else fprintf(stderr, "   *** Directory entry contains error\n");

        /*
         * Scan forward to the end of this file
         */
        do {
          status = ReadTapeRecordLength();
        } while ((status != ST_EOM) && (status != ST_TM));
    }
  } while (status != ST_EOM);
}
