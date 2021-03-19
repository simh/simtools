
#define SYMBOLS__C

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "symbols.h"                   /* my own definitions */

#include "util.h"
#include "assemble_globals.h"
#include "listing.h"
#include "object.h"

/* GLOBALS */
int             symbol_len = SYMMAX_DEFAULT;    /* max. len of symbols. default = 6 */
int             symbol_allow_underscores = 0;   /* allow "_" in symbol names */

SYMBOL         *reg_sym[8];     /* Keep the register symbols in a handy array */


SYMBOL_TABLE    system_st;      /* System symbols (Instructions,
                                   pseudo-ops, registers) */

SYMBOL_TABLE    section_st;     /* Program sections */

SYMBOL_TABLE    symbol_st;      /* User symbols */

SYMBOL_TABLE    macro_st;       /* Macros */

SYMBOL_TABLE    implicit_st;    /* The symbols which may be implicit globals */


void list_section(SECTION *sec);

/* hash_name hashes a name into a value from 0-HASH_SIZE */

int hash_name(
    char *label)
{
    unsigned        accum = 0;

    while (*label)
        accum = (accum << 1) ^ *label++;

    accum %= HASH_SIZE;

    return accum;
}



/* Diagnostic: symflags returns a char* which gives flags I can use to
   show the context of a symbol. */

char           *symflags(
    SYMBOL *sym)
{
    static char     temp[8];
    char           *fp = temp;

    if (sym->flags & SYMBOLFLAG_GLOBAL)
        *fp++ = 'G';
    if (sym->flags & SYMBOLFLAG_PERMANENT)
        *fp++ = 'P';
    if (sym->flags & SYMBOLFLAG_DEFINITION)
        *fp++ = 'D';
    *fp = 0;
    return fp;
}



/* Allocate a new symbol.  Does not add it to any symbol table. */

static SYMBOL  *new_sym(
    char *label)
{
    SYMBOL         *sym = memcheck(malloc(sizeof(SYMBOL)));

    sym->label = memcheck(strdup(label));
    sym->section = NULL;
    sym->value = 0;
    sym->flags = 0;
    return sym;
}

/* Free a symbol. Does not remove it from any symbol table.  */

void free_sym(
    SYMBOL *sym)
{
    if (sym->label) {
        free(sym->label);
        sym->label = NULL;
    }
    free(sym);
}

/* remove_sym removes a symbol from its symbol table. */

void remove_sym(
    SYMBOL *sym,
    SYMBOL_TABLE *table)
{
    SYMBOL        **prevp,
                   *symp;
    int             hash;

    hash = hash_name(sym->label);
    prevp = &table->hash[hash];
    while (symp = *prevp, symp != NULL && symp != sym)
        prevp = &symp->next;

    if (symp)
        *prevp = sym->next;
}

/* lookup_sym finds a symbol in a table */

SYMBOL         *lookup_sym(
    char *label,
    SYMBOL_TABLE *table)
{
    unsigned        hash;
    SYMBOL         *sym;

    hash = hash_name(label);

    sym = table->hash[hash];
    while (sym && strcmp(sym->label, label) != 0)
        sym = sym->next;

    return sym;
}

/* next_sym - returns the next symbol from a symbol table.  Must be
   preceeded by first_sym.  Returns NULL after the last symbol. */

SYMBOL         *next_sym(
    SYMBOL_TABLE *table,
    SYMBOL_ITER *iter)
{
    if (iter->current)
        iter->current = iter->current->next;

    while (iter->current == NULL) {
        if (iter->subscript >= HASH_SIZE)
            return NULL;               /* No more symbols. */
        iter->current = table->hash[iter->subscript];
        iter->subscript++;
    }

    return iter->current;              /* Got a symbol. */
}

/* first_sym - returns the first symbol from a symbol table. Symbols
   are stored in random order. */

SYMBOL         *first_sym(
    SYMBOL_TABLE *table,
    SYMBOL_ITER *iter)
{
    iter->subscript = 0;
    iter->current = NULL;
    return next_sym(table, iter);
}

/* add_table - add a symbol to a symbol table. */

void add_table(
    SYMBOL *sym,
    SYMBOL_TABLE *table)
{
    int             hash = hash_name(sym->label);

    sym->next = table->hash[hash];
    table->hash[hash] = sym;
}

/* add_sym - used throughout to add or update symbols in a symbol
   table.  */

SYMBOL         *add_sym(
    char *labelraw,
    unsigned value,
    unsigned flags,
    SECTION *section,
    SYMBOL_TABLE *table)
{
    SYMBOL         *sym;
    char            label[SYMMAX_MAX + 1];      // big size

    if (isdigit((unsigned char)labelraw[0])) {
        // Don't truncate local labels
        strncpy(label, labelraw, SYMMAX_MAX);
        label[SYMMAX_MAX] = 0;
    } else {
        //JH: truncate symbol to SYMMAX
        strncpy(label, labelraw, symbol_len);
        label[symbol_len] = 0;
    }

    sym = lookup_sym(label, table);
    if (sym != NULL) {
        // A symbol registered as "undefined" can be changed.

        if ((sym->flags & SYMBOLFLAG_UNDEFINED) && !(flags & SYMBOLFLAG_UNDEFINED)) {
            sym->flags &= ~(SYMBOLFLAG_PERMANENT | SYMBOLFLAG_UNDEFINED);
        }

        /* Check for compatible definition */
        else if (sym->section == section && sym->value == value) {
            sym->flags |= flags;       /* Merge flags quietly */
            return sym;                /* 's okay */
        }

        if (!(sym->flags & SYMBOLFLAG_PERMANENT)) {
            /* permit redefinition */
            sym->value = value;
            sym->flags |= flags;
            sym->section = section;
            return sym;
        }

        return NULL;                   /* Bad symbol redefinition */
    }

    sym = new_sym(label);
    sym->flags = flags;
    sym->stmtno = stmtno;
    sym->section = section;
    sym->value = value;

    add_table(sym, table);

    return sym;
}

/* add_symbols adds all the internal symbols. */

void add_symbols(
    SECTION *current_section)
{
    current_pc = add_sym(".", 0, 0, current_section, &symbol_st);

    reg_sym[0] = add_sym("R0", 0, 0, &register_section, &system_st);
    reg_sym[1] = add_sym("R1", 1, 0, &register_section, &system_st);
    reg_sym[2] = add_sym("R2", 2, 0, &register_section, &system_st);
    reg_sym[3] = add_sym("R3", 3, 0, &register_section, &system_st);
    reg_sym[4] = add_sym("R4", 4, 0, &register_section, &system_st);
    reg_sym[5] = add_sym("R5", 5, 0, &register_section, &system_st);
    reg_sym[6] = add_sym("SP", 6, 0, &register_section, &system_st);
    reg_sym[7] = add_sym("PC", 7, 0, &register_section, &system_st);

    //JH: symbols longer than current SYMMAX will be truncated. SYMMAX=6 is minimum!
    add_sym(".ASCII", P_ASCII, 0, &pseudo_section, &system_st);
    add_sym(".ASCIZ", P_ASCIZ, 0, &pseudo_section, &system_st);
    add_sym(".ASECT", P_ASECT, 0, &pseudo_section, &system_st);
    add_sym(".BLKB", P_BLKB, 0, &pseudo_section, &system_st);
    add_sym(".BLKW", P_BLKW, 0, &pseudo_section, &system_st);
    add_sym(".BYTE", P_BYTE, 0, &pseudo_section, &system_st);
    add_sym(".CSECT", P_CSECT, 0, &pseudo_section, &system_st);
    add_sym(".DSABL", P_DSABL, 0, &pseudo_section, &system_st);
    add_sym(".ENABL", P_ENABL, 0, &pseudo_section, &system_st);
    add_sym(".END", P_END, 0, &pseudo_section, &system_st);
    add_sym(".ENDC", P_ENDC, 0, &pseudo_section, &system_st);
    add_sym(".ENDM", P_ENDM, 0, &pseudo_section, &system_st);
    add_sym(".ENDR", P_ENDR, 0, &pseudo_section, &system_st);
    add_sym(".EOT", P_EOT, 0, &pseudo_section, &system_st);
    add_sym(".ERROR", P_ERROR, 0, &pseudo_section, &system_st);
    add_sym(".EVEN", P_EVEN, 0, &pseudo_section, &system_st);
    add_sym(".FLT2", P_FLT2, 0, &pseudo_section, &system_st);
    add_sym(".FLT4", P_FLT4, 0, &pseudo_section, &system_st);
    add_sym(".GLOBL", P_GLOBL, 0, &pseudo_section, &system_st);
    add_sym(".IDENT", P_IDENT, 0, &pseudo_section, &system_st);
    add_sym(".IF", P_IF, 0, &pseudo_section, &system_st);
    add_sym(".IFDF", P_IFDF, 0, &pseudo_section, &system_st);
    add_sym(".IFNDF", P_IFDF, 0, &pseudo_section, &system_st);
    add_sym(".IFF", P_IFF, 0, &pseudo_section, &system_st);
    add_sym(".IFT", P_IFT, 0, &pseudo_section, &system_st);
    add_sym(".IFTF", P_IFTF, 0, &pseudo_section, &system_st);
    add_sym(".IIF", P_IIF, 0, &pseudo_section, &system_st);
    add_sym(".INCLUDE", P_INCLUDE, 0, &pseudo_section, &system_st);
    add_sym(".IRP", P_IRP, 0, &pseudo_section, &system_st);
    add_sym(".IRPC", P_IRPC, 0, &pseudo_section, &system_st);
    add_sym(".LIBRARY", P_LIBRARY, 0, &pseudo_section, &system_st);
    add_sym(".LIMIT", P_LIMIT, 0, &pseudo_section, &system_st);
    add_sym(".LIST", P_LIST, 0, &pseudo_section, &system_st);
    add_sym(".MCALL", P_MCALL, 0, &pseudo_section, &system_st);
    add_sym(".MEXIT", P_MEXIT, 0, &pseudo_section, &system_st);
    add_sym(".NARG", P_NARG, 0, &pseudo_section, &system_st);
    add_sym(".NCHR", P_NCHR, 0, &pseudo_section, &system_st);
    add_sym(".NLIST", P_NLIST, 0, &pseudo_section, &system_st);
    add_sym(".NTYPE", P_NTYPE, 0, &pseudo_section, &system_st);
    add_sym(".ODD", P_ODD, 0, &pseudo_section, &system_st);
    add_sym(".PACKED", P_PACKED, 0, &pseudo_section, &system_st);
    add_sym(".PAGE", P_PAGE, 0, &pseudo_section, &system_st);
    add_sym(".PRINT", P_PRINT, 0, &pseudo_section, &system_st);
    add_sym(".PSECT", P_PSECT, 0, &pseudo_section, &system_st);
    add_sym(".RADIX", P_RADIX, 0, &pseudo_section, &system_st);
    add_sym(".RAD50", P_RAD50, 0, &pseudo_section, &system_st);
    add_sym(".REM", P_REM, 0, &pseudo_section, &system_st);
    add_sym(".REPT", P_REPT, 0, &pseudo_section, &system_st);
    add_sym(".RESTORE", P_RESTORE, 0, &pseudo_section, &system_st);
    add_sym(".SAVE", P_SAVE, 0, &pseudo_section, &system_st);
    add_sym(".SBTTL", P_SBTTL, 0, &pseudo_section, &system_st);
    add_sym(".TITLE", P_TITLE, 0, &pseudo_section, &system_st);
    add_sym(".WORD", P_WORD, 0, &pseudo_section, &system_st);
    add_sym(".MACRO", P_MACRO, 0, &pseudo_section, &system_st);
    add_sym(".WEAK", P_WEAK, 0, &pseudo_section, &system_st);

    add_sym("ADC", I_ADC, OC_1GEN, &instruction_section, &system_st);
    add_sym("ADCB", I_ADCB, OC_1GEN, &instruction_section, &system_st);
    add_sym("ADD", I_ADD, OC_2GEN, &instruction_section, &system_st);
    add_sym("ASH", I_ASH, OC_ASH, &instruction_section, &system_st);
    add_sym("ASHC", I_ASHC, OC_ASH, &instruction_section, &system_st);
    add_sym("ASL", I_ASL, OC_1GEN, &instruction_section, &system_st);
    add_sym("ASLB", I_ASLB, OC_1GEN, &instruction_section, &system_st);
    add_sym("ASR", I_ASR, OC_1GEN, &instruction_section, &system_st);
    add_sym("ASRB", I_ASRB, OC_1GEN, &instruction_section, &system_st);
    add_sym("BCC", I_BCC, OC_BR, &instruction_section, &system_st);
    add_sym("BCS", I_BCS, OC_BR, &instruction_section, &system_st);
    add_sym("BEQ", I_BEQ, OC_BR, &instruction_section, &system_st);
    add_sym("BGE", I_BGE, OC_BR, &instruction_section, &system_st);
    add_sym("BGT", I_BGT, OC_BR, &instruction_section, &system_st);
    add_sym("BHI", I_BHI, OC_BR, &instruction_section, &system_st);
    add_sym("BHIS", I_BHIS, OC_BR, &instruction_section, &system_st);
    add_sym("BIC", I_BIC, OC_2GEN, &instruction_section, &system_st);
    add_sym("BICB", I_BICB, OC_2GEN, &instruction_section, &system_st);
    add_sym("BIS", I_BIS, OC_2GEN, &instruction_section, &system_st);
    add_sym("BISB", I_BISB, OC_2GEN, &instruction_section, &system_st);
    add_sym("BIT", I_BIT, OC_2GEN, &instruction_section, &system_st);
    add_sym("BITB", I_BITB, OC_2GEN, &instruction_section, &system_st);
    add_sym("BLE", I_BLE, OC_BR, &instruction_section, &system_st);
    add_sym("BLO", I_BLO, OC_BR, &instruction_section, &system_st);
    add_sym("BLOS", I_BLOS, OC_BR, &instruction_section, &system_st);
    add_sym("BLT", I_BLT, OC_BR, &instruction_section, &system_st);
    add_sym("BMI", I_BMI, OC_BR, &instruction_section, &system_st);
    add_sym("BNE", I_BNE, OC_BR, &instruction_section, &system_st);
    add_sym("BPL", I_BPL, OC_BR, &instruction_section, &system_st);
    add_sym("BPT", I_BPT, OC_NONE, &instruction_section, &system_st);
    add_sym("BR", I_BR, OC_BR, &instruction_section, &system_st);
    add_sym("BVC", I_BVC, OC_BR, &instruction_section, &system_st);
    add_sym("BVS", I_BVS, OC_BR, &instruction_section, &system_st);
    add_sym("CALL", I_CALL, OC_1GEN, &instruction_section, &system_st);
    add_sym("CALLR", I_CALLR, OC_1GEN, &instruction_section, &system_st);
    add_sym("CCC", I_CCC, OC_NONE, &instruction_section, &system_st);
    add_sym("CLC", I_CLC, OC_NONE, &instruction_section, &system_st);
    add_sym("CLN", I_CLN, OC_NONE, &instruction_section, &system_st);
    add_sym("CLR", I_CLR, OC_1GEN, &instruction_section, &system_st);
    add_sym("CLRB", I_CLRB, OC_1GEN, &instruction_section, &system_st);
    add_sym("CLV", I_CLV, OC_NONE, &instruction_section, &system_st);
    add_sym("CLZ", I_CLZ, OC_NONE, &instruction_section, &system_st);
    add_sym("CMP", I_CMP, OC_2GEN, &instruction_section, &system_st);
    add_sym("CMPB", I_CMPB, OC_2GEN, &instruction_section, &system_st);
    add_sym("COM", I_COM, OC_1GEN, &instruction_section, &system_st);
    add_sym("COMB", I_COMB, OC_1GEN, &instruction_section, &system_st);
    add_sym("DEC", I_DEC, OC_1GEN, &instruction_section, &system_st);
    add_sym("DECB", I_DECB, OC_1GEN, &instruction_section, &system_st);
    add_sym("DIV", I_DIV, OC_ASH, &instruction_section, &system_st);
    add_sym("EMT", I_EMT, OC_MARK, &instruction_section, &system_st);
    add_sym("FADD", I_FADD, OC_1REG, &instruction_section, &system_st);
    add_sym("FDIV", I_FDIV, OC_1REG, &instruction_section, &system_st);
    add_sym("FMUL", I_FMUL, OC_1REG, &instruction_section, &system_st);
    add_sym("FSUB", I_FSUB, OC_1REG, &instruction_section, &system_st);
    add_sym("HALT", I_HALT, OC_NONE, &instruction_section, &system_st);
    add_sym("INC", I_INC, OC_1GEN, &instruction_section, &system_st);
    add_sym("INCB", I_INCB, OC_1GEN, &instruction_section, &system_st);
    add_sym("IOT", I_IOT, OC_NONE, &instruction_section, &system_st);
    add_sym("JMP", I_JMP, OC_1GEN, &instruction_section, &system_st);
    add_sym("JSR", I_JSR, OC_JSR, &instruction_section, &system_st);
    add_sym("MARK", I_MARK, OC_MARK, &instruction_section, &system_st);
    add_sym("MED6X", I_MED6X, OC_NONE, &instruction_section, &system_st);
    add_sym("MED74C", I_MED74C, OC_NONE, &instruction_section, &system_st);
    add_sym("MFPD", I_MFPD, OC_1GEN, &instruction_section, &system_st);
    add_sym("MFPI", I_MFPI, OC_1GEN, &instruction_section, &system_st);
    add_sym("MFPS", I_MFPS, OC_1GEN, &instruction_section, &system_st);
    add_sym("MOV", I_MOV, OC_2GEN, &instruction_section, &system_st);
    add_sym("MOVB", I_MOVB, OC_2GEN, &instruction_section, &system_st);
    add_sym("MTPD", I_MTPD, OC_1GEN, &instruction_section, &system_st);
    add_sym("MTPI", I_MTPI, OC_1GEN, &instruction_section, &system_st);
    add_sym("MTPS", I_MTPS, OC_1GEN, &instruction_section, &system_st);
    add_sym("MUL", I_MUL, OC_ASH, &instruction_section, &system_st);
    add_sym("NEG", I_NEG, OC_1GEN, &instruction_section, &system_st);
    add_sym("NEGB", I_NEGB, OC_1GEN, &instruction_section, &system_st);
    add_sym("NOP", I_NOP, OC_NONE, &instruction_section, &system_st);
    add_sym("RESET", I_RESET, OC_NONE, &instruction_section, &system_st);
    add_sym("RETURN", I_RETURN, OC_NONE, &instruction_section, &system_st);
    add_sym("ROL", I_ROL, OC_1GEN, &instruction_section, &system_st);
    add_sym("ROLB", I_ROLB, OC_1GEN, &instruction_section, &system_st);
    add_sym("ROR", I_ROR, OC_1GEN, &instruction_section, &system_st);
    add_sym("RORB", I_RORB, OC_1GEN, &instruction_section, &system_st);
    add_sym("RTI", I_RTI, OC_NONE, &instruction_section, &system_st);
    add_sym("RTS", I_RTS, OC_1REG, &instruction_section, &system_st);
    add_sym("RTT", I_RTT, OC_NONE, &instruction_section, &system_st);
    add_sym("SBC", I_SBC, OC_1GEN, &instruction_section, &system_st);
    add_sym("SBCB", I_SBCB, OC_1GEN, &instruction_section, &system_st);
    add_sym("SCC", I_SCC, OC_NONE, &instruction_section, &system_st);
    add_sym("SEC", I_SEC, OC_NONE, &instruction_section, &system_st);
    add_sym("SEN", I_SEN, OC_NONE, &instruction_section, &system_st);
    add_sym("SEV", I_SEV, OC_NONE, &instruction_section, &system_st);
    add_sym("SEZ", I_SEZ, OC_NONE, &instruction_section, &system_st);
    add_sym("SOB", I_SOB, OC_SOB, &instruction_section, &system_st);
    add_sym("SPL", I_SPL, OC_1REG, &instruction_section, &system_st);
    add_sym("SUB", I_SUB, OC_2GEN, &instruction_section, &system_st);
    add_sym("SWAB", I_SWAB, OC_1GEN, &instruction_section, &system_st);
    add_sym("SXT", I_SXT, OC_1GEN, &instruction_section, &system_st);
    add_sym("TRAP", I_TRAP, OC_MARK, &instruction_section, &system_st);
    add_sym("TST", I_TST, OC_1GEN, &instruction_section, &system_st);
    add_sym("TSTB", I_TSTB, OC_1GEN, &instruction_section, &system_st);
    add_sym("WAIT", I_WAIT, OC_NONE, &instruction_section, &system_st);
    add_sym("XFC", I_XFC, OC_NONE, &instruction_section, &system_st);
    add_sym("XOR", I_XOR, OC_JSR, &instruction_section, &system_st);
    add_sym("MFPT", I_MFPT, OC_NONE, &instruction_section, &system_st);
    add_sym("CSM", I_CSM, OC_1GEN, &instruction_section, &system_st);
    add_sym("TSTSET", I_TSTSET, OC_1GEN, &instruction_section, &system_st);
    add_sym("WRTLCK", I_WRTLCK, OC_1GEN, &instruction_section, &system_st);

    /* FPP instructions */
    add_sym("ABSD", I_ABSD, OC_FPPDST, &instruction_section, &system_st);
    add_sym("ABSF", I_ABSF, OC_FPPDST, &instruction_section, &system_st);
    add_sym("ADDD", I_ADDD, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("ADDF", I_ADDF, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("CFCC", I_CFCC, OC_NONE, &instruction_section, &system_st);
    add_sym("CLRD", I_CLRD, OC_FPPDST, &instruction_section, &system_st);
    add_sym("CLRF", I_CLRF, OC_FPPDST, &instruction_section, &system_st);
    add_sym("CMPD", I_CMPD, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("CMPF", I_CMPF, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("DIVD", I_DIVD, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("DIVF", I_DIVF, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("LDCDF", I_LDCDF, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("LDCFD", I_LDCFD, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("LDCID", I_LDCID, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("LDCIF", I_LDCIF, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("LDCLD", I_LDCLD, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("LDCLF", I_LDCLF, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("LDD", I_LDD, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("LDEXP", I_LDEXP, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("LDF", I_LDF, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("LDFPS", I_LDFPS, OC_1GEN, &instruction_section, &system_st);
    add_sym("MODD", I_MODD, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("MODF", I_MODF, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("MULD", I_MULD, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("MULF", I_MULF, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("NEGD", I_NEGD, OC_FPPDST, &instruction_section, &system_st);
    add_sym("NEGF", I_NEGF, OC_FPPDST, &instruction_section, &system_st);
    add_sym("SETD", I_SETD, OC_NONE, &instruction_section, &system_st);
    add_sym("SETF", I_SETF, OC_NONE, &instruction_section, &system_st);
    add_sym("SETI", I_SETI, OC_NONE, &instruction_section, &system_st);
    add_sym("SETL", I_SETL, OC_NONE, &instruction_section, &system_st);
    add_sym("STA0", I_STA0, OC_NONE, &instruction_section, &system_st);
    add_sym("STB0", I_STB0, OC_NONE, &instruction_section, &system_st);
    add_sym("STCDF", I_STCDF, OC_FPPACGEN, &instruction_section, &system_st);
    add_sym("STCDI", I_STCDI, OC_FPPACGEN, &instruction_section, &system_st);
    add_sym("STCDL", I_STCDL, OC_FPPACGEN, &instruction_section, &system_st);
    add_sym("STCFD", I_STCFD, OC_FPPACGEN, &instruction_section, &system_st);
    add_sym("STCFI", I_STCFI, OC_FPPACGEN, &instruction_section, &system_st);
    add_sym("STCFL", I_STCFL, OC_FPPACGEN, &instruction_section, &system_st);
    add_sym("STD", I_STD, OC_FPPACGEN, &instruction_section, &system_st);
    add_sym("STEXP", I_STEXP, OC_FPPACGEN, &instruction_section, &system_st);
    add_sym("STF", I_STF, OC_FPPACGEN, &instruction_section, &system_st);
    add_sym("STFPS", I_STFPS, OC_1GEN, &instruction_section, &system_st);
    add_sym("STST", I_STST, OC_1GEN, &instruction_section, &system_st);
    add_sym("SUBD", I_SUBD, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("SUBF", I_SUBF, OC_FPPGENAC, &instruction_section, &system_st);
    add_sym("TSTD", I_TSTD, OC_FPPDST, &instruction_section, &system_st);
    add_sym("TSTF", I_TSTF, OC_FPPDST, &instruction_section, &system_st);

    /* The CIS instructions */
    add_sym("ADDNI", I_ADDN|I_CIS_I, OC_CIS3, &instruction_section, &system_st);
    add_sym("ADDN",  I_ADDN,         OC_NONE, &instruction_section, &system_st);
    add_sym("ADDPI", I_ADDP|I_CIS_I, OC_CIS3, &instruction_section, &system_st);
    add_sym("ADDP",  I_ADDP,         OC_NONE, &instruction_section, &system_st);
    add_sym("ASHNI", I_ASHN|I_CIS_I, OC_CIS3, &instruction_section, &system_st);
    add_sym("ASHN",  I_ASHN,         OC_NONE, &instruction_section, &system_st);
    add_sym("ASHPI", I_ASHP|I_CIS_I, OC_CIS3, &instruction_section, &system_st);
    add_sym("ASHP",  I_ASHP,         OC_NONE, &instruction_section, &system_st);
    add_sym("CMPCI", I_CMPC|I_CIS_I, OC_CIS3, &instruction_section, &system_st);
    add_sym("CMPC",  I_CMPC,         OC_NONE, &instruction_section, &system_st);
    add_sym("CMPNI", I_CMPN|I_CIS_I, OC_CIS2, &instruction_section, &system_st);
    add_sym("CMPN",  I_CMPN,         OC_NONE, &instruction_section, &system_st);
    add_sym("CMPPI", I_CMPP|I_CIS_I, OC_CIS2, &instruction_section, &system_st);
    add_sym("CMPP",  I_CMPP,         OC_NONE, &instruction_section, &system_st);
    add_sym("CVTLNI",I_CVTLN|I_CIS_I,OC_CIS2, &instruction_section, &system_st);
    add_sym("CVTLN", I_CVTLN,        OC_NONE, &instruction_section, &system_st);
    add_sym("CVTLPI",I_CVTLP|I_CIS_I,OC_CIS2, &instruction_section, &system_st);
    add_sym("CVTLP", I_CVTPL,        OC_NONE, &instruction_section, &system_st);
    add_sym("CVTNLI",I_CVTNL|I_CIS_I,OC_CIS2, &instruction_section, &system_st);
    add_sym("CVTNL", I_CVTNL,        OC_NONE, &instruction_section, &system_st);
    add_sym("CVTPLI",I_CVTPL|I_CIS_I,OC_CIS2, &instruction_section, &system_st);
    add_sym("CVTPL", I_CVTPL,        OC_NONE, &instruction_section, &system_st);
    add_sym("CVTNPI",I_CVTNP|I_CIS_I,OC_CIS2, &instruction_section, &system_st);
    add_sym("CVTNP", I_CVTNP,        OC_NONE, &instruction_section, &system_st);
    add_sym("CVTPNI",I_CVTPN|I_CIS_I,OC_CIS2, &instruction_section, &system_st);
    add_sym("CVTPN", I_CVTPN,        OC_NONE, &instruction_section, &system_st);
    add_sym("DIVPI", I_DIVP|I_CIS_I, OC_CIS3, &instruction_section, &system_st);
    add_sym("DIVP",  I_DIVP,         OC_NONE, &instruction_section, &system_st);
    add_sym("LOCCI", I_LOCC|I_CIS_I, OC_CIS2, &instruction_section, &system_st);
    add_sym("LOCC",  I_LOCC,         OC_NONE, &instruction_section, &system_st);
    add_sym("L2D0",  I_L2Dr+0,       OC_NONE, &instruction_section, &system_st);
    add_sym("L2D1",  I_L2Dr+1,       OC_NONE, &instruction_section, &system_st);
    add_sym("L2D2",  I_L2Dr+2,       OC_NONE, &instruction_section, &system_st);
    add_sym("L2D3",  I_L2Dr+3,       OC_NONE, &instruction_section, &system_st);
    add_sym("L2D4",  I_L2Dr+4,       OC_NONE, &instruction_section, &system_st);
    add_sym("L2D5",  I_L2Dr+5,       OC_NONE, &instruction_section, &system_st);
    add_sym("L2D6",  I_L2Dr+6,       OC_NONE, &instruction_section, &system_st);
    add_sym("L2D7",  I_L2Dr+7,       OC_NONE, &instruction_section, &system_st);
    add_sym("L3D0",  I_L3Dr+0,       OC_NONE, &instruction_section, &system_st);
    add_sym("L3D1",  I_L3Dr+1,       OC_NONE, &instruction_section, &system_st);
    add_sym("L3D2",  I_L3Dr+2,       OC_NONE, &instruction_section, &system_st);
    add_sym("L3D3",  I_L3Dr+3,       OC_NONE, &instruction_section, &system_st);
    add_sym("L3D4",  I_L3Dr+4,       OC_NONE, &instruction_section, &system_st);
    add_sym("L3D5",  I_L3Dr+5,       OC_NONE, &instruction_section, &system_st);
    add_sym("L3D6",  I_L3Dr+6,       OC_NONE, &instruction_section, &system_st);
    add_sym("L3D7",  I_L3Dr+7,       OC_NONE, &instruction_section, &system_st);
    add_sym("MATCI", I_MATC|I_CIS_I, OC_CIS2, &instruction_section, &system_st);
    add_sym("MATC",  I_MATC,         OC_NONE, &instruction_section, &system_st);
    add_sym("MOVCI", I_MOVC|I_CIS_I, OC_CIS3, &instruction_section, &system_st);
    add_sym("MOVC",  I_MOVC,         OC_NONE, &instruction_section, &system_st);
    add_sym("MOVRCI",I_MOVRC|I_CIS_I,OC_CIS3, &instruction_section, &system_st);
    add_sym("MOVRC", I_MOVRC,        OC_NONE, &instruction_section, &system_st);
    add_sym("MOVTCI",I_MOVTC|I_CIS_I,OC_CIS4, &instruction_section, &system_st);
    add_sym("MOVTC", I_MOVTC,        OC_NONE, &instruction_section, &system_st);
    add_sym("MULPI", I_MULP|I_CIS_I, OC_CIS3, &instruction_section, &system_st);
    add_sym("MULP",  I_MULP,         OC_NONE, &instruction_section, &system_st);
    add_sym("SCANCI",I_SCANC|I_CIS_I,OC_CIS2, &instruction_section, &system_st);
    add_sym("SCANC", I_SCANC,        OC_NONE, &instruction_section, &system_st);
    add_sym("SKPCI", I_SKPC|I_CIS_I, OC_CIS2, &instruction_section, &system_st);
    add_sym("SKPC",  I_SKPC,         OC_NONE, &instruction_section, &system_st);
    add_sym("SPANCI",I_SPANC|I_CIS_I,OC_CIS2, &instruction_section, &system_st);
    add_sym("SPANC", I_SPANC,        OC_NONE, &instruction_section, &system_st);
    add_sym("SUBNI", I_SUBN|I_CIS_I, OC_CIS3, &instruction_section, &system_st);
    add_sym("SUBN",  I_SUBN,         OC_NONE, &instruction_section, &system_st);
    add_sym("SUBPI", I_SUBP|I_CIS_I, OC_CIS3, &instruction_section, &system_st);
    add_sym("SUBP",  I_SUBP,         OC_NONE, &instruction_section, &system_st);

    add_sym(current_section->label, 0, 0, current_section, &section_st);
}

/* sym_hist is a diagnostic function that prints a histogram of the
   hash table useage of a symbol table.  I used this to try to tune
   the hash function for better spread.  It's not used now. */

void sym_hist(
    SYMBOL_TABLE *st,
    char *name)
{
    int             i;
    SYMBOL         *sym;

    fprintf(lstfile, "Histogram for symbol table %s\n", name);
    for (i = 0; i < 1023; i++) {
        fprintf(lstfile, "%4d: ", i);
        for (sym = st->hash[i]; sym != NULL; sym = sym->next)
            fputc('#', lstfile);
        fputc('\n', lstfile);
    }
}

static int symbol_compar(
    const void *a,
    const void *b)
{
    SYMBOL *sa = *(SYMBOL **)a;
    SYMBOL *sb = *(SYMBOL **)b;

    return strcmp(sa->label, sb->label);
}

void list_symbol_table(
    void)
{
    SYMBOL_ITER iter;
    SYMBOL *sym;
    int skip_locals = 0;
    int longest_symbol = symbol_len;

    fprintf(lstfile,"\n\nSymbol table\n\n");

    /* Count the symbols in the table */
    int nsyms = 0;
    for (sym = first_sym(&symbol_st, &iter); sym != NULL; sym = next_sym(&symbol_st, &iter)) {
        if (skip_locals && sym->flags & SYMBOLFLAG_LOCAL) {
            continue;
        }
        nsyms++;
        int len = strlen(sym->label);
        if (len > longest_symbol) {
            longest_symbol = len;
        }
    }

    /* Sort them by name */
    SYMBOL **symbols = malloc(nsyms * sizeof (SYMBOL *));
    SYMBOL **symbolp = symbols;

    for (sym = first_sym(&symbol_st, &iter); sym != NULL; sym = next_sym(&symbol_st, &iter)) {
        if (skip_locals && sym->flags & SYMBOLFLAG_LOCAL) {
            continue;
        }
        *symbolp++ = sym;
    }

    qsort(symbols, nsyms, sizeof(SYMBOL *), symbol_compar);

    symbolp = symbols;

    /* Print the listing in NCOLS columns. */
    int ncols = (132 / (longest_symbol + 19));
    int nlines = (nsyms + ncols - 1) / ncols;
    int line;
    /*
     * DIRER$  =%004562RGX    006
     * ^        ^^     ^      ^-- for R symbols: program segment number
     * |        ||     +-- Flags: R = relocatable
     * |        ||                G = global
     * |        ||                X = implicit global
     * |        ||                L = local
     * |        ||                W = weak
     * |        |+- value, ****** for if it was not a definition
     * |        +-- % for a register
     * +- label name
     */

    for (line = 0; line < nlines; line++) {
        int i;
        for (i = line; i < nsyms; i += nlines) {
            sym = symbols[i];

            fprintf(lstfile,"%-*s", longest_symbol, sym->label);
            fprintf(lstfile,"%c", (sym->section->flags & PSECT_REL) ? ' ' : '=');
            fprintf(lstfile,"%c", (sym->section->type == SECTION_REGISTER) ? '%' : ' ');
            if (!(sym->flags & SYMBOLFLAG_DEFINITION)) {
                fprintf(lstfile,"******");
            } else {
                fprintf(lstfile,"%06o", sym->value & 0177777);
            }
            fprintf(lstfile,"%c", (sym->section->flags & PSECT_REL) ? 'R' : ' ');
            fprintf(lstfile,"%c", (sym->flags & SYMBOLFLAG_GLOBAL) ?  'G' : ' ');
            fprintf(lstfile,"%c", (sym->flags & SYMBOLFLAG_IMPLICIT_GLOBAL) ? 'X' : ' ');
            fprintf(lstfile,"%c", (sym->flags & SYMBOLFLAG_LOCAL) ?   'L' : ' ');
            fprintf(lstfile,"%c", (sym->flags & SYMBOLFLAG_WEAK) ?    'W' : ' ');
            if (sym->section->sector != 0) {
                fprintf(lstfile,"  %03d ", sym->section->sector);
            } else {
                fprintf(lstfile,"      ");
            }
        }
        fprintf(lstfile,"\n");
    }

    /* List sections */

    fprintf(lstfile,"\n\nProgram sections:\n\n");

    int i;
    for (i = 0; i < sector; i++) {
        list_section(sections[i]);
    }
}

void list_section(
    SECTION *sec)
{
    if (sec == NULL) {
        fprintf(lstfile, "(null)\n");
        return;
    }

    int flags = sec->flags;

    fprintf(lstfile, "%-6s  %06o    %03d   ",
        sec->label, sec->size & 0177777, sec->sector);
    fprintf(lstfile, "(%s,%s,%s,%s,%s,%s)\n",
        (flags & PSECT_RO)   ? "RO"  : "RW",
        (flags & PSECT_DATA) ? "D"   : "I",
        (flags & PSECT_GBL)  ? "GBL" : "LCL",
        (flags & PSECT_REL)  ? "REL" : "ABS",
        (flags & PSECT_COM)  ? "OVR" : "CON",
        (flags & PSECT_SAV)  ? "SAV" : "NOSAV");
}
