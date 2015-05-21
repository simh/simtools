/*

       VMSTIME.C  v1.2

       Author: Paul Nankervis

        V1.2 - cleanup to stop compiler warnings

       Please send bug reports or requests for enhancement
       or improvement via email to:     PaulNank@au1.ibm.com


       This module contains versions of the VMS time routines
       sys$numtim(), sys$asctim() and friends... They are
       intended to be compatible with the routines of the same
       name on a VMS system (so descriptors feature regularly!)

       This code relies on being able to manipluate day numbers
       and times using 32 bit arithmetic to crack a VMS quadword
       byte by byte. If your C compiler doesn't have 32 bit int
       fields give up now! On a 64 bit systems this code could
       be modified to do 64 bit operations directly....

       One advantage of doing arihmetic byte by byte is that
       the code does not depend on what 'endian' the target
       machine is - it will always treat bytes in the same order!
       (Hopefully VMS time bytes will always be in the same order!)

       A couple of stupid questions to go on with:-
           o OK, I give up! What is the difference between a zero
             date and a zero delta time?
           o Anyone notice that the use of 16 bit words in
             sys$numtim restricts delta times to 65535 days?

                                       Paul Nankervis

*/


#include <stdio.h>
#include <memory.h>
#include "vmstime.h"            /* Our header file! */
#include <time.h>               /* C header for $GETTIM to find time */



#ifndef SS$_NORMAL
#define SS$_NORMAL 1
#define SS$_IVTIME 388
#define LIB$_ONEDELTIM 1410020
#endif


#define TIMEBASE 100000         /* 10 millisecond units in quadword */
#define TIMESIZE 8640000        /* Factor between dates & times */

#define QUAD_CENTURY_DAYS 146097
/*   (400*365) + (400/4) - (400/100) + (400/400)   */
#define CENTURY_DAYS    36524
/*   (100*365) + (100/4) - (100/100)    */
#define QUAD_YEAR_DAYS  1461
/*   (4*365) + (4/4)    */
#define YEAR_DAYS       365
/*   365        */
#define OFFSET_DAYS     94187
/*   ((1858_1601)*365) + ((1858_1601)/4) - ((1858_1601)/100)
   + ((1858_1601)/400) + 320
        OFFSET FROM 1/1/1601 TO 17/11/1858  */
#define BASE_YEAR       1601



/* combine_date_time() is an internal routine to put date and time into a
   quadword - basically the opposite of lib_day() .... */

unsigned combine_date_time(int days,struct TIME *timadr,int day_time)
{
    if (day_time >= TIMESIZE) {
        return SS$_IVTIME;
    } else {

        /* Put days into quad timbuf... */

        register unsigned count,time;
        register unsigned char *ptr;
        count = 8;
        ptr = timadr->time;
        time = days;
        do {
            *ptr++ = time;
            time = (time >> 8);
        } while (--count > 0);

        /* Factor in the time... */

        count = 8;
        ptr = timadr->time;
        time = day_time;
        do {
            time += *ptr * TIMESIZE;
            *ptr++ = time;
            time = (time >> 8);
        } while (--count > 0);

        /* Factor by time base... */

        count = 8;
        ptr = timadr->time;
        time = 0;
        do {
            time += *ptr * TIMEBASE;
            *ptr++ = time;
            time = (time >> 8);
        } while (--count > 0);

        return SS$_NORMAL;
    }
}



/* sys_gettim() implemented here by getting UNIX time in seconds since
   1-Jan-1970 using time() and munging into a quadword... Some run time
   systems don't seem to do this properly!!! Note that time() has a
   resolution of only one second. */

unsigned sys_gettim(struct TIME *timadr)
{
    time_t curtim = time(NULL);
    return combine_date_time(40587 + curtim / 86400,timadr,
                             (curtim % 86400) * 100);
}


/* lib_cvt_vectim() takes individual time fields in seven word buffer and
   munges into a quadword... */

unsigned short month_end[] = {0,31,59,90,120,151,181,212,243,273,304,334,365};

unsigned lib_cvt_vectim(unsigned short timbuf[7],struct TIME *timadr)
{
    int delta = 0;
    register unsigned sts,days,day_time;
    sts = SS$_NORMAL;

    /* lib_cvt_vectim packs the seven date/time components into a quadword... */

    if (timbuf[0] == 0 && timbuf[1] == 0) {
        delta = 1;
        days = timbuf[2];
    } else {
        register int leap = 0,year = timbuf[0],month = timbuf[1];
        if (month >= 2) {
            if ((year % 4) == 0) {
                if ((year % 100) == 0) {
                    if ((year % 400) == 0) {
                        leap = 1;
                    }
                } else {
                    leap = 1;
                }
            }
        }
        days = timbuf[2];
        if (year >= 1858 && year <= 9999 && month >= 1 &&
            month <= 12 && days >= 1) {
            days += month_end[month - 1];
            if (month > 2) days += leap;
            if (days <= month_end[month] + leap) {
                year -= BASE_YEAR;
                days += year * 365 + year / 4 - year / 100 + year / 400
                     - OFFSET_DAYS - 1;
            } else {
                sts = SS$_IVTIME;
            }
        } else {
            sts = SS$_IVTIME;
        }
    }
    if (timbuf[3] > 23 || timbuf[4] > 59 ||
        timbuf[5] > 59 || timbuf[6] > 99) {
        sts = SS$_IVTIME;
    }
    if (sts & 1) {
        day_time = timbuf[3] * 360000 + timbuf[4] * 6000 +
            timbuf[5] * 100 + timbuf[6];
        sts = combine_date_time(days,timadr,day_time);
        if (delta) {

            /* We have to 2's complement delta times - sigh!! */

            register unsigned count,time;
            register unsigned char *ptr;
            count = 8;
            ptr = timadr->time;
            time = 1;
            do {
                time = time + ((~*ptr) & 0xFF);
                *ptr++ = time;
                time = (time >> 8);
            } while (--count > 0);
        }
    }
    return sts;
}


/* lib_day() is a routine to crack quadword into day number and time */

unsigned lib_day(int *days,struct TIME *timadr,int *day_time)
{
    register unsigned date,time,count;
    register unsigned char *dstptr,*srcptr;
    struct TIME wrktim;
    int delta;

    /* If no time specified get current using gettim() */

    if (timadr == NULL) {
        register unsigned sts;
        sts = sys_gettim(&wrktim);
        if ((sts & 1) == 0) {
            return sts;
        }
        delta = 0;
        srcptr = wrktim.time + 7;
    } else {

        /* Check specified time for delta... */

        srcptr = timadr->time + 7;
        if ((delta = (*srcptr & 0x80))) {

            /* We have to 2's complement delta times - sigh!! */

            count = 8;
            srcptr = timadr->time;
            dstptr = wrktim.time;
            time = 1;
            do {
                time = time + ((~*srcptr++) & 0xFF);
                *dstptr++ = time;
                time = (time >> 8);
            } while (--count > 0);
            srcptr = wrktim.time + 7;
        }
    }


    /* Throw away the unrequired time precision */

    count = 8;
    dstptr = wrktim.time + 7;
    time = 0;
    do {
        time = (time << 8) | *srcptr--;
        *dstptr-- = time / TIMEBASE;
        time %= TIMEBASE;
    } while (--count > 0);


    /* Seperate the date and time */

    date = time = 0;
    srcptr = wrktim.time + 7;
    count = 8;
    do {
        time = (time << 8) | *srcptr--;
        date = (date << 8) | (time / TIMESIZE);
        time %= TIMESIZE;
    } while (--count > 0);

    /* Return results... */

    if (delta) {
        *days = -(int) date;
        if (day_time != NULL) *day_time = -(int) time;
    } else {
        *days = date;
        if (day_time != NULL) *day_time = time;
    }

    return SS$_NORMAL;
}



/* sys_numtim() takes quadword and breaks it into a seven word time buffer */

unsigned char month_days[] = {31,29,31,30,31,30,31,31,30,31,30,31};

unsigned sys_numtim(unsigned short timbuf[7],struct TIME *timadr)
{
    register int date,time;

    /* Use lib_day to crack time into date/time... */

    {
        int days,day_time;
        register unsigned sts;
        sts = lib_day(&days,timadr,&day_time);
        if ((sts & 1) == 0) {
            return sts;
        }
        date = days;
        time = day_time;
    }

    /* Delta or date... */

    if (date < 0 || time < 0) {
        timbuf[2] = -date;      /* Days */
        timbuf[1] = 0;          /* Month */
        timbuf[0] = 0;          /* Year */
        time = -time;

    } else {

        /* Date... */

        register int year,month;
        date += OFFSET_DAYS;
        year = BASE_YEAR + (date / QUAD_CENTURY_DAYS) * 400;
        date %= QUAD_CENTURY_DAYS;

        /* Kludge century division - last century in quad is longer!! */

        if ((month = date / CENTURY_DAYS) == 4) month = 3;
        date -= month * CENTURY_DAYS;
        year += month * 100;

        /* Use the same technique to find out the quad year and year -
           last year in quad is longer!! */

        year += (date / QUAD_YEAR_DAYS) * 4;
        date %= QUAD_YEAR_DAYS;

        if ((month = date / YEAR_DAYS) == 4) month = 3;
        date -= month * YEAR_DAYS;
        year += month;

        /* Adjust for years which have no Feb 29th */

        if (date++ > 58) {
            if (month != 3) {
                date++;
            } else {
                if ((year % 100) == 0 && (year % 400) != 0) date++;
            }
        }
        /* Figure out what month it is... */

        {
            unsigned char *mthptr = month_days;
            month = 1;
            while (date > *mthptr) {
                date -= *mthptr++;
                month++;
            }
        }

        /* Return date results... */

        timbuf[2] = date;       /* Days */
        timbuf[1] = month;      /* Month */
        timbuf[0] = year;       /* Year */
    }

    /* Return time... */

    timbuf[6] = time % 100;     /* Hundredths */
    time /= 100;
    timbuf[5] = time % 60;      /* Seconds */
    time /= 60;
    timbuf[4] = time % 60;      /* Minutes */
    timbuf[3] = time / 60;      /* Hours */

    return SS$_NORMAL;
}



/* sys_bintim() takes ascii time and convert it to a quadword */

char month_names[] = "-JAN-FEB-MAR-APR-MAY-JUN-JUL-AUG-SEP-OCT-NOV-DEC-";
char time_sep[] = "::.";

unsigned sys_bintim(struct dsc$descriptor *timbuf,struct TIME *timadr)
{
    register int length = timbuf->dsc$w_length;
    register char *chrptr = timbuf->dsc$a_pointer;
    unsigned short wrktim[7];
    int num,tf;


    /* Skip leading spaces... */

    while (length > 0 && *chrptr == ' ') {
        length--;
        chrptr++;
    }

    /* Get the day number... */

    num = -1;
    if (length > 0 && *chrptr >= '0' && *chrptr <= '9') {
        num = 0;
        do {
            num = num * 10 + (*chrptr++ - '0');
        } while (--length > 0 && *chrptr >= '0' && *chrptr <= '9');
    }
    /* Check for month separator "-" - if none delta time... */

    if (length > 0 && *chrptr == '-') {
        chrptr++;

        /* Get current time for defaults... */

        sys_numtim(wrktim,NULL);
        if (num >= 0) wrktim[2] = num;
        num = 0;
        if (--length >= 3 && *chrptr != '-') {
            char *mn = month_names + 1;
            num = 1;
            while (num <= 12) {
                if (memcmp(chrptr,mn,3) == 0) break;
                mn += 4;
                num++;
            }
            chrptr += 3;
            length -= 3;
            wrktim[1] = num;
        }
        /* Now look for year... */

        if (length > 0 && *chrptr == '-') {
            length--;
            chrptr++;
            if (length > 0 && *chrptr >= '0' && *chrptr <= '9') {
                num = 0;
                do {
                    num = num * 10 + (*chrptr++ - '0');
                } while (--length > 0 && *chrptr >= '0' && *chrptr <= '9');
                wrktim[0] = num;
            }
        }
    } else {

        /* Delta time then... */

        wrktim[0] = wrktim[1] = 0;
        wrktim[2] = num;
        wrktim[3] = wrktim[4] = wrktim[5] = wrktim[6] = 0;
    }

    /* Skip any spaces between date and time... */

    while (length > 0 && *chrptr == ' ') {
        length--;
        chrptr++;
    }

    /* Now wrap up time fields... */

    for (tf = 0; tf < 3; tf++) {
        if (length > 0 && *chrptr >= '0' && *chrptr <= '9') {
            num = 0;
            do {
                num = num * 10 + (*chrptr++ - '0');
            } while (--length > 0 && *chrptr >= '0' && *chrptr <= '9');
            wrktim[3 + tf] = num;
            if (num > 59) wrktim[1] = 13;
        }
        if (length > 0 && *chrptr == time_sep[tf]) {
            length--;
            chrptr++;
        } else {
            break;
        }
    }

    /* Hundredths of seconds need special handling... */

    if (length > 0 && *chrptr >= '0' && *chrptr <= '9') {
        tf = 10;
        num = 0;
        do {
            num = num + tf * (*chrptr++ - '0');
            tf = tf / 10;
        } while (--length > 0 && *chrptr >= '0' && *chrptr <= '9');
        wrktim[6] = num;
    }
    /* Now skip any trailing spaces... */

    while (length > 0 && *chrptr == ' ') {
        length--;
        chrptr++;
    }

    /* If anything left then we have a problem... */

    if (length == 0) {
        return lib_cvt_vectim(wrktim,timadr);
    } else {
        return SS$_IVTIME;
    }
}


/* sys_asctim() converts quadword to ascii... */

unsigned sys_asctim(unsigned short *timlen,struct dsc$descriptor *timbuf,
                    struct TIME *timadr,unsigned cvtflg)
{
    register int count,timval;
    unsigned short wrktim[7];
    register int length = timbuf->dsc$w_length;
    register char *chrptr = timbuf->dsc$a_pointer;

    /* First use sys_numtim to get the date/time fields... */

    {
        register unsigned sts;
        sts = sys_numtim(wrktim,timadr);
        if ((sts & 1) == 0) {
            return sts;
        }
    }

    /* See if we want delta days or date... */

    if (cvtflg == 0) {

        /* Check if date or delta time... */

        if (*wrktim) {

            /* Put in days and month... */

            if (length > 0) {
                if ((timval = wrktim[2]) / 10 == 0) {
                    *chrptr++ = ' ';
                } else {
                    *chrptr++ = '0' + timval / 10;
                }
                length--;
            }
            if (length > 0) {
                *chrptr++ = '0' + (timval % 10);
                length--;
            }
            if ((count = length) > 5) count = 5;
            memcpy(chrptr,month_names + (wrktim[1] * 4 - 4),count);
            length -= count;
            chrptr += count;
            timval = *wrktim;
        } else {

            /* Get delta days... */

            timval = wrktim[2];
        }

        /* Common code for year number and delta days!! */

        count = 10000;
        if (timval < count) {
            count = 1000;
            while (length > 0 && timval < count && count > 1) {
                length--;
                *chrptr++ = ' ';
                count /= 10;
            }
        }
        while (length > 0 && count > 0) {
            length--;
            *chrptr++ = '0' + (timval / count);
            timval = timval % count;
            count /= 10;
        }

        /* Space between date and time... */

        if (length > 0) {
            *chrptr++ = ' ';
            length--;
        }
    }
    /* Do time... :-) */

    count = 3;
    do {
        timval = wrktim[count];
        if (length >= 1) *chrptr++ = '0' + (timval / 10);
        if (length >= 2) {
            *chrptr++ = '0' + (timval % 10);
            length -= 2;
        } else {
            length = 0;
        }
        if (count < 6 && length > 0) {
            length--;
            if (count == 5) {
                *chrptr++ = '.';
            } else {
                *chrptr++ = ':';
            }
        }
    } while (++count < 7);

    /* We've done it - time to return length... */

    if (timlen != NULL) *timlen = timbuf->dsc$w_length - length;
    return SS$_NORMAL;
}


/* lib_day_of_week() computes day of week from quadword... */

unsigned lib_day_of_week(struct TIME *timadr,unsigned *weekday)
{
    int days;
    register unsigned sts;

    /* Use lib_day to crack quadword... */

    sts = lib_day(&days,timadr,NULL);
    if (sts & 1) {
        *weekday = ((days + 2) % 7) + 1;
    }
    return sts;
}


/* addx & subx COULD use 32 bit arithmetic to do a word at a time. But
   this only works on hardware which has the same endian as the VAX!
   (Intel works fine!! :-) ) So this version works byte by byte which
   should work on VMS dates regardless of system endian - but they
   will NOT perform arithmetic correctly for other data types on
   systems with opposite endian!!! */

unsigned lib_addx(void *addant,void *addee,void *result,int *lenadd)
{
    register int count;
    register unsigned carry = 0;
    register unsigned char *ant = (unsigned char *) addant;
    register unsigned char *ee = (unsigned char *) addee;
    register unsigned char *res = (unsigned char *) result;
    if (lenadd == NULL) {
        count = 8;
    } else {
        count = *lenadd * 4;
    }

    while (count-- > 0) {
        carry = *ant++ + (carry + *ee++);
        *res++ = carry;
        carry = carry >> 8;
    }
    return SS$_NORMAL;
}


unsigned lib_subx(void *subant,void *subee,void *result,int *lenadd)
{
    register int count;
    register unsigned carry = 0;
    register unsigned char *ant = (unsigned char *) subant;
    register unsigned char *ee = (unsigned char *) subee;
    register unsigned char *res = (unsigned char *) result;
    if (lenadd == NULL) {
        count = 8;
    } else {
        count = *lenadd * 4;
    }

    while (count-- > 0) {
        carry = *ant++ - (carry + *ee++);
        *res++ = carry;
        carry = (carry >> 8) & 1;

    }
    return SS$_NORMAL;
}



unsigned lib_add_times(struct TIME *time1,struct TIME *time2,
                       struct TIME *result)
{
    if (time1->time[7] & 0x80) {
        if (time2->time[7] & 0x80) {
            return lib_addx(time1,time2,result,NULL);
        } else {
            return lib_subx(time2,time1,result,NULL);
        }
    } else {
        if (time2->time[7] & 0x80) {
            return lib_subx(time1,time2,result,NULL);
        } else {
            return LIB$_ONEDELTIM;
        }
    }
}



unsigned lib_sub_times(struct TIME *time1,struct TIME *time2,
                       struct TIME *result)
{
    if ((time1->time[7] & 0x80) != (time2->time[7] & 0x80)) {
        return lib_addx(time1,time2,result,NULL);
    } else {
        register int cmp,count = 7;
        do {
            if ((cmp = (time1->time[count] - time2->time[count]))) break;
        } while (--count >= 0);
        if (cmp < 0) {
            return lib_subx(time1,time2,result,NULL);
        } else {
            return lib_subx(time2,time1,result,NULL);
        }
    }
}



unsigned lib_mult_delta_time(int *multiple,struct TIME *timadr)
{
    register unsigned count = 8,carry = 0;
    register int factor = *multiple;
    register unsigned char *ptr = timadr->time;

    /* Check for delta time... */

    if (timadr->time[7] & 0x80) {

        /* Use absolute factor... */

        if (factor < 0) factor = -factor;

        /* Multiply delta time... */

        do {
            carry += *ptr * factor;
            *ptr++ = carry;
            carry = (carry >> 8);
        } while (--count > 0);

        return SS$_NORMAL;
    } else {
        return SS$_IVTIME;
    }
}
