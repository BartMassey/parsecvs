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
%}
%s CONTENT
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
<INITIAL>text			return TEXT;
[0-9]+\.[0-9.]*			{
					yylval.number = lex_number (yytext);
					return NUMBER;
				}
<CONTENT>[-a-zA-Z_0-9][-a-zA-Z_0-9]* {
					yylval.s = atom (yytext);
					return NAME;
				}
;				BEGIN(INITIAL); return SEMI;
:				return COLON;
@(@@|[^@]|\n)*@			{
					yytext[yyleng-1] = '\0';
					yylval.s = atom (yytext+1);
					return DATA;
				}
" " 				;
\t				;
\n				;
.				fprintf (stderr, "ignoring %c\n", yytext[0]);
%%
int yywrap (void) { return 1; }

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
	
	tm.tm_year = n->n[0] - 1900;
	tm.tm_mon = n->n[1] - 1;
	tm.tm_mday = n->n[2];
	tm.tm_hour = n->n[3];
	tm.tm_min = n->n[4];
	tm.tm_sec = n->n[5];
	tm.tm_isdst = 0;
	tm.tm_zone = 0;
	return mktime (&tm);
}
