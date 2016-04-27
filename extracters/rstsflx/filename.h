extern const char * parsename(const char * p , firqb * out);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern const char * parsenameext(const char * p , firqb * out);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern int parse(const char * p , firqb * out);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern char * r50toascii(word16 r50 , char * string , int space);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern char * r50toascii2(word16 r50[] , char * string , int space);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void r50filename(word16 r50[] , char * name , int space);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void printfqbppn(const firqb * f);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void printfqbname(const firqb * f);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void printcurname(const firqb * f);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void mergename(char * iname , firqb * oname , int tree);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern word cvtr50(const char * in);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void cvtnametor50(const char * in , word16 * out);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
extern void cvtnameexttor50(const char * in , word16 * out);
		/* Prototype include a typedef name.
		   It should be moved after the typedef declaration */
