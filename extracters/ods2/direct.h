/* Direct.h v1.2    Definitions for directory access routines */

/*
        This is part of ODS2 written by Paul Nankervis,
        email address:  Paulnank@au1.ibm.com

        ODS2 is distributed freely for all members of the
        VMS community to use. However all derived works
        must maintain comments in their source to acknowledge
        the contibution of the original author.
*/



struct dir$rec {
    u_word dir$size;
    u_word dir$verlimit;
    u_byte dir$flags;
    u_byte dir$namecount;
    char dir$name[1];
};

struct dir$ent {
    u_word dir$version;
    struct fiddef dir$fid;
};


unsigned direct(struct VCB *vcb,struct dsc$descriptor * fibdsc,
                struct dsc$descriptor *filedsc,unsigned short *reslen,
                struct dsc$descriptor *resdsc,unsigned action);
