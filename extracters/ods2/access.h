/* Access.h v1.2    Definitions for file access routines */

/*
        This is part of ODS2 written by Paul Nankervis,
        email address:  Paulnank@au1.ibm.com

        ODS2 is distributed freely for all members of the
        VMS community to use. However all derived works
        must maintain comments in their source to acknowledge
        the contibution of the original author.
*/


#include "cache.h"
#include "vmstime.h"

#define swapw(w) ((((unsigned int)(w[0]))<<16) | (unsigned int)(w[1]))

#define FH2$M_NOBACKUP   0x2
#define FH2$M_CONTIG     0x80
#define FH2$M_DIRECTORY  0x2000
#define FH2$M_MARKDEL    0x8000
#define FH2$M_ERASE      0x20000

typedef unsigned char u_byte;
typedef unsigned short u_word;
typedef unsigned int u_long;


struct UIC {
    u_word u_grp;
    u_word u_mem;
};


struct fiddef {
    u_word fid$w_num;
    u_word fid$w_seq;
    u_byte fid$b_rvn;
    u_byte fid$b_nmx;
};


struct RECATTR {
    u_byte fat$b_rtype;
    u_byte fat$b_rattrib;
    u_word fat$w_rsize;
    u_word fat$l_hiblk[2];
    u_word fat$l_efblk[2];
    u_word fat$w_ffbyte;
    u_byte fat$b_bktsize;
    u_byte fat$b_vfcsize;
    u_word fat$w_maxrec;
    u_word fat$w_defext;
    u_word fat$w_gbc;
    u_byte fat$_UU0[8];
    u_word fat$w_versions;
};


struct HOME {
    u_long hm2$l_homelbn;
    u_long hm2$l_alhomelbn;
    u_long hm2$l_altidxlbn;
    u_word hm2$w_struclev;
    u_word hm2$w_cluster;
    u_word hm2$w_homevbn;
    u_word hm2$w_alhomevbn;
    u_word hm2$w_altidxvbn;
    u_word hm2$w_ibmapvbn;
    u_long hm2$l_ibmaplbn;
    u_long hm2$l_maxfiles;
    u_word hm2$w_ibmapsize;
    u_word hm2$w_resfiles;
    u_word hm2$w_devtype;
    u_word hm2$w_rvn;
    u_word hm2$w_setcount;
    u_word hm2$w_volchar;
    struct UIC hm2$w_volowner;
    u_long hm2$l_reserved1;
    u_word hm2$w_protect;
    u_word hm2$w_fileprot;
    u_word hm2$w_reserved2;
    u_word hm2$w_checksum1;
    struct TIME hm2$q_credate;
    u_byte hm2$b_window;
    u_byte hm2$b_lru_lim;
    u_word hm2$w_extend;
    struct TIME hm2$q_retainmin;
    struct TIME hm2$q_retainmax;
    struct TIME hm2$q_revdate;
    u_byte hm2$r_min_class[20];
    u_byte hm2$r_max_class[20];
    u_byte hm2$t_reserved3[320];
    u_long hm2$l_serialnum;
    char hm2$t_strucname[12];
    char hm2$t_volname[12];
    char hm2$t_ownername[12];
    char hm2$t_format[12];
    u_word hm2$w_reserved4;
    u_word hm2$w_checksum2;
};


struct IDENT {
    char fi2$t_filename[20];
    u_word fi2$w_revision;
    struct TIME fi2$q_credate;
    struct TIME fi2$q_revdate;
    struct TIME fi2$q_expdate;
    struct TIME fi2$q_bakdate;
    char fi2$t_filenamext[66];
};


struct HEAD {
    u_byte fh2$b_idoffset;
    u_byte fh2$b_mpoffset;
    u_byte fh2$b_acoffset;
    u_byte fh2$b_rsoffset;
    u_word fh2$w_seg_num;
    u_word fh2$w_struclev;
    struct fiddef fh2$w_fid;
    struct fiddef fh2$w_ext_fid;
    struct RECATTR fh2$w_recattr;
    u_long fh2$l_filechar;
    u_word fh2$w_reserved1;
    u_byte fh2$b_map_inuse;
    u_byte fh2$b_acc_mode;
    struct UIC fh2$l_fileowner;
    u_word fh2$w_fileprot;
    struct fiddef fh2$w_backlink;
    u_byte fh2$b_journal;
    u_byte fh2$b_ru_active;
    u_word fh2$w_reserved2;
    u_long fh2$l_highwater;
    u_byte fh2$b_reserved3[8];
    u_byte fh2$r_class_prot[20];
    u_byte fh2$r_restofit[402];
    u_word fh2$w_checksum;
};


struct EXT {
    unsigned phylen;
    unsigned phyblk;
};                              /* Physical extent entry */


#define EXTMAX 20

struct WCB {
    struct CACHE cache;
    unsigned loblk,hiblk;       /* Range of window */
    unsigned hd_base;           /* File blocks prior to header */
    unsigned extcount;          /* Extents in use */
    struct EXT ext[EXTMAX];     /* Mapping extents */
    struct fiddef hd_fid;       /* Header info to create  other WCBs */
};                              /* Window control block */


#define VIOC_CHUNKSIZE 4

struct VIOC {
    struct CACHE cache;
    struct FCB *fcb;            /* File this chunk is for */
    unsigned wrtmask;           /* Bit mask for writable blocks */
    unsigned modmask;           /* Bit mask for modified blocks */
    char data[VIOC_CHUNKSIZE][512];     /* Chunk data */
};                              /* Chunk of a file */


struct FCB {
    struct CACHE cache;
    struct VCB *vcb;            /* Volume this file is for */
    struct VIOC *headvioc;      /* Index file chunk for file header */
    struct HEAD *head;          /* Pointer to header block */
    struct WCB *wcb;            /* Window control block tree */
    struct VIOC *vioc;          /* Virtual I/O chunk tree */
    unsigned modmask;           /* headvioc chunk modmask */
    unsigned hiblock;           /* Highest block mapped */
    unsigned highwater;         /* First high water block */
    unsigned char rvn;          /* Initial file relative volume */
};                              /* File control block */


struct DIRCACHE {
    struct CACHE cache;
    int dirlen;                 /* Length of directory name */
    struct fiddef dirid;        /* File ID of directory */
    char dirnam[1];             /* Directory name */
};                              /* Directory cache entry */


#define VCB_WRITE 1

struct VCB {
    unsigned status;            /* Volume status */
    unsigned devices;           /* Number of volumes in set */
    struct HEAD *idxboot;       /* Pointer to index file boot header */
    struct FCB *fcb;            /* File control block tree */
    struct DIRCACHE *dircache;  /* Directory cache tree */
    struct VCBDEV {
        struct DEV *dev;        /* Pointer to device info */
        struct FCB *idxfcb;     /* Index file control block */
        struct FCB *mapfcb;     /* Bitmap file control block */
        struct HOME home;       /* Volume home block */
    } vcbdev[1];                /* List of volumes devices */
};                              /* Volume control block */


struct DEV {
    struct CACHE cache;
    struct VCB *vcb;            /* Pointer to volume (if mounted) */
    unsigned handle;            /* Device physical I/O handle */
    unsigned status;            /* Device physical status */
    unsigned sectors;           /* Device physical sectors */
    unsigned sectorsize;        /* Device physical sectorsize */
    int devlen;                 /* Length of device name */
    char devnam[1];             /* Device name */
};                              /* Device information */


unsigned device_lookup(unsigned devlen,char *devnam,int create,struct DEV **retdev);

unsigned dismount(struct VCB *vcb);
unsigned mount(unsigned flags,unsigned devices,char *devnam[],char *label[],struct VCB **vcb);

unsigned accesserase(struct VCB *vcb,struct fiddef *fid);
unsigned deaccessfile(struct FCB *fcb);
unsigned accessfile(struct VCB *vcb,struct fiddef *fid,
                    struct FCB **fcb,unsigned wrtflg);

unsigned deaccesschunk(struct VIOC *vioc,unsigned modmask,int reuse);
unsigned accesschunk(struct FCB *fcb,unsigned vbn,struct VIOC **retvioc,
                     char **retbuff,unsigned *retblocks,unsigned wrtblks,
                     unsigned *retmodmask);
