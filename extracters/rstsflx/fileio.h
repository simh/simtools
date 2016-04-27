extern long seqio(firqb * f , long iolen , iohandler io , void * buffer);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void openfile(firqb * f);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void initrandom(firqb * f);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void fileseek(firqb * f , long vbn);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern long getfile(FILE * to , firqb * f , int binary);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern long extfile(firqb * f , long blocks);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern long writefile(firqb * f , long count);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern long putfile(FILE * from , firqb * f , int binary);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern int textfile(char * name);
