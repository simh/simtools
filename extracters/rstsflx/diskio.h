extern void getsize (const char *name, long *tsize, long *rsize, int *dec166);
extern long adjsize (long size);
extern void setrname(void);
extern void ropen(const char * mode);
extern void rseek(long block);
extern void rread(long block , long size , void * buffer);
extern void rwrite(long block , long size , void * buffer);
extern void rclose(void);
