/* Direct.c v1.2 */

/*
        This is part of ODS2 written by Paul Nankervis,
        email address:  Paulnank@au1.ibm.com

        ODS2 is distributed freely for all members of the
        VMS community to use. However all derived works
        must maintain comments in their source to acknowledge
        the contibution of the original author.
*/

/*  This module does all directory file handling - mostly
    lookups of filenames in directory files... */

/*  This version takes relative version from last seen directory record
    - this is no good if there are multiple directory records for a file!  */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <memory.h>
#include "ssdef.h"
#include "descrip.h"
#include "fibdef.h"
#include "access.h"
#include "direct.h"

#define DEBUGx on


int directlookups = 0;
int directsearches = 0;
int directdels = 0;
int directchecks = 0;
int directmatches = 0;


/* directshow - to print directory statistics */

void directshow(void)
{
    printf("DIRECTSHOW Lookups: %d Searches: %d Deletes: %d Checks: %d Matches: %d\n",
           directlookups,directsearches,directdels,directchecks,directmatches);
}


/* namecheck - take a name specification and return name length without
               the version number, an integer version number, and a wildcard flag */

unsigned namecheck(char *str,int len,int *retlen,int *retver,int *wildflag)
{
    int wildcard = 0;
    char *spcbeg = str;
    register int dots = 0;
    register char *spc = spcbeg;
    register char *spcend = spc + len;
    directchecks++;
    while (spc < spcend) {
        register char ch = *spc++;
        if (ch == '.') {
            if ((spc - spcbeg) > 40) return SS$_BADFILENAME;
            spcbeg = spc;
            if (dots++ > 1) break;
        } else {
            if (ch == ';') {
                break;
            } else {
                if (ch == '*' || ch == '%') {
                    wildcard = 1;
                } else {
                    if (ch == '[' || ch == ']' || ch == ':' ||
                        !isprint(ch)) return SS$_BADFILENAME;
                }
            }
        }
    }
    if ((spc - spcbeg) > 40) return SS$_BADFILENAME;
    *retlen = spc - str - 1;
    dots = 0;
    if (spc < spcend) {
        register char ch = *spc;
        if (ch == '*') {
            if (++spc < spcend) return SS$_BADFILENAME;
            dots = 32768;
            wildcard = 1;
        } else {
            register int sign = 1;
            if (ch == '-') {
                spc++;
                sign = -1;
            }
            while (spc < spcend) {
                ch = *spc++;
                if (!isdigit(ch)) return SS$_BADFILENAME;
                dots = dots * 10 + (ch - '0');
            }
            dots *= sign;
        }
    }
    *retver = dots;
    *wildflag = wildcard;
#ifdef DEBUG
    printf("Namecheck %d %d %d\n",*retlen,*retver,*wildflag);
#endif
    return SS$_NORMAL;
}



#define MAT_LT 0
#define MAT_EQ 1
#define MAT_GT 2
#define MAT_NE 3

/* namematch - compare a name specification with a directory entry
               and determine if there is a match, too big, too small... */

int namematch(char *spec,int speclen,char *entry,int entrylen)
{
    int percent = 0;
    register char *spc = spec,*ent = entry;
    register char *spcend = spc + speclen,*entend = ent + entrylen;
    directmatches++;
    /* See how much matches without wildcards... */
    if (spc < spcend && ent < entend) {
        register int count = speclen;
        if (entrylen < count) count = entrylen;
        do {
            register char sch = *spc,ech = *ent;
                if (sch != ech) if (toupper(sch) != toupper(ech))
                    if (sch == '%') {
                        percent = 1;
                    } else {
                        break;
                    }
            spc++;
            ent++;
        } while (--count > 0);
    }
#ifdef DEBUG
    printf("Namematch %3s %d %3s %d\n",spec,speclen,entry,entrylen);
#endif
    /* Mismatch - return result unless wildcard... */
    if (spc >= spcend) {
        if (ent >= entend) {
            return MAT_EQ;
        } else {
            if (percent) {
                return MAT_NE;
            } else {
                return MAT_GT;  /* Entry longer than search spec */
            }
        }
    } else {
        register int offset = 0;
        if (*spc != '*') {
            if (percent) return MAT_NE;
            if (ent < entend)
                if (toupper(*ent) > toupper(*spc)) return MAT_GT;
            return MAT_LT;
        }
        /* See if we can find a match with wildcards */
        spc++;
        if (spc < spcend) {
            do {
                if (spc >= spcend) {
                    if (ent >= entend) break;
                    spc -= offset;
                    ent -= offset - 1;
                    offset = 0;
                } else {
                    register char sch = toupper(*spc);
                    if (sch == '*') {
                        offset = 0;
                        spc++;
                        if (spc >= spcend) break;
                    } else {
                        if (ent < entend) {
                            register char ech = toupper(*ent);
                            if (sch == ech || sch == '%') {
                                offset++;
                                spc++;
                                ent++;
                            } else {
                                spc -= offset;
                                ent -= offset - 1;
                                offset = 0;
                            }
                        } else {
                            return MAT_NE;
                        }
                    }
                }
            } while (1);
        }
    }
    return MAT_EQ;
}


unsigned freesize(char *buffer)
{
    struct dir$rec *dr = (struct dir$rec *) buffer;
    do {
        register char *nr = (char *) dr + dr->dir$size + 2;
        if (nr >= buffer + 512) break;
        dr = (struct dir$rec *) nr;
    } while (1);
    return (char *) dr - buffer - 2;
}

unsigned insrec(void)
{
    printf("Insert directory record\n");
    return 0;
}

unsigned insent(struct FCB *fcb,struct VIOC *vioc,unsigned curblk,
                struct dir$rec *dr,struct dir$ent *de,
                char *buffer,unsigned eofblk)
{
    printf("Insert directory entry\n");
    if (freesize(buffer) >= sizeof(struct dir$ent)) {
        char *ne = (char *) de + sizeof(struct dir$ent);
        memcpy(de,ne,512 - (ne - buffer));
        dr->dir$size -= sizeof(struct dir$ent);
    }
    return 0;
}

/* delent - delete a directory entry */

unsigned delent(struct FCB *fcb,struct VIOC *vioc,unsigned curblk,
                struct dir$rec *dr,struct dir$ent *de,
                char *buffer,unsigned modmask,unsigned eofblk)
{
    unsigned sts = 1;
    unsigned ent;
    directdels++;
    ent = (dr->dir$size - sizeof(struct dir$rec)
           - dr->dir$namecount + 3) / sizeof(struct dir$ent);
    printf("DELENT ent = %d  %d %d\n",ent,curblk,eofblk);
    if (ent > 1) {
        char *ne = (char *) de + sizeof(struct dir$ent);
        memcpy(de,ne,512 - (ne - buffer));
        dr->dir$size -= sizeof(struct dir$ent);
    } else {
        char *nr = (char *) dr + dr->dir$size + 2;
        if (eofblk == 1 || (char *) dr > buffer ||
            (nr <= buffer + 510 && (unsigned short) *nr < 512)) {
            memcpy(dr,nr,512 - (nr - buffer));
        } else {
            printf("DELENT shrinking file size %d  %d\n",curblk,eofblk);
            while (curblk < eofblk) {
                char *nxtbuffer;
                struct VIOC *nxtvioc;
                unsigned nxtmodmask;
                sts = accesschunk(fcb,++curblk,&nxtvioc,&nxtbuffer,NULL,1,&nxtmodmask);
                if ((sts & 1) == 0) break;
                memcpy(buffer,nxtbuffer,512);
                sts = deaccesschunk(vioc,modmask,1);
                if ((sts & 1) == 0) break;
                buffer = nxtbuffer;
                vioc = nxtvioc;
                modmask = nxtmodmask;
            }
            if (sts & 1) {
                fcb->head->fh2$w_recattr.fat$l_efblk[0] = eofblk >> 16;
                fcb->head->fh2$w_recattr.fat$l_efblk[1] = (eofblk & 0xffff);
                eofblk--;
            }
        }
    }
    {
        unsigned retsts = deaccesschunk(vioc,modmask,1);
        if (sts & 1) sts = retsts;
        return sts;
    }
}


/* retent - return information about a directory entry */

unsigned retent(struct FCB *fcb,struct VIOC *vioc,unsigned curblk,
                struct dir$rec *dr,struct dir$ent *de,struct fibdef *fib,
                unsigned short *reslen,struct dsc$descriptor *resdsc,
                int wildcard)
{
    register int scale = 10;
    register int version = de->dir$version;
    register int length = dr->dir$namecount;
    register char *ptr = resdsc->dsc$a_pointer;
    memcpy(ptr,dr->dir$name,length);
    while (version >= scale) scale *= 10;
    ptr += length++;
    *ptr++ = ';';
    do {
        scale /= 10;
        *ptr++ = version / scale + '0';
        version %= scale;
        length++;
    } while (scale > 1);
    *reslen = length;
    memcpy(&fib->fib$w_fid_num,&de->dir$fid,sizeof(struct fiddef));
    if (fib->fib$b_fid_rvn == 0) fib->fib$b_fid_rvn = fcb->rvn;
    if (wildcard || (fib->fib$w_nmctl & FIB$M_WILD)) {
        fib->fib$l_wcc = curblk;
    } else {
        fib->fib$l_wcc = 0;
    }
    return deaccesschunk(vioc,0,1);
}


/* searchent - search for a directory entry */

unsigned searchent(struct FCB * fcb,
                   struct dsc$descriptor * fibdsc,struct dsc$descriptor * filedsc,
                   unsigned short *reslen,struct dsc$descriptor * resdsc,unsigned eofblk,unsigned action)
{
    register unsigned sts,curblk;
    struct VIOC *vioc = NULL;
    unsigned modmask;
    char *searchspec,*buffer;
    int searchlen,version,wildcard,wcc_flag;
    struct fibdef *fib = (struct fibdef *) fibdsc->dsc$a_pointer;
    directlookups++;

    /* 1) Generate start block (wcc gives start point)
       2) Search for start
       3) Scan until found or too big or end   */

    curblk = fib->fib$l_wcc;
    if (curblk != 0) {
        searchspec = resdsc->dsc$a_pointer;
        sts = namecheck(searchspec,*reslen,&searchlen,&version,&wildcard);
        if (action || wildcard) return SS$_BADFILENAME;
        wcc_flag = 1;
    } else {
        searchspec = filedsc->dsc$a_pointer;
        sts = namecheck(searchspec,filedsc->dsc$w_length,&searchlen,&version,&wildcard);
        if ((action && wildcard) || (action > 1 && version < 0)) return SS$_BADFILENAME;
        wcc_flag = 0;
    }
    if ((sts & 1) == 0) return sts;


    /* Identify starting block...*/

    if (*searchspec == '*' || *searchspec == '%') {
        curblk = 1;
    } else {
        register unsigned loblk = 1,hiblk = eofblk;
        if (curblk < 1 || curblk > eofblk) curblk = (eofblk + 1) / 2;
        while (loblk < hiblk) {
            register int cmp;
            register unsigned newblk;
            register struct dir$rec *dr;
            directsearches++;
            sts = accesschunk(fcb,curblk,&vioc,&buffer,NULL,action ? 1 : 0,&modmask);
            if ((sts & 1) == 0) return sts;
            dr = (struct dir$rec *) buffer;
            if (dr->dir$size > 510) {
                cmp = MAT_GT;
            } else {
                cmp = namematch(searchspec,searchlen,dr->dir$name,dr->dir$namecount);
                if (cmp == MAT_EQ) {
                    if (wildcard || version < 1 || version > 32767) {
                        cmp = MAT_NE;   /* no match - want to find start */
                    } else {
                        register struct dir$ent *de =
                            (struct dir$ent *) (dr->dir$name + ((dr->dir$namecount + 1) & ~1));
                        if (de->dir$version < version) {
                            cmp = MAT_GT;       /* too far... */
                        } else {
                            if (de->dir$version > version) {
                                cmp = MAT_LT;   /* further ahead... */
                            }
                        }
                    }
                }
            }
#ifdef DEBUG
            printf("Direct %6.6s %d %6.6s %d (%d<%d<%d)-> %d\n",
                   searchspec,searchlen,dr->dir$name,dr->dir$namecount,
                   loblk,curblk,hiblk,cmp);
#endif
            switch (cmp) {
                case MAT_LT:
                    if (curblk == fib->fib$l_wcc) {
                        newblk = hiblk = loblk = curblk;
                    } else {
                        loblk = curblk;
                        newblk = (loblk + hiblk + 1) / 2;
                    }
                    break;
                case MAT_GT:
                case MAT_NE:
                    newblk = (loblk + curblk) / 2;
                    hiblk = curblk - 1;
                    break;
                default:
                    newblk = hiblk = loblk = curblk;
            }
            if (newblk != curblk) {
                sts = deaccesschunk(vioc,0,1);
                if ((sts & 1) == 0) return sts;
                vioc = NULL;
                curblk = newblk;
            }
        }
    }


    /* Now to read sequentially to find entry... */

    while ((sts & 1) && curblk <= eofblk) {
        register struct dir$rec *dr;
        if (vioc == NULL) {
            sts = accesschunk(fcb,curblk,&vioc,&buffer,NULL,action ? 1 : 0,&modmask);
            if ((sts & 1) == 0) return sts;
        }
        dr = (struct dir$rec *) buffer;
        do {
            register int cmp;
            register char *nr = (char *) dr + dr->dir$size + 2;
            if (nr >= buffer + 512) break;
            cmp = namematch(searchspec,searchlen,dr->dir$name,dr->dir$namecount);
#ifdef DEBUF
            printf("Direct %6.6s %d %6.6s %d -> %d\n",
                   searchspec,searchlen,dr->dir$name,dr->dir$namecount,cmp);
#endif
            if (cmp == MAT_GT) {
                if (wcc_flag) {
                    wcc_flag = 0;
                    searchspec = filedsc->dsc$a_pointer;
                    sts = namecheck(searchspec,filedsc->dsc$w_length,&searchlen,&version,&wildcard);
                    if ((sts & 1) == 0) break;
                } else {
                    curblk = eofblk;    /* give up */
                    break;
                }
            } else {
                if (cmp == MAT_EQ) {
                    register int relver = 0;
                    register struct dir$ent *de = (struct dir$ent *) (dr->dir$name +
                                                                      ((dr->dir$namecount + 1) & ~1));
                    while ((char *) de < nr) {
                        if (version >= de->dir$version || (version < 1 && version >= relver)) {
                            cmp = MAT_GT;
                            if (version > 32767 || version == relver ||
                                version == de->dir$version) cmp = MAT_EQ;
                            if (wcc_flag) {
                                wcc_flag = 0;
                                searchspec = filedsc->dsc$a_pointer;
                                sts = namecheck(searchspec,filedsc->dsc$w_length,&searchlen,&version,&wildcard);
                                if ((sts & 1) == 0) break;
                                if (namematch(searchspec,searchlen,dr->dir$name,
                                              dr->dir$namecount) != MAT_EQ) {
                                    break;
                                }
                                if (cmp == MAT_EQ) {
                                    cmp = MAT_LT;
                                } else {
                                    cmp = MAT_LT;
                                    if (version >= de->dir$version || (version < 1 && version >= relver)) {
                                        cmp = MAT_GT;
                                        if (version > 32767 || version == relver ||
                                            version == de->dir$version) cmp = MAT_EQ;
                                    }
                                }
                            }
                            if (cmp == MAT_EQ) {
                                switch (action) {
                                    case 0:
                                        return
retent(fcb,vioc,curblk,dr,de,fib,reslen,resdsc,wildcard);
                                    case 1:
                                        return
delent(fcb,vioc,curblk,dr,de,buffer,modmask,eofblk);
                                    default:
                                        sts = SS$_DUPFILENAME;
                                        de = (struct dir$ent *) nr;
                                }
                            } else {
                                if (cmp == MAT_GT) break;
                            }
                        }
                        relver--;
                        de++;
                    }
                    if ((sts & 1) == 0) break;
                    if (action == 2) {
                        return insent(fcb,vioc,curblk,dr,de,buffer,eofblk);
                    }
                }
                dr = (struct dir$rec *) nr;
            }
        } while (1);
        {
            register unsigned dests = deaccesschunk(vioc,0,1);
            vioc = NULL;
            if (sts & 1) sts = dests;
        }
        curblk++;
    }
    if (action == 2) {
        return insrec();
    }
    if (sts & 1) {
        fib->fib$l_wcc = 0;
        if (wcc_flag || wildcard) {
            sts = SS$_NOMOREFILES;
        } else {
            sts = SS$_NOSUCHFILE;
        }
    }
    return sts;
}


/* direct - this routine handles all directory manipulations:-
         action 0 - find directory entry
                1 - delete entry
                2 - create an entry   */

unsigned direct(struct VCB *vcb,struct dsc$descriptor * fibdsc,
                struct dsc$descriptor *filedsc,unsigned short *reslen,
                struct dsc$descriptor *resdsc,unsigned action)
{
    struct FCB *fcb;
    register unsigned sts,eofblk;
    register struct fibdef *fib = (struct fibdef *) fibdsc->dsc$a_pointer;
    sts = accessfile(vcb,(struct fiddef *) & fib->fib$w_did_num,&fcb,action);
    if (sts & 1) {
        if (fcb->head->fh2$l_filechar & FH2$M_DIRECTORY) {
            eofblk = swapw(fcb->head->fh2$w_recattr.fat$l_efblk);
            if (fcb->head->fh2$w_recattr.fat$w_ffbyte == 0) --eofblk;
            sts = searchent(fcb,fibdsc,filedsc,reslen,resdsc,eofblk,action);
        } else {
            sts = SS$_BADIRECTORY;
        }
        {
            register unsigned dests = deaccessfile(fcb);
            if (sts & 1) sts = dests;
        }
    }
    return sts;
}
