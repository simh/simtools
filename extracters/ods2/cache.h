/* Cache.h v1.2    Definitions for cache routines */

/*
        This is part of ODS2 written by Paul Nankervis,
        email address:  Paulnank@au1.ibm.com

        ODS2 is distributed freely for all members of the
        VMS community to use. However all derived works
        must maintain comments in their source to acknowledge
        the contibution of the original author.
*/

#ifndef CACHE_WRITE

#define CACHE_WRITE 1
#define CACHE_MODIFIED 2

struct CACHE {
    struct CACHE **parent;
    struct CACHE *left;
    struct CACHE *right;
    struct CACHE *nxtcache;
    struct CACHE *lstcache;
    struct CACHE *(*objmanager) (struct CACHE * cacheobj);
    unsigned keyval;
    unsigned status;
    int refcount;
};

void cacheshow(void);
void cachedump(void);
void cacheprint(struct CACHE *cacheobj,int level);
void cacheflush(void);
int cacherefcount(struct CACHE *cacheobj);
void cachedeltree(struct CACHE *cacheobj);
void cachetouch(struct CACHE *cacheobj);
unsigned cacheuntouch(struct CACHE *cacheobj,unsigned reuse,unsigned modflg);
struct CACHE *cachefree(struct CACHE *cacheobj);
struct CACHE *cachedelete(struct CACHE *cacheobj);
struct CACHE *cachemake(struct CACHE **parent,unsigned length);
void *cachesearch(void **root,unsigned keyval,unsigned keylen,void *key,
                  int (*cmpfunc) (unsigned keylen,void *key,void *node),
                  unsigned *createsize);
#endif
