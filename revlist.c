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
rev_file_rev (cvs_file *cvs, cvs_number *n, time_t date)
{
    rev_file	*f = calloc (1, sizeof (rev_file));
    cvs_patch	*p = cvs_find_patch (cvs, n);

    f->next = NULL;
    f->name = cvs->name;
    f->number = n;
    f->date = date;
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
	e->files = rev_file_rev (cvs, v->number, v->date);
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
		    if (s->number->n[n.c - 1] != n.n[n.c - 2])
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

long
time_compare (time_t a, time_t b)
{
    return (long) a - (long) b;
}

/*
 * The "vendor branch" (1.1.1) is created by importing sources from
 * an external source. In X.org, this was from XFree86. When this
 * tree is imported, cvs sets the 'default' branch in each ,v file
 * to point along this branch. This means that tags made between
 * the time the vendor branch is imported and when a new revision
 * is committed to the head branch are placed on the vendor branch
 * In addition, any files without such a commit appear to adopt
 * the vendor branch as 'head'. We fix this by marking the vendor branch
 * as a additional parent, based entirely on time, for suitable
 * revisions along the main trunk
 */
void
rev_list_patch_vendor_branch (rev_list *rl)
{
    rev_head	*trunk = NULL;
    rev_head	*vendor = NULL;
    rev_head	*h;
    rev_ent	*t, *v;
    time_t	t_time, v_time, pv_time;

    for (h = rl->heads; h; h = h->next) {
	if (cvs_is_trunk (h->ent->files->number))
	    trunk = h;
	if (cvs_is_vendor (h->ent->files->number))
	    vendor = h;
    }
    if (!trunk || !vendor)
	return;
    t = trunk->ent;
    v = vendor->ent;
    while (t && v) {
	/*
	 * search for vendor older than trunk
	 */
	while (v && time_compare (v->files->date, t->files->date) > 0)
	    v = v->parent;
	if (!v)
	    break;
	
	/*
	 * search for trunk parent older than vendor
	 */
	while (t->parent &&
	       time_compare (v->files->date, t->parent->files->date) < 0)
	    t = t->parent;
	t->vendor = v;
	t = t->parent;
	v = v->parent;
    }
}

/*
 * Given a disconnected set of branches, graft the bottom
 * of each branch where it belongs on the parent branch
 */

static void
rev_list_graft_branches (rev_list *rl, cvs_file *cvs)
{
    rev_head	*h;
    rev_ent	*e;
    cvs_version	*cv;
    cvs_branch	*cb;

    /*
     * Glue branches together
     */
    for (h = rl->heads; h; h = h->next) {
	for (e = h->ent; e && e->parent; e = e->parent)
	    ;
	if (e) {
	    for (cv = cvs->versions; cv; cv = cv->next) {
		for (cb = cv->branches; cb; cb = cb->next) {
		    if (cvs_number_compare (cb->number,
					    e->files->number) == 0)
		    {
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
    rev_head	*h;
    rev_ent	*e;
    
    /*
     * Generate trunk branch
     */
    if (trunk) {
	trunk->next = rl->heads;
	rl->heads = trunk;
    }
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
    rev_list_graft_branches (rl, cvs);
    rev_list_patch_vendor_branch (rl);
    return rl;
}

rev_head *
rev_find_branch_by_name (rev_list *rl, char *name)
{
    rev_head	*h;
    rev_tag	*t;

    for (h = rl->heads; h; h = h->next)
	for (t = h->tags; t; t = t->next)
	    if (!strcmp (t->name, name))
		return h;
    return NULL;
}

rev_list *
rev_list_merge (rev_list *a, rev_list *b)
{
    return a;
}
