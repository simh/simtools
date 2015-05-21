/* PHYVMS.C v1.2    Physical I/O module for VMS */

/*
        This is part of ODS2 written by Paul Nankervis,
        email address:  Paulnank@au1.ibm.com

        ODS2 is distributed freely for all members of the
        VMS community to use. However all derived works
        must maintain comments in their source to acknowledge
        the contibution of the original author.
*/

/*  How come VMS is so much easier to do physical I/O on than
    those other ^%$@*! systems?   I can't believe that they have
    different command sets for different device types, and can
    even have different command sets depending on what mode they
    are called from!  Sigh.                                  */


#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <iodef.h>
#include <descrip.h>

#ifdef __GNUC__
unsigned sys$assign();
unsigned sys$qiow();
unsigned sys$dassgn();
#else
#include <starlet.h>
#endif

#include "phyio.h"

#define chk(sts)  {register chksts; if (((chksts = sts) & 1) == 0) lib$stop(chksts);}

unsigned init_count = 0;
unsigned read_count = 0;
unsigned write_count = 0;

void phyio_show(void)
{
    printf("PHYIO_SHOW Initializations: %d Reads: %d Writes: %d\n",
           init_count,read_count,write_count);
}


unsigned phyio_init(int devlen,char *devnam,unsigned *handle,struct phyio_info *info)
{
    struct dsc$descriptor devdsc;
    devdsc.dsc$w_length = devlen;
    devdsc.dsc$a_pointer = devnam;
    init_count++;
    info->status = 0;           /* We don't know anything about this device! */
    info->sectors = 0;
    info->sectorsize = 0;
    *handle = 0;
    return sys$assign(&devdsc,handle,0,0,0,0);
}


unsigned phyio_close(unsigned handle)
{
    return sys$dassgn(handle);
}


unsigned phyio_read(unsigned handle,unsigned block,unsigned length,char *buffer)
{
#ifdef DEBUG
    printf("Phyio read block: %d into %x (%d bytes)\n",block,buffer,length);
#endif
    read_count++;
    return sys$qiow(1,handle,IO$_READLBLK,NULL,0,0,buffer,length,block,0,0,0);
}


unsigned phyio_write(unsigned handle,unsigned block,unsigned length,char *buffer)
{
#ifdef DEBUG
    printf("Phyio write block: %d from %x (%d bytes)\n",block,buffer,length);
#endif
    write_count++;
    printf("Phyio write block: %d from %x (%d bytes)\n",block,buffer,length);
    return sys$qiow(1,handle,IO$_WRITELBLK,NULL,0,0,buffer,length,block,0,0,0);
}
