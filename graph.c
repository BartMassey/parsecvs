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

/* FIXME: never set anywhere - should see what happens if it is */
static bool difffiles = false;

#ifdef __UNUSED__
static void dump_symbols (char *name, cvs_symbol *symbols)
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

static void dump_branches (char *name, cvs_branch *branches)
/* dump a list of branches and their CVS-version roots to standard output */
{
    printf ("%s", name);
    while (branches) {
	dump_number (" ", &branches->number);
	branches = branches->next;
    }
    printf ("\n");
}

static void dump_versions (char *name, cvs_version *versions)
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

static void dump_patches (char *name, cvs_patch *patches)
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

static void dump_file (cvs_file *file)
/* dump the patch list of a given file to standard output */
{
    dump_number ("head", &file->head);  printf ("\n");
    dump_number ("branch", &file->branch); printf ("\n");
    dump_symbols ("symbols", file->symbols);
    dump_versions ("versions", file->versions);
    dump_patches ("patches", file->patches);
}
#endif /* __UNUSED__ */

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

static void
dot_ref_name (FILE *f, rev_ref *ref)
{
    if (ref->parent) {
	dot_ref_name (f, ref->parent);
	fprintf (f, " > ");
    }
    fprintf (f, "%s", ref->name);
}

static void dot_commit_graph (rev_commit *c, rev_ref *branch)
{
    rev_file	*f;

    printf ("\"");
    if (branch)
	dot_ref_name (stdout, branch);
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
	int		i, j;
	for (i = 0; i < c->ndirs; i++) {
	    rev_dir *dir = c->dirs[i];
	    for (j = 0; j < dir->nfiles; j++) {
		 f = dir->files[j];
		 dump_number (f->name, &f->number);
		 printf ("\\n");
	    }
	}
    }
    printf ("%p", c);
    printf ("\"");
}

static void dot_tag_name(FILE *f, Tag *tag)
{
    if (tag->parent) {
	dot_ref_name (f, tag->parent);
	fprintf (f, " > ");
    }
    fprintf (f, "%s", tag->name);
}

static rev_ref *dump_find_branch (rev_list *rl, rev_commit *commit)
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

static void dot_refs (rev_list *rl, rev_ref *refs, char *title, char *shape)
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
		    o->shown = true;
		    if (n)
			printf ("\\n");
		    dot_ref_name (stdout, o);
		    printf (" (%d)", o->degree);
		    n++;
		}
	    printf ("\" [fontsize=6,fixedsize=false,shape=%s];\n", shape);
	}
    }
    for (r = refs; r; r = r->next)
	r->shown = false;
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
		    o->shown = true;
		    if (n)
			printf ("\\n");
		    dot_ref_name (stdout, o);
		    printf (" (%d)", o->degree);
		    n++;
		}
	    printf ("\"");
	    printf (" -> ");
	    if (r->commit)
		dot_commit_graph (r->commit, dump_find_branch (rl,
								r->commit));
	    else
		printf ("LOST");
	    printf (" [weight=%d];\n", !r->tail ? 100 : 3);
	}
    }
    for (r = refs; r; r = r->next)
	r->shown = false;
}

static void dot_tags(rev_list *rl, char *title, char *shape)
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
	dot_tag_name(stdout, r);
	for (n = i + 1; n < count; n++) {
	    if (v[n].t->commit == r->commit) {
		v[n].alias = 1;
		printf ("\\n");
		dot_tag_name(stdout, v[n].t);
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
	dot_tag_name(stdout, r);
	for (n = i + 1; n < count; n++) {
	    if (v[n].alias && v[n].t->commit == r->commit) {
		printf ("\\n");
		dot_tag_name(stdout, v[n].t);
	    }
	}
	printf ("\" -> ");
	if (r->commit)
	    dot_commit_graph (r->commit, dump_find_branch (rl, r->commit));
	else
	    printf ("LOST");
	printf (" [weight=3];\n");
    }
    free(v);
}

#ifdef __UNUSED__
/*
 * Fossil code, apparently from an experiment in eliding intermediate
 * graph nodes.
 */
bool elide = false;

static rev_commit *
dump_get_rev_parent (rev_commit *c)
{
    int	seen = c->seen;

    c = c->parent;
    while (c && c->seen == seen && !c->tail && !c->tagged)
	c = c->parent;
    return c;
}
#endif

#define dump_get_rev_parent(c) ((c)->parent)

static void dot_rev_graph_nodes (rev_list *rl, char *title)
{
    rev_ref	*h;
    rev_commit	*c, *p;
    int		tail;

    printf ("nodesep=0.1;\n");
    printf ("ranksep=0.1;\n");
    printf ("edge [dir=none];\n");
    printf ("node [shape=box,fontsize=6];\n");
    dot_refs (rl, rl->heads, title, "ellipse");
    dot_tags (rl, title, "diamond");
    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	for (c = h->commit; c; c = p) {
	    p = dump_get_rev_parent (c);
	    tail = c->tail;
	    if (!p)
		break;
	    printf ("\t");
	    dot_commit_graph (c, h);
	    printf (" -> ");
	    dot_commit_graph (p, tail ? h->parent : h);
	    if (!tail)
		printf (" [weight=10];");
	    printf ("\n");
	    if (tail)
		break;
	}
    }
}

static void dot_rev_graph_begin (void)
{
    printf ("digraph G {\n");
}

static void dot_rev_graph_end (void)
{
    printf ("}\n");
}

void
dump_rev_graph (rev_list *rl, char *title)
{
    dot_rev_graph_begin ();
    dot_rev_graph_nodes (rl, title);
    dot_rev_graph_end ();
}

/* end */
