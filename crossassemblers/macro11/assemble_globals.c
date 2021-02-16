
#define ASSEMBLE_GLOBALS__C


#include "assemble_globals.h"          /* own definitions */

#include "object.h"


/* GLOBAL VARIABLES */
int             pass = 0;       /* The current assembly pass.  0 = first pass */
int             stmtno = 0;     /* The current source line number */
int             radix = 8;      /* The current input conversion radix */


int             lsb = 0;        /* The current local symbol section identifier */
int             lsb_used = 0;   /* Whether there was a local symbol using this lsb */
int             next_lsb = 0;   /* The number of the next local symbol block */
int             last_macro_lsb = 0; /* The last block in which a macro
                                   automatic label was created */

int             last_locsym = 32768;    /* The last local symbol number generated */


int             enabl_debug = 0;        /* Whether assembler debugging is enabled */

int             opt_enabl_ama = 0;      /* May be changed by command line */
int             enabl_ama;      /* When set, chooses absolute (037) versus
                                   PC-relative */
                                /* (067) addressing mode */
int             enabl_lsb = 0;  /* When set, stops non-local symbol
                                   definitions from delimiting local
                                   symbol sections. */

int             enabl_gbl = 1;  /* Implicit definition of global symbols */

int             enabl_lc = 1;   /* If lowercase disabled, convert assembler
                                   source to upper case. */

int             enabl_lcm = 0;  /* If lowercase disabled, .IF IDN/DIF are
                                   case-sensitive. */

int             suppressed = 0; /* Assembly suppressed by failed conditional */


MLB            *mlbs[MAX_MLBS]; /* macro libraries specified on the
                                   command line */
int             nr_mlbs = 0;    /* Number of macro libraries */

COND            conds[MAX_CONDS];       /* Stack of recent conditions */
int             last_cond;      /* 0 means no stacked cond. */

SECTION        *sect_stack[SECT_STACK_SIZE]; /* 32 saved sections */
int             dot_stack[SECT_STACK_SIZE];  /* 32 saved sections */
int             sect_sp;        /* Stack pointer */

char           *module_name = NULL;     /* The module name (taken from the 'TITLE'); */

char           *ident = NULL;   /* .IDENT name */

EX_TREE        *xfer_address = NULL;    /* The transfer address */

SYMBOL         *current_pc;     /* The current program counter */

unsigned        last_dot_addr;  /* Last coded PC... */
SECTION        *last_dot_section;       /* ...and its program section */

/* The following are dummy psects for symbols which have meaning to
the assembler: */

SECTION         register_section = {
    "", SECTION_REGISTER, 0, 0, 0, 0
};                                     /* the section containing the registers */

SECTION         pseudo_section = {
    "", SECTION_PSEUDO, 0, 0, 0, 0
};                                     /* the section containing the
                                          pseudo-operations */

SECTION         instruction_section = {
    ". ABS.", SECTION_INSTRUCTION, 0, 0, 0, 0
};                                     /* the section containing instructions */

SECTION         macro_section = {
    "", SECTION_SYSTEM, 0, 0, 0, 0
};                                     /* Section for macros */

/* These are real psects that get written out to the object file */

SECTION         absolute_section = {
    ". ABS.", SECTION_SYSTEM, PSECT_GBL | PSECT_COM, 0, 0, 0
};                                     /* The default
                                          absolute section */

SECTION         blank_section = {
    "", SECTION_SYSTEM, PSECT_REL, 0, 0, 1
};                                     /* The default relocatable section */

SECTION        *sections[256] = {
    /* Array of sections in the order they were
       defined */
    &absolute_section, &blank_section,
};

int             sector = 2;     /* number of such sections */
