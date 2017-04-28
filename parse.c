#define PARSE__C

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include "parse.h"                     /* my own definitions */

#include "util.h"
#include "rad50.h"
#include "listing.h"
#include "assemble_globals.h"


/* skipwhite - used everywhere to advance a char pointer past spaces */

char           *skipwhite(
    char *cp)
{
    while (*cp == ' ' || *cp == '\t')
        cp++;
    return cp;
}

/* skipdelim - used everywhere to advance between tokens.  Whitespace
   and one comma are allowed delims. */

char           *skipdelim(
    char *cp)
{
    cp = skipwhite(cp);
    if (*cp == ',')
        cp = skipwhite(cp + 1);
    return cp;
}

/* skipdelim_comma - used to advance between tokens.  Whitespace
   and one comma are allowed delims.
   Set *comma depending on whether a comma was skipped. */

char           *skipdelim_comma(
    char *cp,
    int  *comma)
{
    cp = skipwhite(cp);
    if ((*comma = (*cp == ','))) {
        cp = skipwhite(cp + 1);
    }
    return cp;
}

/* Parses a string from the input stream. */
/* If not bracketed by <...> or ^/.../, then */
/* the string is delimited by trailing comma or whitespace. */
/* Allows nested <>'s */

char           *getstring(
    char *cp,
    char **endp)
{
    int             len;
    int             start;
    char           *str;

    if (!brackrange(cp, &start, &len, endp)) {
        start = 0;
        len = strcspn(cp, " \t\n,;");
        if (endp)
            *endp = cp + len;
    }

    str = memcheck(malloc(len + 1));
    memcpy(str, cp + start, len);
    str[len] = 0;

    return str;
}

/* Parses a string from the input stream for .include and .library.
 * These have a special kind of delimiters. It likes
 * .include /name/ ?name? \name\ "name"
 * but not
 * .include ^/name/ <name> name =name= :name:
 * .include :name: seems to be silently ignored.
 *
 * This should probably follow the exact same rules as .ASCII
 * although that is not mentioned in the manual,
 */
char           *getstring_fn(
    char *cp,
    char **endp)
{
    char            endstr[4];
    int             len;
    char           *str;

    switch (*cp) {
    case '<':
    case ':':
        return NULL;
    }

    if (!ispunct((unsigned char)*cp)) {
        return NULL;
    }

    endstr[0] = *cp;
    endstr[1] = '\n';
    endstr[2] = '\0';
    cp++;

    len = strcspn(cp, endstr);

    if (endp)
        *endp = cp + len + 1;

    str = memcheck(malloc(len + 1));
    memcpy(str, cp, len);
    str[len] = 0;

    return str;
}

/* Get what would be the operation code from the line.  */
/* Used to find the ends of streams without evaluating them, like
   finding the closing .ENDM on a macro definition */

SYMBOL         *get_op(
    char *cp,
    char **endp)
{
    int             local;
    char           *label;
    SYMBOL         *op;

    cp = skipwhite(cp);
    if (EOL(*cp))
        return NULL;

    label = get_symbol(cp, &cp, &local);
    if (label == NULL)
        return NULL;                   /* No operation code. */

    cp = skipwhite(cp);
    if (*cp == ':') {                  /* A label definition? */
        cp++;
        if (*cp == ':')
            cp++;                      /* Skip it */
        free(label);
        label = get_symbol(cp, &cp, NULL);
        if (label == NULL)
            return NULL;
    }

    op = lookup_sym(label, &system_st);
    free(label);

    if (endp)
        *endp = cp;

    return op;
}



/* get_mode - parse a general addressing mode. */

int get_mode(
    char *cp,
    char **endp,
    ADDR_MODE *mode)
{
    EX_TREE        *value;

    mode->offset = NULL;
    mode->rel = 0;
    mode->type = 0;

    cp = skipwhite(cp);

    /* @ means "indirect," sets bit 3 */
    if (*cp == '@') {
        cp++;
        mode->type |= 010;
    }

    /* Immediate modes #imm and @#imm */
    if (*cp == '#') {
        cp++;
        mode->type |= 027;
        mode->offset = parse_expr(cp, 0);
        if (endp)
            *endp = mode->offset->cp;
        return TRUE;
    }

    /* Check for -(Rn) */

    if (*cp == '-') {
        char           *tcp = skipwhite(cp + 1);

        if (*tcp++ == '(') {
            unsigned        reg;

            /* It's -(Rn) */
            value = parse_expr(tcp, 0);
            reg = get_register(value);
            if (reg == NO_REG || (tcp = skipwhite(value->cp), *tcp++ != ')')) {
                free_tree(value);
                return FALSE;
            }
            mode->type |= 040 | reg;
            if (endp)
                *endp = tcp;
            free_tree(value);
            return TRUE;
        }
    }

    /* Check for (Rn) */
    if (*cp == '(') {
        char           *tcp;
        unsigned        reg;

        value = parse_expr(cp + 1, 0);
        reg = get_register(value);

        if (reg == NO_REG || (tcp = skipwhite(value->cp), *tcp++ != ')')) {
            free_tree(value);
            return FALSE;
        }

        tcp = skipwhite(tcp);
        if (*tcp == '+') {
            tcp++;                     /* It's (Rn)+ */
            if (endp)
                *endp = tcp;
            mode->type |= 020 | reg;
            free_tree(value);
            return TRUE;
        }

        if (mode->type == 010) {       /* For @(Rn) there's an implied 0 offset */
            mode->offset = new_ex_lit(0);
            mode->type |= 060 | reg;
            free_tree(value);
            if (endp)
                *endp = tcp;
            return TRUE;
        }

        mode->type |= 010 | reg;       /* Mode 10 is register indirect as
                                          in (Rn) */
        free_tree(value);
        if (endp)
            *endp = tcp;
        return TRUE;
    }

    /* Modes with an offset */

    mode->offset = parse_expr(cp, 0);

    cp = skipwhite(mode->offset->cp);

    if (*cp == '(') {
        unsigned        reg;

        /* indirect register plus offset */
        value = parse_expr(cp + 1, 0);
        reg = get_register(value);
        if (reg == NO_REG || (cp = skipwhite(value->cp), *cp++ != ')')) {
            free_tree(value);
            return FALSE;              /* Syntax error in addressing mode */
        }

        mode->type |= 060 | reg;

        free_tree(value);

        if (endp)
            *endp = cp;
        return TRUE;
    }

    /* Plain old expression. */

    if (endp)
        *endp = cp;

    /* It might be a register, though. */
    if (mode->offset->type == EX_SYM) {
        SYMBOL         *sym = mode->offset->data.symbol;

        if (sym->section->type == SECTION_REGISTER) {
            free_tree(mode->offset);
            mode->offset = NULL;
            mode->type |= sym->value;
            return TRUE;
        }
    }

    /* It's either 067 (PC-relative) or 037 (absolute) mode, depending */
    /* on user option. */

    if (mode->type & 010) {            /* Have already noted indirection? */
        mode->type |= 067;             /* If so, then PC-relative is the only
                                          option */
        mode->rel = 1;                 /* Note PC-relative */
    } else if (enabl_ama) {            /* User asked for absolute adressing? */
        mode->type |= 037;             /* Give it to him. */
    } else {
        mode->type |= 067;             /* PC-relative */
        mode->rel = 1;                 /* Note PC-relative */
    }

    return TRUE;
}

/* Parse PDP-11 64-bit floating point format. */
/* Give a pointer to "size" words to receive the result. */
/* Note: there are probably degenerate cases that store incorrect
   results.  For example, I think rounding up a FLT2 might cause
   exponent overflow.  Sorry. */
/* Note also that the full 49 bits of precision probably aren't
   available on the source platform, given the widespread application
   of IEEE floating point formats, so expect some differences.  Sorry
   again. */

int parse_float(
    char *cp,
    char **endp,
    int size,
    unsigned *flt)
{
    double          d;          /* value */
    double          frac;       /* fractional value */
    ulong64         ufrac;      /* fraction converted to 49 bit
                                   unsigned integer */
    int             i;          /* Number of fields converted by sscanf */
    int             n;          /* Number of characters converted by sscanf */
    int             sexp;       /* Signed exponent */
    unsigned        uexp;       /* Unsigned excess-128 exponent */
    unsigned        sign = 0;   /* Sign mask */

    i = sscanf(cp, "%lf%n", &d, &n);
    if (i == 0)
        return 0;                      /* Wasn't able to convert */

    cp += n;
    if (endp)
        *endp = cp;

    if (d == 0.0) {
        flt[0] = flt[1] = flt[2] = flt[3] = 0;  /* All-bits-zero equals zero */
        return 1;                      /* Good job. */
    }

    frac = frexp(d, &sexp);            /* Separate into exponent and mantissa */
    if (sexp < -128 || sexp > 127)
        return 0;                      /* Exponent out of range. */

    uexp = sexp + 128;                  /* Make excess-128 mode */
    uexp &= 0xff;                       /* express in 8 bits */

    if (frac < 0) {
        sign = 0100000;                /* Negative sign */
        frac = -frac;                  /* fix the mantissa */
    }

    /* The following big literal is 2 to the 49th power: */
    ufrac = (ulong64) (frac * 72057594037927936.0);     /* Align fraction bits */

    /* Round from FLT4 to FLT2 */
    if (size < 4) {
        ufrac += 0x80000000;           /* Round to nearest 32-bit
                                          representation */

        if (ufrac > 0x200000000000) {  /* Overflow? */
            ufrac >>= 1;               /* Normalize */
            uexp--;
        }
    }

    flt[0] = (unsigned) (sign | (uexp << 7) | ((ufrac >> 48) & 0x7F));
    if (size > 1) {
        flt[1] = (unsigned) ((ufrac >> 32) & 0xffff);
        if (size > 2) {
            flt[2] = (unsigned) ((ufrac >> 16) & 0xffff);
            flt[3] = (unsigned) ((ufrac >> 0) & 0xffff);
        }
    }

    return 1;
}


/* The recursive-descent expression parser parse_expr. */

/* This parser was designed for expressions with operator precedence.
   However, MACRO-11 doesn't observe any sort of operator precedence.
   If you feel your source deserves better, give the operators
   appropriate precedence values right here. */

#define ADD_PREC 1
#define MUL_PREC 1
#define AND_PREC 1
#define OR_PREC 1

EX_TREE        *parse_unary(
    char *cp);                  /* Prototype for forward calls */

EX_TREE        *parse_binary(
    char *cp,
    char term,
    int depth)
{
    EX_TREE        *leftp,
                   *rightp,
                   *tp;

    leftp = parse_unary(cp);

    while (leftp->type != EX_ERR) {
        cp = skipwhite(leftp->cp);

        if (*cp == term)
            return leftp;

        switch (*cp) {
        case '+':
            if (depth >= ADD_PREC)
                return leftp;

            rightp = parse_binary(cp + 1, term, ADD_PREC);
            tp = new_ex_bin(EX_ADD, leftp, rightp);
            tp->cp = rightp->cp;
            leftp = tp;
            break;

        case '-':
            if (depth >= ADD_PREC)
                return leftp;

            rightp = parse_binary(cp + 1, term, ADD_PREC);
            tp = new_ex_bin(EX_SUB, leftp, rightp);
            tp->cp = rightp->cp;
            leftp = tp;
            break;

        case '*':
            if (depth >= MUL_PREC)
                return leftp;

            rightp = parse_binary(cp + 1, term, MUL_PREC);
            tp = new_ex_bin(EX_MUL, leftp, rightp);
            tp->cp = rightp->cp;
            leftp = tp;
            break;

        case '/':
            if (depth >= MUL_PREC)
                return leftp;

            rightp = parse_binary(cp + 1, term, MUL_PREC);
            tp = new_ex_bin(EX_DIV, leftp, rightp);
            tp->cp = rightp->cp;
            leftp = tp;
            break;

        case '!':
            if (depth >= OR_PREC)
                return leftp;

            rightp = parse_binary(cp + 1, term, OR_PREC);
            tp = new_ex_bin(EX_OR, leftp, rightp);
            tp->cp = rightp->cp;
            leftp = tp;
            break;

        case '&':
            if (depth >= AND_PREC)
                return leftp;

            rightp = parse_binary(cp + 1, term, AND_PREC);
            tp = new_ex_bin(EX_AND, leftp, rightp);
            tp->cp = rightp->cp;
            leftp = tp;
            break;

        default:
            /* Some unknown character.  Let caller decide if it's okay. */

            return leftp;
        }                              /* end switch */
    }                                  /* end while */

    /* Can't be reached except by error. */
    return leftp;
}

/* get_symbol is used all over the place to pull a symbol out of the
   text.  */

char           *get_symbol(
    char *cp,
    char **endp,
    int *islocal)
{
    int             len;
    char           *symcp;
    int             digits = 0;

    cp = skipwhite(cp);                /* Skip leading whitespace */

    if (!issym((unsigned char)*cp))
        return NULL;

    digits = 0;
    if (isdigit((unsigned char)*cp))
        digits = 2;                    /* Think about digit count */

    for (symcp = cp + 1; issym((unsigned char)*symcp); symcp++) {
        if (!isdigit((unsigned char)*symcp)) /* Not a digit? */
            digits--;                  /* Make a note. */
    }

    if (digits == 2)
        return NULL;                   /* Not a symbol, it's a digit string */

    if (endp)
        *endp = symcp;

    len = (int) (symcp - cp);

    /* Now limit length */
    if (len > symbol_len)
        len = symbol_len;

    symcp = memcheck(malloc(len + 1));

    memcpy(symcp, cp, len);
    symcp[len] = 0;
    upcase(symcp);

    if (islocal) {
        *islocal = 0;

        /* Turn to local label format */
        if (digits == 1) {
            if (symcp[len - 1] == '$') {
                char           *newsym = memcheck(malloc(32));  /* Overkill */

                sprintf(newsym, "%ld$%d", strtol(symcp, NULL, 10), lsb);
                if (enabl_debug && lstfile) {
                    fprintf(lstfile, "lsb %d: %s -> %s\n",
                            lsb, symcp, newsym);
                }
                free(symcp);
                symcp = newsym;
                if (islocal)
                    *islocal = SYMBOLFLAG_LOCAL;
                lsb_used++;
            } else {
                free(symcp);
                return NULL;
            }
        }
    } else {
        /* disallow local label format */
        if (isdigit((unsigned char)*symcp)) {
            free(symcp);
            return NULL;
        }
    }

    return symcp;
}

/*
  brackrange is used to find a range of text which may or may not be
  bracketed.
  If the brackets are <>, then nested brackets are detected.
  If the brackets are of the form ^/.../ no detection of nesting is
  attempted.
  Using brackets ^<...< will mess this routine up.  What in the world
  are you thinking?
*/

int brackrange(
    char *cp,
    int *start,
    int *length,
    char **endp)
{
    char            endstr[6];
    int             endlen;
    int             nest;
    int             len;

    switch (*cp) {
    case '^':
        endstr[0] = cp[1];
        strcpy(endstr + 1, "\n");
        *start = 2;
        endlen = 1;
        break;
    case '<':
        strcpy(endstr, "<>\n");
        endlen = 1;
        *start = 1;
        break;
    default:
        return FALSE;
    }

    cp += *start;

    len = 0;
    if (endstr[1] == '>') { /* <>\n */
        nest = 1;
        while (nest) {
            int             sublen;

            sublen = strcspn(cp + len, endstr);
            if (cp[len + sublen] == '<') {
                nest++;
                sublen++;  /* avoid infinite loop when sublen == 0 */
            } else {
                nest--;
                if (nest > 0 && cp[len + sublen] == '>')
                    sublen++;  /* avoid infinite loop when sublen == 0 */
            }
            len += sublen;
            if (sublen == 0)
                break;
        }
    } else {
        int             sublen;

        sublen = strcspn(cp + len, endstr);
        len += sublen;
    }

    *length = len;
    if (endp)
        *endp = cp + len + endlen;

    return 1;
}

/* parse_unary parses out a unary operator or leaf expression.  */

EX_TREE        *parse_unary(
    char *cp)
{
    EX_TREE        *tp;

    /* Skip leading whitespace */
    cp = skipwhite(cp);

    if (*cp == '%') {                  /* Register notation */
        unsigned        reg;

        cp++;
        reg = strtoul(cp, &cp, 8);
        if (reg > 7)
            return ex_err(NULL, cp);

        /* This returns references to the built-in register symbols */
        tp = new_ex_tree();
        tp->type = EX_SYM;
        tp->data.symbol = reg_sym[reg];
        tp->cp = cp;
        return tp;
    }

    /* Unary negate */
    if (*cp == '-') {
        tp = new_ex_tree();
        tp->type = EX_NEG;
        tp->data.child.left = parse_unary(cp + 1);
        tp->cp = tp->data.child.left->cp;
        return tp;
    }

    /* Unary + I can ignore. */
    if (*cp == '+')
        return parse_unary(cp + 1);

    if (*cp == '^') {
        int             save_radix;

        switch (tolower((unsigned char)cp[1])) {
        case 'c':
            /* ^C, ones complement */
            tp = new_ex_tree();
            tp->type = EX_COM;
            tp->data.child.left = parse_unary(cp + 2);
            tp->cp = tp->data.child.left->cp;
            return tp;
        case 'b':
            /* ^B, binary radix modifier */
            save_radix = radix;
            radix = 2;
            tp = parse_unary(cp + 2);
            radix = save_radix;
            return tp;
        case 'o':
            /* ^O, octal radix modifier */
            save_radix = radix;
            radix = 8;
            tp = parse_unary(cp + 2);
            radix = save_radix;
            return tp;
        case 'd':
            /* ^D, decimal radix modifier */
            save_radix = radix;
            radix = 10;
            tp = parse_unary(cp + 2);
            radix = save_radix;
            return tp;
        case 'x':
            /* ^X, hexadecimal radix modifier */
            save_radix = radix;
            radix = 16;
            tp = parse_unary(cp + 2);
            radix = save_radix;
            return tp;
        case 'r':
            /* ^R, RAD50 literal */  {
                int             start,
                                len;
                char           *endcp;
                unsigned        value;

                cp += 2; /* bracketed range is an extension */
                if (brackrange(cp, &start, &len, &endcp))
                    value = rad50(cp + start, NULL);
                else
                    value = rad50(cp, &endcp);
                tp = new_ex_lit(value);
                tp->cp = endcp;
                return tp;
            }
        case 'f':
            /* ^F, single-word floating point literal indicator */  {
                unsigned        flt[1];
                char           *endcp;

                if (!parse_float(cp + 2, &endcp, 1, flt)) {
                    tp = ex_err(NULL, cp + 2);
                } else {
                    tp = new_ex_lit(flt[0]);
                    tp->cp = endcp;
                }
                return tp;
            }
        case 'p':
            /* psect limits, low or high */ {
                char bound = tolower((unsigned char)cp[2]);
                char *cp2 = skipwhite(cp + 3);
                int islocal = 0;
                char *endcp;
                char *psectname = get_symbol(cp2, &endcp, &islocal);
                SYMBOL *sectsym = lookup_sym(psectname, &section_st);

                if (sectsym && !islocal) {
                    SECTION *psect = sectsym->section;

                    tp = new_ex_tree();
                    tp->type = EX_SYM;
                    tp->data.symbol = sectsym;
                    tp->cp = cp;

                    if (bound == 'l') {
                        ; /* that's it */
                    } else if (bound == 'h') {
                        EX_TREE *rightp = new_ex_lit(psect->size);
                        tp = new_ex_bin(EX_ADD, tp, rightp);
                    } else {
                        tp = ex_err(tp, endcp);
                        /* report(stack->top, "^p: %c not recognized", bound); */
                    }
                } else {
                    /* report(stack->top, "psect name %s not found"); */
                    tp = ex_err(new_ex_lit(0), endcp);
                }
                free(psectname);
                tp->cp = endcp;
                cp = endcp;

                return tp;
            }
        }

        if (ispunct((unsigned char)cp[1])) {
            char           *ecp;

            /* oddly-bracketed expression like this: ^/expression/ */
            tp = parse_binary(cp + 2, cp[1], 0);
            ecp = skipwhite(tp->cp);

            if (*ecp != cp[1])
                return ex_err(tp, ecp);

            tp->cp = ecp + 1;
            return tp;
        }
    }

    /* Bracketed subexpression */
    if (*cp == '<') {
        char           *ecp;

        tp = parse_binary(cp + 1, '>', 0);
        ecp = skipwhite(tp->cp);
        if (*ecp != '>')
            return ex_err(tp, ecp);

        tp->cp = ecp + 1;
        return tp;
    }

    /* Check for ASCII constants */

    if (*cp == '\'') {
        /* 'x single ASCII character */
        cp++;
        tp = new_ex_lit(*cp & 0xff);
        tp->cp = ++cp;
        return tp;
    }

    if (*cp == '\"') {
        /* "xx ASCII character pair */
        cp++;
        tp = new_ex_lit((cp[0] & 0xff) | ((cp[1] & 0xff) << 8));
        tp->cp = cp + 2;
        return tp;
    }

    /* Numeric constants are trickier than they need to be, */
    /* since local labels start with a digit too. */
    if (isdigit((unsigned char)*cp)) {
        char           *label;
        int             local;

        if ((label = get_symbol(cp, NULL, &local)) == NULL) {
            char           *endcp;
            unsigned long   value;
            int             rad = radix;

            /* get_symbol returning NULL assures me that it's not a
               local label.  */

            /* Look for a trailing period, to indicate decimal... */
            for (endcp = cp; isdigit((unsigned char)*endcp); endcp++) ;
            if (*endcp == '.')
                rad = 10;

            value = strtoul(cp, &endcp, rad);
            if (*endcp == '.')
                endcp++;

            tp = new_ex_lit(value);
            tp->cp = endcp;

            return tp;
        }

        free(label);
    }

    /* Now check for a symbol */
    {
        char           *label;
        int             local;
        SYMBOL         *sym;

        /* Optimization opportunity: I don't really need to call
           get_symbol a second time. */

        if (!(label = get_symbol(cp, &cp, &local))) {
            cp++;                      /*JH: eat first char of illegal label, else endless loop on implied .WORD */
            tp = ex_err(NULL, cp);     /* Not a valid label. */
            return tp;
        }

        sym = lookup_sym(label, &symbol_st);
        if (sym == NULL) {
            /* A symbol from the "PST", which means an instruction
               code. */
            sym = lookup_sym(label, &system_st);
        }

        if (sym != NULL) {
            tp = new_ex_tree();
            tp->cp = cp;
            tp->type = EX_SYM;
            tp->data.symbol = sym;

            free(label);
            return tp;
        }

        /* The symbol was not found. Create an "undefined symbol"
           reference. */
        sym = memcheck(malloc(sizeof(SYMBOL)));
        sym->label = label;
        sym->flags = SYMBOLFLAG_UNDEFINED | local;
        sym->stmtno = stmtno;
        sym->next = NULL;
        sym->section = &absolute_section;
        sym->value = 0;

        tp = new_ex_tree();
        tp->cp = cp;
        tp->type = EX_UNDEFINED_SYM;
        tp->data.symbol = sym;

        return tp;
    }
}

/*
  parse_expr - this gets called everywhere.  It parses and evaluates
  an arithmetic expression.
*/

EX_TREE        *parse_expr(
    char *cp,
    int undef)
{
    EX_TREE        *expr;
    EX_TREE        *value;

    expr = parse_binary(cp, 0, 0);     /* Parse into a tree */
    value = evaluate(expr, undef);     /* Perform the arithmetic */
    value->cp = expr->cp;              /* Pointer to end of text is part of
                                          the rootmost node  */
    free_tree(expr);                   /* Discard parse in favor of
                                          evaluation */

    return value;
}

/*
  parse_unary_expr It parses and evaluates
  an arithmetic expression but only a unary: (op)value or <expr>.
  In particular, it doesn't try to divide in <lf>/SOH/.
*/

EX_TREE        *parse_unary_expr(
    char *cp,
    int undef)
{
    EX_TREE        *expr;
    EX_TREE        *value;

    expr = parse_unary(cp);            /* Parse into a tree */
    value = evaluate(expr, undef);     /* Perform the arithmetic */
    value->cp = expr->cp;              /* Pointer to end of text is part of
                                          the rootmost node  */
    free_tree(expr);                   /* Discard parse in favor of
                                          evaluation */

    return value;
}
