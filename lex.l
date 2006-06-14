%{
/*
 *  Copyright © 2006 Keith Packard <keithp@keithp.com>
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
 */
#include "cvs.h"
#include "y.tab.h"
    
static char *
parse_data (int strip);

#define YY_INPUT(buf,result,max_size) { \
    int c = getc (yyin); \
    result = (c == EOF) ? YY_NULL : (buf[0] = c, 1); \
}
    
%}
%s CONTENT SKIP
%%
<INITIAL>head			BEGIN(CONTENT); return HEAD;
<INITIAL>branch			BEGIN(CONTENT); return BRANCH;
<INITIAL>access			BEGIN(CONTENT); return ACCESS;
<INITIAL>symbols		BEGIN(CONTENT); return SYMBOLS;
<INITIAL>locks			BEGIN(CONTENT); return LOCKS;
<INITIAL>comment		BEGIN(CONTENT); return COMMENT;
<INITIAL>expand			BEGIN(CONTENT); return EXPAND;
<INITIAL>date			BEGIN(CONTENT); return DATE;
<INITIAL>branches		BEGIN(CONTENT); return BRANCHES;
<INITIAL>next			BEGIN(CONTENT); return NEXT;
<INITIAL>commitid		BEGIN(CONTENT); return COMMITID;
<INITIAL>strict			BEGIN(CONTENT); return STRICT;
<INITIAL>author			BEGIN(CONTENT); return AUTHOR;
<INITIAL>state			BEGIN(CONTENT); return STATE;
<INITIAL>desc			return DESC;
<INITIAL>log			return LOG;
<INITIAL>text			BEGIN(SKIP); return TEXT;
<SKIP>@				{
					yylval.s = parse_data (0);
					BEGIN(INITIAL);
					return TEXT_DATA;
				}
<CONTENT>[-a-zA-Z_+%][-a-zA-Z_0-9+/%.]* {
					yylval.s = atom (yytext);
					return NAME;
				}
[0-9]+\.[0-9.]*			{
					yylval.number = lex_number (yytext);
					return NUMBER;
				}
;				BEGIN(INITIAL); return SEMI;
:				return COLON;
<INITIAL,CONTENT>@		{
					yylval.s = parse_data (1);
					return DATA;
				}
" " 				;
\t				;
\n				;
.				{ 
				    fprintf (stderr, "%s: (%d) ignoring %c\n", 
					     yyfilename, yylineno,
					     yytext[0]);
				}
%%
int yywrap (void) { return 1; }

struct varbuf {
	int len, cur;
	char *string;
};

static inline void addbuf(struct varbuf *buf, char c)
{
	if (buf->cur == buf->len) 
		buf->string = realloc(buf->string, buf->len *= 2);
	buf->string[buf->cur++] = c;
}

static char *
parse_data (int strip)
{
    int c;
    char *ret;
    struct varbuf buf;

    buf.cur = 0;
    buf.len = 256;
    buf.string = malloc(buf.len);

    if (!strip)
    	addbuf(&buf, '@');
    for(;;) {
	c = getc (yyin);
	if (c == '@') {
	    if (!strip)
	    	addbuf(&buf, c);
	    c = getc (yyin);
	    if (c != '@') 
		break;
	}
	addbuf(&buf, c);
    }
    ungetc (c, yyin);
    addbuf(&buf, 0);
    if (strip) {
       ret = atom (buf.string);
       free (buf.string);
    } else {
       ret = buf.string;
    }
    return ret;
}

cvs_number
lex_number (char *s)
{
    cvs_number	n;
    char	*next;

    n.c = 0;
    while (*s) {
	n.n[n.c] = (int) strtol(s, &next, 10);
	if (next == s)
	    break;
	if (*next == '.')
	    next++;
	s = next;
	n.c++;
    }
    return n;
}

time_t
lex_date (cvs_number *n)
{
	struct tm	tm;
	time_t		d;
	
	tm.tm_year = n->n[0];
	if (tm.tm_year > 1900)
	   tm.tm_year -= 1900;
	tm.tm_mon = n->n[1] - 1;
	tm.tm_mday = n->n[2];
	tm.tm_hour = n->n[3];
	tm.tm_min = n->n[4];
	tm.tm_sec = n->n[5];
	tm.tm_isdst = 0;
	tm.tm_zone = 0;
	d = mktime (&tm);
	if (d == 0) {
	    int i;
	    fprintf (stderr, "%s: (%d) unparsable date: ", yyfilename,
		     yylineno);
	    for (i = 0; i < n->c; i++) {
		if (i) fprintf (stderr, ".");
		fprintf (stderr, "%d", n->n[i]);
	    }
	    fprintf (stderr, "\n");
	}
	return d;
}

char *
lex_text (void)
{
    return yytext;
}
