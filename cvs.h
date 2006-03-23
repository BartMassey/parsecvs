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
#include <stdint.h>
#include <ctype.h>

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
    int			dead;
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
    struct _rev_file	*link;
} rev_file;

extern time_t	time_now;

typedef struct _rev_commit {
    struct _rev_commit	*parent;
    char		tail;
    char		seen;
    char		used;
    char		tailed;
    time_t		date;
    char		*log;
    char		*commitid;
    int			nfiles;
    struct _rev_commit	*user;
    rev_file		*files[0];
} rev_commit;

typedef struct _rev_ref {
    struct _rev_ref	*next;
    rev_commit		*commit;
    struct _rev_ref	*parent;	/* link into tree */
    int			head;
    int			tail;
    int			degree;	/* number of digits in original CVS version */
    cvs_number		number;
    char		*name;
    int			shown;
    time_t		date;
} rev_ref;

typedef struct _rev_list {
    struct _rev_list	*next;
    rev_ref	*heads;
    rev_ref	*tags;
    int		watch;
} rev_list;

extern cvs_file     *this_file;

int yyparse (void);

char *
ctime_nonl (time_t *date);

cvs_number
lex_number (char *);

time_t
lex_date (cvs_number *n);

rev_list *
rev_list_cvs (cvs_file *cvs);

rev_list *
rev_list_merge (rev_list *lists);

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

int
cvs_is_branch_of (cvs_number *trunk, cvs_number *branch);

int
cvs_number_degree (cvs_number *a);

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
dump_ref_name (FILE *f, rev_ref *ref);

void
dump_number_file (FILE *f, char *name, cvs_number *number);

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
dump_log (FILE *f, char *log);

void
dump_file (cvs_file *file);

void
dump_commit (rev_commit *e);

void
dump_refs (rev_ref *refs, char *title);

void
dump_rev_commit (rev_commit *e);

void
dump_rev_head (rev_ref *h);

void
dump_rev_list (rev_list *rl);

void
dump_splits (rev_list *rl);

void
dump_rev_graph_begin (void);

void
dump_rev_graph_nodes (rev_list *rl, char *title);

void
dump_rev_graph_end (void);

void
dump_commit_graph (rev_commit *c);

void
dump_rev_graph (rev_list *rl, char *title);

void
dump_rev_tree (rev_list *rl);

extern int yylex (void);

char *
atom (char *string);

void
discard_atoms (void);

rev_ref *
rev_ref_add (rev_ref **list, rev_commit *commit, char *name, int degree, int head);

rev_ref *
rev_list_add_head (rev_list *rl, rev_commit *commit, char *name, int degree);

rev_ref *
rev_list_add_tag (rev_list *rl, rev_commit *commit, char *name, int degree);

rev_file *
rev_file_rev (char *name, cvs_number *n, time_t date);

void
rev_file_free (rev_file *f);

void
rev_head_free (rev_ref *heads, int free_files);

void
rev_list_set_tail (rev_list *rl);

int
rev_file_later (rev_file *a, rev_file *b);

int
rev_commit_later (rev_commit *a, rev_commit *b);

void
rev_list_validate (rev_list *rl);

#define time_compare(a,b) ((long) (a) - (long) (b))

#endif /* _CVS_H_ */
