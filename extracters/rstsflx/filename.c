/* subroutines to handle rsts file name conversion and file spec parse */

#include <ctype.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "flx.h"
#include "filename.h"
#include "fldef.h"

#define isr50(c) (isalnum (c) || (c) == ' ' || (c) == '?' || (c) == '*')

const char *parsename (const char *p, firqb *out)	/* parse the name part of a spec */
{
	int	n;

	for (n = 0; n < 6; n++) out->name[n] = ' ';
	out->name[6] = '\0';			/* put in terminator */
	if (isr50(*p)) {			/* name present */
		n = 0;
		while (isr50(*p)) {
			if (*p == ' ') {		/* skip blanks */
				p++;
				continue;
			}
			out->flags |= f_name;
			if (*p == '*') {
				if (n < 6) {
					out->name[n++] = '?';
					out->flags |= f_namw;
				} else p++;
			} else {
				if (n < 6) out->name[n++] = tolower (*p);
				if (*p++ == '?') out->flags |= f_namw;
			}
		}
	}
	return (p);
}

const char *parsenameext (const char *p, firqb *out)	/* parse the name.ext part of a spec */
{
	int	n;

	for (n = 0; n < NAMELEN - 1; n++) out->name[n] = ' ';
	p = parsename (p, out);		/* parse the name part */
	out->name[6] = '.';
	out->name[NAMELEN - 1] = '\0';	/* put in terminator */
	if (*p == '.') {		/* extension present */
		p++;
		out->flags |= f_ext;
		if (isr50(*p)) {
			n = 0;
			while (isr50(*p)) {
				if (*p == ' ') {	/* skip blanks */
					p++;
					continue;
				}
				if (*p == '*') {
					if (n < 3) {
						out->name[7 + n++] = '?';
						out->flags |= f_extw;
					} else p++;
				} else {
					if (n < 3) out->name[7 + n++] = tolower (*p);
					if (*p++ == '?') out->flags |= f_extw;
				}
			}
		}
	}
	return (p);
}

int parse (const char *p, firqb *out)	/* returns FALSE if error, TRUE if ok */
{
	char	*pp;

	out->proj = 0;
	out->prog = 0;
	out->flags = 0;
	if (*p == '[' || *p == '(' || *p == '/') {	/* PPN first */
		p++;
		out->flags |= f_ppn;	/* flag PPN present */
		if (*p == '*') {	/* wild proj */
			p++;
			out->proj = 255;
			out->flags |= f_ppnw;
		} else {
			out->proj = strtoul (p, &pp, 10);
			if (pp == p || out->proj > 254) return (FALSE);
			p = pp;
		}
		if (*p != ',' && *p != '/') return (FALSE);
		p++;
		if (*p == '*') {	/* wild prog */
			p++;
			out->prog = 255;
			out->flags |= f_ppnw;
		} else {
			out->prog = strtoul (p, &pp, 10);
			if (pp == p || out->prog > 254) return (FALSE);
			p = pp;
		}
		if (out->proj == 0 && out->prog == 0) return (FALSE);
		if (*p != ']' && *p != ')' &&
		    *p != '/' && *p != '\0') return (FALSE);
		if (*p != '\0') p++;
	} else switch (*p) {			/* see if special PPN char */
	      case '$': 
		out->proj = 1;			/* supply [1,2] */
		out->prog = 2;
		out->flags |= f_ppn;		/* indicate PPN present */
		p++;				/* skip that char */
		break;
	      case '!':
		out->proj = 1;			/* supply [1,3] */
		out->prog = 3;
		out->flags |= f_ppn;		/* indicate PPN present */
		p++;				/* skip that char */
		break;
	      case '%':
		out->proj = 1;			/* supply [1,4] */
		out->prog = 4;
		out->flags |= f_ppn;		/* indicate PPN present */
		p++;				/* skip that char */
		break;
	      case '&':
		out->proj = 1;			/* supply [1,5] */
		out->prog = 5;
		out->flags |= f_ppn;		/* indicate PPN present */
		p++;				/* skip that char */
		break;
		
	      default:				/* no PPN supplied */
		out->proj = 1;			/* supply default of [1,2] */
		out->prog = 2;			/*  don't set f_ppn flag */
	}
	p = parsenameext (p, out);
	if (*p == '<') {			/* protection code present */
		p++;
		out->flags |= f_prot;
		out->newprot = strtoul (p, &pp, 10);
		if (pp == p) return (FALSE);
		p = pp;
		if (*p++ != '>') return (FALSE);
	}
	return (*p == '\0');		/* success if all was parsed */
}

static const char r50table[] = " abcdefghijklmnopqrstuvwxyz$.?0123456789:";

char *r50toascii (word16 r50, char *string, int space)
{
	int	t, c, pos;
 
	pos = 03100;
	for (t = 0; t<3; t++) {
		c = r50 / pos;
		if (c || space) *string++ = r50table[c];
		r50 %= pos;
		pos /= 050;
	}
	*string = '\0';			/* put in string terminator */
	return (string);
}

char *r50toascii2 (word16 r50[], char *string, int space)
{
	string = r50toascii (r50[0], string, space);
	return (r50toascii (r50[1], string, space));
}

void r50filename (word16 r50[], char *name, int space)
{
	name = r50toascii2 (r50, name, space);
	*name++ = '.';
	name = r50toascii (r50[2], name, space);
}

void printfqbppn (const firqb *f)
{
	if (f->proj == 255) printf ("[*,");
	else printf ("[%d,", f->proj);
	if (f->prog == 255) printf ("*]");
	else printf ("%d]", f->prog);
}

void printfqbname (const firqb *f)
{
	int	n;

	printfqbppn (f);
	for (n = 0; n < NAMELEN - 1; n++) 
		if (f->name[n] != ' ') putchar (f->name[n]);
}

void printcurname (const firqb *f)
{
	if (f->stat & us_ufd)
	{
		if (f->cproj == 255)
			printf ("[*,*]");
		else if (f->cprog == 255)
			printf ("[%d,*]", f->cproj);
		else	printf ("[%d,%d]", f->cproj, f->cprog);
	}
	else	printf ("[%d,%d]%s", f->cproj, f->cprog, f->cname);
}

/* merge a native (unix or dos) input filespec or RSTS name.ext with a 
 * (possibly wild) rsts output file spec to produce the specific output
 * filename as a 3-word rad50 value.  If the third argument is TRUE, then
 * the last directory element in the input filename path must be numeric,
 * and is used to set the current PPN fields in the output FIRQB.
 */
void mergename (char *iname, firqb *oname, int tree)
{
	int	n, spflag;
	firqb	inf;
	char	c;
	char	*in1, *in2;
	char	tname[FILENAME_MAX];
	int	ppn;

	if ((in1 = strrchr (iname, '/')) != NULL) in1++;
	else in1 = iname;
	if ((in2 = strrchr (in1, '\\')) != NULL) in1 = in2 + 1;
	inf.flags = 0;				/* clear parse flags */
	if (*(parsenameext (in1, &inf)) != '\0'	/* see if we can parse that */
		|| (inf.flags & F_WILD))	/* it should not have * or ? */
		memcpy (inf.name, "xxxxxx.xxx", NAMELEN);	/* use xxx if bad name */
	oname->cname[6] = '.';			/* make sure . is in */
	spflag = FALSE;				/* no spaces yet */
	for (n = 0; n < NAMELEN - 1; n++) {
		if (n == 6) {
			spflag = FALSE;		/* no spaces yet in ext */
			continue;		/* skip the '.' separator */
		}
		c = oname->name[n];		/* start with output spec */
		if (c == '?' && !spflag) 
			c = inf.name[n];	/* use input if output wild */
		if (c == ' ') spflag = TRUE;	/* stop if we find a space */
		if (spflag) c = ' ';		/* force spaces after space */
		oname->cname[n] = c;		/* store result */
	}
	oname->cname[NAMELEN - 1] = '\0';	/* ensure terminator */
	if (tree) {
		n = in1 - iname;		/* get offset to name part */
		if (n) n--;			/* now to the / */
		else {
			oname->cproj = oname->cprog = 0; /* can't set PPN from iname */
			return;
		}
		strcpy (tname, iname);		/* copy the name */
		tname[n] = '\0';		/* chop off directory part */
		if ((in1 = strrchr (tname, '/')) != NULL) in1++;
		else in1 = tname;
		if ((in2 = strrchr (in1, '\\')) != NULL) in1 = in2 + 1;
		ppn = strtoul (in1, &in2, 10);	/* convert to decimal */
		if (*in2 != '\0') ppn = 0;	/* check for error */
		if (oname->proj == 255) {
			if (strlen (in1) < 4) ppn = 0;
			oname->cproj = ppn / 1000;	/* proj = first digits */
		} else	oname->cproj = oname->proj;
		if (oname->prog == 255)
			oname->cprog = ppn % 1000;
		else	oname->cprog = oname->prog;
		if (oname->cproj > 254 || oname->cprog > 254) 
			oname->cproj = oname->cprog = 0;
	}
	return;
}			

/* routines to convert ascii names to rad50.  These assume that the name
 * is valid rad50, i.e., letters and digits only.  Special case: for the
 * parsing of RTS names, '.' is also accepted as a rad50 character.
 * Any non-rad50 character is treated as space, i.e., rad50 0.
 */

word cvtr50 (const char *in)
{
	int	mul, r50, n;

	for (r50 = 0, mul = 03100, n = 0; n < 3; in++, mul /= 050, n++) {
		if (isdigit (*in))	r50 += (*in - '0' + 036) * mul;
		else if (isalpha (*in))	r50 += (tolower (*in) - 'a' + 001) * mul;
		else if (*in == '.')	r50 += 034 * mul;
	}
	return (r50);
}

/* convert 6-character name */

void cvtnametor50 (const char *in, word16 *out)
{
	out[0] = cvtr50 (in);
	out[1] = cvtr50 (in + 3);
}

/* convert name.ext */

void cvtnameexttor50 (const char *in, word16 *out)
{
	cvtnametor50 (in, out);
	out[2] = cvtr50 (in + 7);	/* skip the "." */
}

