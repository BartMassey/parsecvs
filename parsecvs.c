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

cvs_file	*this_file;

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

static void
dump_log (char *log)
{
    int		j;
    for (j = 0; j < 48; j++) {
	if (log[j] == '\0')
	    break;
	if (log[j] == '\n') {
	    if (j > 5) break;
	    continue;
	}
	if (log[j] == '.')
	    break;
	if (log[j] & 0x80)
	    continue;
	if (log[j] < ' ')
	    continue;
	if (log[j] == '"')
	    putchar ('\\');
	putchar (log[j]);
    }
}

void
dump_ent (rev_ent *e)
{
    rev_file	*f;
    int		i;
    char	*date;

    printf ("\"");
#if 1
    date = ctime (&e->date);
    date[strlen(date)-1] = '\0';
    printf ("%s\\n", date);
    dump_log (e->log);
    printf ("\\n");
    for (i = 0; i < e->nfiles; i++) {
	f = e->files[i];
	dump_number (f->name, &f->number);
	printf ("\\n");
	break;
    }
#endif
    printf ("%08x", (int) e);
    printf ("\"");
}

void
dump_refs (rev_ref *refs)
{
    rev_ref	*r, *o;
    int		n;

    for (r = refs; r; r = r->next) {
	if (!r->shown) {
	    printf ("\t");
	    printf ("\"");
	    n = 0;
	    for (o = r; o; o = o->next)
		if (!o->shown && o->ent == r->ent)
		{
		    o->shown = 1;
		    if (n)
			printf ("\\n");
		    if (o->head)
			printf ("*");
		    printf ("%s", o->name);
		    n++;
		}
	    printf ("\" [fontsize=6,fixedsize=false,shape=ellipse];\n");
	}
    }
    for (r = refs; r; r = r->next)
	r->shown = 0;
    for (r = refs; r; r = r->next) {
	if (!r->shown) {
	    printf ("\t");
	    printf ("\"");
	    n = 0;
	    for (o = r; o; o = o->next)
		if (!o->shown && o->ent == r->ent)
		{
		    o->shown = 1;
		    if (n)
			printf ("\\n");
		    if (o->head)
			printf ("*");
		    printf ("%s", o->name);
		    n++;
		}
	    printf ("\"");
	    printf (" -> ");
	    dump_ent (r->ent);
	    printf ("\n");
	}
    }
    for (r = refs; r; r = r->next)
	r->shown = 0;
}

void
dump_rev_graph (rev_list *rl)
{
    rev_branch	*b;
    rev_ent	*e;

    printf ("digraph G {\n");
    printf ("nodesep=0.1;\n");
    printf ("ranksep=0.1;\n");
    printf ("edge [dir=none];\n");
    printf ("node [shape=box,fontsize=6];\n");
    dump_refs (rl->heads);
    dump_refs (rl->tags);
    for (b = rl->branches; b; b = b->next) {
	for (e = b->ent; e && e->parent; e = e->parent) {
	    printf ("\t");
	    dump_ent (e);
	    printf (" -> ");
	    dump_ent (e->parent);
	    printf ("\n");
	    if (e->tail)
		break;
	}
    }
    printf ("}\n");
}

void
dump_rev_info (rev_list *rl)
{
    rev_branch	*b;
    rev_ent	*e;
    rev_file	*f;
    int		i;

    for (b = rl->branches; b; b = b->next) {
	for (e = b->ent; e; e = e->parent) {
	    for (i = 0; i < e->nfiles; i++) {
		f = e->files[i];
		dump_number (f->name, &f->number);
		printf (" ");
	    }
	    printf ("\n");
	    if (e->tail)
		break;
	}
    }
}

extern FILE *yyin;
static int err = 0;

static rev_list *
rev_list_file (char *name)
{
    rev_list	*rl;

    yyin = fopen (name, "r");
    if (!yyin) {
	perror (name);
	++err;
    }
    this_file = calloc (1, sizeof (cvs_file));
    this_file->name = name;
    yyparse ();
    fclose (yyin);
    rl = rev_list_cvs (this_file);
    cvs_file_free (this_file);
    return rl;
}

typedef struct _rev_split {
    struct _rev_split	*next;
    rev_ent		*childa, *childb;
    rev_ent		*parent;
} rev_split;

static char *
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
    rev_branch	*branch;
    rev_ent	*e, *a, *b;
    int		ai, bi;
    rev_file	*af, *bf;
    char	*which;

    /* Find tails and mark splits */
    for (branch = rl->branches; branch; branch = branch->next) {
	for (e = branch->ent; e; e = e->parent)
	    if (e->tail) {
		for (s = splits; s; s = s->next)
		    if (s->parent == e->parent)
			break;
		if (!s) {
		    s = calloc (1, sizeof (rev_split));
		    s->parent = e->parent;
		    s->childa = e;
		    s->next = splits;
		    splits = s;
		}
	    }
    }
    /* Find join points */
    for (s = splits; s; s = s->next) {
	for (branch = rl->branches; branch; branch = branch->next) {
	    for (e = branch->ent; e; e = e->parent) {
		if (e->parent == s->parent && e != s->childa)
		    s->childb = e;
	    }
	}
    }
    for (s = splits; s; s = s->next) {
	if (s->parent && s->childa && s->childb) {
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
		} else {
		    fprintf (stderr, "ab: %s ", ctime_nonl (&af->date));
		    dump_number_file (stderr, af->name, &af->number);
		    ai++;
		    bi++;
		}
		fprintf (stderr, "\n");
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
    rev_branch	*b;
    rev_ref	*h;
    rev_ent	*e, *p;
    int		i;

    printf ("rev_list {\n");
    for (b = rl->branches; b; b = b->next) {
	for (h = rl->heads; h; h = h->next) {
	    if (h->ent == b->ent)
		printf ("%s:\n", h->name);
	}
	printf ("\t{\n");
	for (e = b->ent; e; e = e->parent) {
	    printf ("\t\t0x%x ", (int) e);
	    dump_log (e->log);
	    printf (" {\n");
	    if (e->parent && e->nfiles > 16) {
		rev_file	*ef, *pf;
		int		ei, pi;
		p = e->parent;
		ei = pi = 0;
		while (ei < e->nfiles && pi < p->nfiles) {
		    ef = e->files[ei];
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
		while (ei < e->nfiles) {
		    ef = e->files[ei];
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
		for (i = 0; i < e->nfiles; i++) {
		    printf ("\t\t\t");
		    dump_number (e->files[i]->name, &e->files[i]->number);
		    printf ("\n");
		}
	    }
	    printf ("\t\t}\n");
	    if (e->tail) {
		printf ("\t\t...\n");
		break;
	    }
#if 0	 
	    if (time_compare (e->date, 1079499163) <= 0) {
		printf ("\t\t...\n");
		break;
	    }
#endif     
	}
	printf ("\t}\n");
    }
    printf ("}\n");
}

int
main (int argc, char **argv)
{
    rev_list	*stack[32], *rl, *old;
    int		i;
    int		j = 1;
    char	name[10240];
    char	*file;

    /* force times using mktime to be interpreted in UTC */
    setenv ("TZ", "UTC", 1);
    memset (stack, '\0', sizeof (stack));
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
	file = atom (file);
	rl = rev_list_file (file);
	if (rl->watch)
	    dump_rev_tree (rl);
	for (i = 0; i < 32; i++) {
//	    fprintf (stderr, "*");
	    if (stack[i]) {
		old = rl;
		rl = rev_list_merge (old, stack[i]);
		if (rl->watch)
		    dump_rev_tree (rl);
		rev_list_free (old, 0);
		rev_list_free (stack[i], 0);
		stack[i] = 0;
	    } else {
		stack[i] = rl;
		break;
	    }
	}
//	fprintf (stderr, "%s\n", file);
    }
    rl = NULL;
    for (i = 0; i < 32; i++) {
//	fprintf (stderr, "+");
	if (stack[i]) {
	    if (rl) {
		old = rl;
		rl = rev_list_merge (rl, stack[i]);
		if (rl->watch)
		    dump_rev_tree (rl);
		rev_list_free (old, 0);
		rev_list_free (stack[i], 0);
	    }
	    else
		rl = stack[i];
	    stack[i] = 0;
	}
    }
//    fprintf (stderr, "\n");
    if (rl) {
	dump_rev_graph (rl);
//	dump_rev_info (rl);
	if (rl->watch)
	    dump_rev_tree (rl);
//	dump_splits (rl);
    }
    rev_list_free (rl, 1);
    discard_atoms ();
    return err;
}
