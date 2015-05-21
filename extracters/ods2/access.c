/* Access.c v1.2 */

/*
        This is part of ODS2 written by Paul Nankervis,
        email address:  Paulnank@au1.ibm.com

        ODS2 is distributed freely for all members of the
        VMS community to use. However all derived works
        must maintain comments in their source to acknowledge
        the contibution of the original author.
*/

/*      This module implements 'accessing' files on an ODS2
        disk volume. It uses its own low level interface to support
        'higher level' APIs. For example it is called by the
        'RMS' routines. I also had a module to implement
        SYS$ASSIGN, SYS$QIOW, etc which called these routines,
        the directory routines, physical I/O etc.... */

/*
        Oh to have time to make mount do a bit more checking... :-(
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include "ssdef.h"
#include "access.h"
#include "phyio.h"


#define DEBUGx on






/* checksum: to produce block checksum values... */

unsigned short checksum(unsigned short *block)
{
    register int count = 255;
    register unsigned result = 0;
    register unsigned short *ptr = block;
    do {
        result += *ptr++;
    } while (--count > 0);
    return result;
}



/* deaccesshead: release header from INDEXF... */

unsigned deaccesshead(struct VIOC *vioc,struct HEAD *head,unsigned modmask)
{
#ifdef DEBUG
    printf("Deaccessing header %x\n",vioc->cache.keyval);
#endif
    if (modmask) head->fh2$w_checksum = checksum((u_word *) head);
    return deaccesschunk(vioc,modmask,1);
}


/* accesshead: find file or extension header from INDEXF... */

unsigned accesshead(struct VCB *vcb,struct fiddef *fid,
                    struct VIOC **vioc,struct HEAD **headbuff,
                    unsigned *modmask,unsigned wrtflg)
{
    register unsigned sts;
    register struct VCBDEV *vcbdev;
    register unsigned idxblk = fid->fid$w_num + (fid->fid$b_nmx << 16) - 1;
    if (fid->fid$b_rvn > vcb->devices) return SS$_NOSUCHFILE;
    if (fid->fid$b_rvn < 2) {
        vcbdev = vcb->vcbdev;
    } else {
        vcbdev = &vcb->vcbdev[fid->fid$b_rvn - 1];
    }
    if (vcbdev->dev == NULL) return SS$_NOSUCHFILE;
    if (wrtflg && ((vcb->status & VCB_WRITE) == 0)) return SS$_WRITLCK;
#ifdef DEBUG
    printf("Accessing header (%d,%d,%d)\n",(fid->fid$b_nmx << 16) +
           fid->fid$w_num,fid->fid$w_seq,fid->fid$b_rvn);
#endif
    idxblk += vcbdev->home.hm2$w_ibmapvbn + vcbdev->home.hm2$w_ibmapsize;
    if (idxblk >= swapw(vcbdev->idxfcb->head->fh2$w_recattr.fat$l_efblk)) return SS$_NOSUCHFILE;
    sts = accesschunk(vcbdev->idxfcb,idxblk,vioc,(char **) headbuff,
                      NULL,wrtflg ? 1 : 0,modmask);
    if (sts & 1) {
        register struct HEAD *head = *headbuff;
        if (checksum((u_word *) head) != head->fh2$w_checksum) {
#ifdef DEBUG
            printf("Accesshead checksum %d != %d\n",checksum((u_word *) head),head->fh2$w_checksum);
#endif
            sts = SS$_NOSUCHFILE;
        } else {
            if (head->fh2$w_fid.fid$w_num != fid->fid$w_num ||
                head->fh2$w_fid.fid$b_nmx != fid->fid$b_nmx ||
                head->fh2$w_fid.fid$w_seq != fid->fid$w_seq ||
                (head->fh2$w_fid.fid$b_rvn != fid->fid$b_rvn &&
                 head->fh2$w_fid.fid$b_rvn != 0)) {
#ifdef DEBUG
                printf("Accesshead fileid doesn't match %d %d %d\n",
                       fid->fid$w_num,fid->fid$w_seq,fid->fid$b_rvn);
#endif
                sts = SS$_NOSUCHFILE;
            } else {
                if (head->fh2$b_idoffset < 38 ||
                    head->fh2$b_idoffset > head->fh2$b_mpoffset ||
                    head->fh2$b_mpoffset > head->fh2$b_acoffset ||
                    head->fh2$b_acoffset > head->fh2$b_rsoffset ||
                    head->fh2$b_map_inuse > (head->fh2$b_acoffset - head->fh2$b_mpoffset)) {
#ifdef DEBUG
                    printf("Accesshead areas incorrect\n");
#endif
                    sts = SS$_NOSUCHFILE;
                }
            }
        }
        if ((sts & 1) == 0) deaccesschunk(*vioc,0,0);
    }
    return sts;
}






unsigned getwindow(struct FCB *fcb,unsigned vbn,unsigned *phyrvn,unsigned *phyblk,unsigned *phylen);

/* This routine has bugs and does NOT work properly yet!!!!
   It may be something simple but I haven't had time to look...
   So DON'T use mount/write!!!  */

unsigned deallocfile(struct FCB *fcb)
{
    register unsigned sts = 1;
    /*
        First mark all file clusters as free in BITMAP.SYS
    */
    register unsigned vbn = 1;
    while (vbn <= fcb->hiblock) {
        register unsigned sts;
        unsigned rvn,mapblk,maplen;
        register struct VCBDEV *vcbdev;
        sts = getwindow(fcb,vbn,&rvn,&mapblk,&maplen);
        if ((sts & 1) == 0) break;
        if (rvn > fcb->vcb->devices) break;
        if (rvn < 2) {
            vcbdev = fcb->vcb->vcbdev;
        } else {
            vcbdev = &fcb->vcb->vcbdev[rvn - 1];
        }
        if (vcbdev->dev == NULL) {
            break;
        } else {
            unsigned *bitmap,blkcount,modmask;
            struct VIOC *vioc;
            register unsigned clustersz = vcbdev->home.hm2$w_cluster;
            register unsigned clusterno = mapblk / clustersz;
            sts = accesschunk(vcbdev->mapfcb,clusterno / 4096 + 2,
                              &vioc,(char **) &bitmap,&blkcount,1,&modmask);
            if (sts & 1) {
                register unsigned wordno = (clusterno % 4096) / (sizeof(unsigned) * 8);
                register unsigned bitno = clusterno % (sizeof(unsigned) * 8);
                do {
                    register unsigned mask = 1 << bitno;
                    vbn += clustersz;
                    maplen -= clustersz;
                    while (maplen > 0 && ++bitno < (sizeof(unsigned) * 8)) {
                        mask |= (mask << 1);
                        vbn += clustersz;
                        maplen -= clustersz;
                    }
                    bitmap[wordno++] |= mask;
                    bitno = 0;
                } while (maplen > 0 && wordno < blkcount / (sizeof(unsigned) * 8));
                sts = deaccesschunk(vioc,modmask,1);
            }
        }
    }
    /*
        Now reset file header bit map in INDEXF.SYS and
        update each of the file headers...
    */
    {
        unsigned rvn = fcb->rvn;
        unsigned modmask = fcb->modmask;
        struct HEAD *head = fcb->head;
        struct VIOC *headvioc = fcb->headvioc;
        do {
            struct fiddef extfid;
            register struct VCBDEV *vcbdev;
            if (rvn > fcb->vcb->devices) break;
            if (rvn < 2) {
                vcbdev = fcb->vcb->vcbdev;
            } else {
                vcbdev = &fcb->vcb->vcbdev[rvn - 1];
            }
            if (vcbdev->dev == NULL) {
                break;
            } else {
                unsigned *bitmap,viocmask;
                struct VIOC *vioc;
                register unsigned fileno = (head->fh2$w_fid.fid$b_nmx << 16) +
                         head->fh2$w_fid.fid$w_num - 1;
                register unsigned idxblk = fileno / 4096 +
                    vcbdev->home.hm2$w_cluster * 4 + 1;
                sts = accesschunk(vcbdev->idxfcb,idxblk,&vioc,
                                  (char **) &bitmap,NULL,1,&viocmask);
                if (sts & 1) {
                    bitmap[(fileno % 4096) / (sizeof(unsigned) * 8)] &=
                         ~(1 << (fileno % (sizeof(unsigned) * 8)));
                    sts = deaccesschunk(vioc,viocmask,1);
                } else {
                    break;
                }
            }
            head->fh2$w_fid.fid$w_num = 0;
            head->fh2$w_fid.fid$b_rvn = 0;
            head->fh2$w_fid.fid$b_nmx = 0;
            head->fh2$w_checksum = 0;
            memcpy(&extfid,&fcb->head->fh2$w_ext_fid,sizeof(struct fiddef));
            sts = deaccesshead(headvioc,head,modmask);
            if ((sts & 1) == 0) break;
            if (extfid.fid$b_rvn == 0) {
                extfid.fid$b_rvn = rvn;
            } else {
                rvn = extfid.fid$b_rvn;
            }
            if (extfid.fid$w_num != 0 || extfid.fid$b_nmx != 0) {
                sts = accesshead(fcb->vcb,&extfid,&headvioc,&head,&modmask,1);
                if ((sts & 1) == 0) break;
            } else {
                break;
            }
        } while (1);
        if (sts & 1) {
            fcb->headvioc = NULL;
            cacheuntouch(&fcb->cache,0,1);
            cachedelete(&fcb->cache);
        }
    }
    return sts;
}



/* deaccessfile: finish accessing a file.... */

unsigned deaccessfile(struct FCB *fcb)
{
#ifdef DEBUG
    printf("Deaccessing file (%x) reference %d\n",fcb->cache.keyval,fcb->cache.refcount);
#endif
    if (fcb->cache.refcount == 1) {
        register unsigned refcount;
        refcount = cacherefcount((struct CACHE *) fcb->wcb) +
            cacherefcount((struct CACHE *) fcb->vioc);
        if (refcount != 0) {
            printf("File reference counts non-zero %d\n",refcount);
#ifdef DEBUG
            printf("File reference counts non-zero %d %d\n",
                   cacherefcount((struct CACHE *) fcb->wcb),cacherefcount((struct CACHE *) fcb->vioc));
#endif
            return SS$_BUGCHECK;
        }
        if (fcb->cache.status & CACHE_WRITE) {
            if (fcb->head->fh2$l_filechar & FH2$M_MARKDEL) {
                return deallocfile(fcb);
            }
        }
    }
    cacheuntouch(&fcb->cache,1,fcb->cache.status & CACHE_WRITE);
    return SS$_NORMAL;
}


/* Object manager for FCB objects:- we point to one of our
   sub-objects (vioc or wcb) in preference to letting the
   cache routines get us!  But we when run out of excuses
   it is time to clean up the file header...  :-(   */

struct CACHE *fcbmanager(struct CACHE *cacheobj)
{
    register struct FCB *fcb = (struct FCB *) cacheobj;
    if (fcb->cache.refcount == 0) {
        if (fcb->vioc != NULL) {
            if (fcb->vioc->cache.refcount == 0) {
                cacheobj = &fcb->vioc->cache;
            } else {
                cacheobj = NULL;
            }
        } else {
            if (fcb->wcb != NULL) {
                if (fcb->wcb->cache.refcount == 0) {
                    cacheobj = &fcb->wcb->cache;
                } else {
                    cacheobj = NULL;
                }
            } else {
#ifdef DEBUG
                printf("Cleaning up file (%x)\n",fcb->cache.keyval);
#endif
                if (fcb->headvioc != NULL) {
#ifdef DEBUG
                    printf("File header deaccess (%x) %x\n",fcb->cache.keyval,fcb->cache.status);
#endif
                    deaccesshead(fcb->headvioc,fcb->head,fcb->modmask);
                    fcb->headvioc = NULL;
                }
            }
        }
    } else {
        cacheobj = NULL;
    }
    return cacheobj;
}


/* accessfile: open up file for access... */

unsigned accessfile(struct VCB * vcb,struct fiddef * fid,struct FCB **fcbadd,
                    unsigned wrtflg)
{
    register struct FCB *fcb;
    unsigned create = sizeof(struct FCB);
    register unsigned fileno = (fid->fid$b_nmx << 16) + fid->fid$w_num;
#ifdef DEBUG
    printf("Accessing file (%d,%d,%d)\n",(fid->fid$b_nmx << 16) +
           fid->fid$w_num,fid->fid$w_seq,fid->fid$b_rvn);
#endif
    if (fileno < 1) return SS$_BADPARAM;
    if (wrtflg && ((vcb->status & VCB_WRITE) == 0)) return SS$_WRITLCK;
    if (fid->fid$b_rvn > 1) fileno |= ((fid->fid$b_rvn - 1) << 24);
    fcb = cachesearch((void *) &vcb->fcb,fileno,0,NULL,NULL,&create);
    if (fcb == NULL) return SS$_INSFMEM;
    /* If not found make one... */
    if (create == 0) {
        fcb->cache.status |= 0x100;     /* For debugging! */
        fcb->rvn = fid->fid$b_rvn;
        if (fcb->rvn == 0 && vcb->devices > 1) fcb->rvn = 1;
        fcb->vcb = vcb;
        fcb->wcb = NULL;
        fcb->headvioc = NULL;
        fcb->vioc = NULL;
        fcb->cache.objmanager = fcbmanager;
    }
    if (wrtflg) {
        if (fcb->headvioc != NULL && (fcb->cache.status & CACHE_WRITE) == 0) {
            deaccesshead(fcb->headvioc,NULL,0);
            fcb->headvioc = NULL;
        }
        fcb->cache.status |= CACHE_WRITE;
    }
    if (fcb->headvioc == NULL) {
        register unsigned sts;
        if (vcb->idxboot != NULL) {
            *fcbadd = fcb;
            fcb->hiblock = 32767;       /* guess at indexf.sys file size */
            fcb->highwater = 0;
            fcb->head = vcb->idxboot;   /* Load bootup header */
        }
        sts = accesshead(vcb,fid,&fcb->headvioc,&fcb->head,&fcb->modmask,wrtflg);
        if (sts & 1) {
            fcb->hiblock = swapw(fcb->head->fh2$w_recattr.fat$l_hiblk);
            if (fcb->head->fh2$b_idoffset > 39) {
                fcb->highwater = fcb->head->fh2$l_highwater;
            } else {
                fcb->highwater = 0;
            }
        } else {
            fcb->cache.objmanager = NULL;
            cacheuntouch(&fcb->cache,0,0);
            cachefree(&fcb->cache);
            return sts;
        }
    }
    *fcbadd = fcb;
    return SS$_NORMAL;
}


/* accesserase: delete a file... */

unsigned accesserase(struct VCB * vcb,struct fiddef * fid)
{
    struct FCB *fcb;
    register int sts;
    sts = accessfile(vcb,fid,&fcb,1);
    if (sts & 1) {
        if (fcb->cache.refcount == 1) {
            fcb->head->fh2$l_filechar |= FH2$M_MARKDEL;
            sts = deaccessfile(fcb);
        } else {
            sts = deaccessfile(fcb);
            sts = SS$_FILELOCKED;
        }
    }
    return sts;
}





/* dismount: finish processing on a volume */

unsigned dismount(struct VCB * vcb)
{
    register unsigned sts,device;
    struct VCBDEV *vcbdev;
    int expectfiles = vcb->devices;
    int openfiles = cacherefcount(&vcb->fcb->cache);
    if (vcb->status & VCB_WRITE) expectfiles *= 2;
#ifdef DEBUG
    printf("Dismounting disk %d\n",openfiles);
#endif
    sts = SS$_NORMAL;
    if (openfiles != expectfiles) {
        sts = SS$_DEVNOTDISM;
    } else {
        vcbdev = vcb->vcbdev;
        for (device = 0; device < vcb->devices; device++) {
            if (vcbdev->dev != NULL) {
                if (vcb->status & VCB_WRITE) {
                    sts = deaccessfile(vcbdev->mapfcb);
                    vcbdev->idxfcb->headvioc->cache.status |= CACHE_MODIFIED;
                    vcbdev->idxfcb->cache.status &= ~CACHE_WRITE;
                    cacheflush();
                }
                cachedeltree(&vcb->fcb->cache);
                sts = deaccesshead(vcbdev->idxfcb->headvioc,NULL,0);
                vcbdev->idxfcb->headvioc = NULL;
                sts = cacheuntouch(&vcbdev->idxfcb->cache,0,0);
                cachedeltree(&vcb->fcb->cache);
            }
            vcbdev++;
        }
        while (vcb->dircache) cachedelete((struct CACHE *) vcb->dircache);
#ifdef DEBUG
        printf("Post close\n");
        cachedump();
#endif
        free(vcb);
    }
    return sts;
}



/* mount: make disk volume available for processing... */

unsigned mount(unsigned flags,unsigned devices,char *devnam[],char *label[],struct VCB **retvcb)
{
    register unsigned device,sts;
    struct VCB *vcb;
    struct VCBDEV *vcbdev;
    if (sizeof(struct HOME) != 512 || sizeof(struct HEAD) != 512) return SS$_NOTINSTALL;
    vcb = (struct VCB *) malloc(sizeof(struct VCB) + (devices - 1) * sizeof(struct VCBDEV));
    if (vcb == NULL) return SS$_INSFMEM;
    vcb->status = 0;
    if (flags & 1) vcb->status |= VCB_WRITE;
    vcb->fcb = NULL;
    vcb->dircache = NULL;
    vcbdev = vcb->vcbdev;
    for (device = 0; device < devices; device++) {
        sts = SS$_NOSUCHVOL;
        vcbdev->dev = NULL;
        if (strlen(devnam[device])) {
            sts = device_lookup(strlen(devnam[device]),devnam[device],1,&vcbdev->dev);
            if (sts & 1) {
                int hba = 1;    /* Header block address... */
                do {
                    if ((sts = phyio_read(vcbdev->dev->handle,hba,sizeof(struct HOME),(char *) &vcbdev->home)) & 1) {
                        if (memcmp(vcbdev->home.hm2$t_format,"DECFILE11B  ",12) != 0) sts = SS$_BADPARAM;
                    }
                } while ((sts & 1) == 0 && (hba += 1) < 100);
                if (sts & 1) {
                    if (vcbdev->home.hm2$w_checksum2 != checksum((unsigned short *) &vcbdev->home)) {
                        sts = SS$_DATACHECK;
                    } else {
                        struct HEAD idxboot;    /* Local for bootstrapping volume */
                        struct fiddef idxfid = {1,1,0,0};
                        idxfid.fid$b_rvn = device + 1;
                        if ((sts = phyio_read(vcbdev->dev->handle,vcbdev->home.hm2$l_ibmaplbn +
                                              vcbdev->home.hm2$w_ibmapsize,sizeof(struct HEAD),(char *) &idxboot)) & 1) {
                            if (idxboot.fh2$w_fid.fid$w_num != idxfid.fid$w_num ||
                                idxboot.fh2$w_fid.fid$b_nmx != idxfid.fid$b_nmx ||
                                idxboot.fh2$w_fid.fid$w_seq != idxfid.fid$w_seq ||
                                (idxboot.fh2$w_fid.fid$b_rvn != 0 &&
                                 idxboot.fh2$w_fid.fid$b_rvn != idxfid.fid$b_rvn))
                                sts = SS$_DATACHECK;
                            if (idxboot.fh2$w_checksum != checksum((unsigned short *) &idxboot)) sts = SS$_DATACHECK;
                        }
                        vcbdev->idxfcb = NULL;
                        if (sts & 1) {
                            vcb->devices = device + 1;
                            vcb->idxboot = &idxboot;    /* For getwindow to find index file*/
                            sts = accessfile(vcb,&idxfid,&vcbdev->idxfcb,flags & 1);
                            vcbdev->mapfcb = NULL;
                            if (sts & 1) {
                                vcbdev->dev->vcb = vcb;
                                if (flags & 1) {
                                    struct fiddef mapfid = {2,2,0,0};
                                    mapfid.fid$b_rvn = device + 1;
                                    sts = accessfile(vcb,&mapfid,&vcbdev->mapfcb,1);
                                }
                            }
                        }
                    }
                }
            }
            if ((sts & 1) == 0) vcbdev->dev = NULL;
        }
        if (device == 0 && vcbdev->dev == NULL) {
            free(vcb);
            return sts;
        }
        vcbdev++;
    }
    vcb->idxboot = NULL;
    if (retvcb != NULL) *retvcb = vcb;
    return sts;
}


/* wincmp: compare two windows routine - return -1 for less, 0 for match... */
/*    as a by product keep highest previous entry so that if a new window
      is required we don't have to go right back to the initial file header */

int wincmp(unsigned keylen,void *key,void *node)
{
    register struct WCB *wcb = (struct WCB *) node;
    if (keylen < wcb->loblk) {
        return -1;
    } else {
        if (keylen <= wcb->hiblk) {
            return 0;
        } else {
            register struct WCB **prev_wcb = (struct WCB **) key;
            if (*prev_wcb == NULL) {
                *prev_wcb = wcb;
            } else {
                if ((*prev_wcb)->hiblk < wcb->hiblk) *prev_wcb = wcb;
            }
            return 1;
        }
    }
}


/* getwindow: find a window to map VBN to LBN ... */

unsigned getwindow(struct FCB * fcb,unsigned vbn,unsigned *phyrvn,unsigned *phyblk,unsigned *phylen)
{
    register struct WCB *wcb;
    struct WCB *prev_wcb = NULL;
    unsigned create = sizeof(struct WCB);
#ifdef DEBUG
    printf("Accessing window for vbn %d, file (%x)\n",vbn,fcb->cache.keyval);
#endif
    wcb = cachesearch((void *) &fcb->wcb,0,vbn,&prev_wcb,wincmp,&create);
    if (wcb == NULL) return SS$_INSFMEM;
    /* If not found make one... */
    if (create == 0) {
        register unsigned wd_base,wd_exts;
        unsigned prev_hiblk,rvn;
        struct VIOC *vioc;
        struct HEAD *head;
        wcb->cache.status |= 0x200;     /* For debugging! */
        vioc = NULL;
        wd_base = 1;
        rvn = fcb->rvn;
        head = fcb->head;
        prev_hiblk = 0;
        if (prev_wcb != NULL) {
            register unsigned sts;
            register struct fiddef *fid = &prev_wcb->hd_fid;
            register struct fiddef *filefid = &fcb->head->fh2$w_fid;
            if (fid->fid$w_num != filefid->fid$w_num ||
                fid->fid$b_nmx != filefid->fid$b_nmx ||
                ((fid->fid$b_rvn > 1 || fcb->rvn > 1) && fid->fid$b_rvn != fcb->rvn)) {
                wd_base = prev_wcb->hd_base;
                rvn = prev_wcb->hd_fid.fid$b_rvn;
                sts = accesshead(fcb->vcb,fid,&vioc,&head,NULL,0);
                if ((sts & 1) == 0) {
                    cacheuntouch(&wcb->cache,0,0);
                    cachefree(&wcb->cache);
                    return sts;
                }
            }
            prev_hiblk = prev_wcb->hiblk;
        }
        wcb->hd_base = wd_base;
        wd_exts = 0;
#ifdef DEBUG
        printf("Making window %d %d\n",wd_base,prev_hiblk);
#endif
        do {
            register unsigned short *mp = (unsigned short *) head + head->fh2$b_mpoffset;
            register unsigned short *me = mp + head->fh2$b_map_inuse;
            while (mp < me) {
                register unsigned phylen,phyblk;
                switch ((*mp) >> 14) {
                    case 0:
                        phylen = 0;
                        mp++;
                        break;
                    case 1:
                        phylen = ((*mp) & 0377) + 1;
                        phyblk = (((*mp) & 037400) << 8) + mp[1];
                        mp += 2;
                        break;
                    case 2:
                        phylen = ((*mp) & 037777) + 1;
                        phyblk = (mp[2] << 16) + mp[1];
                        mp += 3;
                        break;
                    case 3:
                        phylen = (((*mp) & 037777) << 16) + mp[1] + 1;
                        phyblk = (mp[3] << 16) + mp[2];
                        mp += 4;
                }
                if (phylen > 0 && wd_base > prev_hiblk) {
                    register struct EXT *ext;
                    if (wd_exts == 0) wcb->loblk = wd_base;
                    ext = &wcb->ext[wd_exts++];
                    ext->phylen = phylen;
                    ext->phyblk = phyblk;
                    wd_base += phylen;
                    if (wd_exts >= EXTMAX) {
                        if (wd_base > vbn) {
                            break;
                        } else {
                            wd_exts = 0;
                        }
                    }
                } else {
                    wd_base += phylen;
                }
            }
            if (wd_base > vbn) {
                break;
            } else {
                register unsigned sts;
                struct fiddef extfid;
                memcpy(&extfid,&head->fh2$w_ext_fid,sizeof(struct fiddef));
                if (extfid.fid$b_rvn != 0 && extfid.fid$b_rvn != rvn) {
                    wd_exts = 0;/* Can't let window extend across devices */
                    rvn = extfid.fid$b_rvn;
                } else {
                    extfid.fid$b_rvn = rvn;
                }
                if (vioc != NULL) deaccesshead(vioc,NULL,0);
                sts = accesshead(fcb->vcb,&extfid,&vioc,&head,NULL,0);
                if ((sts & 1) == 0) {
                    cacheuntouch(&wcb->cache,0,0);
                    cachefree(&wcb->cache);
                    return sts;
                }
                wcb->hd_base = wd_base;
            }
        } while (wd_base <= vbn);
        memcpy(&wcb->hd_fid,&head->fh2$w_fid,sizeof(struct fiddef));
        wcb->hd_fid.fid$b_rvn = rvn;
        wcb->hiblk = wd_base - 1;
        wcb->extcount = wd_exts;
        if (vioc != NULL) deaccesshead(vioc,NULL,0);
    } {
        register struct EXT *ext = wcb->ext;
        register unsigned extcnt = wcb->extcount;
        register unsigned togo = vbn - wcb->loblk;
        while (togo >= ext->phylen) {
            togo -= (ext++)->phylen;
            if (extcnt-- < 1) return SS$_BUGCHECK;
        }
        *phyrvn = wcb->hd_fid.fid$b_rvn;
        *phyblk = ext->phyblk + togo;
        *phylen = ext->phylen - togo;
#ifdef DEBUG
        printf("Mapping vbn %d to %d (%d -> %d)[%d] file (%x)\n",
               vbn,*phyblk,wcb->loblk,wcb->hiblk,wcb->hd_base,fcb->cache.keyval);
#endif
        cacheuntouch(&wcb->cache,1,0);
    }
    return SS$_NORMAL;
}


/* Object manager for VIOC objects:- if the object has been
   modified then we need to flush it to disk before we let
   the cache routines do anything to it...
   This version rewrites the whole chunk - but we really should
   just rewrite the blocks modified as indicated by modmask!  */

struct CACHE *viocmanager(struct CACHE * cacheobj)
{
    register struct VIOC *vioc = (struct VIOC *) cacheobj;
    if (vioc->cache.status & CACHE_MODIFIED) {
        register int length = VIOC_CHUNKSIZE;
        register struct FCB *fcb = vioc->fcb;
        register unsigned base = vioc->cache.keyval;
        register char *address = (char *) vioc->data;
        printf("\nviocmanager writing vbn %d\n",base);
        cachetouch(&fcb->cache);
        do {
            register unsigned sts;
            unsigned rvn,mapblk,maplen;
            register struct VCBDEV *vcbdev;
            if (fcb->highwater > 0 && base >= fcb->highwater) break;
            sts = getwindow(fcb,base,&rvn,&mapblk,&maplen);
            if (sts & 1) {
                if (maplen > (unsigned)length) maplen = length;
                if (fcb->highwater > 0 && base + maplen > fcb->highwater) {
                    maplen = fcb->head->fh2$l_highwater - base;
                }
                if (rvn > fcb->vcb->devices) {
                    sts = SS$_NOSUCHFILE;
                } else {
                    if (rvn < 2) {
                        vcbdev = fcb->vcb->vcbdev;
                    } else {
                        vcbdev = &fcb->vcb->vcbdev[rvn - 1];
                    }
                    if (vcbdev->dev == NULL) return NULL;
                    sts = phyio_write(vcbdev->dev->handle,mapblk,maplen * 512,address);
                }
            }
            if ((sts & 1) == 0) {
                return NULL;
            }
            length -= maplen;
            base += maplen;
            address += maplen * 512;
        } while (length > 0);
        cacheuntouch(&fcb->cache,1,0);
        cacheobj->status &= ~CACHE_MODIFIED;
    }
    return cacheobj;
}


/* deaccess a VIOC (chunk of a file) */

unsigned deaccesschunk(struct VIOC *vioc,unsigned modmask,int reuse)
{
#ifdef DEBUG
    printf("Deaccess chunk %8x\n",vioc->cache.keyval);
#endif
    if ((vioc->wrtmask | modmask) == vioc->wrtmask) {
        vioc->modmask |= modmask;
        if (vioc->cache.refcount == 1) vioc->wrtmask = 0;
        return cacheuntouch(&vioc->cache,reuse,modmask);
    } else {
        return SS$_WRITLCK;
    }
}




/* accesschunk: return pointer to a 'chunk' of a file ... */

unsigned accesschunk(struct FCB *fcb,unsigned vbn,struct VIOC **retvioc,
                     char **retbuff,unsigned *retblocks,unsigned wrtblks,
                     unsigned *retmodmask)
{
    /*
        First find cache entry...
    */
    register struct VIOC *vioc;
    unsigned create = sizeof(struct VIOC);
    register unsigned base = (vbn - 1) / VIOC_CHUNKSIZE * VIOC_CHUNKSIZE + 1;
#ifdef DEBUG
    printf("Access chunk %8x %d (%x)\n",base,vbn,fcb->cache.keyval);
#endif
    if (wrtblks && ((fcb->cache.status & CACHE_WRITE) == 0)) return SS$_WRITLCK;
    if (vbn < 1 || vbn > fcb->hiblock) return SS$_ENDOFFILE;
    vioc = cachesearch((void *) &fcb->vioc,base,0,NULL,NULL,&create);
    if (vioc == NULL) return SS$_INSFMEM;
    /*
        If not found make one...
    */
    if (create == 0) {
        register unsigned length;
        register char *address;
        register unsigned mapbase = base;
        vioc->cache.status |= 0x400;    /* For debugging! */
        vioc->fcb = fcb;
        vioc->wrtmask = 0;
        vioc->modmask = 0;
        length = fcb->hiblock - mapbase + 1;
        if (length > VIOC_CHUNKSIZE) length = VIOC_CHUNKSIZE;
        address = (char *) vioc->data;
        do {
            if (fcb->highwater > 0 && mapbase >= fcb->highwater) {
                memset(address,0,length * 512);
                length = 0;
            } else {
                register unsigned sts;
                unsigned rvn,mapblk,maplen;
                register struct VCBDEV *vcbdev;
                sts = getwindow(fcb,mapbase,&rvn,&mapblk,&maplen);
                if (sts & 1) {
                    if (maplen > length) maplen = length;
                    if (fcb->highwater > 0 && mapbase + maplen > fcb->highwater) {
                        maplen = fcb->head->fh2$l_highwater - mapbase;
                    }
                    if (rvn > fcb->vcb->devices) {
                        sts = SS$_NOSUCHFILE;
                    } else {
                        if (rvn < 2) {
                            vcbdev = fcb->vcb->vcbdev;
                        } else {
                            vcbdev = &fcb->vcb->vcbdev[rvn - 1];
                        }
                        if (vcbdev->dev == NULL) return SS$_NOSUCHFILE;
                        sts = phyio_read(vcbdev->dev->handle,mapblk,maplen * 512,address);
                    }
                }
                if ((sts & 1) == 0) {
                    cacheuntouch(&vioc->cache,0,0);
                    cachefree(&vioc->cache);
                    return sts;
                }
                length -= maplen;
                mapbase += maplen;
                address += maplen * 512;
            }
        } while (length > 0);
    }
    if (wrtblks) {
        vioc->cache.status |= CACHE_WRITE;
        vioc->cache.objmanager = viocmanager;
    }
    /*
        Return result to caller...
    */
    *retvioc = vioc;
    *retbuff = vioc->data[vbn - base];
    if (wrtblks || retblocks != NULL || retmodmask != NULL) {
        register unsigned modmask = 0;
        register unsigned blocks = base + VIOC_CHUNKSIZE - vbn;
        if (blocks > fcb->hiblock - vbn) blocks = fcb->hiblock - vbn + 1;
            if (wrtblks) if (blocks > wrtblks) blocks = wrtblks;
        if (retblocks != NULL) *retblocks = blocks;
        if (wrtblks) {
            modmask = 1 << (vbn - base);
            if (blocks > 1) {
                while (--blocks > 0) modmask |= modmask << 1;
            }
            vioc->wrtmask |= modmask;
        }
        if (retmodmask != NULL) *retmodmask = modmask;
    }
    return SS$_NORMAL;
}
