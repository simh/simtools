extern char doquery (void (*printfile)(void *), void *name);
extern void dodisk (int argc, char **argv);
extern void dofiles(int argc , char **argv , commandaction action , int nullflag);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void doexit(int argc , char **argv);
extern commandhandler scanargs(int argc , char **argv);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void initialize_readline (void);
extern char *stripwhite (char *string);
