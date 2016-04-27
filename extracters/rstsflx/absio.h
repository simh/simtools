extern void absname (const char *rname);
extern int absopen (const char *rname, const char *mode);
extern void absseek (long block);
extern long absread (long sec, long count, void *buffer);
extern long abswrite (long sec, long count, void *buffer);
extern void absclose (void);
