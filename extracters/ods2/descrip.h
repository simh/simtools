/* Descrip.h v1.2    Definitions for descriptors */

/*
        This is part of ODS2 written by Paul Nankervis,
        email address:  Paulnank@au1.ibm.com

        ODS2 is distributed freely for all members of the
        VMS community to use. However all derived works
        must maintain comments in their source to acknowledge
        the contibution of the original author.
*/


#if defined(VMS) && !defined(__GNUC__)

#include <descrip.h>

#else

#ifndef DSC$K_DTYPE_T
#define DSC$K_DTYPE_T 0
#define DSC$K_CLASS_S 0

struct dsc$descriptor {
    unsigned short dsc$w_length;
    unsigned char dsc$w_type;
    unsigned char dsc$w_class;
    char *dsc$a_pointer;
};

#define $DESCRIPTOR(string,name) struct dsc$descriptor name = {sizeof(sring)-1,0,0,string};

#endif
#endif
