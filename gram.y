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
    cvs_version	**vlist;
    cvs_patch	*patch;
    cvs_patch	**patches;
    cvs_branch	*branch;
    cvs_file	*file;
}

%token		HEAD BRANCH ACCESS SYMBOLS LOCKS COMMENT DATE
%token		BRANCHES NEXT COMMITID EXPAND
%token		DESC LOG TEXT STRICT AUTHOR STATE
%token		SEMI COLON
%token		BRAINDAMAGED_NUMBER
%token <s>	HEX NAME DATA TEXT_DATA
%token <number>	NUMBER

%type <s>	text log
%type <symbol>	symbollist symbol symbols
%type <version>	revision
%type <vlist>	revisions
%type <date>	date
%type <branch>	branches numbers
%type <s>	opt_commitid commitid
%type <s>	desc name
%type <s>	author state
%type <number>	next opt_number
%type <patch>	patch
%type <patches>	patches


%%
file		: headers revisions desc patches
		  {
		  }
		;
headers		: header headers
		|
		;
header		: HEAD opt_number SEMI
		  { this_file->head = $2; }
		| BRANCH NUMBER SEMI
		  { this_file->branch = $2; }
		| ACCESS SEMI
		| symbollist
		  { this_file->symbols = $1; }
		| LOCKS locks SEMI lock_type
		| COMMENT DATA SEMI
		| EXPAND DATA SEMI
		  { this_file->expand = $2; }
		;
locks		: locks lock
		|
		;
lock		: NAME COLON NUMBER
		;
lock_type	: STRICT SEMI
		|
		;
symbollist	: SYMBOLS symbols SEMI
		  { $$ = $2; }
		;
symbols		: symbols symbol
		  { $2->next = $1; $$ = $2; }
		| symbols fscked_symbol
		  { $$ = $1; }
		|
		  { $$ = NULL; }
		;
symbol		: name COLON NUMBER
		  {
			$$ = calloc (1, sizeof (cvs_symbol));
			$$->name = $1;
			$$->number = $3;
		  }
		;
fscked_symbol	: name COLON BRAINDAMAGED_NUMBER
		  {
			fprintf(stderr, "ignoring symbol %s (FreeBSD RELENG_2_1_0 braindamage?)\n", $1);
		  }
		;
name		: NAME
		| NUMBER
		  {
		    char    name[CVS_MAX_REV_LEN];
		    cvs_number_string (&$1, name);
		    $$ = atom (name);
		  }
		;
revisions	: revisions revision
		  { *$1 = $2; $$ = &$2->next; }
		|
		  { $$ = &this_file->versions; }
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
			hash_version($$);
			++this_file->nversions;
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
			hash_branch($$);
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
patches		: patches patch
		  { *$1 = $2; $$ = &$2->next; }
		|
		  { $$ = &this_file->patches; }
		;
patch		: NUMBER log text
		  { $$ = calloc (1, sizeof (cvs_patch));
		    $$->number = $1;
		    $$->log = $2;
		    $$->text = $3;
		    hash_patch($$);
		  }
		;
log		: LOG DATA
		  { $$ = $2; }
		;
text		: TEXT TEXT_DATA
		  { $$ = $2; }
		;
%%

void yyerror (char *msg)
{
	fprintf (stderr, "parse error %s at %s\n", msg, lex_text ());
	exit(1);
}
