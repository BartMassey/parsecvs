/*
 * Copyright © 2006 Sean Estabrooks <seanlkml@sympatico.ca>
 * Copyright © 2006 Keith Packard <keithp@keithp.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * Large portions of code contained in this file were obtained from
 * the original RCS application under GPLv2 or later license, it retains
 * the copyright of the original authors listed below:
 *
 * Copyright 1982, 1988, 1989 Walter Tichy
 *   Copyright 1990, 1991, 1992, 1993, 1994, 1995 Paul Eggert
 *      Distributed under license by the Free Software Foundation, Inc.
 */
#include <limits.h>
#include <stdarg.h>
#include "cvs.h"

typedef unsigned char uchar;
struct alloclist {
	void* alloc;
	struct alloclist *nextalloc;
};

struct out_buffer_type {
	char *text, *ptr, *end_of_text;
	size_t size;
};

struct in_buffer_type {
	uchar *buffer;
	uchar *ptr;
	int read_count;
};

struct diffcmd {
	long line1, nlines, adprev, dafter;
};

const int initial_out_buffer_size = 1024;
char const ciklog[] = "checked in with -k by ";

#define KEYLENGTH 8 /* max length of any of the above keywords */
#define KDELIM '$' /* keyword delimiter */
#define VDELIM ':' /* separates keywords from values */
#define SDELIM '@' /* string delimiter */
#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

char const *const Keyword[] = {
	0, "Author", "Date", "Header", "Id", "Locker", "Log",
	"Name", "RCSfile", "Revision", "Source", "State"
};
enum markers { Nomatch, Author, Date, Header, Id, Locker, Log,
	Name, RCSfile, Revision, Source, State };
enum stringwork {ENTER, EDIT};

enum expand_mode {EXPANDKKV, EXPANDKKVL, EXPANDKK, EXPANDKV, EXPANDKO, EXPANDKB};
enum expand_mode Gexpand;
char * Glog;
int Gkvlen = 0;
char* Gkeyval = NULL;
char const *Gfilename;
char *Gabspath;
cvs_version *Gversion;
char Gversion_number[CVS_MAX_REV_LEN];
struct out_buffer_type *Goutbuf;
struct in_buffer_type in_buffer_store;
struct in_buffer_type *Ginbuf = &in_buffer_store;

/*
 * Gline contains pointers to the lines in the currently edit buffer
 * It is a 0-origin array that represents Glinemax-Ggapsize lines.
 * Gline[0 .. Ggap-1] and Gline[Ggap+Ggapsize .. Glinemax-1] hold
 * pointers to lines.  Gline[Ggap .. Ggap+Ggapsize-1] contains garbage.
 * Any @s in lines are duplicated.
 * Lines are terminated by \n, or (for a last partial line only) by single @.
 */
static int depth;
static struct {
	Node *next_branch;
	Node *node;
	uchar **line;
	size_t gap, gapsize, linemax;
} stack[CVS_MAX_DEPTH/2];
#define Gline stack[depth].line
#define Ggap stack[depth].gap
#define Ggapsize stack[depth].gapsize
#define Glinemax stack[depth].linemax

static void fatal_system_error(char const *s)
{
	perror(s);
	exit(1);
}

static void fatal_error(char const *format,...)
{
	va_list args;

	fprintf(stderr, "rcsco2git fatal: ");
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fprintf(stderr, "\n");
	exit(1);
}

static void* xmalloc(size_t size)
{
        void *ret = malloc(size);
        if (!ret && !size)
                ret = malloc(1);
        if (!ret)
                fatal_system_error("Out of memory, malloc failed");
        return ret;
}

static void* xrealloc(void *ptr, size_t size)
{
        void *ret = realloc(ptr, size);
        if (!ret && !size)
                ret = realloc(ptr, 1);
        if (!ret)
                fatal_system_error("Out of memory, realloc failed");
        return ret;
}

/* backup one position in the input buffer, unless at start of buffer
 *   return character at new position, or EOF if we could not back up
 */
static int in_buffer_ungetc(void)
{
	int c;
	if (Ginbuf->read_count == 0)
		return EOF;
	--Ginbuf->read_count;
	--Ginbuf->ptr;
	c = *Ginbuf->ptr;
	if (c == SDELIM) {
		--Ginbuf->ptr;
		c = *Ginbuf->ptr;
	}
	return c;
}

static int in_buffer_getc(void)
{
	int c;
	c = *(Ginbuf->ptr++);
	++Ginbuf->read_count;
	if (c == SDELIM) {
		c = *(Ginbuf->ptr++);
		if (c != SDELIM) {
			Ginbuf->ptr -= 2;
			--Ginbuf->read_count;
			return EOF;
		}
	}
	return c ;
}

static uchar * in_get_line(void)
{
	int c;
	uchar *ptr = Ginbuf->ptr;
	c=in_buffer_getc();
	if (c == EOF)
		return NULL;
	while (c != EOF && c != '\n')
		c = in_buffer_getc();
	return ptr;
}

static uchar * in_buffer_loc(void)
{
	return(Ginbuf->ptr);
}

static void in_buffer_init(uchar *text, int bypass_initial)
{
	Ginbuf->ptr = Ginbuf->buffer = text;
	Ginbuf->read_count=0;
	if (bypass_initial && *Ginbuf->ptr++ != SDELIM)
		fatal_error("Illegal buffer, missing @ %s", text);
}

static void out_buffer_init(void)
{
	char *t;
	Goutbuf = xmalloc(sizeof(struct out_buffer_type));
	memset(Goutbuf, 0, sizeof(struct out_buffer_type));
	Goutbuf->size = initial_out_buffer_size;
	t = xmalloc(Goutbuf->size);
	Goutbuf->text = t;
	Goutbuf->ptr = t;
	Goutbuf->end_of_text = t + Goutbuf->size;
}

static void out_buffer_enlarge(void)
{
	int ptroffset = Goutbuf->ptr - Goutbuf->text;
	Goutbuf->size *= 2;
	Goutbuf->text = xrealloc(Goutbuf->text, Goutbuf->size);
	Goutbuf->end_of_text = Goutbuf->text + Goutbuf->size;
	Goutbuf->ptr = Goutbuf->text + ptroffset;
}

static unsigned long  out_buffer_count(void)
{
	return (unsigned long) (Goutbuf->ptr - Goutbuf->text);
}

static char *out_buffer_text(void)
{
	return Goutbuf->text;
}

static void out_buffer_cleanup(void)
{
	free(Goutbuf->text);
	free(Goutbuf);
}

inline static void out_putc(int c)
{
	*Goutbuf->ptr++ = c;
	if (Goutbuf->ptr >= Goutbuf->end_of_text)
		out_buffer_enlarge();
}

static void out_printf(const char *fmt, ...)
{
	int ret, room;
	va_list ap;
	while (1) {
		room = Goutbuf->end_of_text - Goutbuf->ptr;
		va_start(ap, fmt);
		ret = vsnprintf(Goutbuf->ptr, room, fmt, ap);
		va_end(ap);
		if (ret > -1 && ret < room) {
			Goutbuf->ptr += ret;
			return;
		}
		out_buffer_enlarge();
	}
}

static int out_fputs(const char *s)
{
	while (*s)
		out_putc(*s++);
	return 0;
}

static void out_awrite(char const *s, size_t len)
{
	while (len--)
		out_putc(*s++);
}

static int latin1_alpha(int c)
{
	if (c >= 192 && c != 215 && c != 247 ) return 1;
	if ((c >= 97 && c < 123) || (c >= 65 && c < 91)) return 1;
	return 0;
}

static int latin1_whitespace(uchar c)
{
	if (c == 32 || (c >= 8 && c <= 13 && c != 10)) return 1;
	return 0;
}

static enum expand_mode expand_override(char const *s)
{
	char * const expand_names[] = {"kv", "kvl", "k", "v", "o", "b"};
	int i;
	for (i=0; i < 6; ++i)
		if (strcmp(s,expand_names[i]) == 0)
			return (enum expand_mode) i;
	return EXPANDKK;
}

static char const * basefilename(char const *p)
{
	char const *b = p, *q = p;
	for (;;)
	    switch (*q++) {
		case '/': b = q; break;
		case 0: return b;
	    }
}

/* Convert relative RCS filename to absolute path */
static char const * getfullRCSname(void)
{
	char *wdbuf = NULL;
	int wdbuflen = 0;

	size_t dlen;

	char const *r;
	char* d;

	if (Gfilename[0] == '/')
		return Gfilename;

	/* If we've already calculated the absolute path, return it */
	if (Gabspath)
		return Gabspath;

	/* Get working directory and strip any trailing slashes */
	wdbuflen = _POSIX_PATH_MAX + 1;
	wdbuf = xmalloc(wdbuflen);
	while (!getcwd(wdbuf, wdbuflen)) {
		if (errno == ERANGE)
			xrealloc(wdbuf, wdbuflen<<1);
		else	fatal_system_error("getcwd");
	}

	/* Trim off trailing slashes */
	dlen = strlen(wdbuf);
	while (dlen && wdbuf[dlen-1] == '/')
		--dlen;
	wdbuf[dlen] = 0;

	/* Ignore leading `./'s in Gfilename. */
	for (r = Gfilename;  r[0]=='.' && r[1] == '/';  r += 2)
		while (r[2] == '/')
			r++;

	/* Build full pathname.  */
	Gabspath = d = xmalloc(dlen + strlen(r) + 2);
	memcpy(d, wdbuf, dlen);
	d += dlen;
	*d++ = '/';
	strcpy(d, r);
	free(wdbuf);

	return Gabspath;
}

/* Check if string starts with a keyword followed by a KDELIM or VDELIM */
static enum markers trymatch(char const *string)
{
        int j;
	char const *p, *s;
	for (j = sizeof(Keyword)/sizeof(*Keyword);  (--j);  ) {
		p = Keyword[j];
		s = string;
		while (*p++ == *s++) {
			if (!*p) {
				switch (*s) {
				case KDELIM:
				case VDELIM:
					return (enum markers)j;
				default:
					return Nomatch;
				}
			}
		}
        }
        return(Nomatch);
}

/* Before line N, insert line L.  N is 0-origin.  */
static void insertline(unsigned long n, uchar * l)
{
	if (n > Glinemax - Ggapsize)
		fatal_error("edit script tried to insert beyond eof");
	if (!Ggapsize) {
		if (Glinemax) {
			Ggap = Ggapsize = Glinemax; Glinemax <<= 1;
			Gline = xrealloc(Gline, sizeof(uchar *) * Glinemax);
		} else {
			Glinemax = Ggapsize = 1024;
			Gline = xmalloc(sizeof(uchar *) *  Glinemax);
		}
	}
	if (n < Ggap)
		memmove(Gline+n+Ggapsize, Gline+n, (Ggap-n) * sizeof(uchar *));
	else if (Ggap < n)
		memmove(Gline+Ggap, Gline+Ggap+Ggapsize, (n-Ggap) * sizeof(uchar *));
	Gline[n] = l;
	Ggap = n + 1;
	Ggapsize--;
}

/* Delete lines N through N+NLINES-1.  N is 0-origin.  */
static void deletelines(unsigned long n, unsigned long nlines)
{
	unsigned long l = n + nlines;
	if (Glinemax-Ggapsize < l  ||  l < n)
		fatal_error("edit script tried to delete beyond eof");
	if (l < Ggap)
		memmove(Gline+l+Ggapsize, Gline+l, (Ggap-l) * sizeof(uchar *));
	else if (Ggap < n)
		memmove(Gline+Ggap, Gline+Ggap+Ggapsize, (n-Ggap) * sizeof(uchar *));
	Ggap = n;
	Ggapsize += nlines;
}

static long parsenum(void)
{
	int c;
	long ret = 0;
	for(c=in_buffer_getc(); isdigit(c); c=in_buffer_getc())
		ret = (ret * 10) + (c - '0');
	in_buffer_ungetc();
	return ret;
}

static int parse_next_delta_command(struct diffcmd *dc)
{
	int cmd;
	long line1, nlines;

	cmd = in_buffer_getc();
	if (cmd==EOF)
		return -1;

	line1 = parsenum();

	while (in_buffer_getc() == ' ')
		;
	in_buffer_ungetc();

	nlines = parsenum();

	while (in_buffer_getc() != '\n')
		;

	if (!nlines || (cmd != 'a' && cmd != 'd') || line1+nlines < line1)
		fatal_error("Corrupt delta");

	if (cmd == 'a') {
		if (line1 < dc->adprev)
			fatal_error("backward insertion in delta");
		dc->adprev = line1 + 1;
	} else if (cmd == 'd') {
		if (line1 < dc->adprev  ||  line1 < dc->dafter)
			fatal_error("backward deletion in delta");
		dc->adprev = line1;
		dc->dafter = line1 + nlines;
	}

	dc->line1 = line1;
	dc->nlines = nlines;
	return cmd == 'a';
}

static void escape_string(register char const *s)
{
	register char c;
	for (;;) {
		switch ((c = *s++)) {
		case 0:		return;
		case '\t':	out_fputs("\\t"); break;
		case '\n':	out_fputs("\\n"); break;
		case ' ':	out_fputs("\\040"); break;
		case KDELIM:	out_fputs("\\044"); break;
		case '\\':	out_fputs("\\\\"); break;
		default:	out_putc(c); break;
		}
	}
}

/* output the appropriate keyword value(s) */
static void keyreplace(enum markers marker)
{
	const char *target_lockedby = NULL;	// Not wired in yet

	int c;
	char const *xxp;
	size_t cs, cw, ls;
	char *leader = NULL;
	char date_string[25];
	uchar *kdelim_ptr = NULL;
	enum expand_mode exp = Gexpand;
	char const *sp = Keyword[(int)marker];

	strftime(date_string, 25,
		"%Y/%m/%d %H:%M:%S", localtime(&Gversion->date));

	if (exp != EXPANDKV)
		out_printf("%c%s", KDELIM, sp);

	if (exp != EXPANDKK) {
		if (exp != EXPANDKV)
			out_printf("%c%c", VDELIM, ' ');

		switch (marker) {
		case Author:
			out_fputs(Gversion->author);
			break;
		case Date:
			out_fputs(date_string);
			break;
		case Id:
		case Header:
			if (marker == Id )
				escape_string(basefilename(Gfilename));
			else	escape_string(getfullRCSname());
			out_printf(" %s %s %s %s",
				Gversion_number, date_string,
				Gversion->author, Gversion->state);
			if (target_lockedby && exp == EXPANDKKVL)
				out_printf(" %s", target_lockedby);
			break;
		case Locker:
			if (target_lockedby && exp == EXPANDKKVL)
				out_fputs(target_lockedby);
			break;
		case Log:
		case RCSfile:
			escape_string(basefilename(Gfilename));
			break;
		case Revision:
			out_fputs(Gversion_number);
			break;
		case Source:
			escape_string(getfullRCSname());
			break;
		case State:
			out_fputs(Gversion->state);
			break;
		default:
			break;
		}

		if (exp != EXPANDKV)
			out_putc(' ');
	}

#if 0
/* Closing delimiter is processed again in expandline */
	if (exp != EXPANDKV)
	    out_putc(KDELIM);
#endif

	if (marker == Log) {
		/*
		 * "Closing delimiter is processed again in expandline"
		 * does not apply here, since we consume the input.
		 */
		if (exp != EXPANDKV)
			out_putc(KDELIM);

		sp = Glog;
		ls = strlen(Glog);
		if (sizeof(ciklog)-1<=ls && !memcmp(sp,ciklog,sizeof(ciklog)-1))
			return;

		/* Back up to the start of the current input line */
                int num_kdelims = 0;
		for (;;) {
			c = in_buffer_ungetc();
			if (c == EOF)
				break;
			if (c == '\n') {
				in_buffer_getc();
				break;
			}
			if (c == KDELIM) {
                                num_kdelims++;
                                /* It is possible to have multiple keywords
                                   on one line. Make sure we don't backtrack
                                   into some other keyword! */
                                if (num_kdelims > 2) {
                                        in_buffer_getc();
                                        break;
                                }
				kdelim_ptr = in_buffer_loc();
                        }
		}

		/* Copy characters before `$Log' into LEADER.  */
		xxp = leader = xmalloc(kdelim_ptr - in_buffer_loc());
		for (cs = 0; ;  cs++) {
			c = in_buffer_getc();
			if (c == KDELIM)
				break;
			leader[cs] = c;
		}

		/* Convert traditional C or Pascal leader to ` *'.  */
		for (cw = 0;  cw < cs;  cw++)
			if (!latin1_whitespace(xxp[cw]))
				break;
		if (cw+1 < cs &&  xxp[cw+1] == '*' &&
		    (xxp[cw] == '/'  ||  xxp[cw] == '(')) {
			size_t i = cw+1;
			for (;;) {
				if (++i == cs) {
					leader[cw] = ' ';
					break;
				} else if (!latin1_whitespace(xxp[i]))
					break;
			}
		}

		/* Skip `$Log ... $' string.  */
		do {
			c = in_buffer_getc();
		} while (c != KDELIM);

		out_putc('\n');
		out_awrite(xxp, cs);
		out_printf("Revision %s  %s  %s",
				Gversion_number,
				date_string,
				Gversion->author);

		/* Do not include state: it may change and is not updated.  */
		cw = cs;
		for (;  cw && (xxp[cw-1]==' ' || xxp[cw-1]=='\t');  --cw)
			;
		for (;;) {
			out_putc('\n');
			out_awrite(xxp, cw);
			if (!ls)
				break;
			--ls;
			c = *sp++;
			if (c != '\n') {
				out_awrite(xxp+cw, cs-cw);
				do {
					out_putc(c);
					if (!ls)
						break;
					--ls;
					c = *sp++;
				} while (c != '\n');
			}
		}
		free(leader);
	}
}

static int expandline(void)
{
	register int c = 0;
	char * tp;
	register int e, r;
	char const *tlim;
        enum markers matchresult;
	int orig_size;

	if (Gkvlen < KEYLENGTH+3) {
		Gkvlen = KEYLENGTH + 3;
		Gkeyval = xrealloc(Gkeyval, Gkvlen);
	}
	e = 0;
	r = -1;

        for (;;) {
	    c = in_buffer_getc();
	    for (;;) {
		switch (c) {
		    case EOF:
			goto uncache_exit;
		    default:
			out_putc(c);
			r = 0;
			break;
		    case '\n':
			out_putc(c);
			r = 2;
			goto uncache_exit;
		    case KDELIM:
			r = 0;
                        /* check for keyword */
                        /* first, copy a long enough string into keystring */
			tp = Gkeyval;
			*tp++ = KDELIM;
			for (;;) {
			    c = in_buffer_getc();
			    if (tp <= &Gkeyval[KEYLENGTH] && latin1_alpha(c))
					*tp++ = c;
			    else	break;
                        }
			*tp++ = c; *tp = '\0';
			matchresult = trymatch(Gkeyval+1);
			if (matchresult==Nomatch) {
				tp[-1] = 0;
				out_fputs(Gkeyval);
				continue;   /* last c handled properly */
			}

			/* Now we have a keyword terminated with a K/VDELIM */
			if (c==VDELIM) {
			      /* try to find closing KDELIM, and replace value */
			      tlim = Gkeyval + Gkvlen;
			      for (;;) {
				     c = in_buffer_getc();
				      if (c=='\n' || c==KDELIM)
					break;
				      *tp++ =c;
				      if (tlim <= tp) {
					    orig_size = Gkvlen;
					    Gkvlen *= 2;
					    Gkeyval = xrealloc(Gkeyval, Gkvlen);
					    tlim = Gkeyval + Gkvlen;
					    tp = Gkeyval + orig_size;

					}
				      if (c==EOF)
					   goto keystring_eof;
			      }
			      if (c!=KDELIM) {
				    /* couldn't find closing KDELIM -- give up */
				    *tp = 0;
				    out_fputs(Gkeyval);
				    continue;   /* last c handled properly */
			      }
			}
			/*
			 * CVS will expand keywords that have
			 * overlapping delimiters, eg "$Name$Id$".  To
			 * support that (mis)feature, push the closing
			 * delimiter back on the input so that the
			 * loop will resume processing starting with
			 * it.
			 */
			if (c == KDELIM)
				in_buffer_ungetc();

			/* now put out the new keyword value */
			keyreplace(matchresult);
			e = 1;
			break;
                }
		break;
	    }
        }

    keystring_eof:
	*tp = 0;
	out_fputs(Gkeyval);
    uncache_exit:
	return r + e;
}

static void process_delta(Node *node, enum stringwork func)
{
	long editline = 0, linecnt = 0, adjust = 0;
	int editor_command;
	struct diffcmd dc;
	uchar *ptr;

	Glog = node->p->log;
	in_buffer_init((uchar *)node->p->text, 1);
	Gversion = node->v;
	cvs_number_string(&Gversion->number, Gversion_number);

	switch (func) {
	case ENTER:
		while( (ptr=in_get_line()) )
			insertline(editline++, ptr);
	case EDIT:
		dc.dafter = dc.adprev = 0;
		while ((editor_command = parse_next_delta_command(&dc)) >= 0) {
			if (editor_command) {
				editline = dc.line1 + adjust;
				linecnt = dc.nlines;
				while(linecnt--)
					insertline(editline++, in_get_line());
				adjust += dc.nlines;
			} else {
				deletelines(dc.line1 - 1 + adjust, dc.nlines);
				adjust -= dc.nlines;
			}
		}
		break;
	}
}

static void finishedit(void)
{
	uchar **p, **lim, **l = Gline;
	for (p=l, lim=l+Ggap;  p<lim;  ) {
		in_buffer_init(*p++, 0);
		expandline();
	}
	for (p+=Ggapsize, lim=l+Glinemax;  p<lim;  ) {
		in_buffer_init(*p++, 0);
		expandline();
	}
}

static void snapshotline(register uchar * l)
{
	register int c;
	do {
		if ((c = *l++) == SDELIM  &&  *l++ != SDELIM)
			return;
		out_putc(c);
	} while (c != '\n');

}

static void snapshotedit(void)
{
	uchar **p, **lim, **l=Gline;
	for (p=l, lim=l+Ggap;  p<lim;  )
		snapshotline(*p++);
	for (p+=Ggapsize, lim=l+Glinemax;  p<lim;  )
		snapshotline(*p++);
}

static void enter_branch(Node *node)
{
	uchar **p = xmalloc(sizeof(uchar *) * stack[depth].linemax);
	memcpy(p, stack[depth].line, sizeof(uchar *) * stack[depth].linemax);
	stack[depth + 1] = stack[depth];
	stack[depth + 1].next_branch = node->sib;
	stack[depth + 1].line = p;
	depth++;
}

void generate_files(cvs_file *cvs, void (*hook)(Node *node, void *buf, unsigned long len))
{
	int expand_override_enabled = 1;
	int expandflag = Gexpand < EXPANDKO;
	Node *node = head_node;
	depth = 0;
	Gfilename = cvs->name;
	if (cvs->expand && expand_override_enabled)
		Gexpand = expand_override(cvs->expand);
	else	Gexpand = EXPANDKK;
	Gabspath = NULL;
	Gline = NULL; Ggap = Ggapsize = Glinemax = 0;
	stack[0].node = node;
	process_delta(node, ENTER);
	while (1) {
		if (node->file) {
			out_buffer_init();
			if (expandflag)
				finishedit();
			else
				snapshotedit();
			hook(node, out_buffer_text(), out_buffer_count());
			out_buffer_cleanup();
		}
		node = node->down;
		if (node) {
			enter_branch(node);
			goto Next;
		}
		while ((node = stack[depth].node->to) == NULL) {
			free(stack[depth].line);
			if (!depth)
				goto Done;
			node = stack[depth--].next_branch;
			if (node) {
				enter_branch(node);
				break;
			}
		}
Next:
		stack[depth].node = node;
		process_delta(node, EDIT);
	}
Done:
	free(Gkeyval);
	Gkeyval = NULL;
	Gkvlen = 0;
	free(Gabspath);
}
