/*     ODS2.C v1.2   Mainline ODS2 program   */

/*
        This is part of ODS2 written by Paul Nankervis,
        email address:  Paulnank@au1.ibm.com

        ODS2 is distributed freely for all members of the
        VMS community to use. However all derived works
        must maintain comments in their source to acknowledge
        the contibution of the original author.

        The modules in ODS2 are:-

                ACCESS.C        Routines for accessing ODS2 disks
                CACHE.C         Routines for managing memory cache
                DEVICE.C        Routines to maintain device information
                DIRECT.C        Routines for handling directories
                ODS2.C          The mainline program
                PHYVMS.C        Routine to perform physical I/O
                RMS.C           Routines to handle RMS structures
                VMSTIME.C       Routines to handle VMS times

        On non-VMS platforms PHYVMS.C should be replaced as follows:-

                OS/2            PHYOS2.C
                Windows 95/NT   PHYNT.C

        For example under OS/2 the program is compiled using the GCC
        compiler with the single command:-

                gcc -fdollars-in-identifiers ods2.c,rms.c,direct.c,
                      access.c,device.c,cache.c,phyos2.c,vmstime.c
*/

/*  This version will compile and run using normal VMS I/O by
    defining VMSIO
*/

/*  This is the top level set of routines. It is fairly
    simple minded asking the user for a command, doing some
    primitive command parsing, and then calling a set of routines
    to perform whatever function is required (for example COPY).
    Some routines are implemented in different ways to test the
    underlying routines - for example TYPE is implemented without
    a NAM block meaning that it cannot support wildcards...
    (sorry! - could be easily fixed though!)
*/

#define DEBUGx on
#define VMSIOx on

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "descrip.h"
#include "ssdef.h"

#ifdef VMSIO
#include <starlet.h>
#include <rms.h>
unsigned sys$setddir();
#else
#include "rms.h"
#include "access.h"
#endif


#define PRINT_ATTR (FAB$M_CR | FAB$M_PRN | FAB$M_FTN)



/* keycomp: routine to compare parameter to a keyword - case insensitive! */

int keycomp(char *param,char *keywrd)
{
    while (*param != '\0') {
        if (tolower(*param++) != *keywrd++) return 0;
    }
    return 1;
}


/* checkquals: routine to find a qualifer in a list of possible values */

int checkquals(char *qualset[],int qualc,char *qualv[])
{
    int result = 0;
    while (qualc-- > 0) {
        int i = 0;
        while (qualset[i] != NULL) {
            if (keycomp(qualv[qualc],qualset[i])) {
                result |= 1 << i;
                i = -1;
                break;
            }
            i++;
        }
        if (i >= 0) printf("%%ODS2-W-ILLQUAL, Unknown qualifer '%s' ignored\n",qualv[qualc]);
    }
    return result;
}


/* dir: a directory routine */

char *dirquals[] = {"date","file","size",NULL};

unsigned dir(int argc,char *argv[],int qualc,char *qualv[])
{
    char res[NAM$C_MAXRSS + 1],rsa[NAM$C_MAXRSS + 1];
    int sts,options;
    int filecount = 0;
    struct NAM nam = cc$rms_nam;
    struct FAB fab = cc$rms_fab;
    struct XABDAT dat = cc$rms_xabdat;
    struct XABFHC fhc = cc$rms_xabfhc;
    nam.nam$l_esa = res;
    nam.nam$b_ess = NAM$C_MAXRSS;
    fab.fab$l_nam = &nam;
    fab.fab$l_xab = &dat;
    dat.xab$l_nxt = &fhc;
    fab.fab$l_fna = argv[1];
    fab.fab$b_fns = strlen(fab.fab$l_fna);
    fab.fab$l_dna = "*.*;*";
    fab.fab$b_dns = strlen(fab.fab$l_dna);
    options = checkquals(dirquals,qualc,qualv);
    sts = sys$parse(&fab);
    if (sts & 1) {
        char dir[NAM$C_MAXRSS + 1];
        int namelen;
        int dirlen = 0;
        int dirfiles = 0,dircount = 0;
        int dirblocks = 0,totblocks = 0;
        int printcol = 0;
#ifdef DEBUG
        res[nam.nam$b_esl] = '\0';
        printf("Parse: %s\n",res);
#endif
        nam.nam$l_rsa = rsa;
        nam.nam$b_rss = NAM$C_MAXRSS;
        fab.fab$l_fop = FAB$M_NAM;
        while ((sts = sys$search(&fab)) & 1) {
            if (dirlen != nam.nam$b_dev + nam.nam$b_dir ||
                memcmp(rsa,dir,nam.nam$b_dev + nam.nam$b_dir) != 0) {
                if (dirfiles > 0) {
                    if (printcol > 0) printf("\n");
                    printf("\nTotal of %d file%s",dirfiles,(dirfiles == 1 ? "" : "s"));
                    if (options & 4) {
                        printf(", %d block%s.\n",dirblocks,(dirblocks == 1 ? "" : "s"));
                    } else {
                        fputs(".\n",stdout);
                    }
                }
                dirlen = nam.nam$b_dev + nam.nam$b_dir;
                memcpy(dir,rsa,dirlen);
                dir[dirlen] = '\0';
                printf("\nDirectory %s\n\n",dir);
                filecount += dirfiles;
                totblocks += dirblocks;
                dircount++;
                dirfiles = 0;
                dirblocks = 0;
                printcol = 0;
            }
            rsa[nam.nam$b_rsl] = '\0';
            namelen = nam.nam$b_name + nam.nam$b_type + nam.nam$b_ver;
            if (options == 0) {
                if (printcol > 0) {
                    int newcol = (printcol + 20) / 20 * 20;
                    if (newcol + namelen >= 80) {
                        fputs("\n",stdout);
                        printcol = 0;
                    } else {
                        printf("%*s",newcol - printcol," ");
                        printcol = newcol;
                    }
                }
                fputs(rsa + dirlen,stdout);
                printcol += namelen;
            } else {
                if (namelen > 18) {
                    printf("%s\n                   ",rsa + dirlen);
                } else {
                    printf("%-19s",rsa + dirlen);
                }
                sts = sys$open(&fab);
                if ((sts & 1) == 0) {
                    printf("Open error: %d\n",sts);
                } else {
                    sts = sys$close(&fab);
                    if (options & 2) {
                        char fileid[100];
                        sprintf(fileid,"(%d,%d,%d)",
                                (nam.nam$b_fid_nmx << 16) | nam.nam$w_fid_num,
                                nam.nam$w_fid_seq,nam.nam$b_fid_rvn);
                        printf("  %-22s",fileid);
                    }
                    if (options & 4) {
                        unsigned filesize = fhc.xab$l_ebk;
                        if (fhc.xab$w_ffb == 0) filesize--;
                        printf("%9d",filesize);
                        dirblocks += filesize;
                    }
                    if (options & 1) {
                        char tim[24];
                        struct dsc$descriptor timdsc;
                        timdsc.dsc$w_length = 23;
                        timdsc.dsc$a_pointer = tim;
                        sts = sys$asctim(0,&timdsc,&dat.xab$q_cdt,0);
                        if ((sts & 1) == 0) printf("Asctim error: %d\n",sts);
                        tim[23] = '\0';
                        printf("  %s",tim);
                    }
                    printf("\n");
                }
            }
            dirfiles++;
        }
        if (sts == RMS$_NMF) sts = 1;
        if (printcol > 0) printf("\n");
        if (dirfiles > 0) {
            printf("\nTotal of %d file%s",dirfiles,(dirfiles == 1 ? "" : "s"));
            if (options & 4) {
                printf(", %d block%s.\n",dirblocks,(dirblocks == 1 ? "" : "s"));
            } else {
                fputs(".\n",stdout);
            }
            filecount += dirfiles;
            totblocks += dirblocks;
            if (dircount > 1) {
                printf("\nGrand total of %d director%s, %d file%s",
                       dircount,(dircount == 1 ? "y" : "ies"),
                       filecount,(filecount == 1 ? "" : "s"));
                if (options & 4) {
                    printf(", %d block%s.\n",totblocks,(totblocks == 1 ? "" : "s"));
                } else {
                    fputs(".\n",stdout);
                }
            }
        }
    }
    if (sts & 1) {
        if (filecount < 1) printf("%%DIRECT-W-NOFILES, no files found\n");
    } else {
        printf("%%DIR-E-ERROR Status: %d\n",sts);
    }
    return sts;
}


/* copy: a file copy routine */

#define MAXREC 32767

unsigned copy(int argc,char *argv[],int qualc,char *qualv[])
{
    int sts;
    struct NAM nam = cc$rms_nam;
    struct FAB fab = cc$rms_fab;
    char res[NAM$C_MAXRSS + 1],rsa[NAM$C_MAXRSS + 1];
    int filecount = 0;
    nam.nam$l_esa = res;
    nam.nam$b_ess = NAM$C_MAXRSS;
    fab.fab$l_nam = &nam;
    fab.fab$l_fna = argv[1];
    fab.fab$b_fns = strlen(fab.fab$l_fna);
    sts = sys$parse(&fab);
    if (sts & 1) {
        nam.nam$l_rsa = rsa;
        nam.nam$b_rss = NAM$C_MAXRSS;
        fab.fab$l_fop = FAB$M_NAM;
        while ((sts = sys$search(&fab)) & 1) {
            sts = sys$open(&fab);
            if ((sts & 1) == 0) {
                printf("%%COPY-F-OPENFAIL, Open error: %d\n",sts);
                perror("-COPY-F-ERR ");
            } else {
                struct RAB rab = cc$rms_rab;
                rab.rab$l_fab = &fab;
                if ((sts = sys$connect(&rab)) & 1) {
                    FILE *tof;
                    char name[NAM$C_MAXRSS + 1];
                    unsigned records = 0;
                    {
                        char *out = name,*inp = argv[2];
                        int dot = 0;
                        while (*inp != '\0') {
                            if (*inp == '*') {
                                inp++;
                                if (dot) {
                                    memcpy(out,nam.nam$l_type + 1,nam.nam$b_type - 1);
                                    out += nam.nam$b_type - 1;
                                } else {
                                    unsigned length = nam.nam$b_name;
                                    if (*inp == '\0') length += nam.nam$b_type;
                                    memcpy(out,nam.nam$l_name,length);
                                    out += length;
                                }
                            } else {
                                if (*inp == '.') {
                                    dot = 1;
                                } else {
                                    if (strchr(":]\\/",*inp)) dot = 0;
                                }
                                *out++ = *inp++;
                            }
                        }
                        *out++ = '\0';
                    }
                    tof = fopen(name,"w");
                    if (tof == NULL) {
                        printf("%%COPY-F-OPENOUT, Could not open %s\n",name);
                        perror("-COPY-F-ERR ");
                    } else {
                        char rec[MAXREC + 2];
                        filecount++;
                        rab.rab$l_ubf = rec;
                        rab.rab$w_usz = MAXREC;
                        while ((sts = sys$get(&rab)) & 1) {
                            unsigned rsz = rab.rab$w_rsz;
                            if (fab.fab$b_rat & PRINT_ATTR) rec[rsz++] = '\n';
                            if (fwrite(rec,rsz,1,tof) == 1) {
                                records++;
                            } else {
                                printf("%%COPY-F- fwrite error!!\n");
                                perror("-COPY-F-ERR ");
                                break;
                            }
                        }
                        if (fclose(tof)) {
                            printf("%%COPY-F- fclose error!!\n");
                            perror("-COPY-F-ERR ");
                        }
                    }
                    sys$disconnect(&rab);
                    if (sts == RMS$_EOF) {
                        rsa[nam.nam$b_rsl] = '\0';
                        printf("%%COPY-S-COPIED, %s copied to %s (%d record%s)\n",
                               rsa,name,records,(records == 1 ? "" : "s"));
                        sts = 1;
                    }
                }
                sys$close(&fab);
            }
        }
        if (sts == RMS$_NMF) sts = 1;
    }
    if (sts & 1) {
        if (filecount > 0) printf("%%COPY-S-NEWFILES, %d file%s created\n",
                                  filecount,(filecount == 1 ? "" : "s"));
    } else {
        printf("%%COPY-F-ERROR Status: %d\n",sts);
    }
    return sts;
}


/* diff: a simple file difference routine */

unsigned diff(int argc,char *argv[],int qualc,char *qualv[])
{
    int sts;
    struct FAB fab = cc$rms_fab;
    FILE *tof;
    int records = 0;
    fab.fab$l_fna = argv[1];
    fab.fab$b_fns = strlen(fab.fab$l_fna);
    tof = fopen(argv[2],"r");
    if (tof == NULL) {
        printf("Could not open file %s\n",argv[1]);
        sts = 0;
    } else {
        if ((sts = sys$open(&fab)) & 1) {
            struct RAB rab = cc$rms_rab;
            rab.rab$l_fab = &fab;
            if ((sts = sys$connect(&rab)) & 1) {
                char rec[MAXREC + 2],cpy[MAXREC + 1];
                rab.rab$l_ubf = rec;
                rab.rab$w_usz = MAXREC;
                while ((sts = sys$get(&rab)) & 1) {
                    strcpy(rec + rab.rab$w_rsz,"\n");
                    fgets(cpy,MAXREC,tof);
                    if (strcmp(rec,cpy) != 0) {
                        printf("%%DIFF-F-DIFFERENT Files are different!\n");
                        sts = 4;
                        break;
                    } else {
                        records++;
                    }
                }
                sys$disconnect(&rab);
            }
            sys$close(&fab);
        }
        fclose(tof);
        if (sts == RMS$_EOF) sts = 1;
    }
    if (sts & 1) {
        printf("%%DIFF-I-Compared %d records\n",records);
    } else {
        printf("%%DIFF-F-Error %d in difference\n",sts);
    }
    return sts;
}


/* typ: a file TYPE routine */

unsigned typ(int argc,char *argv[],int qualc,char *qualv[])
{
    int sts;
    int records = 0;
    struct FAB fab = cc$rms_fab;
    fab.fab$l_fna = argv[1];
    fab.fab$b_fns = strlen(fab.fab$l_fna);
    if ((sts = sys$open(&fab)) & 1) {
        struct RAB rab = cc$rms_rab;
        rab.rab$l_fab = &fab;
        if ((sts = sys$connect(&rab)) & 1) {
            char rec[MAXREC + 2];
            rab.rab$l_ubf = rec;
            rab.rab$w_usz = MAXREC;
            while ((sts = sys$get(&rab)) & 1) {
                unsigned rsz = rab.rab$w_rsz;
                if (fab.fab$b_rat & PRINT_ATTR) rec[rsz++] = '\n';
                rec[rsz++] = '\0';
                fputs(rec,stdout);
                records++;
            }
            sys$disconnect(&rab);
        }
        sys$close(&fab);
        if (sts == RMS$_EOF) sts = 1;
    }
    if ((sts & 1) == 0) {
        printf("%%TYPE-F-ERROR Status: %d\n",sts);
    }
    return sts;
}



/* search: a simple file search routine */

unsigned search(int argc,char *argv[],int qualc,char *qualv[])
{
    int sts = 0;
    int filecount = 0;
    int findcount = 0;
    char res[NAM$C_MAXRSS + 1],rsa[NAM$C_MAXRSS + 1];
    struct NAM nam = cc$rms_nam;
    struct FAB fab = cc$rms_fab;
    register char *searstr = argv[2];
    register char firstch = tolower(*searstr++);
    register char *searend = searstr + strlen(searstr);
    {
        char *str = searstr;
        while (str < searend) {
            *str = tolower(*str);
            str++;
        }
    }
    nam = cc$rms_nam;
    fab = cc$rms_fab;
    nam.nam$l_esa = res;
    nam.nam$b_ess = NAM$C_MAXRSS;
    fab.fab$l_nam = &nam;
    fab.fab$l_fna = argv[1];
    fab.fab$b_fns = strlen(fab.fab$l_fna);
    fab.fab$l_dna = "";
    fab.fab$b_dns = strlen(fab.fab$l_dna);
    sts = sys$parse(&fab);
    if (sts & 1) {
        nam.nam$l_rsa = rsa;
        nam.nam$b_rss = NAM$C_MAXRSS;
        fab.fab$l_fop = FAB$M_NAM;
        while ((sts = sys$search(&fab)) & 1) {
            sts = sys$open(&fab);
            if ((sts & 1) == 0) {
                printf("%%SEARCH-F-OPENFAIL, Open error: %d\n",sts);
            } else {
                struct RAB rab = cc$rms_rab;
                rab.rab$l_fab = &fab;
                if ((sts = sys$connect(&rab)) & 1) {
                    int printname = 1;
                    char rec[MAXREC + 2];
                    filecount++;
                    rab.rab$l_ubf = rec;
                    rab.rab$w_usz = MAXREC;
                    while ((sts = sys$get(&rab)) & 1) {
                        register char *strng = rec;
                        register char *strngend = strng + (rab.rab$w_rsz - (searend - searstr));
                        while (strng < strngend) {
                            register char ch = *strng++;
                            if (ch == firstch || (ch >= 'A' && ch <= 'Z' && ch + 32 == firstch)) {
                                register char *str = strng;
                                register char *cmp = searstr;
                                while (cmp < searend) {
                                    register char ch2 = *str++;
                                    ch = *cmp;
                                    if (ch2 != ch && (ch2 < 'A' || ch2 > 'Z' || ch2 + 32 != ch)) break;
                                    cmp++;
                                }
                                if (cmp >= searend) {
                                    findcount++;
                                    rec[rab.rab$w_rsz] = '\0';
                                    if (printname) {
                                        rsa[nam.nam$b_rsl] = '\0';
                                        printf("\n******************************\n%s\n\n",rsa);
                                        printname = 0;
                                    }
                                    fputs(rec,stdout);
                                    if (fab.fab$b_rat & PRINT_ATTR) fputc('\n',stdout);
                                    break;
                                }
                            }
                        }
                    }
                    sys$disconnect(&rab);
                }
                if (sts == SS$_NOTINSTALL) {
                    printf("%%SEARCH-W-NOIMPLEM, file operation not implemented\n");
                    sts = 1;
                }
                sys$close(&fab);
            }
        }
        if (sts == RMS$_NMF || sts == RMS$_FNF) sts = 1;
    }
    if (sts & 1) {
        if (filecount < 1) {
            printf("%%SEARCH-W-NOFILES, no files found\n");
        } else {
            if (findcount < 1) printf("%%SEARCH-I-NOMATCHES, no strings matched\n");
        }
    } else {
        printf("%%SEARCH-F-ERROR Status: %d\n",sts);
    }
    return sts;
}


/* del: you don't want to know! */

unsigned del(int argc,char *argv[],int qualc,char *qualv[])
{
    int sts = 0;
    struct NAM nam = cc$rms_nam;
    struct FAB fab = cc$rms_fab;
    char res[NAM$C_MAXRSS + 1],rsa[NAM$C_MAXRSS + 1];
    int filecount = 0;
    nam.nam$l_esa = res;
    nam.nam$b_ess = NAM$C_MAXRSS;
    fab.fab$l_nam = &nam;
    fab.fab$l_fna = argv[1];
    fab.fab$b_fns = strlen(fab.fab$l_fna);
    printf("WARNING! This bit is broken - volume corruption VERY likely!\n");
    sts = sys$parse(&fab);
    if (sts & 1) {
        if (nam.nam$b_ver < 2) {
            printf("%%DELETE-F-NOVER, you must specify a version!!\n");
        } else {
            nam.nam$l_rsa = rsa;
            nam.nam$b_rss = NAM$C_MAXRSS;
            fab.fab$l_fop = FAB$M_NAM;
            while ((sts = sys$search(&fab)) & 1) {
                sts = sys$erase(&fab);
                if ((sts & 1) == 0) {
                    printf("%%DELETE-F-DELERR, Delete error: %d\n",sts);
                } else {
                    filecount++;
                    rsa[nam.nam$b_rsl] = '\0';
                    printf("%%DELETE-I-DELETED, Deleted %s\n",rsa);
                }
            }
            if (sts == RMS$_NMF) sts = 1;
        }
        if (sts & 1) {
            if (filecount < 1) {
                printf("%%DELETE-W-NOFILES, no files deleted\n");
            }
        } else {
            printf("%%DELETE-F-ERROR Status: %d\n",sts);
        }
    }
    return sts;
}


/* show: the show command */

unsigned show(int argc,char *argv[],int qualc,char *qualv[])
{
    unsigned sts = 1;
    if (keycomp(argv[1],"default")) {
        unsigned short curlen;
        char curdir[NAM$C_MAXRSS + 1];
        struct dsc$descriptor curdsc;
        curdsc.dsc$w_length = NAM$C_MAXRSS;
        curdsc.dsc$a_pointer = curdir;
        if ((sts = sys$setddir(NULL,&curlen,&curdsc)) & 1) {
            curdir[curlen] = '\0';
            printf(" %s\n",curdir);
        } else {
            printf("Error %d getting default\n",sts);
        }
    } else {
        if (keycomp(argv[1],"time")) {
            char timstr[24];
            unsigned short timlen;
            struct dsc$descriptor timdsc;
            timdsc.dsc$w_length = 23;
            timdsc.dsc$a_pointer = timstr;
            sys$asctim(&timlen,&timdsc,NULL,0);
            timstr[timlen] = '\0';
            printf("  %s\n",timstr);
        } else {
            printf("%%SHOW-W-WHAT '%s'?\n",argv[1]);
        }
    }
    return sts;
}

/* set: the set command */

unsigned set(int argc,char *argv[],int qualc,char *qualv[])
{
    unsigned sts = 1;
    if (keycomp(argv[1],"default")) {
        struct dsc$descriptor defdsc;
        defdsc.dsc$a_pointer = argv[2];
        defdsc.dsc$w_length = strlen(defdsc.dsc$a_pointer);
        if (((sts = sys$setddir(&defdsc,NULL,NULL)) & 1) == 0) {
            printf("Error %d setting default to %s\n",sts,argv[2]);
        }
    } else {
        printf("%%SET-W-WHAT '%s'?\n",argv[1]);
    }
    return sts;
}


#ifndef VMSIO

/* The bits we need when we don't have real VMS routines underneath... */

unsigned dodismount(int argc,char *argv[],int qualc,char *qualv[])
{
    struct DEV *dev;
    register int sts = device_lookup(strlen(argv[1]),argv[1],0,&dev);
    if (sts & 1) {
        if (dev->vcb != NULL) {
            sts = dismount(dev->vcb);
        } else {
            sts = SS$_DEVNOTMOUNT;
        }
    }
    if ((sts & 1) == 0) printf("%%DISMOUNT-E-STATUS Error: %d\n",sts);
    return sts;
}

char *mouquals[] = {"write",NULL};

unsigned domount(int argc,char *argv[],int qualc,char *qualv[])
{
    char *dev = argv[1];
    char *lab = argv[2];
    int sts = 1,devices = 0;
    char *devs[100],*labs[100];
    int options = checkquals(mouquals,qualc,qualv);
    while (*lab != '\0') {
        labs[devices++] = lab;
        while (*lab != ',' && *lab != '\0') lab++;
        if (*lab != '\0') {
            *lab++ = '\0';
        } else {
            break;
        }
    }
    devices = 0;
    while (*dev != '\0') {
        devs[devices++] = dev;
        while (*dev != ',' && *dev != '\0') dev++;
        if (*dev != '\0') {
            *dev++ = '\0';
        } else {
            break;
        }
    }
    if (devices > 0) {
        unsigned i;
        struct VCB *vcb;
        sts = mount(options,devices,devs,labs,&vcb);
        if (sts & 1) {
            for (i = 0; i < vcb->devices; i++)
                if (vcb->vcbdev[i].dev != NULL)
                    printf("%%MOUNT-I-MOUNTED, Volume %12.12s mounted on %s\n",
                           vcb->vcbdev[i].home.hm2$t_volname,vcb->vcbdev[i].dev->devnam);
        } else {
            printf("Mount failed with %d\n",sts);
        }
    }
    return sts;
}


void directshow(void);
void phyio_show(void);

/* statis: print some simple statistics */

unsigned statis(int argc,char *argv[],int qualc,char *qualv[])
{
    printf("Statistics:-\n");
    directshow();
    cacheshow();
    phyio_show();
    return 1;
}

/* dump: a simple debugging aid */

unsigned dump(int argc,char *argv[],int qualc,char *qualv[])
{
    printf("Cache Dump:-\n");
    cachedump();
    return 1;
}
#endif


/* help: a routine to print a pre-prepared help text... */

unsigned help(int argc,char *argv[],int qualc,char *qualv[])
{
    printf("\nODS2 v1.2\n");
    printf(" Please send problems/comments to Paulnank@au1.ibm.com\n");
    printf(" Commands are:\n");
    printf("  copy        difference      directory     exit\n");
    printf("  mount       show_default    show_time     search\n");
    printf("  set_default type\n");
    printf(" Example:-\n    $ mount e:\n");
    printf("    $ search e:[vms$common.decc*...]*.h rms$_wld\n");
    printf("    $ set default e:[sys0.sysmgr]\n");
    printf("    $ copy *.com;-1 c:\\*.*\n");
    printf("    $ directory/file/size/date [-.sys*...].%%\n");
    printf("    $ exit\n");
    return 1;
}


/* informaion about the commands we know... */

struct CMDSET {
    char *name;
    unsigned (*proc) (int argc,char *argv[],int qualc,char *qualv[]);
    unsigned int minlen;
    unsigned int minargs;
    unsigned int maxargs;
    unsigned int maxquals;
} cmdset[] = {
    {
        "copy",copy,3,3,3,0
},
    {
        "delete",del,3,2,2,0
},
    {
        "difference",diff,3,3,3,0
},
    {
        "directory",dir,3,1,2,6
},
    {
        "exit",NULL,2,0,0,0
},
    {
        "help",help,2,1,1,0
},
    {
        "show",show,2,2,2,0
},
    {
        "search",search,3,3,3,0
},
    {
        "set",set,3,2,3,0
},
#ifndef VMSIO
    {
        "dismount",dodismount,3,2,2,0
},
    {
        "mount",domount,3,2,3,2
},
    {
        "statistics",statis,3,1,1,0
},
    {
        "dump",dump,3,1,1,0
},
#endif
    {
        "type",typ,3,2,2,0
},
    {
        NULL,NULL,0,0,0,0
}
};


/* cmdexecute: identify and execute a command */

int cmdexecute(unsigned int argc,char *argv[],unsigned int qualc,char *qualv[])
{
    char *ptr = argv[0];
    struct CMDSET *cmd = cmdset;
    unsigned cmdsiz = strlen(ptr);
    while (*ptr != '\0') {
        *ptr = tolower(*ptr);
        ptr++;
    }
    while (cmd->name != NULL) {
        if (cmdsiz >= cmd->minlen && cmdsiz <= strlen(cmd->name)) {
            if (keycomp(argv[0],cmd->name)) {
                if (cmd->proc == NULL) {
                    return 0;
                } else {
                    if (argc < cmd->minargs || argc > cmd->maxargs) {
                        printf("%%ODS2-E-PARAMS, Incorrect number of command parameters\n");
                    } else {
                        if (qualc > cmd->maxquals) {
                            printf("%%ODS2-E-QUALS, Too many command qualifiers\n");
                        } else {
                            (*cmd->proc) (argc,argv,qualc,qualv);
#ifndef VMSIO
                            cacheflush();
#endif
                        }
                    }
                    return 1;
                }
            }
        }
        cmd++;
    }
    printf("%%ODS2-E-ILLCMD, Illegal or ambiguous command '%s'\n",argv[0]);
    return 1;
}

/* cmdsplit: break a command line into its components */

int cmdsplit(char *str)
{
    int argc = 0,qualc = 0;
    char *argv[32],*qualv[32];
    char *sp = str;
    int i;
    for (i = 0; i < 32; i++) argv[i] = qualv[i] = "";
    while (*sp != '\0') {
        while (*sp == ' ') sp++;
        if (*sp != '\0') {
            if (*sp == '/') {
                *sp++ = '\0';
                qualv[qualc++] = sp;
            } else {
                argv[argc++] = sp;
            }
            while (*sp != ' ' && *sp != '/' && *sp != '\0') sp++;
            if (*sp == '\0') {
                break;
            } else {
                if (*sp != '/') *sp++ = '\0';
            }
        }
    }
    if (argc > 0) return cmdexecute(argc,argv,qualc,qualv);
    return 1;
}


/* main: the simple mainline of this puppy... */

int main(int argc,char *argv[])
{
    char str[2048];
    str[sizeof(str)-1] = 0;
    printf(" ODS2 v1.2\n");
    while (1) {
        printf("$> ");
        if (fgets(str, sizeof(str)-1, stdin) == NULL) break;
            if (strlen(str)) if ((cmdsplit(str) & 1) == 0) break;
    }
    return 1;
}
