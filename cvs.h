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

#ifndef _CVS_H_
#define _CVS_H_

#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#define CVS_MAX_DEPTH	12

typedef struct _cvs_number {
    int			c;
    short		n[CVS_MAX_DEPTH];
} cvs_number;

typedef struct _cvs_symbol {
    struct _cvs_symbol	*next;
    char		*name;
    cvs_number		number;
} cvs_symbol;

typedef struct _cvs_branch {
    struct _cvs_branch	*next;
    cvs_number		number;
} cvs_branch;

typedef struct _cvs_version {
    struct _cvs_version	*next;
    cvs_number		number;
    time_t		date;
    char		*author;
    cvs_branch		*branches;
    cvs_number		parent;	/* next in ,v file */
    char		*commitid;
} cvs_version;

typedef struct _cvs_patch {
    struct _cvs_patch	*next;
    cvs_number		number;
    char		*log;
    char		*text;
} cvs_patch;

typedef struct {
    char		*name;
    cvs_number		head;
    cvs_number		branch;
    cvs_symbol		*symbols;
    cvs_version		*versions;
    cvs_patch		*patches;
} cvs_file;

typedef struct _rev_file {
    char		*name;
    cvs_number		number;
    time_t		date;
    char		*log;
    char		*commitid;
} rev_file;

typedef struct _rev_ent {
    struct _rev_ent	*parent;
    char		tail;
    char		seen;
    int			nfiles;
    rev_file		*files[0];
} rev_ent;

typedef struct _rev_branch {
    struct _rev_branch	*next;
    rev_ent		*ent;
} rev_branch;

typedef struct _rev_ref {
    struct _rev_ref	*next;
    rev_ent		*ent;
    int			head;
    char		*name;
    int			shown;
} rev_ref;

typedef struct {
    rev_branch	*branches;
    rev_ref	*heads;
    rev_ref	*tags;
} rev_list;

extern cvs_file     *this_file;

int yyparse (void);

cvs_number
lex_number (char *);

time_t
lex_date (cvs_number *n);

rev_list *
rev_list_cvs (cvs_file *cvs);

rev_list *
rev_list_merge (rev_list *a, rev_list *b);

void
rev_list_free (rev_list *rl, int free_files);

int
cvs_is_head (cvs_number *n);

int
cvs_same_branch (cvs_number *a, cvs_number *b);

int
cvs_number_compare (cvs_number *a, cvs_number *b);

int
cvs_number_compare_n (cvs_number *a, cvs_number *b, int l);

cvs_number
cvs_previous_rev (cvs_number *n);

cvs_number
cvs_master_rev (cvs_number *n);

cvs_number
cvs_branch_head (cvs_file *f, cvs_number *branch);

cvs_number
cvs_branch_parent (cvs_file *f, cvs_number *branch);

cvs_patch *
cvs_find_patch (cvs_file *f, cvs_number *n);

cvs_version *
cvs_find_version (cvs_file *cvs, cvs_number *number);

int
cvs_is_trunk (cvs_number *number);

int
cvs_is_vendor (cvs_number *number);

void
cvs_file_free (cvs_file *cvs);

long
time_compare (time_t a, time_t b);

void
dump_number (char *name, cvs_number *number);

void
dump_symbols (char *name, cvs_symbol *symbols);

void
dump_branches (char *name, cvs_branch *branches);

void
dump_versions (char *name, cvs_version *versions);

void
dump_patches (char *name, cvs_patch *patches);

void
dump_file (cvs_file *file);

void
dump_ent (rev_ent *e);

void
dump_refs (rev_ref *refs);

void
dump_rev_list (rev_list *rl);

extern int yylex (void);

char *
atom (char *string);

void
discard_atoms (void);

void
rev_ref_add (rev_ref **list, rev_ent *ent, char *name, int head);

void
rev_list_add_head (rev_list *rl, rev_ent *ent, char *name);

void
rev_list_add_tag (rev_list *rl, rev_ent *ent, char *name);

void
rev_list_add_branch (rev_list *rl, rev_ent *ent);

void
rev_branch_free (rev_branch *branches, int free_files);

#define time_compare(a,b) ((long) (a) - (long) (b))

#endif /* _CVS_H_ */
