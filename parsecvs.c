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
dump_number (char *name, cvs_number *number)
{
    int i;
    printf ("%s ", name);
    if (number) {
	for (i = 0; i < number->c; i++) {
	    printf ("%d", number->n[i]);
	    if (i < number->c - 1) printf (".");
	}
    }
}

void
dump_symbols (char *name, cvs_symbol *symbols)
{
    printf ("%s\n", name);
    while (symbols) {
	printf ("\t");
	dump_number (symbols->name, symbols->number);
	printf ("\n");
	symbols = symbols->next;
    }
}

void
dump_branches (char *name, cvs_branch *branches)
{
    printf ("%s", name);
    while (branches) {
	dump_number (" ", branches->number);
	branches = branches->next;
    }
    printf ("\n");
}

void
dump_versions (char *name, cvs_version *versions)
{
    printf ("%s\n", name);
    while (versions) {
	dump_number  ("\tnumber:", versions->number); printf ("\n");
	printf       ("\t\tdate:     %s", ctime (&versions->date));
	printf       ("\t\tauthor:   %s\n", versions->author);
	dump_branches("\t\tbranches:", versions->branches);
	dump_number  ("\t\tparent:  ", versions->parent); printf ("\n");
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
	dump_number ("\tnumber: ", patches->number); printf ("\n");
	printf ("\t\tlog: %d bytes\n", strlen (patches->log));
	printf ("\t\ttext: %d bytes\n", strlen (patches->text));
	patches = patches->next;
    }
}

void
dump_file (cvs_file *file)
{
    dump_number ("head", file->head);  printf ("\n");
    dump_number ("branch", file->branch); printf ("\n");
    dump_symbols ("symbols", file->symbols);
    dump_versions ("versions", file->versions);
    dump_patches ("patches", file->patches);
}

void
dump_ent (rev_ent *e)
{
    rev_file	*f;

    printf ("\"");
    for (f = e->files; f; f = f->next) {
	char	*date = ctime (&f->date);
	date[strlen(date)-1] = '\0';
	dump_number (f->name, f->number);
	printf ("\\n%s", date);
	if (f->next) printf ("\\n");
    }
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
	    printf ("\"");
	    printf (" -- ");
	    dump_ent (r->ent);
	    printf ("\n");
	}
    }
    for (r = refs; r; r = r->next)
	r->shown = 0;
}

void
dump_revlist (rev_list *rl)
{
    rev_branch	*b;
    rev_ent	*e;

    printf ("graph G {\n");
    dump_refs (rl->heads);
    dump_refs (rl->tags);
    for (b = rl->branches; b; b = b->next) {
	for (e = b->ent; e && e->parent; e = e->parent) {
	    printf ("\t");
	    dump_ent (e);
	    printf (" -- ");
	    dump_ent (e->parent);
	    printf ("\n");
	    if (e->vendor) {
		printf ("\t");
		dump_ent (e);
		printf (" -- ");
		dump_ent (e->vendor);
		printf ("\n");
	    }
	    if (e->tail)
		break;
	}
    }
    printf ("}\n");
}

extern FILE *yyin;
static int err = 0;

static rev_list *
add_file (rev_list *all, char *name)
{
    rev_list	*rl;

    yyin = fopen (name, "r");
    if (!yyin) {
	perror (name);
	++err;
    }
    fprintf (stderr, "%s\n", name);
    this_file = calloc (1, sizeof (cvs_file));
    this_file->name = name;
    yyparse ();
    fclose (yyin);
    rl = rev_list_cvs (this_file);
    if (all)
	all = rev_list_merge (all, rl);
    else
	all = rl;
    return all;
}

int
main (int argc, char **argv)
{
    rev_list	*all = NULL;
    char	name[10240];
    int		i;

    if (argc == 1)
	while (gets (name))
	    all = add_file (all, strdup (name));
    else
	for (i = 1; i < argc; i++)
	    all = add_file (all, argv[i]);
    if (all)
	dump_revlist (all);
    return err;
}
