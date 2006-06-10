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
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#ifndef MAXPATHLEN
#define MAXPATHLEN  10240
#endif

cvs_file	*this_file;

rev_execution_mode rev_mode = ExecuteGit;

int elide = 0;
int difffiles = 1;
int allfiles = 1;
    
void
dump_number_file (FILE *f, char *name, cvs_number *number)
{
    int i;
    fprintf (f, "%s ", name);
    if (number) {
	for (i = 0; i < number->c; i++) {
	    fprintf (f, "%d", number->n[i]);
	    if (i < number->c - 1) fprintf (f, ".");
	}
    }
}

void
dump_number (char *name, cvs_number *number)
{
    dump_number_file (stdout, name, number);
}

void
dump_symbols (char *name, cvs_symbol *symbols)
{
    printf ("%s\n", name);
    while (symbols) {
	printf ("\t");
	dump_number (symbols->name, &symbols->number);
	printf ("\n");
	symbols = symbols->next;
    }
}

void
dump_branches (char *name, cvs_branch *branches)
{
    printf ("%s", name);
    while (branches) {
	dump_number (" ", &branches->number);
	branches = branches->next;
    }
    printf ("\n");
}

void
dump_versions (char *name, cvs_version *versions)
{
    printf ("%s\n", name);
    while (versions) {
	dump_number  ("\tnumber:", &versions->number); printf ("\n");
	printf       ("\t\tdate:     %s", ctime (&versions->date));
	printf       ("\t\tauthor:   %s\n", versions->author);
	dump_branches("\t\tbranches:", versions->branches);
	dump_number  ("\t\tparent:  ", &versions->parent); printf ("\n");
	if (versions->commitid)
	    printf   ("\t\tcommitid: %s\n", versions->commitid);
	printf ("\n");
	versions = versions->next;
    }
}

void
dump_patches (char *name, cvs_patch *patches)
{
    printf ("%s\n", name);
    while (patches) {
	dump_number ("\tnumber: ", &patches->number); printf ("\n");
	printf ("\t\tlog: %d bytes\n", strlen (patches->log));
	printf ("\t\ttext: %d bytes\n", strlen (patches->text));
	patches = patches->next;
    }
}

void
dump_file (cvs_file *file)
{
    dump_number ("head", &file->head);  printf ("\n");
    dump_number ("branch", &file->branch); printf ("\n");
    dump_symbols ("symbols", file->symbols);
    dump_versions ("versions", file->versions);
    dump_patches ("patches", file->patches);
}

void
dump_log (FILE *f, char *log)
{
    int		j;
    for (j = 0; j < 48; j++) {
	if (log[j] == '\0')
	    break;
	if (log[j] == '\n') {
	    if (j > 5) break;
	    continue;
	}
	if (log[j] & 0x80)
	    continue;
	if (log[j] < ' ')
	    continue;
	if (log[j] == '(' || log[j] == ')' ||
	    log[j] == '[' || log[j] == ']' ||
	    log[j] == '{' || log[j] == '}')
	    continue;
	if (log[j] == '"')
	    putc ('\\', f);
	putc (log[j], f);
	if (log[j] == '.' && isspace (log[j+1]))
	    break;
    }
}

void
dump_commit_graph (rev_commit *c, rev_ref *branch)
{
    rev_file	*f;
    int		i;

    printf ("\"");
    if (branch)
	dump_ref_name (stdout, branch);
//    if (c->tail)
//	printf ("*** TAIL");
    printf ("\\n");
    printf ("%s\\n", ctime_nonl (&c->date));
    dump_log (stdout, c->log);
    printf ("\\n");
    if (difffiles) {
	rev_diff    *diff = rev_commit_diff (c->parent, c);
	rev_file_list   *fl;

	for (fl = diff->add; fl; fl = fl->next) {
	    if (!rev_file_list_has_filename (diff->del, fl->file->name)) {
		printf ("+");
		dump_number (fl->file->name, &fl->file->number);
		printf ("\\n");
	    }
	}
	for (fl = diff->add; fl; fl = fl->next) {
	    if (rev_file_list_has_filename (diff->del, fl->file->name)) {
		printf ("|");
		dump_number (fl->file->name, &fl->file->number);
		printf ("\\n");
	    }
	}
	for (fl = diff->del; fl; fl = fl->next) {
	    if (!rev_file_list_has_filename (diff->add, fl->file->name)) {
		printf ("-");
		dump_number (fl->file->name, &fl->file->number);
		printf ("\\n");
	    }
	}
	rev_diff_free (diff);
    } else {
	for (i = 0; i < c->nfiles; i++) {
	    f = c->files[i];
	    dump_number (f->name, &f->number);
	    printf ("\\n");
	    if (!allfiles)
		break;
	}
    }
    printf ("%08x", (int) c);
    printf ("\"");
}

void
dump_ref_name (FILE *f, rev_ref *ref)
{
    if (ref->parent) {
	dump_ref_name (f, ref->parent);
	fprintf (f, " > ");
    }
    fprintf (f, "%s", ref->name);
}

static
rev_ref *
dump_find_branch (rev_list *rl, rev_commit *commit)
{
    rev_ref	*h;
    rev_commit	*c;

    for (h = rl->heads; h; h = h->next)
    {
	if (h->tail)
	    continue;
	for (c = h->commit; c; c = c->parent)
	{
	    if (c == commit)
		return h;
	    if (c->tail)
		break;
	}
    }
    return NULL;
}

void
dump_refs (rev_list *rl, rev_ref *refs, char *title, char *shape)
{
    rev_ref	*r, *o;
    int		n;

    for (r = refs; r; r = r->next) {
	if (!r->shown) {
	    printf ("\t");
	    printf ("\"");
	    if (title)
		printf ("%s\\n", title);
	    if (r->tail)
		printf ("TAIL\\n");
	    n = 0;
	    for (o = r; o; o = o->next)
		if (!o->shown && o->commit == r->commit)
		{
		    o->shown = 1;
		    if (n)
			printf ("\\n");
		    dump_ref_name (stdout, o);
		    printf (" (%d)", o->degree);
		    n++;
		}
	    printf ("\" [fontsize=6,fixedsize=false,shape=%s];\n", shape);
	}
    }
    for (r = refs; r; r = r->next)
	r->shown = 0;
    for (r = refs; r; r = r->next) {
	if (!r->shown) {
	    printf ("\t");
	    printf ("\"");
	    if (title)
		printf ("%s\\n", title);
	    if (r->tail)
		printf ("TAIL\\n");
	    n = 0;
	    for (o = r; o; o = o->next)
		if (!o->shown && o->commit == r->commit)
		{
		    o->shown = 1;
		    if (n)
			printf ("\\n");
		    dump_ref_name (stdout, o);
		    printf (" (%d)", o->degree);
		    n++;
		}
	    printf ("\"");
	    printf (" -> ");
	    if (r->commit)
		dump_commit_graph (r->commit, dump_find_branch (rl,
								r->commit));
	    else
		printf ("LOST");
	    printf (" [weight=%d];\n", r->head  && !r->tail ? 100 : 3);
	}
    }
    for (r = refs; r; r = r->next)
	r->shown = 0;
}

static rev_commit *
dump_get_rev_parent (rev_commit *c)
{
    int	seen = c->seen;

    c = c->parent;
    while (elide && c && c->seen == seen && !c->tail && !c->tagged)
	c = c->parent;
    return c;
}

void
dump_rev_graph_nodes (rev_list *rl, char *title)
{
    rev_ref	*h;
    rev_commit	*c, *p;
    int		tail;

    printf ("nodesep=0.1;\n");
    printf ("ranksep=0.1;\n");
    printf ("edge [dir=none];\n");
    printf ("node [shape=box,fontsize=6];\n");
    dump_refs (rl, rl->heads, title, "ellipse");
    dump_refs (rl, rl->tags, title, "diamond");
    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	for (c = h->commit; c; c = p) {
	    p = dump_get_rev_parent (c);
	    tail = c->tail;
	    if (!p)
		break;
	    printf ("\t");
	    dump_commit_graph (c, h);
	    printf (" -> ");
	    dump_commit_graph (p, tail ? h->parent : h);
	    if (!tail)
		printf (" [weight=10];");
	    printf ("\n");
	    if (tail)
		break;
	}
    }
}

void
dump_rev_graph_begin ()
{
    printf ("digraph G {\n");
}

void
dump_rev_graph_end ()
{
    printf ("}\n");
}

void
dump_rev_graph (rev_list *rl, char *title)
{
    dump_rev_graph_begin ();
    dump_rev_graph_nodes (rl, title);
    dump_rev_graph_end ();
}

void
dump_rev_commit (rev_commit *c)
{
    rev_file	*f;
    int		i;

    for (i = 0; i < c->nfiles; i++) {
	f = c->files[i];
	dump_number (f->name, &f->number);
	printf (" ");
    }
    printf ("\n");
}

void
dump_rev_head (rev_ref *h)
{
    rev_commit	*c;
    for (c = h->commit; c; c = c->parent) {
	dump_rev_commit (c);
	if (c->tail)
	    break;
    }
}

void
dump_rev_list (rev_list *rl)
{
    rev_ref	*h;

    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	dump_rev_head (h);
    }
}

extern FILE *yyin;
static int err = 0;
char *yyfilename;

static rev_list *
rev_list_file (char *name)
{
    rev_list	*rl;
    struct stat	buf;

    yyin = fopen (name, "r");
    if (!yyin) {
	perror (name);
	++err;
    }
    yyfilename = name;
    this_file = calloc (1, sizeof (cvs_file));
    this_file->name = name;
    assert (fstat (fileno (yyin), &buf) == 0);
    this_file->mode = buf.st_mode;
    yyparse ();
    fclose (yyin);
    yyfilename = 0;
    rl = rev_list_cvs (this_file);
	    
    cvs_file_free (this_file);
    return rl;
}

typedef struct _rev_split {
    struct _rev_split	*next;
    rev_commit		*childa, *childb;
    rev_commit		*parent;
    rev_commit		*topa, *topb;
} rev_split;

char *
ctime_nonl (time_t *date)
{
    char	*d = ctime (date);
    
    d[strlen(d)-1] = '\0';
    return d;
}

void
dump_splits (rev_list *rl)
{
    rev_split	*splits = NULL, *s;
    rev_ref	*head;
    rev_commit	*c, *a, *b;
    int		ai, bi;
    rev_file	*af, *bf;
    char	*which;

    /* Find tails and mark splits */
    for (head = rl->heads; head; head = head->next) {
	if (head->tail)
	    continue;
	for (c = head->commit; c; c = c->parent)
	    if (c->tail) {
		for (s = splits; s; s = s->next)
		    if (s->parent == c->parent)
			break;
		if (!s) {
		    s = calloc (1, sizeof (rev_split));
		    s->parent = c->parent;
		    s->childa = c;
		    s->topa = head->commit;
		    s->next = splits;
		    splits = s;
		}
	    }
    }
    /* Find join points */
    for (s = splits; s; s = s->next) {
	for (head = rl->heads; head; head = head->next) {
	    if (head->tail)
		continue;
	    for (c = head->commit; c; c = c->parent) {
		if (c->parent == s->parent && c != s->childa) {
		    s->childb = c;
		    s->topb = head->commit;
		}
	    }
	}
    }
    for (s = splits; s; s = s->next) {
	if (s->parent && s->childa && s->childb) {
	    for (head = rl->heads; head; head = head->next) {
		if (head->commit == s->topa)
		    fprintf (stderr, "%s ", head->name);
	    }
	    fprintf (stderr, "->");
	    for (head = rl->heads; head; head = head->next) {
		if (head->commit == s->topb)
		    fprintf (stderr, "%s ", head->name);
	    }
	    fprintf (stderr, "\n");
	    a = s->childa;
	    b = s->childb;
	    ai = bi = 0;
	    while (ai < a->nfiles && bi < b->nfiles) {
		af = a->files[ai];
		bf = b->files[bi];
		if (af != bf) {
		    if (rev_file_later (af, bf)) {
			fprintf (stderr, "a : %s ", ctime_nonl (&af->date));
			dump_number_file (stderr, af->name, &af->number);
			ai++;
		    } else {
			fprintf (stderr, " b: %s ", ctime_nonl (&bf->date));
			dump_number_file (stderr, bf->name, &bf->number);
			bi++;
		    }
		    fprintf (stderr, "\n");
		} else {
//		    fprintf (stderr, "ab: %s ", ctime_nonl (&af->date));
//		    dump_number_file (stderr, af->name, &af->number);
//		    fprintf (stderr, "\n");
		    ai++;
		    bi++;
		}
	    }
	    which = "a ";
	    if (ai >= a->nfiles) {
		a = b;
		ai = bi;
		which = " b";
	    }
	    while (ai < a->nfiles) {
		af = a->files[ai];
		fprintf (stderr, "%s: ", which);
		dump_number_file (stderr, af->name, &af->number);
		fprintf (stderr, "\n");
		ai++;
	    }
	}
    }
}

void
dump_rev_tree (rev_list *rl)
{
    rev_ref	*h;
    rev_ref	*oh;
    rev_commit	*c, *p;
    int		i;
    int		tail;

    printf ("rev_list {\n");

    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	for (oh = rl->heads; oh; oh = oh->next) {
	    if (h->commit == oh->commit)
		printf ("%s:\n", oh->name);
	}
	printf ("\t{\n");
	tail = h->tail;
	for (c = h->commit; c; c = p) {
	    printf ("\t\t0x%x ", (int) c);
	    dump_log (stdout, c->log);
	    if (tail) {
		printf ("\n\t\t...\n");
		break;
	    }
	    printf (" {\n");
	    
	    p = c->parent;
	    if (p && c->nfiles > 16) {
		rev_file	*ef, *pf;
		int		ei, pi;
		ei = pi = 0;
		while (ei < c->nfiles && pi < p->nfiles) {
		    ef = c->files[ei];
		    pf = p->files[pi];
		    if (ef != pf) {
			if (rev_file_later (ef, pf)) {
			    fprintf (stdout, "+ ");
			    dump_number_file (stdout, ef->name, &ef->number);
			    ei++;
			} else {
			    fprintf (stdout, "- ");
			    dump_number_file (stdout, pf->name, &pf->number);
			    pi++;
			}
			fprintf (stdout, "\n");
		    } else {
			ei++;
			pi++;
		    }
		}
		while (ei < c->nfiles) {
		    ef = c->files[ei];
		    fprintf (stdout, "+ ");
		    dump_number_file (stdout, ef->name, &ef->number);
		    ei++;
		    fprintf (stdout, "\n");
		}
		while (pi < p->nfiles) {
		    pf = p->files[pi];
		    fprintf (stdout, "- ");
		    dump_number_file (stdout, pf->name, &pf->number);
		    pi++;
		    fprintf (stdout, "\n");
		}
	    } else {
		for (i = 0; i < c->nfiles; i++) {
		    printf ("\t\t\t");
		    dump_number (c->files[i]->name, &c->files[i]->number);
		    printf ("\n");
		}
	    }
	    printf ("\t\t}\n");
	    tail = c->tail;
#if 0	 
	    if (time_compare (c->date, 1079499163) <= 0) {
		printf ("\t\t...\n");
		break;
	    }
#endif     
	}
	printf ("\t}\n");
    }
    printf ("}\n");
}

time_t	time_now;

static int
strcommon (char *a, char *b)
{
    int	c = 0;
    
    while (*a == *b) {
	if (!*a)
	    break;
	a++;
	b++;
	c++;
    }
    return c;
}

typedef struct _rev_filename {
    struct _rev_filename	*next;
    char		*file;
} rev_filename;

#define STATUS	stdout
#define PROGRESS_LEN	20
static int load_current_file, load_total_files;

static void load_status (char *name)
{
    int	spot = load_current_file * PROGRESS_LEN / load_total_files;
    int	    s;
    int	    l;

    if (rev_mode == ExecuteGraph)
	return;
    l = strlen (name);
    if (l > 41) name += l - 41;

    fprintf (STATUS, "Load: %35.35s ", name);
    for (s = 0; s < PROGRESS_LEN + 1; s++)
	putc (s == spot ? '*' : '.', STATUS);
    fprintf (STATUS, " %5d of %5d\r", load_current_file, load_total_files);
    fflush (STATUS);
}

static void load_status_next (void)
{
    if (rev_mode == ExecuteGraph)
	return;
    fprintf (STATUS, "\n");
    fflush (STATUS);
}
    
int
main (int argc, char **argv)
{
    rev_filename    *fn_head, **fn_tail = &fn_head, *fn;
    rev_list	    *head, **tail = &head;
    rev_list	    *rl;
    int		    j = 1;
    char	    name[10240], *last = NULL;
    int		    strip = -1;
    int		    c;
    char	    *file;
    int		    nfile = 0;

    /* force times using mktime to be interpreted in UTC */
    setenv ("TZ", "UTC", 1);
    time_now = time (NULL);
    for (;;)
    {
	if (argc < 2) {
	    int l;
	    if (fgets (name, sizeof (name) - 1, stdin) == NULL)
		break;
	    l = strlen (name);
	    if (name[l-1] == '\n')
		name[l-1] = '\0';
	    file = name;
	} else {
	    file = argv[j++];
	    if (!file)
		break;
	}
	fn = calloc (1, sizeof (rev_filename));
	fn->file = atom (file);
	*fn_tail = fn;
	fn_tail = &fn->next;
	if (strip > 0) {
	    c = strcommon (fn->file, last);
	    if (c < strip)
		strip = c;
	} else if (strip < 0) {
	    int i;

	    strip = 0;
	    for (i = 0; i < strlen (fn->file); i++)
		if (fn->file[i] == '/')
		    strip = i + 1;
	}
	last = fn->file;
	nfile++;
    }
    if (git_system ("git-init-db --shared") != 0)
	exit (1);
    load_total_files = nfile;
    load_current_file = 0;
    while (fn_head) {
	fn = fn_head;
	fn_head = fn_head->next;
	++load_current_file;
	load_status (fn->file + strip);
	rl = rev_list_file (fn->file);
	if (rl->watch)
	    dump_rev_tree (rl);
	*tail = rl;
	tail = &rl->next;
	free(fn);
    }
    load_status_next ();
    rl = rev_list_merge (head);
    if (rl) {
	switch (rev_mode) {
	case ExecuteGraph:
	    dump_rev_graph (rl, NULL);
	    break;
	case ExecuteSplits:
	    dump_splits (rl);
	    break;
	case ExecuteGit:
	    git_rev_list_commit (rl, strip);
	    break;
	}
    }
    rev_list_free (rl, 0);
    while (head) {
	rl = head;
	head = head->next;
	rev_list_free (rl, 1);
    }
    discard_atoms ();
    return err;
}
