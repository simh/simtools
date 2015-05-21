/* RMS.H v1.2   RMS routine definitions */

/*
        This is part of ODS2 written by Paul Nankervis,
        email address:  Paulnank@au1.ibm.com

        ODS2 is distributed freely for all members of the
        VMS community to use. However all derived works
        must maintain comments in their source to acknowledge
        the contibution of the original author.
*/

/* If not using GNU C on VMS use real RMS structures :-)...
   otherwise define minimum subset to meet our requirements   */

#if defined(VMS) && !defined(__GNUC__)

#include <rms.h>

#else

#include "vmstime.h"

#define RMS$_RTB 98728
#define RMS$_EOF 98938
#define RMS$_FNF 98962
#define RMS$_NMF 99018
#define RMS$_WCC 99050
#define RMS$_BUG 99380
#define RMS$_DIR 99532
#define RMS$_ESS 99588
#define RMS$_FNM 99628
#define RMS$_IFI 99684
#define RMS$_NAM 99804
#define RMS$_RSS 99988
#define RMS$_WLD 100164
#define RMS$_DNF 114762

#define NAM$C_MAXRSS 255
#define NAM$M_SYNCHK 1
#define FAB$M_NAM 0x1000000

#define XAB$C_DAT 18
#define XAB$C_FHC 29
#define XAB$C_PRO 19

struct XABDAT {
    void *xab$l_nxt;
    int xab$b_cod;
    int xab$w_rvn;
    struct TIME xab$q_bdt;
    struct TIME xab$q_cdt;
    struct TIME xab$q_edt;
    struct TIME xab$q_rdt;
};

#ifdef RMS_INITIALIZE
struct XABDAT cc$rms_xabdat = {NULL,XAB$C_DAT,0,
        {{0,0,0,0,0,0,0,0}}, {{0,0,0,0,0,0,0,0}},
        {{0,0,0,0,0,0,0,0}}, {{0,0,0,0,0,0,0,0}}};
#else
extern struct XABDAT cc$rms_xabdat;
#endif



struct XABFHC {
    void *xab$l_nxt;
    int xab$b_cod;
    int xab$b_atr;
    int xab$b_bkz;
    int xab$w_dxq;
    int xab$l_ebk;
    int xab$w_ffb;
    int xab$w_gbc;
    int xab$l_hbk;
    int xab$b_hsz;
    int xab$w_lrl;
    int xab$w_verlimit;
};

#ifdef RMS_INITIALIZE
struct XABFHC cc$rms_xabfhc = {NULL,XAB$C_FHC,0,0,0,0,0,0,0,0,0,0};
#else
extern struct XABFHC cc$rms_xabfhc;
#endif



struct XABPRO {
    void *xab$l_nxt;
    int xab$b_cod;
    int xab$w_pro;
    int xab$l_uic;
};

#ifdef RMS_INITIALIZE
struct XABPRO cc$rms_xabpro = {NULL,XAB$C_PRO,0,0};
#else
extern struct XABPRO cc$rms_xabpro;
#endif

#define NAM$M_WILDCARD 0x100

struct NAM {
    unsigned short nam$w_did_num;
    unsigned short nam$w_did_seq;
    unsigned char nam$b_did_rvn;
    unsigned char nam$b_did_nmx;
    unsigned short nam$w_fid_num;
    unsigned short nam$w_fid_seq;
    unsigned char nam$b_fid_rvn;
    unsigned char nam$b_fid_nmx;
    int nam$b_ess;
    int nam$b_rss;
    int nam$b_esl;
    char *nam$l_esa;
    int nam$b_rsl;
    char *nam$l_rsa;
    int nam$b_dev;
    char *nam$l_dev;
    int nam$b_dir;
    char *nam$l_dir;
    int nam$b_name;
    char *nam$l_name;
    int nam$b_type;
    char *nam$l_type;
    int nam$b_ver;
    char *nam$l_ver;
    int nam$l_wcc;
    int nam$b_nop;
    int nam$l_fnb;
};

#ifdef RMS_INITIALIZE
struct NAM cc$rms_nam = {0,0,0,0,0,0,0,0,0,0,0,NULL,0,NULL,0,NULL,0,NULL,0,NULL,0,NULL,0,NULL,0,0,0};
#else
extern struct NAM cc$rms_nam;
#endif

#define RAB$C_SEQ 0
#define RAB$C_RFA 2

struct RAB {
    struct FAB *rab$l_fab;
    char *rab$l_ubf;
    char *rab$l_rhb;
    unsigned rab$w_usz;
    unsigned rab$w_rsz;
    int rab$b_rac;
    unsigned short rab$w_rfa[3];
};

#ifdef RMS_INITIALIZE
struct RAB cc$rms_rab = {NULL,NULL,NULL,0,0,0,{0,0,0}};
#else
extern struct RAB cc$rms_rab;
#endif


#define FAB$C_SEQ 0
#define FAB$C_REL 16
#define FAB$C_IDX 32
#define FAB$C_HSH 48

#define FAB$M_FTN 1
#define FAB$M_CR  2
#define FAB$M_PRN 4
#define FAB$M_BLK 8

#define FAB$C_UDF 0
#define FAB$C_FIX 1
#define FAB$C_VAR 2
#define FAB$C_VFC 3
#define FAB$C_STM 4
#define FAB$C_STMLF 5
#define FAB$C_STMCR 6

struct FAB {
    struct NAM *fab$l_nam;
    int fab$w_ifi;
    char *fab$l_fna;
    char *fab$l_dna;
    int fab$b_fns;
    int fab$b_dns;
    int fab$l_alq;
    int fab$b_bks;
    int fab$w_deq;
    int fab$b_fsz;
    int fab$w_gbc;
    int fab$w_mrs;
    int fab$l_fop;
    int fab$b_org;
    int fab$b_rat;
    int fab$b_rfm;
    void *fab$l_xab;
};

#ifdef RMS_INITIALIZE
struct FAB cc$rms_fab = {NULL,0,NULL,NULL,0,0,0,0,0,0,0,0,0,0,0,0,NULL};
#else
extern struct FAB cc$rms_fab;
#endif
#endif


#define sys$search      sys_search
#define sys$parse       sys_parse
#define sys$setddir     sys_setddir
#define sys$connect     sys_connect
#define sys$disconnect  sys_disconnect
#define sys$get         sys_get
#define sys$display     sys_display
#define sys$close       sys_close
#define sys$open        sys_open
#define sys$erase       sys_erase


unsigned sys_search(struct FAB *fab);
unsigned sys_parse(struct FAB *fab);
unsigned sys_connect(struct RAB *rab);
unsigned sys_disconnect(struct RAB *rab);
unsigned sys_get(struct RAB *rab);
unsigned sys_display(struct FAB *fab);
unsigned sys_close(struct FAB *fab);
unsigned sys_open(struct FAB *fab);
unsigned sys_erase(struct FAB *fab);
unsigned sys_setddir(struct dsc$descriptor *newdir,unsigned short *oldlen,
                     struct dsc$descriptor *olddir);
