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
#include <getopt.h>

#ifndef MAXPATHLEN
#define MAXPATHLEN  10240
#endif

cvs_file	*this_file;

rev_execution_mode rev_mode = ExecuteExport;

int elide = 0;
int difffiles = 0;
int allfiles = 1;
int verbose = 0;

void
dump_number_file (FILE *f, char *name, cvs_number *number)
/* dump a filename/CVS-version pair to a specified file pointer */
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
/* dump a filename/CVS-version pair to standard output */
{
    dump_number_file (stdout, name, number);
}

void
dump_symbols (char *name, cvs_symbol *symbols)
/* dump a list of symbols and their CVS-version values to standard output */
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
/* dump a list of branches and their CVS-version roots to standard output */
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
/* dump metadata of a list of versions to standard output */
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
/* dump metadata of a list of patches to standard output */
{
    printf ("%s\n", name);
    while (patches) {
	dump_number ("\tnumber: ", &patches->number); printf ("\n");
	printf ("\t\tlog: %d bytes\n", (int)strlen (patches->log));
	printf ("\t\ttext: %d bytes\n", (int)strlen (patches->text));
	patches = patches->next;
    }
}

void
dump_file (cvs_file *file)
/* dump the patch list of a given file to standard output */
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
    int		i, j;

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
	if (!allfiles) {
	    dump_number (c->file->name, &c->file->number);
	    printf ("\\n");
	} else {
	    for (i = 0; i < c->ndirs; i++) {
		rev_dir *dir = c->dirs[i];
		for (j = 0; j < dir->nfiles; j++) {
		     f = dir->files[j];
		     dump_number (f->name, &f->number);
		     printf ("\\n");
		}
	    }
	}
    }
    printf ("%p", c);
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

static void dump_tag_name(FILE *f, Tag *tag)
{
    if (tag->parent) {
	dump_ref_name (f, tag->parent);
	fprintf (f, " > ");
    }
    fprintf (f, "%s", tag->name);
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
	    printf (" [weight=%d];\n", !r->tail ? 100 : 3);
	}
    }
    for (r = refs; r; r = r->next)
	r->shown = 0;
}

static void dump_tags(rev_list *rl, char *title, char *shape)
{
    Tag	*r;
    int n;
    int i, count;
    struct {
	int alias;
	Tag *t;
    } *v;

    for (r = all_tags, count = 0; r; r = r->next, count++)
	;

    v = calloc(count, sizeof(*v));

    for (r = all_tags, i = 0; r; r = r->next)
	v[i++].t = r;

    for (i = 0; i < count; i++) {
	if (v[i].alias)
	    continue;
	r = v[i].t;
	printf ("\t\"");
	if (title)
	    printf ("%s\\n", title);
	dump_tag_name(stdout, r);
	for (n = i + 1; n < count; n++) {
	    if (v[n].t->commit == r->commit) {
		v[n].alias = 1;
		printf ("\\n");
		dump_tag_name(stdout, v[n].t);
	    }
	}
	printf ("\" [fontsize=6,fixedsize=false,shape=%s];\n", shape);
    }
    for (i = 0; i < count; i++) {
	if (v[i].alias)
	    continue;
	r = v[i].t;
	printf ("\t\"");
	if (title)
	    printf ("%s\\n", title);
	dump_tag_name(stdout, r);
	for (n = i + 1; n < count; n++) {
	    if (v[n].alias && v[n].t->commit == r->commit) {
		printf ("\\n");
		dump_tag_name(stdout, v[n].t);
	    }
	}
	printf ("\" -> ");
	if (r->commit)
	    dump_commit_graph (r->commit, dump_find_branch (rl, r->commit));
	else
	    printf ("LOST");
	printf (" [weight=3];\n");
    }
    free(v);
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
    dump_tags (rl, title, "diamond");
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
    int		i, j;

    for (i = 0; i < c->ndirs; i++) {
	rev_dir	*dir = c->dirs[i];
	
	for (j = 0; j < dir->nfiles; j++) {
	    f = dir->files[j];
	    dump_number (f->name, &f->number);
	    printf (" ");
	}
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
extern int yylineno;

static rev_list *
rev_list_file (char *name, int *nversions)
{
    rev_list	*rl;
    struct stat	buf;

    yyin = fopen (name, "r");
    if (!yyin) {
	perror (name);
	++err;
    }
    yyfilename = name;
    yylineno = 0;
    this_file = calloc (1, sizeof (cvs_file));
    this_file->name = name;
    if (yyin)
	assert (fstat (fileno (yyin), &buf) == 0);
    this_file->mode = buf.st_mode;
    yyparse ();
    fclose (yyin);
    yyfilename = 0;
    rl = rev_list_cvs (this_file);
	    
    *nversions = this_file->nversions;
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
#if 0
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
#endif
}

void
dump_rev_tree (rev_list *rl)
{
    rev_ref	*h;
    rev_ref	*oh;
    rev_commit	*c, *p;
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
	    printf ("\t\t%p ", c);
	    dump_log (stdout, c->log);
	    if (tail) {
		printf ("\n\t\t...\n");
		break;
	    }
	    printf (" {\n");
	    
	    p = c->parent;
#if 0
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
#endif
	    printf ("\t\t}\n");
	    tail = c->tail;
	}
	printf ("\t}\n");
    }
    printf ("}\n");
}

time_t	time_now;

static int
strcommon (char *a, char *b)
/* return the length of the common prefix of strings a and b */
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

int load_current_file, load_total_files;
int commit_time_window = 60;
bool force_dates = false;

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

    while (1) {
	static struct option options[] = {
	    { "help",		    0, 0, 'h' },
	    { "version",	    0, 0, 'V' },
	    { "verbose",	    0, 0, 'v' },
	    { "commit-time-window", 1, 0, 'w' },
	    { "author-map",         1, 0, 'A' },
            { "graph",              0, 0, 'g' },
	};
	int c = getopt_long(argc, argv, "+hVw:gvA:T", options, NULL);
	if (c < 0)
	    break;
	switch (c) {
	case 'h':
	    printf("Usage: parsecvs [OPTIONS] [FILE]...\n"
		   "Parse RCS files and populate git repository.\n\n"
                   "Mandatory arguments to long options are mandatory for short options too.\n"
                   " -h --help                       This help\n"
		   " -g --graph                      Dump the commit graph\n"
                   " -v --version                    Print version\n"
                   " -w --commit-time-window=WINDOW  Time window for commits\n"
		   " -A --authormap                  Author map file\n"
		   " -T                              Force deteministic dates\n"
		   "\n"
		   "Example: find -name '*,v' | parsecvs\n");
	    return 0;
	case 'g':
	    rev_mode = ExecuteGraph;
	    break;
	case 'v':
	    verbose++;
#ifdef YYDEBUG
	    extern int yydebug;
	    yydebug = 1;
#endif /* YYDEBUG */
	    break;
	case 'V':
	    printf("%s: version " VERSION "\n", argv[0]);
	    return 0;
	case 'w':
	    commit_time_window = atoi (optarg);
	    break;
	case 'A':
	    load_author_map (optarg);
	    break;
	case 'T':
	    force_dates = true;
	    break;
	default: /* error message already emitted */
	    fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
	    return 1;
	}
    }

    argv[optind-1] = argv[0];
    argv += optind-1;
    argc -= optind-1;

    /* force times using mktime to be interpreted in UTC */
    setenv ("TZ", "UTC", 1);
    time_now = time (NULL);
    for (;;)
    {
	struct stat stb;

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

	if (stat(file, &stb) != 0)
	    continue;
	else if (S_ISDIR(stb.st_mode) != 0)
	    continue;

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
    if (rev_mode == ExecuteExport)
	export_init();
    load_total_files = nfile;
    load_current_file = 0;
    while (fn_head) {
	int nversions;
	
	fn = fn_head;
	fn_head = fn_head->next;
	++load_current_file;
	if (verbose)
	    fprintf(stderr, "parsecvs: processing %s\n", fn->file);
	load_status (fn->file + strip);
	rl = rev_list_file (fn->file, &nversions);
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
	case ExecuteExport:
	    export_commits (rl, strip);
	    break;
	}
    }
    if (rl)
	rev_list_free (rl, 0);
    while (head) {
	rl = head;
	head = head->next;
	rev_list_free (rl, 1);
    }
    discard_atoms ();
    discard_tags ();
    rev_free_dirs ();
    rev_commit_cleanup ();
    free_author_map ();
    return err;
}
