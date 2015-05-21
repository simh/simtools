/*

       VMSTIME.H  v1.1

       Author: Paul Nankervis

       Please send bug reports to PaulNank@au1.ibm.com

*/


#ifndef __VMSTIME__
#define __VMSTIME__ loaded

#include "descrip.h"


struct TIME {
    unsigned char time[8];      /* Structure of time */
};                              /* Look out Einstein!! :-)  */

#define lib$add_times       lib_add_times
#define lib$addx            lib_addx
#define lib$cvt_vectim      lib_cvt_vectim
#define lib$day             lib_day
#define lib$day_of_week     lib_day_of_week
#define lib$mult_delta_time lib_mult_delta_time
#define lib$sub_times       lib_sub_times
#define lib$subx            lib_subx
#define sys$asctim          sys_asctim
#define sys$bintim          sys_bintim
#define sys$gettim          sys_gettim
#define sys$numtim          sys_numtim


unsigned sys_gettim(struct TIME *timadr);
unsigned lib_cvt_vectim(unsigned short timbuf[7],struct TIME *timadr);
unsigned lib_day(int *days,struct TIME *timadr,int *day_time);
unsigned sys_numtim(unsigned short timbuf[7],struct TIME *timadr);
unsigned sys_bintim(struct dsc$descriptor *timbuf,struct TIME *timadr);
unsigned sys_asctim(unsigned short *timlen,struct dsc$descriptor *timbuf,
                    struct TIME *timadr,unsigned cvtflg);
unsigned lib_day_of_week(struct TIME *timadr,unsigned *weekday);
unsigned lib_addx(void *addant,void *addee,void *result,int *lenadd);
unsigned lib_subx(void *subant,void *subee,void *result,int *lenadd);
unsigned lib_add_times(struct TIME *time1,struct TIME *time2,
                       struct TIME *result);
unsigned lib_sub_times(struct TIME *time1,struct TIME *time2,
                       struct TIME *result);
unsigned lib_mult_delta_time(int *multiple,struct TIME *timadr);

#endif
