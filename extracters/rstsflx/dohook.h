extern long findsym(const char * sym);
extern void setptr(long blk , long size , void * buf);
extern int silchk(firqb * f , savsilindex * x);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void readfile(firqb * f , long count , void * buf);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void * getdata(firqb * f , long address);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void cleanup(void);
extern void dohook(int argc , char ** argv);
