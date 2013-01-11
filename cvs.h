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
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#ifndef MAXPATHLEN
#define MAXPATHLEN  10240
#endif

#define CVS_MAX_DEPTH	20
#define CVS_MAX_REV_LEN	(CVS_MAX_DEPTH * 11)

typedef struct _cvs_number {
    int			c;
    short		n[CVS_MAX_DEPTH];
} cvs_number;

struct _cvs_version;
struct _cvs_patch;
struct _rev_file;

typedef struct node {
	struct node *hash_next;
	cvs_number number;
	struct _cvs_version *v;
	struct _cvs_patch *p;
	struct node *next;
	struct node *to;
	struct node *down;
	struct node *sib;
	struct _rev_file *file;
	int starts;
} Node;

typedef struct _cvs_symbol {
    struct _cvs_symbol	*next;
    char		*name;
    cvs_number		number;
} cvs_symbol;

typedef struct _cvs_branch {
    struct _cvs_branch	*next;
    cvs_number		number;
    Node		*node;
} cvs_branch;

typedef struct _cvs_version {
    struct _cvs_version	*next;
    cvs_number		number;
    time_t		date;
    char		*author;
    char		*state;
    int			dead;
    cvs_branch		*branches;
    cvs_number		parent;	/* next in ,v file */
    char		*commitid;
    Node		*node;
} cvs_version;

typedef struct _cvs_patch {
    struct _cvs_patch	*next;
    cvs_number		number;
    char		*log;
    char		*text;
    Node		*node;
} cvs_patch;


typedef struct {
    char		*name;
    cvs_number		head;
    cvs_number		branch;
    cvs_symbol		*symbols;
    cvs_version		*versions;
    cvs_patch		*patches;
    mode_t		mode;
    int			nversions;
    char 		*expand;
} cvs_file;

typedef struct _rev_file {
    char		*name;
    cvs_number		number;
    time_t		date;
    char		*sha1;
    int                 mark;
    mode_t		mode;
    struct _rev_file	*link;
} rev_file;

typedef struct _rev_dir {
    int			nfiles;
    rev_file		*files[0];
} rev_dir;

extern time_t	time_now;

extern int commit_time_window;

extern int load_current_file, load_total_files;

extern int verbose;

typedef struct _rev_commit {
    struct _rev_commit	*parent;
    char		tail;
    char		seen;
    char		used;
    char		tailed;
    char		tagged;
    time_t		date;
    char		*log;
    char		*author;
    char		*commitid;
    char		*sha1;
    int                 mark;
    struct _rev_commit	*user;
    rev_file		*file;		/* first file */
    int			nfiles;
    int			ndirs;
    rev_dir		*dirs[0];
} rev_commit;

typedef struct _rev_ref {
    struct _rev_ref	*next;
    rev_commit		*commit;
    struct _rev_ref	*parent;	/* link into tree */
    int			tail;
    int			degree;	/* number of digits in original CVS version */
    int			depth;	/* depth in branching tree (1 is trunk) */
    cvs_number		number;
    char		*name;
    int			shown;
} rev_ref;

typedef struct _rev_list {
    struct _rev_list	*next;
    rev_ref	*heads;
    int		watch;
} rev_list;

typedef struct _rev_file_list {
    struct _rev_file_list   *next;
    rev_file		    *file;
} rev_file_list;

typedef struct _rev_diff {
    rev_file_list	*del;
    rev_file_list	*add;
    int			ndel;
    int			nadd;
} rev_diff;

typedef enum _rev_execution_mode {
    ExecuteExport, ExecuteGraph, ExecuteSplits
} rev_execution_mode;

typedef struct _cvs_author {
    struct _cvs_author	*next;
    char		*name;
    char		*full;
    char		*email;
    char		*timezone;
} cvs_author;

cvs_author * fullname (char *);

int load_author_map (char *);

extern rev_execution_mode	rev_mode;

extern cvs_file     *this_file;

int yyparse (void);

extern char *yyfilename;

char *
ctime_nonl (time_t *date);

cvs_number
lex_number (char *);

time_t
lex_date (cvs_number *n);

char *
lex_text (void);

rev_list *
rev_list_cvs (cvs_file *cvs);

rev_list *
rev_list_merge (rev_list *lists);

void
rev_list_free (rev_list *rl, int free_files);

enum { Ncommits = 256 };

typedef struct _chunk {
	struct _chunk *next;
	rev_commit *v[Ncommits];
} Chunk;

typedef struct _tag {
	struct _tag *next;
	struct _tag *hash_next;
	char *name;
	Chunk *commits;
	int count;
	int left;
	rev_commit *commit;
	rev_ref *parent;
	char *last;
} Tag;

extern Tag *all_tags;
void tag_commit(rev_commit *c, char *name);
rev_commit **tagged(Tag *tag);
void discard_tags(void);

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

Node *
cvs_find_version (cvs_file *cvs, cvs_number *number);

int
cvs_is_trunk (cvs_number *number);

int
cvs_is_vendor (cvs_number *number);

void
cvs_file_free (cvs_file *cvs);

char *
cvs_number_string (cvs_number *n, char *str);

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
dump_refs (rev_list *rl, rev_ref *refs, char *title, char *shape);

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
dump_commit_graph (rev_commit *c, rev_ref *branch);

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
rev_list_add_head (rev_list *rl, rev_commit *commit, char *name, int degree);

int
rev_commit_has_file (rev_commit *c, rev_file *f);

rev_diff *
rev_commit_diff (rev_commit *old, rev_commit *new);

int
rev_file_list_has_filename (rev_file_list *fl, char *name);

void
rev_diff_free (rev_diff *d);

rev_ref *
rev_branch_of_commit (rev_list *rl, rev_commit *commit);

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

void 
export_blob(Node *node, void *buf, unsigned long len);

void
export_init(void);

int
export_commits (rev_list *rl, int strip);

void
free_author_map (void);

/*
 * rev - string representation of the rcs revision number eg. 1.1
 * path - RCS filename path eg. ./cfb16/Makefile,v
 * sha1_hex - a buffer of at least 41 characters to receive
 *           the ascii hexidecimal id of the resulting object
 */
void generate_files(cvs_file *cvs, void (*hook)(Node *node, void *buf, unsigned long len));

rev_dir **
rev_pack_files (rev_file **files, int nfiles, int *ndr);

void
rev_free_dirs (void);
    
void
rev_commit_cleanup (void);

void 
load_status (char *name);

void 
load_status_next (void);

void hash_version(cvs_version *);
void hash_patch(cvs_patch *);
void hash_branch(cvs_branch *);
void clean_hash(void);
void build_branches(void);
extern Node *head_node;

void reset_commits(rev_commit **, int);
void discard_tree(void);

extern void *xrealloc(void *ptr, size_t size);
extern void *xcalloc(size_t nmemb, size_t size);
extern void *xmalloc(size_t size);

#endif /* _CVS_H_ */
