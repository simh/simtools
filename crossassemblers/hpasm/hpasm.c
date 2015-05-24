/*
**  HPASM.C  --  Assembler for HP2100
*/
/****************************************************/
#include <stdio.h>
#include <string.h>
#include <ctype.h>
/****************************************************/
#define MXSYM 1500
#define MXLABEL 5
#define ALLOW_SPECIALS_IN_LABEL 1
/****************************************************/
typedef struct {
    char   name[MXLABEL+1];
    long   value;
} SYM;
/****************************************************/
long     nbsyms;
SYM      symtab[MXSYM];  
long     pass;
long     addr;
char     line[80];
long     lp;
long     line_count;
long     err_count;
long     print_flag;     /* =1 if current line has been printed */
int      ifn_flag=0;     /* =1 if IFN does not skip */
int      ifz_flag=0;     /* =1 if IFZ does not skip */
int      xif_flag=0;     /* =1 if we are skipping until XIF */
int      rep_count=0;    /* >0 if REP statement in process */
/****************************************************/
/*
**  Variables for listing output routines
*/
char     listfilename[256];
FILE    *listfile;
/****************************************************/
start_listing (nam)
/*
**  Initialize listing output file
*/
char *nam;
{
    char *ppos;

    strncpy (listfilename, nam, 250);
    if (ppos = strrchr (listfilename, '.')) strcpy (ppos, ".lst");
        else strcat (listfilename, ".lst");
    listfile = fopen (listfilename, "w");
}
/****************************************************/
finish_listing () {
/*
**  Finish off listing output file
*/
    fclose (listfile);
    printf ("Listing is in %s\n", listfilename);
}
/****************************************************/
/*
**  Variables for binary output routines
*/
char     outfilename[256];
FILE    *outfile;
long     outaddr;
long     outcount;
long     outbufsize=27;
long     outbuf[27];
/****************************************************/
start_output (nam)
/*
**  Initialize binary output file
*/
char *nam;
{
    char *ppos;

    strncpy (outfilename, nam, 250);
    if (ppos = strrchr (outfilename, '.')) strcpy (ppos, ".bin");
        else strcat (outfilename, ".bin");
    outfile = fopen (outfilename, "wb");
    outaddr = 0;
    outcount = 0;
}
/****************************************************/
send_output () {
/*
**  Write block to binary output file
*/
    long ii,sum;

    fputc (outcount, outfile);
    fputc (0, outfile);
    fputc (outaddr >> 8, outfile);
    fputc (outaddr & 255, outfile);
    sum = outaddr;
    for (ii=0 ; ii < outcount ; ii++) {
        fputc (outbuf[ii] >> 8, outfile);
        fputc (outbuf[ii] & 255, outfile);
        sum += outbuf[ii];
    }
    fputc (sum >> 8, outfile);
    fputc (sum & 255, outfile);
    outcount = 0;
}
/****************************************************/
output_word (addr,word) 
/*
**  Write word to binary output file
*/
long addr;
long word;
{
    if (outcount && (outaddr + outcount) != addr) {
        send_output();
    }
    if (!outcount) outaddr = addr;
    outbuf[outcount++] = word;
    if (outcount == outbufsize) {
        send_output();
    }
}
/****************************************************/
finish_output () {
/*
**  Finish current output block, write trailing leader,
**  and close output file
*/
    int ii;
    if (outcount) send_output();
    for (ii=0; ii<20; ii++) fputc (0, outfile);
    fclose (outfile);
    printf ("Output is in %s\n", outfilename);
}
/****************************************************/
emit (code)
long code;
{
    fprintf (listfile, "  %05lo %06lo  ", addr, code);
    if (!print_flag) {
        fprintf (listfile, "%s", line);
        print_flag = 1;
    }
    fprintf (listfile, "\n");

    output_word (addr, code);
    addr++;
}
/****************************************************/
err (text)
char *text;
{
    if (!print_flag) {
        fprintf (listfile, "  %12s  %s\n", " ", line);
        print_flag = 1;
    }
    fprintf (listfile, "ERROR: %s\n", text);
    err_count++;
}
/****************************************************/
int is_valid_label_char (ch)
char ch;
{
    if (isalpha(ch)) return (1);
    if (isdigit(ch)) return (1);
    if (ch=='.') return (1);
#if ALLOW_SPECIALS_IN_LABEL
    if (ch=='&') return (1);
    if (ch=='?') return (1);
    if (ch=='#') return (1);
    if (ch=='/') return (1);
    if (ch=='%') return (1);
    if (ch=='$') return (1);
    if (ch=='[') return (1);
    if (ch=='@') return (1);
    if (ch=='!') return (1);
    if (ch=='^') return (1);
#endif
    return (0);
}
/****************************************************/
void insert_label (label, value)
char *label;
long  value;
/*
**  Insert label into symbol table
*/
{
    long i;
    if (strlen(label)>MXLABEL) {
        err ("Symbol name > 5");
        return;
    }
    for (i=0; i<nbsyms; i++) {
        if (!strcmp(symtab[i].name, label)) {
            err ("Duplicate symbol");
            return;
        }
    }
    if (nbsyms == MXSYM) {
        err ("Symbol table is full!");
        return;
    }
    strcpy (symtab[nbsyms].name, label);
    symtab[nbsyms].value = value;
    nbsyms++;
}
/****************************************************/
void find_label (label, value)
char *label;
long *value;
{
    long i;
    for (i=0; i<nbsyms; i++) {
        if (!strcmp(symtab[i].name, label)) {
            *value = symtab[i].value;
            return;
        }
    }
    err ("Undefined symbol");
    *value=0;
}
/****************************************************/
show_labels () 
{
    long i;
    for (i=0; i<nbsyms; i++) {
        fprintf (listfile, "%-6s %06lo \n", symtab[i].name, symtab[i].value );
    }
}
/****************************************************/
int double_to_hp (num, a, b) 
    double num;
    long *a,*b;
/*
**  Convert floating point number to HP 21xx format
**  Accepts:
**    num       Number to convert
**  Returns:
**    a,b       Number in HP format
**    ret       0=Success, -2=Underflow, -3=Overflow
*/
{
    int neg=0;
    long frac;
    int exp=0;

    *a = 0;  *b = 0;

    if (num == 0.0) return (0);

    if (num < 0.0) { num = -num; neg = 1; }

    while (num >= 1.0) { num = num / 2; exp++; }
    while (num <  0.5) { num = num * 2; exp--; }

    if (neg) num = -num;

    if (neg) num += (0177 / 65536.0 / 65536.0);
        else num += (0200 / 65536.0 / 65536.0);

    if (neg && num >= -0.5) { num = num * 2; exp--; }
    if (!neg && num >= 1.0) { num = num / 2; exp++; }

    if (exp < -128)  return (-2);

    if (exp > 127) {
        if (neg) { *a = 0x8000; *b = 0x01FE; }
            else { *a = 0x7FFF; *b = 0xFFFE; }
        return (-3);
    }

    frac = num * 65536.0 * 128.0  * 256.0;
    exp = exp << 1;
    if (exp < 0) exp++;
    *a = (frac >> 16) & 0xFFFF;
    *b = (frac & 0xFF00) | (exp & 0x00FF);

    return (0);
}
/****************************************************/
parse_digits (base, out) 
int base;
long *out;
{
    long dig, val;
    val = 0;
    dig = line[lp] - '0';
    while (dig>=0 && dig<base) {
        val = val*base + dig;
        dig = line[++lp] - '0';
    }
    *out = val;
}
/****************************************************/
parse_const (out)
long *out;
{
    long save, val;
    save = lp;
    parse_digits (10, &val);
    if (toupper(line[lp])=='B') {
        lp = save;
        parse_digits (8,&val);
        if (toupper(line[lp])=='B') lp++;
        else err ("Illegal octal digit");
    }
    *out = val;
}
/****************************************************/
parse_sym (out)
long *out;
{
    char  sym[MXLABEL+1];
    int   count,done;
    char  ch;
    count=0;
    done=0;
    while (!done) {
        ch = line[lp];
        if (is_valid_label_char(ch)) {
            if (count<MXLABEL) sym[count]=ch;
            count++;
            lp++;
        } else {
            done=1;
        }
    }
    if (count>MXLABEL) {
        err ("Illegal symbol");
        *out=0;
    } else {
        sym[count]=0;
        find_label (sym, out);
    }
}
/****************************************************/
parse_term (out)
long *out;
{
    char ch;
    ch = line[lp];
    if (isdigit(ch)) parse_const (out);
    else if (is_valid_label_char(ch)) parse_sym (out);
    else if (ch=='*') { lp++; *out = addr; }
    else {
        err ("Syntax error in expression");
        *out=0;
    }
}
/****************************************************/
parse_neg (out)
long *out;
{
    long temp;
    if (line[lp]=='-') {
        lp++;
        parse_term (&temp);
        *out = (-temp) & 0xFFFF;
    } else if (line[lp]=='+') {
        lp++;
        parse_term (out);
    } else {
        parse_term (out);
    }
}
/****************************************************/
parse_sum (out)
long *out;
{
    long v1,v2,op;
    parse_neg (&v1);
    while (line[lp]=='+' || line[lp]=='-') {  
        op = line[lp++];
        parse_neg (&v2);
        if (op=='+') v1 = v1 + v2;
        if (op=='-') v1 = v1 - v2;
    }
    *out = v1 & 0xFFFF;
}  
/****************************************************/
parse_arg (out)
long *out;
{
    while (isspace (line[lp])) lp++;
/*    while (line[lp]==' ') lp++; */
    parse_sum (out);
}
/****************************************************/
parse_oct (out)
long *out;
{
    long sign,val;

    while (isspace (line[lp])) lp++;
/*    while (line[lp]==' ') lp++; */
    sign = line[lp];
    if (sign=='+' || sign=='-') lp++;
    parse_digits (8, &val);
    if (sign=='-') val = (-val) & 0xFFFF;
    *out =val;
}
/****************************************************/
parse_dec (out, nbwords)
long *out, *nbwords;
{
    long save,sign,val,a,b;
    extern double atof();
    double num;

    while (isspace (line[lp])) lp++;
/*    while (line[lp]==' ') lp++; */
    save = lp;
    sign = line[lp];
    if (sign=='+' || sign=='-') lp++;
    parse_digits (10, &val);
    if (sign=='-') val = (-val) & 0xFFFF;
    out[0] = val;
    *nbwords = 1;

    if (line[lp]=='.' || toupper(line[lp])=='E') {
        num = atof( &(line[save]) );
        while (isdigit(line[lp]) || line[lp]=='.' || toupper(line[lp])=='E') lp++;
        double_to_hp (num, &a, &b);
        out[0] = a;
        out[1] = b;
        *nbwords = 2;
    }
}
/****************************************************/
mem_group (opcode, code) 
int opcode;
long *code;
{
    long arg, out;
    parse_arg (&arg);
    if (arg < 02000) {
        out = opcode + arg;         
    } else if ((arg & 0776000) == (addr & 0776000)) {
        out = opcode + 02000 + (arg & 01777);
    } else {
        err ("Operand page error");
        out = opcode;
    }
    if (line[lp]==',' && toupper(line[lp+1])=='I') {
        out = out + 0100000L;
        lp += 2;
    }
    code[0] = out;
}
/****************************************************/
io_group (opcode, code)
int opcode;
long *code;
{
    long arg, out;
    parse_arg (&arg);
    if (arg > 077) {
        err ("Select code > 77B");
        arg = 0;
    }
    out = opcode + arg;
    if (line[lp]==',' && toupper(line[lp+1])=='C') {
        out += 001000;
        lp += 2;
    }
    code[0] = out;
}
/****************************************************/
overflow_group (opcode, code)
int opcode;
long *code;
{
    code[0] = opcode;
    if (isspace (line[lp]) && toupper(line[lp+1])=='C') {
/*    if (line[lp]==' ' && line[lp+1]=='C') { */
        code[0] += 001000;
        lp += 2;
    }
}
/****************************************************/
parse_label (label)
char *label;
{
    int i=0;
    while (is_valid_label_char(line[lp])) {
        if (i<MXLABEL) label[i++]=line[lp];
        lp++;
    }
    label[i]=0;
}
/****************************************************/
int try (instr) 
char *instr;
{
    int ii,out;
    for (ii=0; instr[ii]; ii++) {
        if (toupper(line[lp+ii]) != instr[ii]) {
            return (0);
        }
    }
    if (isalpha(line[lp+ii])) out=0;
    else {
        lp += ii;
        if (line[lp]==',') lp++;
        out=1;
    }
    return (out);
}
/****************************************************/
int end_of_line()
{
    int ll,out;
    ll=lp;
    out=0;
    if (isspace (line[ll])) return 1;
    while (isspace (line[ll])) ll++;
/*    if (line[ll]=' ') return(1);
    while (line[ll]==' ') ll++; */
    if (line[ll]==';' || line[ll]=='\n' || line[ll]==0) {
        out=1;
    }
    return (out);
}
/****************************************************/
int try_mem_group (code)
long *code;
{
    int ok=1;
    if (try("NOP")) code[0]=0;
    else if (try("AND")) mem_group (010000, code);
    else if (try("XOR")) mem_group (020000, code);
    else if (try("IOR")) mem_group (030000, code);
    else if (try("JSB")) mem_group (014000, code);
    else if (try("JMP")) mem_group (024000, code);
    else if (try("ISZ")) mem_group (034000, code);
    else if (try("ADA")) mem_group (040000, code);
    else if (try("ADB")) mem_group (044000, code);
    else if (try("CPA")) mem_group (050000, code);
    else if (try("CPB")) mem_group (054000, code);
    else if (try("LDA")) mem_group (060000, code);
    else if (try("LDB")) mem_group (064000, code);
    else if (try("STA")) mem_group (070000, code);
    else if (try("STB")) mem_group (074000, code);
    else ok=0;
    return (ok);
}
/****************************************************/
int try_srg_a (code)
long *code;
{
    int ok=0;
    long out=0;
    long save=lp;
    if      (try("ALS")) out += 001000;
    else if (try("ARS")) out += 001100;
    else if (try("RAL")) out += 001200;
    else if (try("RAR")) out += 001300;
    else if (try("ALR")) out += 001400;
    else if (try("ERA")) out += 001500;
    else if (try("ELA")) out += 001600;
    else if (try("ALF")) out += 001700;
    if (try("CLE")) out += 000040;
    if (try("SLA")) out += 000010;
    if      (try("ALS")) out += 000020;
    else if (try("ARS")) out += 000021;
    else if (try("RAL")) out += 000022;
    else if (try("RAR")) out += 000023;
    else if (try("ALR")) out += 000024;
    else if (try("ERA")) out += 000025;
    else if (try("ELA")) out += 000026;
    else if (try("ALF")) out += 000027;
    if (out && end_of_line()) {
        code[0] = out;
        ok=1;
    } else {
        lp = save;
        ok=0;
    }
    return (ok);
}
/****************************************************/
int try_srg_b (code)
long *code;
{
    int ok=0;
    long out=0;
    long save=lp;
    if      (try("BLS")) out += 001000;
    else if (try("BRS")) out += 001100;
    else if (try("RBL")) out += 001200;
    else if (try("RBR")) out += 001300;
    else if (try("BLR")) out += 001400;
    else if (try("ERB")) out += 001500;
    else if (try("ELB")) out += 001600;
    else if (try("BLF")) out += 001700;
    if (try("CLE")) out += 000040;
    if (try("SLB")) out += 000010;
    if      (try("BLS")) out += 000020;
    else if (try("BRS")) out += 000021;
    else if (try("RBL")) out += 000022;
    else if (try("RBR")) out += 000023;
    else if (try("BLR")) out += 000024;
    else if (try("ERB")) out += 000025;
    else if (try("ELB")) out += 000026;
    else if (try("BLF")) out += 000027;
    if (out && end_of_line()) {
        code[0] = out + 004000;
        ok=1;
    } else {
        lp = save;
        ok=0;
    } 
    return (ok);
}
/****************************************************/
int try_asg_a (code)
long *code;
{
    int ok=0;
    long out=0;
    long save=lp;
    if      (try("CLA")) out += 000400;
    else if (try("CMA")) out += 001000;
    else if (try("CCA")) out += 001400;
    if (try("SEZ")) out += 000040;
    if      (try("CLE")) out += 000100;
    else if (try("CME")) out += 000200;
    else if (try("CCE")) out += 000300;
    if (try("SSA")) out += 000020;
    if (try("SLA")) out += 000010;
    if (try("INA")) out += 000004;
    if (try("SZA")) out += 000002;
    if (try("RSS")) out += 000001;
    if (out && end_of_line()) {
        code[0] = out + 002000;
        ok=1;
    } else {
        lp = save;
        ok=0;
    }
    return (ok);
}
/****************************************************/
int try_asg_b (code)
long *code;
{
    int ok=0;
    long out=0;
    long save=lp;
    if      (try("CLB")) out += 000400;
    else if (try("CMB")) out += 001000;
    else if (try("CCB")) out += 001400;
    if (try("SEZ")) out += 000040;
    if      (try("CLE")) out += 000100;
    else if (try("CME")) out += 000200;
    else if (try("CCE")) out += 000300;
    if (try("SSB")) out += 000020;
    if (try("SLB")) out += 000010;
    if (try("INB")) out += 000004;
    if (try("SZB")) out += 000002;
    if (try("RSS")) out += 000001;
    if (out && end_of_line()) {
        code[0] = out + 006000;
        ok=1;
    } else {
        lp = save;
        ok=0;
    }
    return (ok);
}
/****************************************************/
int try_io_group (code)
long *code;
{
    int ok=1;
    if (try("NOP")) code[0]=0;
    else if (try("HLT")) io_group (0102000, code);
    else if (try("STF")) io_group (0102100, code);
    else if (try("CLF")) io_group (0103100, code);
    else if (try("SFC")) io_group (0102200, code);
    else if (try("SFS")) io_group (0102300, code);
    else if (try("MIA")) io_group (0102400, code);
    else if (try("MIB")) io_group (0106400, code);
    else if (try("LIA")) io_group (0102500, code);
    else if (try("LIB")) io_group (0106500, code);
    else if (try("OTA")) io_group (0102600, code);
    else if (try("OTB")) io_group (0106600, code);
    else if (try("STC")) io_group (0102700, code);
    else if (try("CLC")) io_group (0106700, code);
    else ok=0;
    return (ok);
}

/****************************************************/
int try_overflow (code)
long *code;
{
    int ok=1;
    if      (try("STO")) code[0] = 0102101;
    else if (try("CLO")) code[0] = 0103101;
    else if (try("SOC")) overflow_group (0102201, code);
    else if (try("SOS")) overflow_group (0102301, code);
    else ok=0;
    return (ok);
}
/****************************************************/
asm_pass (f)
FILE *f;
{
    int  done,count;
    char label[MXLABEL+1];
    long arg,code[2];
    done=0;
    while (!done) {
        if (!fgets(line, sizeof(line), f)) {
            done=1;
        } else {
            line_count++;
            lp=strlen(line);
            if (lp>0 && line[lp-1]=='\n') line[lp-1]=0;
            print_flag = 0;
            count = rep_count;
            if (!count) count=1;
            rep_count=0;
            if (line[0]=='*') {
                if (pass==2) {
                    fprintf (listfile, "%s\n", line);
                    print_flag = 1;
                }
            } else 
            while (count > 0) {
                count--;
                lp=0;
                parse_label (label);
                while (isspace (line[lp])) lp++;
/*                while (line[lp]==' ') lp++; */
                if (try("IFN")) {
                    if (!ifn_flag) xif_flag=1;
                } else if (try("IFZ")) {
                    if (!ifz_flag) xif_flag=1;
                } else if (try("XIF")) {
                    xif_flag=0;
                } else if (xif_flag) {
                    while (line[lp]) lp++;
                } else if (try("ORG")) {
                    parse_arg (&arg);
                    addr = arg;
                    if (pass==2) {
                        fprintf (listfile, "  %05lo         %s\n", addr, line);
                        print_flag = 1;
                        if (strlen(label)) err ("Unexpected label");
                    }
                } else if (try("EQU")) {
                    parse_arg (&arg);
                    if (pass==1) {
                        if (strlen(label)) insert_label (label, arg);
                    } else {
                        fprintf (listfile, "  %05lo         %s\n", arg, line);
                        print_flag = 1;
                        if (!strlen(label)) err ("Label required");
                    }
                } else if (!isalpha(line[lp])) {
                    if (pass==2) err ("No instruction on line");
                } else {

                    if (pass==1 && strlen(label)) insert_label (label,addr);

                    if (try("HED") || try("SUP") || try("SPC") ||
                        try("SKP") || try("UNS") || try("UNL") ||
                        try("LST")) {

                        /* Listing control ops are ignored */

                    } else if (try("END")) {

                        /* No action required */

                    } else if (try("REP")) {
                        parse_arg (&arg);
                        if (arg < 1 || arg > 9999) {
                            err ("Illegal repeat count");
                        } else {
                            rep_count = arg;
                        }
                    } else if (try("ABS")) {
                        if (pass==1) addr++;
                        else {
                            parse_arg (code);
                            emit (code[0]);
                        }
                    } else if (try("DEF")) {
                        if (pass==1) addr++;
                        else {
                            parse_arg (code);
                            if (line[lp]==',' && toupper(line[lp+1])=='I') {
                                code[0] |= 0x8000;
                                lp += 2;
                            }
                            emit (code[0]);
                        }
                    } else if (try("DEC")) {
                        long nbwords;
                        parse_dec (code, &nbwords);
                        if (pass==1) addr += nbwords;
                        else {
                            if (nbwords > 0) emit (code[0]);
                            if (nbwords > 1) emit (code[1]);
                        }
                    } else if (try("OCT")) {
                        while (1) {
                            parse_oct (code);
                            if (pass==1) addr++;
                            else emit (code[0]);
                            if (line[lp]==',') lp++;
                            else break;
                        }
                    } else if (try("ASC")) {
                        long nbwords,c1,c2;
                        parse_arg (&nbwords);
                        if (pass==1) addr += nbwords;
                        else if (line[lp++]!=',') err ("Missing comma");
                        else 
                          while (nbwords>0) {
                             if (line[lp]>31) c1=line[lp++]; else c1=' ';
                             if (line[lp]>31) c2=line[lp++]; else c2=' ';
                             emit (c1*256 + c2);
                             nbwords--;
                          }
                    } else if (try("BSS")) {
                        long nbwords;
                        if (pass==2) {
                            fprintf (listfile, "  %05lo         %s\n", 
                                     addr, line);
                            print_flag = 1;
                        }
                        parse_arg (&nbwords);
                        addr += nbwords;
                    }
                    else if (pass==1) addr++;
                    else if (try_mem_group(code)) emit (code[0]);
                    else if (try_srg_a(code)) emit (code[0]);
                    else if (try_srg_b(code)) emit (code[0]);
                    else if (try_asg_a(code)) emit (code[0]);
                    else if (try_asg_b(code)) emit (code[0]);
                    else if (try_io_group(code)) emit (code[0]);
                    else if (try_overflow(code)) emit (code[0]);
                    else {
                        emit (0L);
                        err ("Unknown instruction");
                    }
                }
                if (pass==2 && !end_of_line()) err ("Missing End-of-line");
                if (pass==2 && !print_flag) fprintf (listfile, "  %12s  %s\n",
                                                     " ", line);
            }
        }
    }
}       
/****************************************************/
main (argc,argv)
int argc;
char *argv[];
{
    FILE *f1;
    int ii,jj;

    jj=1;
    for (ii=1 ; ii < argc ; ii++) {
        if (!strcmp(argv[ii],"-n")) ifn_flag=1;
        else if (!strcmp(argv[ii],"-z")) ifz_flag=1;
        else argv[jj++] = argv[ii];
    }
    argc = jj;

    if (argc < 2) {
        printf ("Usage: hpasm options filename...\n");
        return (-1);
    }

    start_output (argv[1]);
    start_listing (argv[1]);

    printf ("<< PASS 1 >>\n");
    nbsyms=0;
    err_count=0;
    pass=1;

    for (ii=1 ; ii < argc; ii++) {
        f1 = fopen (argv[ii], "r");
        if (!f1) {
            printf ("File '%s' not found!\n\n", argv[ii]);
        } else {
            line_count=0;
            printf ("%s ", argv[ii]);
            fflush (stdout);
            asm_pass (f1);
            fclose (f1);
            printf ("%d lines\n", line_count);
        }
    }

    show_labels();

    printf ("<< PASS 2 >>\n");
    pass=2;

    for (ii=1 ; ii < argc; ii++) {
        f1 = fopen (argv[ii], "r");
        if (!f1) {
            printf ("File '%s' not found!\n\n", argv[ii]);
        } else {
            line_count=0;
            printf ("%s ", argv[ii]);
            fflush (stdout);
            asm_pass (f1);
            printf ("%d lines\n", line_count);
            fclose (f1);
        }
    }

    fprintf (listfile, "%ld ERRORS\n", err_count);
    printf ("%ld ERRORS\n", err_count);

    finish_output();
    finish_listing();

    return (0);
}
/****************************************************/
