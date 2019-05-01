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
 * Support routines for reading/writing SIMH tape container files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>

#include "fsio.h"

/*++
 *      t a p e V e r i f y
 *
 *  Verify that the container format is valid, leaving the tape positioned
 *  at beginning-of-tape.
 *
 * Inputs:
 *
 *      container       - pointer open container file
 *      eot             - return end-of-tape info here, NUL if not needed
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *       1 if format is valid, 0 otherwise
 *
 --*/
int tapeVerify(
  FILE *container,
  off_t *eot
)
{
  int errorCount = 0, tmSeen = 0;
  uint32_t meta, header, bc;
  off_t position;
  struct stat stat;

  /*
   * Determine the size of the file.
   */
  fstat(fileno(container), &stat);

  for (;;) {
    position = ftello(container);

    /*
     * If we are positioned at the end-of-file, there is a tape mark or
     * end-of-media marker missing. Treat it as though one is present.
     */
    if (position == stat.st_size)
      break;

    if (fread(&meta, sizeof(meta), 1, container) != 1)
      return 0;

    bc = le32toh(meta);

    switch (bc) {
      case ST_TM:
        if (++tmSeen <= 1)
          break;
        /* Treat second TM in a row as end of medium */
        /* FALLTHROUGH */

      case ST_EOM:
        if (fseeko(container, -sizeof(meta), SEEK_CUR) != 0)
          return 0;

        if (errorCount)
          printf("mount: Tape contains error records\n");
        goto done;
        
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
          return 0;

        bc = RECLEN(bc & ST_LENGTH);

        /*
         * Check if we are seeking ouside of the file. If so, this is not
         * a .tap container file.
         */
        if ((position + bc + (2 * sizeof(meta))) > (unsigned long long)stat.st_size)
          return 0;

        if (fseeko(container, bc, SEEK_CUR) != 0)
          return 0;
        if (fread(&meta, sizeof(meta), 1, container) != 1)
          return 0;

        bc = le32toh(meta);
        
        if (header != bc)
          return 0;
    }
  }
 done:
  if (eot != NULL)
    *eot = ftello(container);

  /*
   * Position at beginning-of-tape.
   */
  if (fseeko(container, 0, SEEK_SET) == 0)
    return 1;
  return 0;
}

/*++
 *      t a p e G e t P o s i t i o n
 *
 *  Get the current position of the tape.
 *
 * Inputs:
 *
 *      container       - pointer open container file
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      Current position of the tape
 *
 --*/
off_t tapeGetPosition(
  FILE *container
)
{
  return ftello(container);
}

/*++
 *      t a p e S e t P o s i t i o n
 *
 *  Position the tape to a position previously obtained by tapeGetPosition().
 *
 * Inputs:
 *
 *      container       - pointer open container file
 *      pos             - requested position
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if successfully positioned, 0 if error
 *
 --*/
int tapeSetPosition(
  FILE *container,
  off_t pos
)
{
  return fseeko(container, pos, SEEK_SET) == 0 ? 1 : 0;
}

/*++
 *      t a p e S k i p R e c o r d F
 *
 *  Skip over the next record in the forward direction.
 *
 * Inputs:
 *
 *      container       - pointer open container file
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      ST_FAIL         - error accessing container file
 *      ST_EOM          - end of media detected
 *      ST_TM           - tape mark detected
 *      Other           - record length (including error flag)
 *
 --*/
uint32_t tapeSkipRecordF(
  FILE *container
)
{
  return tapeReadRecordLength(container);
}

/*++
 *      t a p e S k i p R e c o r d R
 *
 *  Skip over the next record in the reverse direction.
 *
 * Inputs:
 *
 *      container       - pointer open container file
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      ST_FAIL         - error accessing container file
 *      ST_EOM          - end of media detected
 *      ST_TM           - tape mark detected
 *      Other           - record length (including error flag)
 *
 --*/
uint32_t tapeSkipRecordR(
  FILE *container
)
{
  return tapeReadRecordLengthReverse(container);
}

/*++
 *      t a p e P e e k R e c o r d L e n g t h
 *
 *  Get the length of the next record on the tape without actually reading
 *  the data or changing the current position of the tape.
 *
 * Inputs:
 *
 *      container       - pointer open container file
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      ST_FAIL         - error accessing the container file
 *      ST_EOM          - end of media detected
 *      ST_TM           - tape mark detected
 *      Other           - record length (including error flag)
 *
 --*/
uint32_t tapePeekRecordLength(
  FILE *container
)
{
  off_t pos = ftello(container);
  uint32_t meta;

  if ((fread(&meta, sizeof(meta), 1, container) != 1) ||
      (fseeko(container, pos, SEEK_SET) != 0))
    return ST_FAIL;

  return le32toh(meta);
}

/*++
 *      t a p e R e a d R e c o r d
 *
 *  Read the next record from the tape into the specified buffer. If the
 *  buffer is smaller than the record, the entire record will be consumed,
 *  losing data.
 *
 * Inputs:
 *
 *      container       - pointer open container file
 *      buf             - pointer to the buffer to receive the data
 *      len             - length of the buffer
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      ST_FAIL         - error accessing the container file
 *      ST_EOM          - end of media detected
 *      ST_TM           - tape mark detected
 *      Other           - record length (including error flag)
 *                        if the buffer is smaller than the record, the
 *                        length returned will be that of the buffer
 *
 --*/
uint32_t tapeReadRecord(
  FILE *container,
  void *buf,
  int len
)
{
  off_t pos = ftello(container);
  uint32_t meta, bc, erflag, length;

  if (fread(&meta, sizeof(meta), 1, container) != 1)
    return ST_FAIL;

  bc = le32toh(meta);

  switch (bc) {
    case ST_EOM:
    case ST_TM:
      return bc;

    default:
      erflag = bc & ST_ERROR;
      bc &= ST_LENGTH;

      length = (uint32_t)len;
      if (bc < length)
        length = bc;

      if (fread(buf, sizeof(uint8_t), length, container) != length)
        return ST_FAIL;

      /*
       * Now position the file after this record.
       */
      pos += RECLEN(bc) + (2 * sizeof(meta));
      if (fseeko(container, pos, SEEK_SET) != 0)
        return ST_FAIL;

      return erflag | length;
  }
  return ST_FAIL;
}

/*++
 *      t a p e R e a d R e c o r d L e n g t h
 *
 *  Get the length of the next record on the tape without actually reading
 *  the data. The tape will be positioned at the start of the next record.
 *
 * Inputs:
 *
 *      container       - pointer open container file
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      ST_FAIL         - error accessing container file
 *      ST_EOM          - end of media detected
 *      ST_TM           - tape mark detected
 *      Other           - record length (including error flag)
 *
 --*/
uint32_t tapeReadRecordLength(
  FILE *container
)
{
  off_t pos = ftello(container);
  uint32_t meta, bc, erflag;

  if (fread(&meta, sizeof(meta), 1, container) != 1)
    return ST_FAIL;

  bc = le32toh(meta);

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
      if (fseeko(container, pos, SEEK_SET) != 0)
        return ST_FAIL;

      return erflag | bc;
  }
  return ST_FAIL;
}

/*++
 *      t a p e R e a d R e c o r d L e n g t h R e v e r s e
 *
 *  Get the length of the previous record on the tape without actually reading
 *  the data. The tape will be positioned at the start of the previous record.
 *
 * Inputs:
 *
 *      container       - pointer open container file
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      ST_FAIL         - error accessing container file
 *      ST_EOM          - end of media detected
 *      ST_TM           - tape mark detected
 *      Other           - record length (including error flag)
 *
 --*/
uint32_t tapeReadRecordLengthReverse(
  FILE *container
)
{
  uint32_t meta, bc, erflag;
  off_t delta;

  if ((fseeko(container, -sizeof(meta), SEEK_CUR) != 0) ||
      (fread(&meta, sizeof(meta), 1, container) != 1))
    return ST_FAIL;

  bc = le32toh(meta);

  switch (bc) {
    case ST_EOM:
    case ST_TM:
      if (fseeko(container, -sizeof(meta), SEEK_CUR) != 0)
        return ST_FAIL;
      return bc;

    default:
      erflag = bc & ST_ERROR;
      bc &= ST_LENGTH;

      /*
       * Now position the file before this record.
       */
      delta = RECLEN(bc) + (2 * sizeof(meta));
      if (fseeko(container, -delta, SEEK_CUR) != 0)
        return ST_FAIL;

      return erflag | bc;
  }
  return ST_FAIL;
}

/*++
 *      t a p e W r i t e R e c o r d
 *
 *  Write a record to the tape at it's current position leaving the tape
 *  positioned after the newly written record.
 *
 * Inputs:
 *
 *      container       - pointer open container file
 *      buf             - pointer to the record to be written
 *      len             - length of the record
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if record was successfully written, 0 otherwise
 *
 --*/
int tapeWriteRecord(
  FILE *container,
  void *buf,
  int len
)
{
  uint32_t meta = htole16(len);
  int datalen = (len + 1) & ~1;

  if ((fwrite(&meta, sizeof(meta), 1, container) != 1) ||
      (fwrite(buf, datalen, 1, container) != 1) ||
      (fwrite(&meta, sizeof(meta), 1, container) != 1))
    return 0;

  return 1;
}

/*++
 *      t a p e W r i t e E O M
 *
 *  Write an end-of-media record to the tape at it's current position and,
 *  optionally, backup the tape to before the newly written record.
 *
 * Inputs:
 *
 *      container       - pointer open container file
 *      backup          - if 1, position the tape before the new record
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if EOM record successfully written, 0 otherwise
 *
 --*/
int tapeWriteEOM(
  FILE *container,
  int backup
)
{
  uint32_t eom = htole32(ST_EOM);

  if (fwrite(&eom, sizeof(eom), 1, container) != 1)
    return 0;

  if (backup)
    if (fseeko(container, -sizeof(eom), SEEK_CUR) != 0)
      return 0;

  return 1;
}

/*++
 *      t a p e W r i t e T M
 *
 *  Write a tape mark record to the tape at it's current position.
 *
 * Inputs:
 *
 *      container       - pointer open container file
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if EOM record successfully written, 0 otherwise
 *
 --*/
int tapeWriteTM(
  FILE *container
)
{
  uint32_t tm = htole32(ST_TM);

  if (fwrite(&tm, sizeof(tm), 1, container) != 1)
    return 0;

  return 1;
}

/*++
 *      t a p e E O M
 *
 *  Position the tape to the end of media so that a subsequent write will
 *  append a file to the tape.
 *
 * Inputs:
 *
 *      container       - pointer open container file
 *      eot             - pointer to end-of-tape position, NULL if
 *                        not available
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if tape successfully positioned, 0 otherwise
 *
 --*/
int tapeEOM(
  FILE *container,
  off_t *eot
)
{
  int whence = eot == NULL ? SEEK_END : SEEK_SET;
  off_t pos = eot == NULL ? 0 : *eot;
  uint32_t bc1, bc2;

  /*
   * Move to the end of the tape and then look backwards to see how it is
   * terminated.
   */
  if (fseeko(container, pos, whence) == 0) {
    if (ftello(container) == 0) {
      /*
       * Empty file, we are correctly positioned.
       */
      return 1;
    }

    if ((bc1 = tapeReadRecordLengthReverse(container)) == ST_FAIL)
      return 0;

    if ((bc1 == ST_EOM) || (bc1 == ST_TM)) {
      if (ftello(container) == 0) {
        /*
         * Only ST_EOM or ST_TM present, we are correctly positioned.
         */
        return 1;
      }

      if ((bc2 = tapeReadRecordLengthReverse(container)) == ST_FAIL)
        return 0;

      if (bc2 == ST_TM) {
        if (ftello(container) == 0) {
          /*
           * Only ST_TM followed by ST_TM or ST_EOM, we are correctly
           * positioned
           */
          return 1;
        }
      
        /*
         * ST_TM followed by ST_TM or ST_EOM with at least one data block
         * present, skip over the initial ST_TM.
         */
        if (fseeko(container, sizeof(uint32_t), SEEK_CUR) == 0)
          return 1;
      } else {
        /*
         * Only a single ST_TM at the end of the container file. This
         * indicates that there is a missing ST_TM or ST_EOM. Position
         * the tape at the logical end-of-tape so that any subsequent file
         * write will fix the problem.
         */
        if (fseeko(container, pos, whence) == 0)
          return 1;
      }
    }
  }
  return 0;
}

/*++
 *      t a p e R e w i n d
 *
 *  Rewind the tape.
 *
 * Inputs:
 *
 *      container       - pointer open container file
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
void tapeRewind(
  FILE *container
)
{
  fseeko(container, 0, SEEK_SET);
}

/*++
 *      t a p e S k i p F o r w a r d
 *
 *  Skip forward over a number of files. If end-of-media is reached, the skip
 *  operation will terminate early.
 *
 * Inputs:
 *
 *      container       - pointer open container file
 *      count           - # of files to skip
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if skip was successful, 0 otherwise
 *
 --*/
int tapeSkipForward(
  FILE *container,
  unsigned long count
)
{
  unsigned long i;
  uint32_t bc;

  for (i = 0; i < count; i++) {
    /*
     * Peek at the next record. If it's a tape mark or end-of-media there
     * are not more files to skip.
     */
    switch (tapePeekRecordLength(container)) {
      case ST_FAIL:
        return 0;

      case ST_TM:
      case ST_EOM:
        return 1;
    }
    /*
     * Skip forward over 1 file.
     */
    do {
      switch (bc = tapeReadRecordLength(container)) {
        case ST_FAIL:
          return 0;

        case ST_EOM:
          if (fseeko(container, -sizeof(uint32_t), SEEK_CUR) != 0)
            return 0;
          break;
      }
    } while ((bc != ST_TM) && (bc != ST_EOM));
  }
  return 1;
}

/*++
 *      t a p e S k i p R e v e r s e
 *
 *  Skip backwards over a number of files. If beginning-of-tape is reached,
 *  the skip operation will terminate early.
 *
 * Inputs:
 *
 *      container       - pointer open container file
 *      count           - # of files to skip
 *
 * Outputs:
 *
 *      None
 *
 * Returns:
 *
 *      1 if skip was successful, 0 otherwise
 *
 --*/
int tapeSkipReverse(
  FILE *container,
  unsigned long count
)
{
  unsigned long i;
  uint32_t bc;

  for (i = 0; i < count; i++) {
    /*
     * If we are at beginning-of-tape, there are no more files to skip.
     */
    if (ftello(container) == 0)
      return 1;

    /*
     * If we are not at the beginning of tape, the previous record should
     * be a tape mark.
     */
    if (tapeReadRecordLengthReverse(container) != ST_TM)
      return 0;

    /*
     * Now skip over the remainder of the file.
     */
    do {
      if ((bc = tapeReadRecordLengthReverse(container)) == ST_FAIL)
        return 0;
    } while ((bc != ST_TM) && (ftello(container) != 0));

    /*
     * Skip over the tape mark since it marks the end of the previous file.
     */
    if (bc == ST_TM) {
      if (fseeko(container, sizeof(uint32_t), SEEK_CUR) != 0)
        return 0;
    }
  }
  return 1;
}

