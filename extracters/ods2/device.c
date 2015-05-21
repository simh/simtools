/* Device.c v1.2  Module to remember and find devices...*/

/*
        This is part of ODS2 written by Paul Nankervis,
        email address:  Paulnank@au1.ibm.com

        ODS2 is distributed freely for all members of the
        VMS community to use. However all derived works
        must maintain comments in their source to acknowledge
        the contibution of the original author.
*/

/* Should have mechanism to return actual device name... */

/*  This module is simple enough - it just keeps track of
    device names and initialization... */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <ctype.h>
#include "ssdef.h"
#include "access.h"
#include "phyio.h"


int devcmp(unsigned keylen,void *key,void *node)
{
    register struct DEV *devnode = (struct DEV *) node;
    register int cmp = keylen - devnode->devlen;
    if (cmp == 0) {
        register unsigned len = keylen;
        register char *keynam = (char *) key;
        register char *devnam = devnode->devnam;
        while (len-- > 0) {
            cmp = toupper(*keynam++) - toupper(*devnam++);
            if (cmp != 0) break;
        }
    }
    return cmp;
}

struct DEV *dev_root = NULL;

unsigned device_lookup(unsigned devlen,char *devnam,
                       int create,struct DEV **retdev)
{
    register struct DEV *dev;
    register unsigned sts,devsiz = 0;
    unsigned devcreate = 0;
    while (devsiz < devlen) {
        if (devnam[devsiz] == ':') break;
        devsiz++;
    }
    if (create) devcreate = sizeof(struct DEV) + devsiz + 2;
    dev = (struct DEV *) cachesearch((void **) &dev_root,0,devsiz,
                                     (void *) devnam,devcmp,&devcreate);
    if (dev == NULL) {
        if (create) {
            sts = SS$_INSFMEM;
        } else {
            sts = SS$_NOSUCHDEV;
        }
    } else {
        struct phyio_info info;
        *retdev = dev;
        if (create && (devcreate == 0)) {
            memcpy(dev->devnam,devnam,devsiz);
            memcpy(dev->devnam + devsiz,":",2);
            dev->devlen = devsiz;
            sts = phyio_init(devsiz + 1,dev->devnam,&dev->handle,&info);
            if (sts & 1) {
                dev->status = info.status;
                dev->sectors = info.sectors;
                dev->sectorsize = info.sectorsize;
            } else {
                cacheuntouch((struct CACHE *) dev,0,0);
                cachefree((struct CACHE *) dev);
            }
        } else {
            sts = 1;
        }
    }
    return sts;
}
