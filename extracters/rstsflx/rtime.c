/* subroutines to handle RSTS date/time */

#include <stdio.h>
#include <time.h>
#include <string.h>

#include "flx.h"
#include "rtime.h"
#include "fldef.h"

static const char months[12][4] =   {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
				     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static int	days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

char *cvtdate (word date, char *buf)
{
	long	day, yr;
        int	mon;

	if (date == 0) {			/* no date present */
		memcpy (buf, "  none   ", DATELEN);
		return (buf + DATELEN - 1);	/* point to terminator */
	}		
	yr = (long)date / 1000 + 1970;
	day = date % 1000;
	if (yr & 3) days[1] = 28;
	else	    days[1] = 29;		/* check for leap year */
	for (mon = 0; mon < 12; mon++) {
		if (day <= days[mon]) break;
		day -= days[mon];
	}
	sprintf (buf, "%2ld-%3s-%04ld", day, months[mon], yr);
	return (buf + DATELEN - 1);		/* point to terminator */
}

char *cvttime (word time, char *buf)
{
	int	hour, min;
	char	m;

	time &= at_msk;				/* mask out any flags */
	if (time == 0) {			/* no time present */
		memcpy (buf, "  none  ", RTIMELEN);
		return (buf + RTIMELEN - 1);	/* point to terminator */
	}		
	time = 1440 - time;			/* now time since midnight */
	hour = time / 60;
	min = time % 60;
	if (hour >= 12) {
		hour -= 12;
		m = 'p';
	} else	m = 'a';
	if (hour == 0) hour = 12;
	sprintf (buf, "%2d:%02d %1cm", hour, min, m);
	return (buf + RTIMELEN - 1);		/* point to terminator */
}

word curdate ()					/* current date in rsts form */
{
	struct tm	*tmb;
	time_t		now;

	time (&now);
	tmb = localtime (&now);
	return ((tmb->tm_year - 70) * 1000 + tmb->tm_yday + 1);
}

word curtime ()					/* current time in rsts form */
{
	struct tm	*tmb;
	time_t		now;

	time (&now);
	tmb = localtime (&now);
	return (1440 - (tmb->tm_hour * 60 + tmb->tm_min));
}
