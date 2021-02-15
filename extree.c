#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extree.h"                    /* my own definitions */

#include "util.h"
#include "assemble_globals.h"
#include "object.h"


/* Diagnostic: print an expression tree.  I used this in various
   places to help me diagnose parse problems, by putting in calls to
   print_tree when I didn't understand why something wasn't working.
   This is currently dead code, nothing calls it; but I don't want it
   to go away. Hopefully the compiler will realize when it's dead, and
   eliminate it. */

void print_tree(
    FILE *printfile,
    EX_TREE *tp,
    int depth)
{
    SYMBOL         *sym;

    if (tp == NULL) {
        fprintf(printfile, "(null)");
        return;
    }

    switch (tp->type) {
    case EX_LIT:
        fprintf(printfile, "%o", tp->data.lit & 0177777);
        break;

    case EX_SYM:
    case EX_TEMP_SYM:
        sym = tp->data.symbol;
        fprintf(printfile, "%s{%s%o:%s}", tp->data.symbol->label, symflags(sym), sym->value,
                sym->section->label);
        break;

    case EX_UNDEFINED_SYM:
        fprintf(printfile, "%s{%o:undefined}", tp->data.symbol->label, tp->data.symbol->value);
        break;

    case EX_COM:
        fprintf(printfile, "^C<");
        print_tree(printfile, tp->data.child.left, depth + 4);
        fprintf(printfile, ">");
        break;

    case EX_NEG:
        fprintf(printfile, "-<");
        print_tree(printfile, tp->data.child.left, depth + 4);
        fputc('>', printfile);
        break;

    case EX_ERR:
        fprintf(printfile, "{expression error}");
        if (tp->data.child.left) {
            fputc('<', printfile);
            print_tree(printfile, tp->data.child.left, depth + 4);
            fputc('>', printfile);
        }
        break;

    case EX_ADD:
        fputc('<', printfile);
        print_tree(printfile, tp->data.child.left, depth + 4);
        fputc('+', printfile);
        print_tree(printfile, tp->data.child.right, depth + 4);
        fputc('>', printfile);
        break;

    case EX_SUB:
        fputc('<', printfile);
        print_tree(printfile, tp->data.child.left, depth + 4);
        fputc('-', printfile);
        print_tree(printfile, tp->data.child.right, depth + 4);
        fputc('>', printfile);
        break;

    case EX_MUL:
        fputc('<', printfile);
        print_tree(printfile, tp->data.child.left, depth + 4);
        fputc('*', printfile);
        print_tree(printfile, tp->data.child.right, depth + 4);
        fputc('>', printfile);
        break;

    case EX_DIV:
        fputc('<', printfile);
        print_tree(printfile, tp->data.child.left, depth + 4);
        fputc('/', printfile);
        print_tree(printfile, tp->data.child.right, depth + 4);
        fputc('>', printfile);
        break;

    case EX_AND:
        fputc('<', printfile);
        print_tree(printfile, tp->data.child.left, depth + 4);
        fputc('&', printfile);
        print_tree(printfile, tp->data.child.right, depth + 4);
        fputc('>', printfile);
        break;

    case EX_OR:
        fputc('<', printfile);
        print_tree(printfile, tp->data.child.left, depth + 4);
        fputc('!', printfile);
        print_tree(printfile, tp->data.child.right, depth + 4);
        fputc('>', printfile);
        break;

    case EX_LSH:
        fputc('<', printfile);
        print_tree(printfile, tp->data.child.left, depth + 4);
        fputc('_', printfile);
        print_tree(printfile, tp->data.child.right, depth + 4);
        fputc('>', printfile);
        break;
    }

    if (depth == 0)
        fputc('\n', printfile);
}

/* free_tree frees an expression tree. */

void free_tree(
    EX_TREE *tp)
{
    switch (tp->type) {
    case EX_UNDEFINED_SYM:
    case EX_TEMP_SYM:
        free(tp->data.symbol->label);
        free(tp->data.symbol);
        break;

    case EX_LIT:
    case EX_SYM:
        break;

    case EX_COM:
    case EX_NEG:
        free_tree(tp->data.child.left);
        break;

    case EX_ERR:
        if (tp->data.child.left)
            free_tree(tp->data.child.left);
        break;

    case EX_ADD:
    case EX_SUB:
    case EX_MUL:
    case EX_DIV:
    case EX_AND:
    case EX_OR:
    case EX_LSH:
        free_tree(tp->data.child.left);
        free_tree(tp->data.child.right);
        break;
    }
    free(tp);
}

/* new_temp_sym allocates a new EX_TREE entry of type "TEMPORARY
   SYMBOL" (slight semantic difference from "UNDEFINED"). */

static EX_TREE *new_temp_sym(
    char *label,
    SECTION *section,
    unsigned value)
{
    SYMBOL         *sym;
    EX_TREE        *tp;

    sym = memcheck(malloc(sizeof(SYMBOL)));
    sym->label = memcheck(strdup(label));
    sym->flags = 0;
    sym->stmtno = stmtno;
    sym->next = NULL;
    sym->section = section;
    sym->value = value;

    tp = new_ex_tree();
    tp->type = EX_TEMP_SYM;
    tp->data.symbol = sym;

    return tp;
}

SYMBOL *dup_symbol(
    SYMBOL *sym)
{
    SYMBOL *res;

    if (sym == NULL) {
        return NULL;
    }

    res = memcheck(malloc(sizeof(SYMBOL)));
    res->label = memcheck(strdup(sym->label));
    res->flags = sym->flags;
    res->stmtno = sym->stmtno;
    res->next = NULL;
    res->section = sym->section;
    res->value = sym->value;

    return res;
}


EX_TREE *dup_tree(
    EX_TREE *tp)
{
    EX_TREE *res = NULL;

    if (tp == NULL) {
        return NULL;
    }

    res = new_ex_tree();
    res->type = tp->type;
    res->cp = tp->cp;

    switch (tp->type) {
    case EX_UNDEFINED_SYM:
    case EX_TEMP_SYM:
        res->data.symbol = dup_symbol(tp->data.symbol);
        break;

    /* The symbol reference in EX_SYM is not freed in free_tree() */
    case EX_SYM:
        res->data.symbol = tp->data.symbol;
        break;

    case EX_LIT:
        res->data.lit = tp->data.lit;
        break;

    case EX_COM:
    case EX_NEG:
    case EX_ERR:
        res->data.child.left = dup_tree(tp->data.child.left);
        break;

    case EX_ADD:
    case EX_SUB:
    case EX_MUL:
    case EX_DIV:
    case EX_AND:
    case EX_OR:
    case EX_LSH:
        res->data.child.left = dup_tree(tp->data.child.left);
        res->data.child.right = dup_tree(tp->data.child.right);
        break;
    }

    return res;
}

#define RELTYPE(tp) (((tp)->type == EX_SYM || (tp)->type == EX_TEMP_SYM) && \
        (tp)->data.symbol->section->flags & PSECT_REL)

/* evaluate "evaluates" an EX_TREE, ideally trying to produce a
   constant value, else a symbol plus an offset.  */
EX_TREE        *evaluate(
    EX_TREE *tp,
    int undef)
{
    EX_TREE        *res;
    char           *cp = tp->cp;

    switch (tp->type) {
    case EX_SYM:
        {
            SYMBOL         *sym = tp->data.symbol;

            /* Change some symbols to "undefined" */

            if (undef) {
                int             change = 0;

                /* I'd prefer this behavior, but MACRO.SAV is a bit too primitive. */
#if 0
                /* A temporary symbol defined later is "undefined." */
                if (!(sym->flags & PERMANENT) && sym->stmtno > stmtno)
                    change = 1;
#endif

                /* A global symbol with no assignment is "undefined." */
                /* Go figure. */
                if ((sym->flags & (SYMBOLFLAG_GLOBAL | SYMBOLFLAG_DEFINITION)) == SYMBOLFLAG_GLOBAL)
                    change = 1;

                if (change) {
                    res = new_temp_sym(tp->data.symbol->label, tp->data.symbol->section,
                                       tp->data.symbol->value);
                    res->type = EX_UNDEFINED_SYM;
                    break;
                }
            }

            /* Turn defined absolute symbol to a literal */
            if (!(sym->section->flags & PSECT_REL)
                && (sym->flags & (SYMBOLFLAG_GLOBAL | SYMBOLFLAG_DEFINITION)) != SYMBOLFLAG_GLOBAL
                && sym->section->type != SECTION_REGISTER) {
                res = new_ex_lit(sym->value);
                break;
            }

            /* Make a temp copy of any reference to "." since it might
               change as complex relocatable expressions are written out
             */
            if (strcmp(sym->label, ".") == 0) {
                res = new_temp_sym(".", sym->section, sym->value);
                break;
            }

            /* Copy other symbol reference verbatim. */
            res = dup_tree(tp);
            break;
        }

    case EX_LIT:
        res = dup_tree(tp);
        break;

    case EX_TEMP_SYM:
    case EX_UNDEFINED_SYM:
        /* Copy temp and undefined symbols */
        res = new_temp_sym(tp->data.symbol->label, tp->data.symbol->section, tp->data.symbol->value);
        res->type = tp->type;
        break;

    case EX_COM:
        /* Complement */
        tp = evaluate(tp->data.child.left, undef);
        if (tp->type == EX_LIT) {
            /* Complement the literal */
            res = new_ex_lit(~tp->data.lit);
            free_tree(tp);
        } else {
            /* Copy verbatim. */
            res = new_ex_tree();
            res->type = EX_COM;
            res->cp = tp->cp;
            res->data.child.left = tp;
        }

        break;

    case EX_NEG:
        tp = evaluate(tp->data.child.left, undef);
        if (tp->type == EX_LIT) {
            /* negate literal */
            res = new_ex_lit((unsigned) -(int) tp->data.lit);
            free_tree(tp);
        } else if (tp->type == EX_TEMP_SYM ||
                   (tp->type == EX_SYM &&
                    (tp->data.symbol->flags & SYMBOLFLAG_DEFINITION))) {
            /* Make a temp sym with the negative value of the given
               sym (this works for symbols within relocatable sections
               too) */
            res = new_temp_sym("*TEMP", tp->data.symbol->section, (unsigned) -(int) tp->data.symbol->value);
            res->cp = tp->cp;
            free_tree(tp);
        } else {
            /* Copy verbatim. */
            res = new_ex_tree();
            res->type = EX_NEG;
            res->cp = tp->cp;
            res->data.child.left = tp;
        }
        break;

    case EX_ERR:
        /* Copy */
        res = dup_tree(tp);
        break;

    case EX_ADD:
        {
            EX_TREE        *left,
                           *right;

            left = evaluate(tp->data.child.left, undef);
            right = evaluate(tp->data.child.right, undef);

            /* Both literals?  Sum them and return result. */
            if (left->type == EX_LIT && right->type == EX_LIT) {
                res = new_ex_lit(left->data.lit + right->data.lit);
                free_tree(left);
                free_tree(right);
                break;
            }

            /* Commutative: A+x == x+A.
               Simplify by putting the literal on the right */
            if (left->type == EX_LIT) {
                EX_TREE        *temp = left;

                left = right;
                right = temp;
            }

            if (right->type == EX_LIT &&        /* Anything plus 0 == itself */
                right->data.lit == 0) {
                res = left;
                free_tree(right);
                break;
            }

            /* Relative symbol plus lit is replaced with a temp sym
               holding the sum */
            if (RELTYPE(left) && right->type == EX_LIT) {
                SYMBOL         *sym = left->data.symbol;

                res = new_temp_sym("*ADD", sym->section, sym->value + right->data.lit);
                free_tree(left);
                free_tree(right);
                break;
            }

            /* Associative:  <A+x>+y == A+<x+y> */
            /*  and if x+y is constant, I can do that math. */
            if (left->type == EX_ADD && right->type == EX_LIT) {
                EX_TREE        *leftright = left->data.child.right;

                if (leftright->type == EX_LIT) {
                    /* Do the shuffle */
                    res = left;
                    leftright->data.lit += right->data.lit;
                    free_tree(right);
                    break;
                }
            }

            /* Associative:  <A-x>+y == A+<y-x> */
            /*  and if y-x is constant, I can do that math. */
            if (left->type == EX_SUB && right->type == EX_LIT) {
                EX_TREE        *leftright = left->data.child.right;

                if (leftright->type == EX_LIT) {
                    /* Do the shuffle */
                    res = left;
                    leftright->data.lit = right->data.lit - leftright->data.lit;
                    free_tree(right);
                    break;
                }
            }

            /* Anything else returns verbatim */
            res = new_ex_bin(EX_ADD, left, right);
        }
        break;

    case EX_SUB:
        {
            EX_TREE        *left,
                           *right;

            left = evaluate(tp->data.child.left, undef);
            right = evaluate(tp->data.child.right, undef);

            /* Both literals?  Subtract them and return a lit. */
            if (left->type == EX_LIT && right->type == EX_LIT) {
                res = new_ex_lit(left->data.lit - right->data.lit);
                free_tree(left);
                free_tree(right);
                break;
            }

            if (right->type == EX_LIT &&        /* Symbol minus 0 == symbol */
                right->data.lit == 0) {
                res = left;
                free_tree(right);
                break;
            }

            /* A relocatable minus an absolute - make a new temp sym
               to represent that. */
            if (RELTYPE(left) && right->type == EX_LIT) {
                SYMBOL         *sym = left->data.symbol;

                res = new_temp_sym("*SUB", sym->section, sym->value - right->data.lit);
                free_tree(left);
                free_tree(right);
                break;
            }

            if (RELTYPE(left) && RELTYPE(right) && left->data.symbol->section == right->data.symbol->section) {
                /* Two defined symbols in the same psect.  Resolve
                   their difference as a literal. */
                res = new_ex_lit(left->data.symbol->value - right->data.symbol->value);
                free_tree(left);
                free_tree(right);
                break;
            }

            /* Associative:  <A+x>-y == A+<x-y> */
            /*  and if x-y is constant, I can do that math. */
            if (left->type == EX_ADD && right->type == EX_LIT) {
                EX_TREE        *leftright = left->data.child.right;

                if (leftright->type == EX_LIT) {
                    /* Do the shuffle */
                    res = left;
                    leftright->data.lit -= right->data.lit;
                    free_tree(right);
                    break;
                }
            }

            /* Associative:  <A-x>-y == A-<x+y> */
            /*  and if x+y is constant, I can do that math. */
            if (left->type == EX_SUB && right->type == EX_LIT) {
                EX_TREE        *leftright = left->data.child.right;

                if (leftright->type == EX_LIT) {
                    /* Do the shuffle */
                    res = left;
                    leftright->data.lit += right->data.lit;
                    free_tree(right);
                    break;
                }
            }

            /* Anything else returns verbatim */
            res = new_ex_bin(EX_SUB, left, right);
        }
        break;

    case EX_MUL:
        {
            EX_TREE        *left,
                           *right;

            left = evaluate(tp->data.child.left, undef);
            right = evaluate(tp->data.child.right, undef);

            /* Can only multiply if both are literals */
            if (left->type == EX_LIT && right->type == EX_LIT) {
                res = new_ex_lit(left->data.lit * right->data.lit);
                free_tree(left);
                free_tree(right);
                break;
            }

            /* Commutative: A*x == x*A.
               Simplify by putting the literal on the right */
            if (left->type == EX_LIT) {
                EX_TREE        *temp = left;

                left = right;
                right = temp;
            }

            if (right->type == EX_LIT &&        /* Symbol times 1 == symbol */
                right->data.lit == 1) {
                res = left;
                free_tree(right);
                break;
            }

            if (right->type == EX_LIT &&        /* Symbol times 0 == 0 */
                right->data.lit == 0) {
                res = right;
                free_tree(left);
                break;
            }

            /* Associative: <A*x>*y == A*<x*y> */
            /* If x*y is constant, I can do this math. */
            /* Is this safe?  I will potentially be doing it */
            /* with greater accuracy than the target platform. */
            /* Hmmm. */

            if (left->type == EX_MUL && right->type == EX_LIT) {
                EX_TREE        *leftright = left->data.child.right;

                if (leftright->type == EX_LIT) {
                    /* Do the shuffle */
                    res = left;
                    leftright->data.lit *= right->data.lit;
                    free_tree(right);
                    break;
                }
            }

            /* Anything else returns verbatim */
            res = new_ex_bin(EX_MUL, left, right);
        }
        break;

    case EX_DIV:
        {
            EX_TREE        *left,
                           *right;

            left = evaluate(tp->data.child.left, undef);
            right = evaluate(tp->data.child.right, undef);

            /* Can only divide if both are literals */
            if (left->type == EX_LIT && right->type == EX_LIT) {
                res = new_ex_lit(left->data.lit / right->data.lit);
                free_tree(left);
                free_tree(right);
                break;
            }

            if (right->type == EX_LIT &&        /* Symbol divided by 1 == symbol */
                right->data.lit == 1) {
                res = left;
                free_tree(right);
                break;
            }

            /* Anything else returns verbatim */
            res = new_ex_bin(EX_DIV, left, right);
        }
        break;

    case EX_AND:
        {
            EX_TREE        *left,
                           *right;

            left = evaluate(tp->data.child.left, undef);
            right = evaluate(tp->data.child.right, undef);

            /* Operate if both are literals */
            if (left->type == EX_LIT && right->type == EX_LIT) {
                res = new_ex_lit(left->data.lit & right->data.lit);
                free_tree(left);
                free_tree(right);
                break;
            }

            /* Commutative: A&x == x&A.
               Simplify by putting the literal on the right */
            if (left->type == EX_LIT) {
                EX_TREE        *temp = left;

                left = right;
                right = temp;
            }

            if (right->type == EX_LIT &&        /* Symbol AND 0 == 0 */
                right->data.lit == 0) {
                res = new_ex_lit(0);
                free_tree(left);
                free_tree(right);
                break;
            }

            if (right->type == EX_LIT &&        /* Symbol AND 0177777 == symbol */
                right->data.lit == 0177777) {
                res = left;
                free_tree(right);
                break;
            }

            /* Anything else returns verbatim */
            res = new_ex_bin(EX_AND, left, right);
        }
        break;

    case EX_OR:
        {
            EX_TREE        *left,
                           *right;

            left = evaluate(tp->data.child.left, undef);
            right = evaluate(tp->data.child.right, undef);

            /* Operate if both are literals */
            if (left->type == EX_LIT && right->type == EX_LIT) {
                res = new_ex_lit(left->data.lit | right->data.lit);
                free_tree(left);
                free_tree(right);
                break;
            }

            /* Commutative: A!x == x!A.
               Simplify by putting the literal on the right */
            if (left->type == EX_LIT) {
                EX_TREE        *temp = left;

                left = right;
                right = temp;
            }

            if (right->type == EX_LIT &&        /* Symbol OR 0 == symbol */
                right->data.lit == 0) {
                res = left;
                free_tree(right);
                break;
            }

            if (right->type == EX_LIT &&        /* Symbol OR 0177777 == 0177777 */
                right->data.lit == 0177777) {
                res = new_ex_lit(0177777);
                free_tree(left);
                free_tree(right);
                break;
            }

            /* Anything else returns verbatim */
            res = new_ex_bin(EX_OR, left, right);
        }
        break;

    case EX_LSH:
        {
            EX_TREE        *left,
                           *right;

            left = evaluate(tp->data.child.left, undef);
            right = evaluate(tp->data.child.right, undef);
            /* Operate if both are literals */
            if (left->type == EX_LIT && right->type == EX_LIT) {
                int shift = right->data.lit;
                if (shift < 0)
                    res = new_ex_lit(left->data.lit >> -shift);
                else
                    res = new_ex_lit(left->data.lit << shift);
                free_tree(left);
                free_tree(right);
                break;
            }

            if (right->type == EX_LIT &&        /* Symbol shifted 0 == symbol */
                right->data.lit == 0) {
                res = left;
                free_tree(right);
                break;
            }

            if (right->type == EX_LIT &&        /* Anything shifted 16 == 0 */
                ((int)right->data.lit > 15 ||
                 (int)right->data.lit < -15)) {
                res = new_ex_lit(0);
                free_tree(left);
                free_tree(right);
                break;
            }

            if (right->type == EX_LIT) {        /* Other shifts become * or / */
                int shift = right->data.lit;
                if (shift > 0)
                    res = new_ex_bin(EX_MUL, left, new_ex_lit(1 << shift));
                else
                    res = new_ex_bin(EX_DIV, left, new_ex_lit(1 << -shift));
                free_tree(right);
                break;
            }

            /* Anything else returns verbatim */
            res = new_ex_bin(EX_LSH, left, right);
        }
        break;

    default:
        fprintf(stderr, "Invalid tree\n");
        return NULL;
    }

    res->cp = cp;
    return res;
}


/* Allocate an EX_TREE */

EX_TREE        *new_ex_tree(
    void)
{
    EX_TREE        *tr = memcheck(calloc(1, sizeof(EX_TREE)));

    return tr;
}


/* Create an EX_TREE representing a parse error */

EX_TREE        *ex_err(
    EX_TREE *tp,
    char *cp)
{
    EX_TREE        *errtp;

    errtp = new_ex_tree();
    errtp->cp = cp;
    errtp->type = EX_ERR;
    errtp->data.child.left = tp;

    return errtp;
}

/* Create an EX_TREE representing a literal value */

EX_TREE        *new_ex_lit(
    unsigned value)
{
    EX_TREE        *tp;

    tp = new_ex_tree();
    tp->type = EX_LIT;
    tp->data.lit = value;

    return tp;
}

/* Create an EX_TREE representing a binary expression */

EX_TREE        *new_ex_bin(
    int type,
    EX_TREE *left,
    EX_TREE *right)
{
    EX_TREE        *tp;

    tp = new_ex_tree();
    tp->type = type;
    tp->data.child.left = left;
    tp->data.child.right = right;

    return tp;
}

