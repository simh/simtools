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
 * Routines/data which are common to multiple DEC file systems.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>

#include "fsio.h"

char rad50[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789";

char *month[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/*
 * Table of days/month for both normal and leap years.
 */
unsigned short mnth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
unsigned short mnthl[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

/*
 * ASCII character set
 */
char *Ascii[128] = {
  "NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL",
  " BS", " HT", " LF", " VT", " FF", " CR", " SO", " SI",
  "DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB",
  "CAN", " EM", "SUB", "ESC", " FS", " GS", " RS", " US",
  "   ", " ! ", " \" ", " # ", " $ ", " % ", " & ", " \' ",
  " ( ", " ) ", " * ", " + ", " , ", " - ", " . ", " / ",
  " 0 ", " 1 ", " 2 ", " 3 ", " 4 ", " 5 ", " 6 ", " 7 ",
  " 8 ", " 9 ", " : ", " ; ", " < ", " = ", " > ", " ? ",
  " @ ", " A ", " B ", " C ", " D ", " E ", " F ", " G ",
  " H ", " I ", " J ", " K ", " L ", " M ", " N ", " O ",
  " P ", " Q ", " R ", " S ", " T ", " U ", " V ", " W ",
  " X ", " Y ", " Z ", " [ ", " \\ ", " ] ", " ^ ", " _ ",
  " ` ", " a ", " b ", " c ", " d ", " e ", " f ", " g ",
  " h ", " i ", " j ", " k ", " l ", " m ", " n ", " o ",
  " p ", " q ", " r ", " s ", " t ", " u ", " v ", " w ",
  " x ", " y ", " z ", " { ", " | ", " } ", " ~ ", "DEL"
};

/*++
 *      r 5 0 A s c
 *
 *  Convert a 16-bit rad50 value into 3 ASCII characters.
 *
 * Inputs:
 *
 *      value           - rad50 value to be converted
 *      outbuf          - pointer to 3 character buffer to receive the
 *                        converted output
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
void r50Asc(
  uint16_t value,
  char *outbuf
)
{
  outbuf[2] = rad50[value % 050];
  value /= 050;
  outbuf[1] = rad50[value % 050];
  outbuf[0] = rad50[value / 050];
}

/*++
 *      r 5 0 A s c N o S p a c e
 *
 *  Convert a 16-bit rad50 value into up to 3 ASCII characters. Spaces are
 *  dropped from the conversion.
 *
 * Inputs:
 *
 *      value           - rad50 value to be converted
 *      outbuf          - pointer to 3 character buffer to receive the
 *                        converted output
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
void r50AscNoSpace(
  uint16_t value,
  char *outbuf
)
{
  uint16_t value2 = value / 050;

  /*
   * The rad50 representation of ' ' is zero.
   */
  if ((value2 / 050) != 0)
    *outbuf++ = rad50[value2 / 050];
  if ((value2 % 050) != 0)
    *outbuf++ = rad50[value2 % 050];
  if ((value % 050) != 0)
    *outbuf++ = rad50[value % 050];
}

/*++
 *      a s c R 5 0
 *
 *  Convert 3 ASCII characters into a single 16-bit rad50 value. If an input
 *  character is not in the rad50 character set it is quietly converted to '%'.
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
uint16_t ascR50(
  char *inbuf
)
{
  uint16_t value;
  char *ptr;

  if ((ptr = strchr(rad50, toupper(*inbuf++))) == NULL)
    ptr = strchr(rad50, '%');

  value = (ptr - rad50) * 03100;

  if ((ptr = strchr(rad50, toupper(*inbuf++))) == NULL)
    ptr = strchr(rad50, '%');

  value += (ptr - rad50) * 050;

  if ((ptr = strchr(rad50, toupper(*inbuf++))) == NULL)
    ptr = strchr(rad50, '%');

  value += ptr - rad50;

  return value;
}

/*++
 *      d o s 1 1 D a t e
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
 *      Pointer to the date string
 *
 --*/
char *dos11Date(
  uint16_t value,
  char *buf
)
{
  unsigned short year, doyr, leapyr;
  unsigned short *table;

  /*
   * The DOS/BATCH-11 date format is documented as having 3 reserved bit.
   * Version 9.20C (and later?) makes use of these bits to allow dates up
   * to 2035. Unfortunately, the date input routine only allows up to 1999!
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
  return buf;
}
