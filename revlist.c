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

rev_file *
rev_file_rev (cvs_file *cvs, cvs_number *n)
{
    rev_file	*f = calloc (1, sizeof (rev_file));
    cvs_patch	*p = cvs_find_patch (cvs, n);

    f->next = NULL;
    f->name = cvs->name;
    f->number = n;
    if (p)
	f->log = p->log;
}

rev_tag *
rev_make_tag (char *name)
{
    rev_tag *t = calloc (1, sizeof (rev_tag));
    t->name = name;
    return t;
}

rev_tag *
rev_tag_rev (cvs_file *cvs, cvs_number *branch)
{
    cvs_symbol	*s;
    rev_tag	*tags = NULL;

    for (s = cvs->symbols; s; s = s->next) {
	if (cvs_number_compare (s->number, branch) == 0) {
	    rev_tag	*t = rev_make_tag (s->name);
	    t->next = tags;
	    tags = t;
	}
    }
    return tags;
}

rev_head *
rev_head_cvs (cvs_file *cvs, cvs_number *branch)
{
    cvs_number	n;
    rev_head	*h = calloc (1, sizeof (rev_head));
    cvs_symbol	*s;
    int		i;
    cvs_version	*v;
    rev_ent	*e;

    n = *branch;
    n.n[n.c-1] = 0;
    while ((v = cvs_find_version (cvs, &n))) {
	e = calloc (1, sizeof (rev_ent));
	e->files = rev_file_rev (cvs, v->number);
	e->tags = rev_tag_rev (cvs, v->number);
	e->parent = h->ent;
	h->ent = e;
	n = *v->number;
    }
    /*
     * Locate a symbolic name for this head
     */
    for (s = cvs->symbols; s; s = s->next) {
	if (cvs_is_head (s->number) && s->number->c == n.c) {
	    for (i = 0; i < n.c - 1; i++) {
		/* deal with wacky branch tag .0. revision format */
		if (i == n.c - 2) {
		    if (s->number->n[n.c - 2] != n.n[n.c - 1])
			break;
		} else {
		    if (s->number->n[i] != n.n[i])
			break;
		}
	    }
	    if (i == n.c - 1) {
		rev_tag	*t = rev_make_tag (s->name);
		t->next = h->tags;
		h->tags = t;
	    }
	}
    }
    return h;
}

rev_ent *
rev_find_ent (rev_list *rl, char *name, cvs_number *number)
{
    rev_head	*h;
    rev_ent	*e;
    rev_file	*f;

    for (h = rl->heads; h; h = h->next)
	for (e = h->ent; e; e = e->parent)
	    for (f = e->files; f; f = f->next)
		if (!strcmp (f->name, name) &&
		    cvs_number_compare (f->number, number) == 0)
		    return e;
    return NULL;
}

rev_list *
rev_list_cvs (cvs_file *cvs)
{
    rev_list	*rl = calloc (1, sizeof (rev_list));
    cvs_version	*cv;
    cvs_symbol	*cs;
    cvs_branch	*cb;
    cvs_number	*one_one = lex_number ("1.1");
    rev_head	*trunk = rev_head_cvs (cvs, one_one);
#if 0
    cvs_number	*one_one_one_one = lex_number ("1.1.1.1");
    rev_head	*vendor = rev_head_cvs (cvs, one_one_one_one);
#endif
    rev_head	*h;
    rev_ent	*e;
    
    /*
     * Generate known branches
     */
    if (trunk) {
	trunk->next = rl->heads;
	rl->heads = trunk;
    }
#if 0
    if (vendor) {
	vendor->next = rl->heads;
	rl->heads = vendor;
    }
#endif
    /*
     * Search for other branches
     */
    for (cv = cvs->versions; cv; cv = cv->next) {
	for (cb = cv->branches; cb; cb = cb->next) {
	    h = rev_head_cvs (cvs, cb->number);
	    h->next = rl->heads;
	    rl->heads = h;
	}
    }
    /*
     * Glue branches together
     */
    for (h = rl->heads; h; h = h->next) {
	for (e = h->ent; e && e->parent; e = e->parent)
	    ;
	if (e) {
	    for (cv = cvs->versions; cv; cv = cv->next) {
		for (cb = cv->branches; cb; cb = cb->next) {
		    if (cvs_number_compare (cb->number, e->files->number) == 0) {
			e->parent = rev_find_ent (rl, cvs->name, cv->number);
			e->tail = 1;
			if (!e->parent) {
			    dump_number ("can't find parent", cv->number);
			    printf ("\n");
			}
		    }
		}
	    }
	}
    }
    return rl;
}

rev_list *
rev_list_merge (rev_list *a, rev_list *b)
{
    return NULL;
}
