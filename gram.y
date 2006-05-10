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
    
void yyerror (char *msg);

%}

%union {
    int		i;
    time_t	date;
    char	*s;
    cvs_number	number;
    cvs_symbol	*symbol;
    cvs_version	*version;
    cvs_patch	*patch;
    cvs_branch	*branch;
    cvs_file	*file;
}

%token		HEAD BRANCH ACCESS SYMBOLS LOCKS COMMENT DATE
%token		BRANCHES NEXT COMMITID EXPAND
%token		DESC LOG TEXT STRICT AUTHOR STATE
%token		SEMI COLON
%token <s>	HEX NAME DATA TEXT_DATA
%token <number>	NUMBER

%type <s>	text log
%type <symbol>	symbollist symbol symbols
%type <version>	revisions revision
%type <date>	date
%type <branch>	branches numbers
%type <s>	opt_commitid commitid
%type <s>	desc
%type <s>	author state
%type <number>	next opt_number
%type <patch>	patches patch


%%
file		: headers revisions desc patches
		  {
			this_file->versions = $2;
			this_file->patches = $4;
		  }
		;
headers		: header headers
		|
		;
header		: HEAD NUMBER SEMI
		  { this_file->head = $2; }
		| BRANCH NUMBER SEMI
		  { this_file->branch = $2; }
		| ACCESS SEMI
		| symbollist
		  { this_file->symbols = $1; }
		| LOCKS SEMI lock_type SEMI
		| COMMENT DATA SEMI
		| EXPAND DATA SEMI
		  { this_file->expand = $2; }
		;
lock_type	: STRICT
		;
symbollist	: SYMBOLS symbols SEMI
		  { $$ = $2; }
		;
symbols		: symbols symbol
		  { $2->next = $1; $$ = $2; }
		|
		  { $$ = NULL; }
		;
symbol		: NAME COLON NUMBER
		  {
			$$ = calloc (1, sizeof (cvs_symbol));
			$$->name = $1;
			$$->number = $3;
		  }
		;
revisions	: revision revisions
		  { $1->next = $2; $$ = $1; }
		|
		  { $$ = NULL; }
		;
revision	: NUMBER date author state branches next opt_commitid
		  {
			$$ = calloc (1, sizeof (cvs_version));
			$$->number = $1;
			$$->date = $2;
			$$->author = $3;
			$$->state = $4;
			$$->dead = !strcmp ($4, "dead");
			$$->branches = $5;
			$$->parent = $6;
			$$->commitid = $7;
		  }
		;
date		: DATE NUMBER SEMI
		  {
			$$ = lex_date (&$2);
		  }
		;
author		: AUTHOR NAME SEMI
		  { $$ = $2; }
		;
state		: STATE NAME SEMI
		  { $$ = $2; }
		;
branches	: BRANCHES numbers SEMI
		  { $$ = $2; }
		;
numbers		: NUMBER numbers
		  {
			$$ = calloc (1, sizeof (cvs_branch));
			$$->next = $2;
			$$->number = $1;
		  }
		|
		  { $$ = NULL; }
		;
next		: NEXT opt_number SEMI
		  { $$ = $2; }
		;
opt_number	: NUMBER
		  { $$ = $1; }
		|
		  { $$.c = 0; }
		;
opt_commitid	: commitid
		  { $$ = $1; }
		|
		  { $$ = NULL; }
		;
commitid	: COMMITID NAME SEMI
		  { $$ = $2; }
		;
desc		: DESC DATA
		  { $$ = $2; }
		;
patches		: patch patches
		  { $1->next = $2; $$ = $1; }
		|
		  { $$ = NULL; }
		;
patch		: NUMBER log text
		  { $$ = calloc (1, sizeof (cvs_patch));
		    $$->number = $1;
		    $$->log = $2;
		    $$->text = $3;
		  }
		;
log		: LOG DATA
		  { $$ = $2; }
		;
text		: TEXT TEXT_DATA
		  { $$ = $2; }
		;
%%
extern char *yytext;

void yyerror (char *msg)
{
	fprintf (stderr, "parse error %s at %s\n", msg, yytext);
}
