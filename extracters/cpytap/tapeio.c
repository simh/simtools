/* tapeio.c: Tape I/O routines

   Copyright (c) 2017, John Forecast

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

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include "tapeio.h"

char buffer[MAXRCLNT];
int rLength, occupied;

static int verifyFormat(FILE *);

/*++
 *      OpenTapeForRead
 *
 *  Open an existing SIMH .tap format file for read access. If the file is
 *  successfully opened, scan the file to determine if it is a valid .tap
 *  format file and whether there are error records present.
 *
 * Inputs:
 *
 *      handle          - file handle returned here
 *      name            - name of the file
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      TIO_SUCCESS     - file successfully opened, format is valid
 *      TIO_ERROR       - file successfully opened, error records present
 *      TIO_CORRUPT     - file successfully opened, format is invalid
 *      TIO_OPENFAIL    - file open failed
 *
 *      Note the file remains open if the return status is TIO_SUCCESS or
 *      TIO_ERROR
 *
 --*/
int OpenTapeForRead(
  FILE **handle,
  char *name
)
{
  FILE *tfile;

  if ((tfile = fopen(name, "r")) != NULL) {
    int status;

    status = verifyFormat(tfile);
    rewind(tfile);
    if ((status != TIO_SUCCESS) && (status != TIO_ERROR))
      CloseTape(tfile);
    else *handle = tfile;
    return status;
  }
  return TIO_OPENFAIL;
}

/*++
 *      OpenTapeForWrite
 *
 *  Create a new SIMH .tap format fiole for write access. Two tape marks are
 *  written to the file and the file handle is rewound to the beginning of the
 *  file. If the file already exists, and error (TIO_CREATEFAIL) is returned.
 *
 * Inputs:
 *
 *      handle          - file handle returned here
 *      name            - name of the file
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      TIO_SUCCESS     - file successfully created
 *      TIO_IOERROR     - I/O error writing the initial file contents
 *      TIO_CREATEFAIL  - file create failed
 *
 --*/
int OpenTapeForWrite(
  FILE **handle,
  char *name
)
{
  FILE *tfile;

  /*
   * Fail if the file exists
   */
  if (access(name, F_OK) == 0)
    return TIO_CREATEFAIL;

  if ((tfile = fopen(name, "w+")) != NULL) {
    uint32 tm = 0;
    int status = TIO_SUCCESS;

    /*
     * Write 2 tape marks
     */
    if (fwrite(&tm, sizeof(tm), 2, tfile) != 2)
      status = TIO_IOERROR;

    if (status == TIO_SUCCESS) {
      rewind(tfile);
      *handle = tfile;
      return TIO_SUCCESS;
    }

    /*
     * Failed to write the 2 tape marks. Try to delete the file before
     * returning an error.
     */
    CloseTape(tfile);
    unlink(name);
    return TIO_IOERROR;
  }
  return TIO_CREATEFAIL;
}

/*++
 *      OpenTapeForAppend
 *
 *  Open an existing SIMH .tap format file for write access, leaving the
 *  file handle positioned just before the final tape mark. If the file is
 *  successfully opened, scan the file to determine if it is a valid .tap
 *  format file and whether there are error records present.
 *
 * Inputs:
 *
 *      handle          - file handle returned here
 *      name            - name of the file
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      TIO_SUCCESS     - file successfully opened, format is valid
 *      TIO_ERROR       - file successfully opened, error records present
 *      TIO_CORRUPT     - file successfully opened, format is invalid
 *      TIO_OPENFAIL    - file open failed
 *
 *      Note the file remains open if the return status is TIO_SUCCESS or
 *      TIO_ERROR
 *
 --*/
int OpenTapeForAppend(
  FILE **handle,
  char *name
)
{
  FILE *tfile;

  if ((tfile =fopen(name, "r+")) != NULL) {
    int status;

    status = verifyFormat(tfile);
    if ((status != TIO_SUCCESS) && (status != TIO_ERROR))
      CloseTape(tfile);
    else *handle = tfile;
    return status;
  }
  return TIO_OPENFAIL;
}

/*++
 *      CloseTape
 *
 *  If the tape is open, close it.
 *
 * Inputs:
 *
 *      handle          - file handle for the tape
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
void CloseTape(
  FILE *handle
)
{
  if (handle != NULL)
    fclose(handle);
}

/*++
 *      verifyFormat
 *
 *  Verify the format of the SIMH .tap file. If the format is valid, leave
 *  the file handle positioned right before the last tape mark in the file.
 *
 * Inputs:
 *
 *      handle          - file handle for the tape
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      TIO_SUCCESS     - file format is correct
 *      TIO_ERROR       - file format is correct, error records detected
 *      TIO_CORRUPT     - file format is invalid
 *      TIO_IOERROR     - I/O errror while processing file
 *
 --*/
static int verifyFormat(
  FILE *handle
)
{
  int errorCount = 0, tmSeen = 0;
  uint8 meta[4];
  uint32 header, bc;
  off_t position;
  struct stat stat;

  /*
   * Determine the size of the file.
   */
  fstat(fileno(handle), &stat);

  for (;;) {
    position = ftello(handle);

    /*
     * If we are position at the end of file, there is a tape mark missing.
     * Treat it as though there is one present.
     */
    if (position == stat.st_size)
      return TIO_SUCCESS;

    if (fread(meta, sizeof(meta), 1, handle) != 1)
      return TIO_CORRUPT;

    bc = (((unsigned int)meta[3]) << 24) |
         (((unsigned int)meta[2]) << 16) |
         (((unsigned int)meta[1]) << 8) |
         (unsigned int)meta[0];

    switch (bc) {
      case ST_TM:
        if (++tmSeen <= 1)
          break;
        /* Treat second TM in a row as end of medium */
        /* FALLTHROUGH */

      case ST_EOM:
        if (fseek(handle, -sizeof(meta), SEEK_CUR) != 0)
          return TIO_IOERROR;
        return errorCount ? TIO_ERROR : TIO_SUCCESS;

      case ST_GAP:
        break;

      default:
        /*
         * Record descriptor
         */
        tmSeen = 0;

        header = bc;
        if ((bc & ST_ERROR) != 0)
          errorCount++;
        if ((bc & ST_MBZ) != 0)
          return TIO_CORRUPT;

        bc = RECLEN(bc & ST_LENGTH);

        /*
         * Check if we are seeking outside of the file. If so, this is not
         * a .tap container file.
         */
        if ((position + bc + (2 * sizeof(meta))) > (unsigned long long)stat.st_size)
          return TIO_CORRUPT;

        if (fseek(handle, bc, SEEK_CUR) != 0)
          return TIO_CORRUPT;
        if (fread(meta, sizeof(meta), 1, handle) != 1)
          return TIO_CORRUPT;

        bc = (((unsigned int)meta[3]) << 24) |
             (((unsigned int)meta[2]) << 16) |
             (((unsigned int)meta[1]) << 8) |
             (unsigned int)meta[0];
        if (header != bc)
          return TIO_CORRUPT;
    }
  }
}

/*++
 *      ReadTapeRecord
 *
 *  Read the next record from the tape into the specified buffer. If the
 *  buffer is smaller than the record, the entire record will be consumed.
 *
 * Inputs:
 *
 *      handle          - file handle for the tape
 *      buf             - pointer to the buffer to receive the data
 *      len             - length of the buffer
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      ST_EOM          - end of medium detected
 *      ST_TM           - tape mark detected
 *      Other           - record length (including error flag)
 *                        if the buffer is smaller than the record, the
 *                        length returned will be that of the buffer
 *
 --*/
uint32 ReadTapeRecord(
  FILE *handle,
  void *buf,
  int len
)
{
  long pos = ftell(handle);
  uint8 meta[4];
  uint32 bc, erflag, length;

  /*
   * Note: any I/O errors are treated as "end of medium" detection.
   */
  if (fread(meta, sizeof(meta), 1, handle) != 1)
    return ST_EOM;

  bc = (((unsigned int)meta[3]) << 24) |
       (((unsigned int)meta[2]) << 16) |
       (((unsigned int)meta[1]) << 8) |
       (unsigned int)meta[0];

  switch (bc) {
    case ST_EOM:
    case ST_TM:
      return bc;

    default:
      erflag = bc & ST_ERROR;
      bc &= ST_LENGTH;

      length = (uint32)len;
      if (bc < length)
        length = bc;

      if (fread(buf, sizeof(uint8), length, handle) != length)
        return ST_EOM;

      /*
       * Now position the file after this record.
       */
      pos += RECLEN(bc) + (2 * sizeof(meta));
      if (fseek(handle, pos, SEEK_SET) != 0)
        return ST_EOM;

      return erflag | length;
  }
  return ST_EOM;
}

/*++
 *      ReadTapeRecordLength
 *
 *  Get the length of the next record on the tape without actually reading
 *  the data.
 *
 * Inputs:
 *
 *      handle          - file handle for the tape
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      ST_EOM          - end of medium detected
 *      ST_TM           - tape mark detected
 *      Other           - record length (including error flag)
 *                        if the buffer is smaller than the record, the
 *                        length returned will be that of the buffer
  *
  --*/
uint32 ReadTapeRecordLength(
  FILE *handle
)
 {
   long pos = ftell(handle);
   uint8 meta[4];
   uint32 bc, erflag;

   /*
    * Note: any I/O errors are treated as "end of medium" detection.
    */
   if (fread(meta, sizeof(meta), 1, handle) != 1)
     return ST_EOM;

   bc = (((unsigned int)meta[3]) << 24) |
        (((unsigned int)meta[2]) << 16) |
        (((unsigned int)meta[1]) << 8) |
        (unsigned int)meta[0];

   switch (bc) {
     case ST_EOM:
     case ST_TM:
       return bc;

     default:
       erflag = bc & ST_ERROR;
       bc &= ST_LENGTH;

       /*
        * Now position the file after this record.
        */
       pos += RECLEN(bc) + (2 * sizeof(meta));
       if (fseek(handle, pos, SEEK_SET) != 0)
         return ST_EOM;

       return erflag | bc;
   }
   return ST_EOM;
 }

/*++
 *      WriteTapeRecord
 *
 *  Write a record to the tape at it's current position.
 *
 * Inputs:
 *
 *      handle          - file handle for the tape
 *      buf             - pointer to the buffer to be written
 *      len             - length of the buffer
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      0 if record was written successfully, -1 if write failed
 *
 --*/
int WriteTapeRecord(
  FILE *handle,
  void *buf,
  int len
)
{
  uint8 meta[4];
  int datalen;

  meta[0] = len & 0xFF;
  meta[1] = (len >> 8) & 0xFF;
  meta[2] = (len >> 16) & 0xFF;
  meta[3] = (len >> 24) & 0xFF;

  datalen = ((len + 1)  & ST_LENGTH) & ~1;

  if (fwrite(meta, sizeof(meta), 1, handle) != 1)
    return -1;

  if (fwrite(buf, datalen, 1, handle) != 1)
    return -1;

  if (fwrite(meta, sizeof(meta), 1, handle) != 1)
    return -1;

  return 0;
}

/*++
 *      SkipToNextTapeMark
 *
 *  Skip forward to the next tape mark and position the file just past the
 *  tape mark.
 *
 * Inputs:
 *
 *      handle          - file handle for the tape
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      ST_EOM          - end of medium detected
 *      ST_TM           - tape mark detected
 *
 --*/
unsigned int SkipToNextTapeMark(
  FILE *handle
)
{
  long pos = ftell(handle);
  uint8 meta[4];
  uint32 bc;

  for (;;) {
    /*
     * Note: any I/O errors are treated as "end of medium" detection.
     */
    if (fread(meta, sizeof(meta), 1, handle) != 1)
      return ST_EOM;

    bc = (((unsigned int)meta[3]) << 24) |
         (((unsigned int)meta[2]) << 16) |
         (((unsigned int)meta[1]) << 8) |
         (unsigned int)meta[0];

    switch (bc) {
      case ST_EOM:
      case ST_TM:
        return bc;

      default:
        bc &= ST_LENGTH;

        /*
         * Now position the file after this record.
         */
        pos += RECLEN(bc) + (2 * sizeof(meta));
        if (fseek(handle, pos, SEEK_SET) != 0)
          return ST_EOM;
        break;
    }
  }
}

/*++
 *      WriteTapeMark
 *
 *  Write a tape mark to the tape at it's current position and, optionally,
 *  backup to before the tape mark.
 *
 * Inputs:
 *
 *      handle          - file handle for the tape
 *      backup          - if 1, reposition the tape to before the tape mark
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      0 if tape mark was written successfully, -1 if write failed
 *
 --*/
int WriteTapeMark(
  FILE *handle,
  int backup
)
{
  uint32 tm = 0;

  if (fwrite(&tm, sizeof(tm), 1, handle) != 1)
    return -1;

  if (backup)
    if (fseek(handle, -sizeof(tm), SEEK_CUR) != 0)
      return -1;

  return 0;
}

/*++
 *      initTapeBuffering
 *
 *  Initialize variables for writes to tape for ASCII mode transfers
 *  (translates LF -> CRLF).
 *
 * Inputs:
 *
 *      reclen          - size of the tape record buffer to use
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
void initTapeBuffering(
  int reclen
)
{
  rLength = reclen;
  occupied = 0;
}

/*++
 *      flushTapeBuffering
 *
 *  Flush any pending data out to the current tape.
 *
 * Inputs:
 *
 *      handle          - file handle for the tape
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      0 if data was successfully flushed, -1 if write failed
 *
 --*/
int flushTapeBuffering(
  FILE *handle
)
{
  uint32 count = occupied;

  occupied = 0;

  if (count != 0)
    return WriteTapeRecord(handle, buffer, count);

  return 0;
}

/*++
 *      writeTapeBuffering
 *
 *  Write a character to the current tape, buffering the data into records.
 *
 * Inputs:
 *
 *      handle          - file handle for the tape
 *      ch              - the character to be output
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      0 if character was successfully buffered or written to tape, -1 if
 *      write failed
 *
 --*/
int writeTapeBuffering(
  FILE *handle,
  char ch
)
{
  buffer[occupied++] = ch;

  if (occupied == rLength) {
    occupied = 0;
    return WriteTapeRecord(handle, buffer, rLength);
  }
  return 0;
}
