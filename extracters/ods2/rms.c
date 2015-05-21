/* Rms.c v1.2  RMS components */

/*
        This is part of ODS2 written by Paul Nankervis,
        email address:  Paulnank@au1.ibm.com

        ODS2 is distributed freely for all members of the
        VMS community to use. However all derived works
        must maintain comments in their source to acknowledge
        the contibution of the original author.
*/

/*  Boy some cleanups are needed here - especially for
    error handling and deallocation of memory after errors..
    For now we have to settle for loosing memory left/right/center...

    This module implements file name parsing, file searches,
    file opens, gets, etc...       */


#define DEBUGx x

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <ctype.h>
#include "descrip.h"
#include "ssdef.h"
#include "fibdef.h"

#define RMS_INITIALIZE doit     /* used by rms.h to create templates */
#include "rms.h"
#include "access.h"
#include "direct.h"


/* Table of file name component delimeters... */

char char_delim[] = {
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0, 0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0};


/* Routine to find size of file name components */

unsigned name_delim(char *str,int len,int size[5])
{
    register unsigned ch;
    register char *curptr = str;
    register char *endptr = curptr + len;
    register char *begptr = curptr;
    while (curptr < endptr) {
        ch = (*curptr++ & 127);
        if (char_delim[ch]) break;
    }
    if (curptr > begptr && ch == ':') {
        size[0] = (curptr - begptr);
        begptr = curptr;
        while (curptr < endptr) {
            ch = (*curptr++ & 127);
            if (char_delim[ch]) break;
        }
    } else {
        size[0] = 0;
    }
    if (ch == '[' || ch == '<') {
        if (curptr != begptr + 1) return RMS$_FNM;
        while (curptr < endptr) {
            ch = (*curptr++ & 127);
                if (char_delim[ch]) if (ch != '.') break;
        }
        if (curptr < begptr + 2 || (ch != ']' && ch != '>')) return RMS$_FNM;
        size[1] = (curptr - begptr);
        begptr = curptr;
        while (curptr < endptr) {
            ch = (*curptr++ & 127);
            if (char_delim[ch]) break;
        }
    } else {
        size[1] = 0;
    }
    if (curptr > begptr && char_delim[ch]) {
        size[2] = (curptr - begptr) - 1;
        begptr = curptr - 1;
    } else {
        size[2] = (curptr - begptr);
        begptr = curptr;
    }
    if (curptr > begptr && ch == '.') {
        while (curptr < endptr) {
            ch = (*curptr++ & 127);
            if (char_delim[ch]) break;
        }
        if (curptr > begptr && char_delim[ch]) {
            size[3] = (curptr - begptr) - 1;
            begptr = curptr - 1;
        } else {
            size[3] = (curptr - begptr);
            begptr = curptr;
        }
    } else {
        size[3] = 0;
    }
    if (curptr > begptr && (ch == ';' || ch == '.')) {
        while (curptr < endptr) {
            ch = (*curptr++ & 127);
            if (char_delim[ch]) break;
        }
        size[4] = (curptr - begptr);
    } else {
        size[4] = 0;
    }
#ifdef DEBUG
    printf("Name delim %d %d %d %d %d\n",size[0],size[1],size[2],size[3],size[4]);
#endif
    if (curptr >= endptr) {
        return 1;
    } else {
        return RMS$_FNM;
    }
}


/* Function to compare directory cache names - directory cache NOT IN USE */

int dircmp(unsigned keylen,void *key,void *node)
{
    register struct DIRCACHE *dirnode = (struct DIRCACHE *) node;
    register int cmp = keylen - dirnode->dirlen;
    if (cmp == 0) {
        register unsigned len = keylen;
        register char *keynam = (char *) key;
        register char *dirnam = dirnode->dirnam;
        while (len-- > 0) {
            cmp = toupper(*keynam++) - toupper(*dirnam++);
            if (cmp != 0) break;
        }
    }
    return cmp;
}


/* Routine to find directory name in cache... NOT CURRENTLY IN USE!!! */

unsigned dircache(struct VCB *vcb,char *dirnam,int dirlen,struct fiddef *dirid)
{
    register struct DIRCACHE *dir;
    if (dirlen < 1) {
        dirid->fid$w_num = 4;
        dirid->fid$w_seq = 4;
        dirid->fid$b_rvn = 0;
        dirid->fid$b_nmx = 0;
        return 1;
    } else {
        unsigned create = 0;
        dir = cachesearch((void *) &vcb->dircache,0,dirlen,dirnam,dircmp,&create);
        if (dir != NULL) {
            memcpy(dirid,&dir->dirid,sizeof(struct fiddef));
            return 1;
        }
        return 0;
    }
}


/*      For file context info we use WCCDIR and WCCFILE structures...
        Each WCCFILE structure contains one WCCDIR structure for file
        context. Each level of directory has an additional WCCDIR record.
        For example DKA200:[PNANKERVIS.F11]RMS.C is loosley stored as:-
                        next         next
                WCCFILE  -->  WCCDIR  -->  WCCDIR
                RMS.C;       F11.DIR;1    PNANKERVIS.DIR;1

        WCCFILE is pointed to by fab->fab$l_nam->nam$l_wcc and if a
        file is open also by ifi_table[fab->fab$w_ifi]  (so that close
        can easily locate it).

        Most importantly WCCFILE contains a resulting filename field
        which stores the resulting file spec. Each WCCDIR has a prelen
        length to indicate how many characters in the specification
        exist before the bit contributed by this WCCDIR. (ie to store
        the device name and any previous directory name entries.)  */


#define STATUS_INIT 1
#define STATUS_TMPDIR  2
#define STATUS_WILDCARD 4

struct WCCDIR {
    struct WCCDIR *wcd_next;
    struct WCCDIR *wcd_prev;
    int wcd_status;
    int wcd_wcc;
    int wcd_prelen;
    unsigned short wcd_reslen;
    struct dsc$descriptor wcd_serdsc;
    struct fiddef wcd_dirid;
    char wcd_sernam[1];         /* Must be last in structure */
};                              /* Directory context */


#define STATUS_RECURSE 8
#define STATUS_TMPWCC  16
#define MAX_FILELEN 1024

struct WCCFILE {
    struct FAB *wcf_fab;
    struct VCB *wcf_vcb;
    struct FCB *wcf_fcb;
    int wcf_status;
    struct fiddef wcf_fid;
    char wcf_result[MAX_FILELEN];
    struct WCCDIR wcf_wcd;      /* Must be last..... (dynamic length). */
};                              /* File context */


/* Function to remove WCCFILE and WCCDIR structures when not required */

void cleanup_wcf(struct WCCFILE *wccfile)
{
    if (wccfile != NULL) {
        struct WCCDIR *wcc = wccfile->wcf_wcd.wcd_next;
        wccfile->wcf_wcd.wcd_next = NULL;
        wccfile->wcf_wcd.wcd_prev = NULL;
        /* should deaccess volume */
        free(wccfile);
        while (wcc != NULL) {
            struct WCCDIR *next = wcc->wcd_next;
            wcc->wcd_next = NULL;
            wcc->wcd_prev = NULL;
            free(wcc);
            wcc = next;
        }
    }
}


/* Function to perform an RMS search... */

unsigned do_search(struct FAB *fab,struct WCCFILE *wccfile)
{
    int sts;
    struct fibdef fibblk;
    struct WCCDIR *wcc;
    struct dsc$descriptor fibdsc,resdsc;
    struct NAM *nam = fab->fab$l_nam;
    wcc = &wccfile->wcf_wcd;
    if (fab->fab$w_ifi != 0) return RMS$_IFI;

    /* if first time through position at top directory... WCCDIR */

    while ((wcc->wcd_status & STATUS_INIT) == 0 && wcc->wcd_next != NULL) {
        wcc = wcc->wcd_next;
    }
    fibdsc.dsc$w_length = sizeof(struct fibdef);
    fibdsc.dsc$a_pointer = (char *) &fibblk;
    while (1) {
        if ((wcc->wcd_status & STATUS_INIT) == 0 || wcc->wcd_wcc != 0) {
            wcc->wcd_status |= STATUS_INIT;
            resdsc.dsc$w_length = 256 - wcc->wcd_prelen;
            resdsc.dsc$a_pointer = wccfile->wcf_result + wcc->wcd_prelen;
            memcpy(&fibblk.fib$w_did_num,&wcc->wcd_dirid,sizeof(struct fiddef));
            fibblk.fib$w_nmctl = 0;     /* FIB$M_WILD; */
            fibblk.fib$l_acctl = 0;
            fibblk.fib$w_fid_num = 0;
            fibblk.fib$w_fid_seq = 0;
            fibblk.fib$b_fid_rvn = 0;
            fibblk.fib$b_fid_nmx = 0;
            fibblk.fib$l_wcc = wcc->wcd_wcc;
#ifdef DEBUG
            wcc->wcd_sernam[wcc->wcd_serdsc.dsc$w_length] = '\0';
            wccfile->wcf_result[wcc->wcd_prelen + wcc->wcd_reslen] = '\0';
            printf("Ser: '%s' (%d,%d,%d) WCC: %d Prelen: %d '%s'\n",wcc->wcd_sernam,
                   fibblk.fib$w_did_num | (fibblk.fib$b_did_nmx << 16),
                   fibblk.fib$w_did_seq,fibblk.fib$b_did_rvn,
                   wcc->wcd_wcc,wcc->wcd_prelen,wccfile->wcf_result + wcc->wcd_prelen);
#endif
            sts = direct(wccfile->wcf_vcb,&fibdsc,&wcc->wcd_serdsc,&wcc->wcd_reslen,&resdsc,0);
        } else {
            sts = SS$_NOMOREFILES;
        }
        if (sts & 1) {
#ifdef DEBUG
            wccfile->wcf_result[wcc->wcd_prelen + wcc->wcd_reslen] = '\0';
            printf("Fnd: '%s'  (%d,%d,%d) WCC: %d\n",wccfile->wcf_result + wcc->wcd_prelen,
                   fibblk.fib$w_fid_num | (fibblk.fib$b_fid_nmx << 16),
                   fibblk.fib$w_fid_seq,fibblk.fib$b_fid_rvn,
                   wcc->wcd_wcc);
#endif
            wcc->wcd_wcc = fibblk.fib$l_wcc;
            if (wcc->wcd_prev) {/* go down directory */
                if (wcc->wcd_prev->wcd_next != wcc) printf("wcd_PREV corruption\n");
                if (fibblk.fib$w_fid_num != 4 || fibblk.fib$b_fid_nmx != 0 ||
                    wcc == &wccfile->wcf_wcd ||
                    memcmp(wcc->wcd_sernam,"000000.",7) == 0) {
                    memcpy(&wcc->wcd_prev->wcd_dirid,&fibblk.fib$w_fid_num,sizeof(struct fiddef));
                    if (wcc->wcd_next) wccfile->wcf_result[wcc->wcd_prelen - 1] = '.';
                    wcc->wcd_prev->wcd_prelen = wcc->wcd_prelen + wcc->wcd_reslen - 5;
                    wcc = wcc->wcd_prev;        /* go down one level */
                    if (wcc->wcd_prev == NULL) wccfile->wcf_result[wcc->wcd_prelen - 1] = ']';
                }
            } else {
                if (nam != NULL) {
                    int fna_size[5];
                    memcpy(&nam->nam$w_fid_num,&fibblk.fib$w_fid_num,sizeof(struct fiddef));
                    nam->nam$b_rsl = wcc->wcd_prelen + wcc->wcd_reslen;
                    name_delim(wccfile->wcf_result,nam->nam$b_rsl,fna_size);
                    nam->nam$l_dev = nam->nam$l_rsa;
                    nam->nam$b_dev = fna_size[0];
                    nam->nam$l_dir = nam->nam$l_dev + fna_size[0];
                    nam->nam$b_dir = fna_size[1];
                    nam->nam$l_name = nam->nam$l_dir + fna_size[1];
                    nam->nam$b_name = fna_size[2];
                    nam->nam$l_type = nam->nam$l_name + fna_size[2];
                    nam->nam$b_type = fna_size[3];
                    nam->nam$l_ver = nam->nam$l_type + fna_size[3];
                    nam->nam$b_ver = fna_size[4];
                    if (nam->nam$b_rsl <= nam->nam$b_rss) {
                        memcpy(nam->nam$l_rsa,wccfile->wcf_result,nam->nam$b_rsl);
                    } else {
                        return RMS$_RSS;
                    }
                }
                memcpy(&wccfile->wcf_fid,&fibblk.fib$w_fid_num,sizeof(struct fiddef));

                return 1;
            }
        } else {
#ifdef DEBUG
            printf("Err: %d\n",sts);
#endif
            if (sts == SS$_BADIRECTORY) {
                if (wcc->wcd_next != NULL) {
                    if (wcc->wcd_next->wcd_status & STATUS_INIT) sts = SS$_NOMOREFILES;
                }
            }
            if (sts == SS$_NOMOREFILES) {
                wcc->wcd_status &= ~STATUS_INIT;
                wcc->wcd_wcc = 0;
                wcc->wcd_reslen = 0;
                if (wcc->wcd_status & STATUS_TMPDIR) {
                    struct WCCDIR *savwcc = wcc;
                    if (wcc->wcd_next != NULL) wcc->wcd_next->wcd_prev = wcc->wcd_prev;
                    if (wcc->wcd_prev != NULL) wcc->wcd_prev->wcd_next = wcc->wcd_next;
                    wcc = wcc->wcd_next;
                    memcpy(wccfile->wcf_result + wcc->wcd_prelen + wcc->wcd_reslen - 6,".DIR;1",6);
                    free(savwcc);
                } else {
                    if ((wccfile->wcf_status & STATUS_RECURSE) && wcc->wcd_prev == NULL) {
                        struct WCCDIR *newwcc;
                        newwcc = (struct WCCDIR *) malloc(sizeof(struct WCCDIR) + 8);
                        newwcc->wcd_next = wcc->wcd_next;
                        newwcc->wcd_prev = wcc;
                        newwcc->wcd_wcc = 0;
                        newwcc->wcd_status = STATUS_TMPDIR;
                        newwcc->wcd_reslen = 0;
                        if (wcc->wcd_next != NULL) {
                            wcc->wcd_next->wcd_prev = newwcc;
                        }
                        wcc->wcd_next = newwcc;
                        memcpy(&newwcc->wcd_dirid,&wcc->wcd_dirid,sizeof(struct fiddef));
                        newwcc->wcd_serdsc.dsc$w_length = 7;
                        newwcc->wcd_serdsc.dsc$a_pointer = newwcc->wcd_sernam;
                        memcpy(newwcc->wcd_sernam,"*.DIR;1",7);
                        newwcc->wcd_prelen = wcc->wcd_prelen;
                        wcc = newwcc;

                    } else {
                        if (wcc->wcd_next != NULL) {
#ifdef DEBUG
                            if (wcc->wcd_next->wcd_prev != wcc) printf("wcd_NEXT corruption\n");
#endif
                            wcc = wcc->wcd_next;        /* backup one level */
                            memcpy(wccfile->wcf_result + wcc->wcd_prelen + wcc->wcd_reslen - 6,".DIR;1",6);
                        } else {
                            sts = RMS$_NMF;
                            break;      /* giveup */
                        }
                    }
                }
            } else {
                if (sts == SS$_NOSUCHFILE) {
                    if (wcc->wcd_prev) {
                        sts = RMS$_DNF;
                    } else {
                        sts = RMS$_FNF;
                    }
                }
                break;          /* error - abort! */
            }
        }
    }
    cleanup_wcf(wccfile);
    if (nam != NULL) nam->nam$l_wcc = 0;
    fab->fab$w_ifi = 0;         /* must dealloc memory blocks! */
    return sts;
}


/* External entry for search function... */

unsigned sys_search(struct FAB *fab)
{
    struct NAM *nam = fab->fab$l_nam;
    struct WCCFILE *wccfile;
    if (nam == NULL) return RMS$_NAM;
    wccfile = (struct WCCFILE *) nam->nam$l_wcc;
    if (wccfile == NULL) return RMS$_WCC;
    return do_search(fab,wccfile);
}



#define DEFAULT_SIZE 120
char default_buffer[DEFAULT_SIZE];
char *default_name = "DKA200:[000000].;";
int default_size[] = {7,8,0,1,1};


/* Function to perform RMS parse.... */

unsigned do_parse(struct FAB *fab,struct WCCFILE **wccret)
{
    struct WCCFILE *wccfile;
    char *fna = fab->fab$l_fna;
    char *dna = fab->fab$l_dna;
    struct NAM *nam = fab->fab$l_nam;
    int sts;
    int fna_size[5] = {0, 0, 0, 0, 0},dna_size[5] = {0, 0, 0, 0, 0};
    if (fab->fab$w_ifi != 0) return RMS$_IFI;
        if (nam != NULL) if (nam->nam$l_wcc == 0) {
            cleanup_wcf((struct WCCFILE *) nam->nam$l_wcc);
            nam->nam$l_wcc = 0;
        }
    /* Break up file specifications... */

    sts = name_delim(fna,fab->fab$b_fns,fna_size);
    if ((sts & 1) == 0) return sts;
    if (dna) {
        sts = name_delim(dna,fab->fab$b_dns,dna_size);
        if ((sts & 1) == 0) return sts;
    }
    /* Make WCCFILE entry for rest of processing */

    {
        wccfile = (struct WCCFILE *) malloc(sizeof(struct WCCFILE) + 256);
        if (wccfile == NULL) return SS$_INSFMEM;
        wccfile->wcf_fab = fab;
        wccfile->wcf_vcb = NULL;
        wccfile->wcf_fcb = NULL;
        wccfile->wcf_status = 0;
        wccfile->wcf_wcd.wcd_status = 0;
    }

    /* Combine file specifications */

    {
        int field,ess = MAX_FILELEN;
        char *esa,*def = default_name;
        esa = wccfile->wcf_result;
        for (field = 0; field < 5; field++) {
            char *src;
            int len = fna_size[field];
            if (len > 0) {
                src = fna;
            } else {
                len = dna_size[field];
                if (len > 0) {
                    src = dna;
                } else {
                    len = default_size[field];
                    src = def;
                }
            }
            fna += fna_size[field];
            if (field == 1) {
                int dirlen = len;
                if (len < 3) {
                    dirlen = len = default_size[field];
                    src = def;
                } else {
                    char ch1 = *(src + 1);
                    char ch2 = *(src + 2);
                    if (ch1 == '.' || (ch1 == '-' &&
                                       (ch2 == '-' || ch2 == '.' || ch2 == ']'))) {
                        char *dir = def;
                        int count = default_size[1] - 1;
                        len--;
                        src++;
                        while (len >= 2 && *src == '-') {
                            len--;
                            src++;
                            if (count < 2 || (count == 7 &&
                                              memcmp(dir,"[000000",7) == 0)) return RMS$_DIR;
                            while (count > 1) {
                                if (dir[--count] == '.') break;
                            }
                        }
                        if (count < 2 && len < 2) {
                            src = "[000000]";
                            dirlen = len = 8;
                        } else {
                            if (*src != '.' && *src != ']') return RMS$_DIR;
                            if (*src == '.' && count < 2) {
                                src++;
                                len--;
                            }
                            dirlen = len + count;
                            if ((ess -= count) < 0) return RMS$_ESS;
                            memcpy(esa,dir,count);
                            esa += count;
                        }
                    }
                }
                fna_size[field] = dirlen;
            } else {
                fna_size[field] = len;
            }
            dna += dna_size[field];
            def += default_size[field];
            if ((ess -= len) < 0) return RMS$_ESS;
            while (len-- > 0) {
                register char ch;
                *esa++ = ch = *src++;
                if (ch == '*' || ch == '%')
                    wccfile->wcf_status |= STATUS_WILDCARD;
            }
        }
        /* Pass back results... */
        if (nam) {
            nam->nam$l_dev = nam->nam$l_esa;
            nam->nam$b_dev = fna_size[0];
            nam->nam$l_dir = nam->nam$l_dev + fna_size[0];
            nam->nam$b_dir = fna_size[1];
            nam->nam$l_name = nam->nam$l_dir + fna_size[1];
            nam->nam$b_name = fna_size[2];
            nam->nam$l_type = nam->nam$l_name + fna_size[2];
            nam->nam$b_type = fna_size[3];
            nam->nam$l_ver = nam->nam$l_type + fna_size[3];
            nam->nam$b_ver = fna_size[4];
            nam->nam$b_esl = esa - wccfile->wcf_result;
            nam->nam$l_fnb = 0;
            if (wccfile->wcf_status & STATUS_WILDCARD) nam->nam$l_fnb = NAM$M_WILDCARD;
            if (nam->nam$b_esl <= nam->nam$b_ess) {
                memcpy(nam->nam$l_esa,wccfile->wcf_result,nam->nam$b_esl);
            } else {
                return RMS$_ESS;
            }
        }
    }
    sts = 1;
        if (nam != NULL) if (nam->nam$b_nop & NAM$M_SYNCHK) sts = 0;

    /* Now build up WCC structures as required */

    if (sts) {
        int dirlen,dirsiz;
        char *dirnam;
        struct WCCDIR *wcc;
        struct DEV *dev;
        sts = device_lookup(fna_size[0],wccfile->wcf_result,0,&dev);
        if ((sts & 1) == 0) return sts;
        if ((wccfile->wcf_vcb = dev->vcb) == NULL) return SS$_DEVNOTMOUNT;
        wcc = &wccfile->wcf_wcd;
        wcc->wcd_prev = NULL;
        wcc->wcd_next = NULL;



        /* find directory name - chop off ... if found */

        dirnam = wccfile->wcf_result + fna_size[0] + 1;
        dirlen = fna_size[1] - 2;       /* Don't include [] */
        if (dirlen >= 3) {
            if (memcmp(dirnam + dirlen - 3,"...",3) == 0) {
                wccfile->wcf_status |= STATUS_RECURSE;
                dirlen -= 3;
                wccfile->wcf_status |= STATUS_WILDCARD;
            }
        }
        /* see if we can find directory in cache... */

        dirsiz = dirlen;
        do {
            char *dirend = dirnam + dirsiz;
            if (dircache(wccfile->wcf_vcb,dirnam,dirsiz,&wcc->wcd_dirid)) break;
            while (dirsiz > 0) {
                dirsiz--;
                if (char_delim[*--dirend & 127]) break;
            }
        } while (1);


        /* Create directory wcc blocks for what's left ... */

        while (dirsiz < dirlen) {
            int seglen = 0;
            char *dirptr = dirnam + dirsiz;
            struct WCCDIR *wcd;
            do {
                if (char_delim[*dirptr++ & 127]) break;
                seglen++;
            } while (dirsiz + seglen < dirlen);
            wcd = (struct WCCDIR *) malloc(sizeof(struct WCCDIR) + seglen + 8);
            wcd->wcd_wcc = 0;
            wcd->wcd_status = 0;
            wcd->wcd_prelen = 0;
            wcd->wcd_reslen = 0;
            memcpy(wcd->wcd_sernam,dirnam + dirsiz,seglen);
            memcpy(wcd->wcd_sernam + seglen,".DIR;1",7);
            wcd->wcd_serdsc.dsc$w_length = seglen + 6;
            wcd->wcd_serdsc.dsc$a_pointer = wcd->wcd_sernam;
            wcd->wcd_prev = wcc;
            wcd->wcd_next = wcc->wcd_next;
            if (wcc->wcd_next != NULL) wcc->wcd_next->wcd_prev = wcd;
            wcc->wcd_next = wcd;
            wcd->wcd_prelen = fna_size[0] + dirsiz + 1; /* Include [. */
            memcpy(&wcd->wcd_dirid,&wccfile->wcf_wcd.wcd_dirid,sizeof(struct fiddef));
            dirsiz += seglen + 1;
        }
        wcc->wcd_wcc = 0;
        wcc->wcd_status = 0;
        wcc->wcd_reslen = 0;
        wcc->wcd_serdsc.dsc$w_length = fna_size[2] + fna_size[3] + fna_size[4];
        wcc->wcd_serdsc.dsc$a_pointer = wcc->wcd_sernam;
        memcpy(wcc->wcd_sernam,wccfile->wcf_result + fna_size[0] + fna_size[1],
               wcc->wcd_serdsc.dsc$w_length);
#ifdef DEBUG
        wcc->wcd_sernam[wcc->wcd_serdsc.dsc$w_length] = '\0';
        printf("Parse spec is %s\n",wccfile->wcf_wcd.wcd_sernam);
        for (dirsiz = 0; dirsiz < 5; dirsiz++) printf("  %d",fna_size[dirsiz]);
        printf("\n");
#endif
    }
    if (wccret != NULL) *wccret = wccfile;
    if (nam != NULL) nam->nam$l_wcc = (int) wccfile;
    return SS$_NORMAL;
}


/* External entry for parse function... */

unsigned sys_parse(struct FAB *fab)
{
    struct NAM *nam = fab->fab$l_nam;
    if (nam == NULL) return RMS$_NAM;
    return do_parse(fab,NULL);
}


/* Function to set default directory (heck we can sneak in the device...)  */

unsigned sys_setddir(struct dsc$descriptor *newdir,unsigned short *oldlen,
                     struct dsc$descriptor *olddir)
{
    unsigned sts = 1;
    if (oldlen != NULL) {
        int retlen = default_size[0] + default_size[1];
        if (retlen > olddir->dsc$w_length) retlen = olddir->dsc$w_length;
        *oldlen = retlen;
        memcpy(olddir->dsc$a_pointer,default_name,retlen);
    }
    if (newdir != NULL) {
        struct FAB fab = cc$rms_fab;
        struct NAM nam = cc$rms_nam;
        fab.fab$l_nam = &nam;
        nam.nam$b_nop |= NAM$M_SYNCHK;
        nam.nam$b_ess = DEFAULT_SIZE;
        nam.nam$l_esa = default_buffer;
        fab.fab$b_fns = newdir->dsc$w_length;
        fab.fab$l_fna = newdir->dsc$a_pointer;
        sts = sys_parse(&fab);
        if (sts & 1) {
            if (nam.nam$b_name + nam.nam$b_type + nam.nam$b_ver > 2) return RMS$_DIR;
            if (nam.nam$l_fnb & NAM$M_WILDCARD) return RMS$_WLD;
            default_name = default_buffer;
            default_size[0] = nam.nam$b_dev;
            default_size[1] = nam.nam$b_dir;
            memcpy(default_name + nam.nam$b_dev + nam.nam$b_dir,".;",3);
        }
    }
    return sts;
}


/* This version of connect only resets record pointer */

unsigned sys_connect(struct RAB *rab)
{
    rab->rab$w_rfa[0] = 0;
    rab->rab$w_rfa[1] = 0;
    rab->rab$w_rfa[2] = 0;
    rab->rab$w_rsz = 0;
    if (rab->rab$l_fab->fab$b_org == FAB$C_SEQ) {
        return 1;
    } else {
        return SS$_NOTINSTALL;
    }
}


/* Disconnect is even more boring */

unsigned sys_disconnect(struct RAB *rab)
{
    return 1;
}



#define IFI_MAX 10
struct WCCFILE *ifi_table[] = {
    NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL
};


/* get for sequential files */

unsigned sys_get(struct RAB *rab)
{
    char delim,*buffer,*output;
    unsigned cpylen,reclen,block,blocks,offset,eofblk;
    unsigned endflg,rfm,sts;
    struct VIOC *vioc;
    struct FCB *fcb = ifi_table[rab->rab$l_fab->fab$w_ifi]->wcf_fcb;

    rfm = rab->rab$l_fab->fab$b_rfm;
    if (rfm == FAB$C_STM || rfm == FAB$C_STMCR || rfm == FAB$C_STMLF) {
        delim = 1;
        if (rfm == FAB$C_STMLF) {
            delim = '\n';
        } else {
            if (rfm == FAB$C_STMCR) delim = '\r';
        }
        reclen = rab->rab$w_usz;
    } else {
        delim = 0;
        if (rfm == FAB$C_FIX) {
            reclen = rab->rab$l_fab->fab$w_mrs;
        }
    }

    offset = rab->rab$w_rfa[2] + rab->rab$w_rsz;
    block = (rab->rab$w_rfa[1] << 16) + rab->rab$w_rfa[0];
    if (block == 0 && offset == 0) {
        block = 1;
    } else {
        if (rab->rab$b_rac != RAB$C_RFA) {
            if (delim) {
                offset++;
            } else {
                if (rfm == FAB$C_VAR) {
                    offset += 2;
                } else {
                    if (rfm == FAB$C_VFC) offset += 2 + rab->rab$l_fab->fab$b_fsz;
                }
                if (offset & 1) offset++;
                if (rab->rab$l_fab->fab$b_rat & FAB$M_BLK) offset = (offset + 511) / 512 * 512;
            }
            block += offset / 512;
        }
    }
    rab->rab$w_rfa[0] = block & 0xffff;
    rab->rab$w_rfa[1] = block >> 16;
    rab->rab$w_rfa[2] = offset = offset % 512;


    eofblk = swapw(fcb->head->fh2$w_recattr.fat$l_efblk);
    if (block > eofblk || (block == eofblk &&
                           offset >= fcb->head->fh2$w_recattr.fat$w_ffbyte)) return RMS$_EOF;
    sts = accesschunk(fcb,block,&vioc,&buffer,&blocks,0,NULL);
    if ((sts & 1) == 0) return sts;

    if (rfm == FAB$C_VAR || rfm == FAB$C_VFC) {
        unsigned short *lenptr = (unsigned short *) (buffer + offset);
        reclen = *lenptr;
        offset += 2;
    }
#ifdef DEBUG
    printf("Block %d Offset %d Reclen %d Eofblk %d\n",block,offset,reclen,eofblk);
#endif

    if (reclen > rab->rab$w_usz) {
        sts = deaccesschunk(vioc,0,0);
        return RMS$_RTB;
    }
    endflg = 0;
    if (block + reclen / 512 >= eofblk) {
        unsigned remaining = (eofblk - block) * 512 - offset +
           fcb->head->fh2$w_recattr.fat$w_ffbyte;
        if (remaining < reclen) {
            reclen = remaining;
            endflg = 1;
        }
    }
    cpylen = 0;
    output = rab->rab$l_ubf;
    while (cpylen < reclen) {
        unsigned seglen = blocks * 512 - offset;
        if (seglen > reclen - cpylen) seglen = reclen - cpylen;
        if (delim) {
            char *inptr = buffer + offset;
            if (delim != 1) {
                while (seglen-- > 0) {
                    char ch = *inptr++;
                    if (ch == delim) {
                        reclen = cpylen;
                        break;
                    }
                    if (cpylen++ < reclen) {
                        *output++ = ch;
                    } else {
                        sts = RMS$_RTB;
                        reclen = cpylen;
                        break;
                    }
                }
            } else {
                while (seglen-- > 0) {
                    char ch = *inptr++;
                    if (ch != 0 || cpylen > 0) {
                        if (ch == '\f' || ch == '\v' || ch == '\n' || ch == '\r') {
                            reclen = cpylen;
                        } else {
                            if (cpylen++ < reclen) {
                                *output++ = ch;
                            } else {
                                sts = RMS$_RTB;
                                reclen = cpylen;
                                break;
                            }
                        }
                    }
                }
            }
            if (cpylen == reclen && endflg) break;
        } else {
            if (rfm == FAB$C_VFC) {
                unsigned fsz = rab->rab$l_fab->fab$b_fsz;
                if (fsz > seglen) fsz = seglen;
                if (cpylen < fsz) {
                    fsz = fsz - cpylen;
                    if (rab->rab$l_rhb) memcpy(rab->rab$l_rhb + cpylen,buffer + offset,fsz);
                    cpylen += fsz;
                    offset += fsz;
                    seglen -= fsz;
                }
            }
            memcpy(output,buffer + offset,seglen);
            output += seglen;
            cpylen += seglen;
        }
        if (cpylen < reclen) {
            sts = deaccesschunk(vioc,0,0);
            block += blocks;
            if (block > eofblk) return RMS$_EOF;
            sts = accesschunk(fcb,block,&vioc,&buffer,&blocks,0,NULL);
            if ((sts & 1) == 0) return sts;
            offset = 0;
        }
    }
    if (rfm == FAB$C_VFC) {
        rab->rab$w_rsz = cpylen - rab->rab$l_fab->fab$b_fsz;;
    } else {
        rab->rab$w_rsz = cpylen;
    }
    deaccesschunk(vioc,0,1);
    return sts;
}


/* display to fill fab & xabs with info from the file header... */

unsigned sys_display(struct FAB *fab)
{
    struct XABDAT *xab = fab->fab$l_xab;

    struct HEAD *head = ifi_table[fab->fab$w_ifi]->wcf_fcb->head;
    unsigned short *pp = (unsigned short *) head;
    struct IDENT *id = (struct IDENT *) (pp + head->fh2$b_idoffset);
    int ifi_no = fab->fab$w_ifi;
    if (ifi_no == 0 || ifi_no >= IFI_MAX) return RMS$_IFI;
    fab->fab$l_alq = swapw(head->fh2$w_recattr.fat$l_hiblk);
    fab->fab$b_bks = head->fh2$w_recattr.fat$b_bktsize;
    fab->fab$w_deq = head->fh2$w_recattr.fat$w_defext;
    fab->fab$b_fsz = head->fh2$w_recattr.fat$b_vfcsize;
    fab->fab$w_gbc = head->fh2$w_recattr.fat$w_gbc;
    fab->fab$w_mrs = head->fh2$w_recattr.fat$w_maxrec;
    fab->fab$b_org = head->fh2$w_recattr.fat$b_rtype & 0xf0;
    fab->fab$b_rfm = head->fh2$w_recattr.fat$b_rtype & 0x0f;
    fab->fab$b_rat = head->fh2$w_recattr.fat$b_rattrib;
    while (xab != NULL) {
        switch (xab->xab$b_cod) {
            case XAB$C_DAT:
                memcpy(&xab->xab$q_bdt,&id->fi2$q_bakdate,sizeof(struct TIME));
                memcpy(&xab->xab$q_cdt,&id->fi2$q_credate,sizeof(struct TIME));
                memcpy(&xab->xab$q_edt,&id->fi2$q_expdate,sizeof(struct TIME));
                memcpy(&xab->xab$q_rdt,&id->fi2$q_revdate,sizeof(struct TIME));
                xab->xab$w_rvn = id->fi2$w_revision;
                break;
            case XAB$C_FHC:{
                    struct XABFHC *fhc = (struct XABFHC *) xab;
                    fhc->xab$b_atr = head->fh2$w_recattr.fat$b_rattrib;
                    fhc->xab$b_bkz = head->fh2$w_recattr.fat$b_bktsize;
                    fhc->xab$w_dxq = head->fh2$w_recattr.fat$w_defext;
                    fhc->xab$l_ebk = swapw(head->fh2$w_recattr.fat$l_efblk);
                    fhc->xab$w_ffb = head->fh2$w_recattr.fat$w_ffbyte;
                    if (fhc->xab$l_ebk == 0) {
                        fhc->xab$l_ebk = fab->fab$l_alq;
                        if (fhc->xab$w_ffb == 0) fhc->xab$l_ebk++;
                    }
                    fhc->xab$w_gbc = head->fh2$w_recattr.fat$w_gbc;
                    fhc->xab$l_hbk = swapw(head->fh2$w_recattr.fat$l_hiblk);
                    fhc->xab$b_hsz = head->fh2$w_recattr.fat$b_vfcsize;
                    fhc->xab$w_lrl = head->fh2$w_recattr.fat$w_maxrec;
                    fhc->xab$w_verlimit = head->fh2$w_recattr.fat$w_versions;
                }
                break;
            case XAB$C_PRO:{
                    struct XABPRO *pro = (struct XABPRO *) xab;
                    pro->xab$w_pro = head->fh2$w_fileprot;
                    memcpy(&pro->xab$l_uic,&head->fh2$l_fileowner,4);
                }
        }
        xab = xab->xab$l_nxt;
    }

    return 1;
}


/* close a file */

unsigned sys_close(struct FAB *fab)
{
    int sts;
    int ifi_no = fab->fab$w_ifi;
    if (ifi_no < 1 || ifi_no >= IFI_MAX) return RMS$_IFI;
    sts = deaccessfile(ifi_table[ifi_no]->wcf_fcb);
    if (sts & 1) {
        ifi_table[ifi_no]->wcf_fcb = NULL;
        if (ifi_table[ifi_no]->wcf_status & STATUS_TMPWCC) {
            cleanup_wcf(ifi_table[ifi_no]);
            if (fab->fab$l_nam != NULL) fab->fab$l_nam->nam$l_wcc = 0;
        }
        fab->fab$w_ifi = 0;
        ifi_table[ifi_no] = NULL;
    }
    return sts;
}


/* open a file */

unsigned sys_open(struct FAB *fab)
{
    unsigned sts;
    int ifi_no = 1;
    int wcc_flag = 0;
    struct WCCFILE *wccfile = NULL;
    struct NAM *nam = fab->fab$l_nam;
    if (fab->fab$w_ifi != 0) return RMS$_IFI;
    while (ifi_table[ifi_no] != NULL && ifi_no < IFI_MAX) ifi_no++;
    if (ifi_no >= IFI_MAX) return RMS$_IFI;
    if (nam != NULL) {
        wccfile = (struct WCCFILE *) nam->nam$l_wcc;
    }
    if (wccfile == NULL) {
        sts = do_parse(fab,&wccfile);
        if (sts & 1) {
            wcc_flag = 1;
            if (wccfile->wcf_status & STATUS_WILDCARD) {
                sts = RMS$_WLD;
            } else {
                sts = do_search(fab,wccfile);
            }
            wccfile->wcf_status |= STATUS_TMPWCC;
        }
    } else {
        sts = 1;
    }
    if (sts & 1) sts = accessfile(wccfile->wcf_vcb,&wccfile->wcf_fid,&wccfile->wcf_fcb,0);
    if (sts & 1) {
        ifi_table[ifi_no] = wccfile;
        fab->fab$w_ifi = ifi_no;
        sys_display(fab);
    }
    if (wcc_flag && ((sts & 1) == 0)) {
        cleanup_wcf(wccfile);
        if (nam != NULL) nam->nam$l_wcc = 0;
    }
    return sts;
}


/* blow away a file */

unsigned sys_erase(struct FAB *fab)
{
    unsigned sts;
    int ifi_no = 1;
    int wcc_flag = 0;
    struct WCCFILE *wccfile = NULL;
    struct NAM *nam = fab->fab$l_nam;
    if (fab->fab$w_ifi != 0) return RMS$_IFI;
    while (ifi_table[ifi_no] != NULL && ifi_no < IFI_MAX) ifi_no++;
    if (ifi_no >= IFI_MAX) return RMS$_IFI;
    if (nam != NULL) {
        wccfile = (struct WCCFILE *) fab->fab$l_nam->nam$l_wcc;
    }
    if (wccfile == NULL) {
        sts = do_parse(fab,&wccfile);
        if (sts & 1) {
            wcc_flag = 1;
            if (wccfile->wcf_status & STATUS_WILDCARD) {
                sts = RMS$_WLD;
            } else {
                sts = do_search(fab,wccfile);
            }
        }
    } else {
        sts = 1;
    }
    if (sts & 1) {
        struct fibdef fibblk;
        struct dsc$descriptor fibdsc,serdsc;
        fibdsc.dsc$w_length = sizeof(struct fibdef);
        fibdsc.dsc$a_pointer = (char *) &fibblk;
        serdsc.dsc$w_length = wccfile->wcf_wcd.wcd_reslen;
        serdsc.dsc$a_pointer = wccfile->wcf_result + wccfile->wcf_wcd.wcd_prelen;
        memcpy(&fibblk.fib$w_did_num,&wccfile->wcf_wcd.wcd_dirid,sizeof(struct fiddef));
        fibblk.fib$w_nmctl = 0;
        fibblk.fib$l_acctl = 0;
        fibblk.fib$w_fid_num = 0;
        fibblk.fib$w_fid_seq = 0;
        fibblk.fib$b_fid_rvn = 0;
        fibblk.fib$b_fid_nmx = 0;
        fibblk.fib$l_wcc = 0;
        sts = direct(wccfile->wcf_vcb,&fibdsc,&serdsc,NULL,NULL,1);
        if (sts & 1) sts = accesserase(wccfile->wcf_vcb,&wccfile->wcf_fid);
    }
    if (wcc_flag) {
        cleanup_wcf(wccfile);
        if (nam != NULL) nam->nam$l_wcc = 0;
    }
    return sts;
}
