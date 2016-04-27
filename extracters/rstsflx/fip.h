extern void fbwrite(void);
extern void checkwrite(void);
extern void fbread(long block);
extern void readdcn(long dcn);
extern int ulk(word link);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void readlk2(word link);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern int readlk(word link);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern int readlktbl(word link);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern long scanbytes(long start , long last , int clusiz , long clucount);
extern long scanbits(long start , long last , int clusiz , long clucount);
extern long getclu(int clusiz , long size);
extern void retclu(long pos , int clusiz);
extern void readlabel(void);
extern void setppn(firqb * f , int proj , int prog , word ppnent , int which);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern word nextppn(firqb * f , int which);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern word initfilescan(firqb * f , int which);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern int wmatch(const char * wn , const char * n);
extern int nextfileindir(firqb * f);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern int nextfile(firqb * f);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern int findfile(firqb * f , const char * name);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern int findqb(const firqb * f);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void updqb(const firqb * f , long delta);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void upddlw(const firqb * f);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void upddla(const firqb * f);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern int extdir2(int newcm , int clusiz , byte flags , const ufdlabel * newl);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern int extdir(void);
extern int allocufd(word dirne , firqb * f);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern int getent(void);
extern void retent(word link);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void retfile(word nlink);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void delfile(firqb * f);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern int crefile(firqb * f , const word16 * rtsname,
		const ufdrms1 *rms1, const ufdrms2 *rms2);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern int protfile(firqb * f);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern int makedir(firqb * f , int newclu);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern int remdir(firqb * f , word ne);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void findsat(void);
extern void rmount(void);
extern void rmountrw(void);
extern void rumount(void);
extern void rumountrw(void);
