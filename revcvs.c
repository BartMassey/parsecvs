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

static rev_file *
rev_file_rev (cvs_file *cvs, cvs_number *n, time_t date, char *commitid)
{
    rev_file	*f = calloc (1, sizeof (rev_file));
    cvs_patch	*p = cvs_find_patch (cvs, n);

    f->next = NULL;
    f->name = cvs->name;
    f->number = *n;
    f->date = date;
    if (p)
	f->log = p->log;
    f->commitid = commitid;
    return f;
}

/*
 * Locate an entry for the specific file at the specific version
 */

static rev_ent *
rev_find_ent (rev_list *rl, char *name, cvs_number *number)
{
    rev_branch	*b;
    rev_ent	*e;
    rev_file	*f;

    for (b = rl->branches; b; b = b->next)
	for (e = b->ent; e; e = e->parent)
	    for (f = e->files; f; f = f->next)
		if (!strcmp (f->name, name) &&
		    cvs_number_compare (&f->number, number) == 0)
		    return e;
    return NULL;
}

/*
 * Construct a branch using CVS revision numbers
 */
static rev_ent *
rev_branch_cvs (cvs_file *cvs, cvs_number *branch)
{
    cvs_number	n;
    rev_ent	*head = NULL;
    cvs_version	*v;
    rev_ent	*e;

    n = *branch;
    n.n[n.c-1] = 0;
    while ((v = cvs_find_version (cvs, &n))) {
	e = calloc (1, sizeof (rev_ent));
	e->files = rev_file_rev (cvs, &v->number, v->date, v->commitid);
	e->parent = head;
	head = e;
	n = v->number;
    }
    return head;
}

/*
 * The "vendor branch" (1.1.1) is created by importing sources from
 * an external source. In X.org, this was from XFree86. When this
 * tree is imported, cvs sets the 'default' branch in each ,v file
 * to point along this branch. This means that tags made between
 * the time the vendor branch is imported and when a new revision
 * is committed to the head branch are placed on the vendor branch
 * In addition, any files without such a commit appear to adopt
 * the vendor branch as 'head'. We fix this by merging these two
 * branches together as if they were the same
 */
static void
rev_list_patch_vendor_branch (rev_list *rl)
{
    rev_branch	*trunk = NULL;
    rev_branch	*vendor = NULL, **vendor_p = NULL;
    rev_branch	*b;
    rev_ent	*t, *v, *n;
    rev_ent	**tail;
    rev_branch	**b_p;

    for (b_p = &rl->branches; (b = *b_p); b_p = &(b->next)) {
	if (cvs_is_trunk (&b->ent->files->number))
	    trunk = b;
	if (cvs_is_vendor (&b->ent->files->number)) {
	    vendor = b;
	    vendor_p = b_p;
	}
    }
    assert (trunk);
    assert (trunk != vendor);
    if (vendor) {
	t = trunk->ent;
	v = vendor->ent;
	/* patch out vendor branch */
	*vendor_p = vendor->next;
	tail = &trunk->ent;
	while (v) {
	    if (time_compare (t->files->date, v->files->date) > 0) {
		n = t;
		t = t->parent;
	    } else {
		n = v;
		if (v->tail)
		    v = NULL;
		else
		    v = v->parent;
	    }
	    *tail = n;
	    tail = &n->parent;
	}
	*tail = t;
    }
    /*
     * Set HEAD tag
     */
    rev_list_add_head (rl, trunk->ent, "HEAD");
}

/*
 * Given a disconnected set of branches, graft the bottom
 * of each branch where it belongs on the parent branch
 */

static void
rev_list_graft_branches (rev_list *rl, cvs_file *cvs)
{
    rev_branch	*b;
    rev_ent	*e;
    cvs_version	*cv;
    cvs_branch	*cb;

    /*
     * Glue branches together
     */
    for (b = rl->branches; b; b = b->next) {
	for (e = b->ent; e && e->parent; e = e->parent)
	    ;
	if (e) {
	    for (cv = cvs->versions; cv; cv = cv->next) {
		for (cb = cv->branches; cb; cb = cb->next) {
		    if (cvs_number_compare (&cb->number,
					    &e->files->number) == 0)
		    {
			e->parent = rev_find_ent (rl, cvs->name, &cv->number);
			e->tail = 1;
			if (!e->parent) {
			    dump_number ("can't find parent", &cv->number);
			    printf ("\n");
			}
		    }
		}
	    }
	}
    }
}

/*
 * For each symbol, locate the appropriate ent
 */

static void
rev_list_set_refs (rev_list *rl, cvs_file *cvs)
{
    rev_branch	*b;
    cvs_symbol	*s;
    rev_ent	*e;
    rev_ref	**store;
    int		head;
    
    /*
     * Locate a symbolic name for this head
     */
    for (s = cvs->symbols; s; s = s->next) {
	e = NULL;
	if (cvs_is_head (&s->number)) {
	    store = &rl->heads;
	    head = 1;
	    for (b = rl->branches; b; b = b->next)
		if (cvs_same_branch (&b->ent->files->number, &s->number))
		{
		    e = b->ent;
		    break;
		}
	    if (!e) {
		cvs_number	n;
		char		*name;

		name = rl->branches->ent->files->name;
		n = s->number;
		while (n.c >= 4) {
		    n.c -= 2;
		    e = rev_find_ent (rl, name, &n);
		    if (e)
			break;
		}
	    }
	} else {
	    head = 0;
	    store = &rl->tags;
	    e = rev_find_ent (rl, rl->branches->ent->files->name, &s->number);
	}
	if (e)
	    rev_ref_add (store, e, s->name, head);
    }
}

rev_list *
rev_list_cvs (cvs_file *cvs)
{
    rev_list	*rl = calloc (1, sizeof (rev_list));
    cvs_version	*cv;
    cvs_branch	*cb;
    cvs_number	one_one = lex_number ("1.1");
    rev_ent	*trunk = rev_branch_cvs (cvs, &one_one);
    rev_ent	*branch;
    
    /*
     * Generate trunk branch
     */
    if (trunk)
	rev_list_add_branch (rl, trunk);
    /*
     * Search for other branches
     */
    for (cv = cvs->versions; cv; cv = cv->next) {
	for (cb = cv->branches; cb; cb = cb->next) {
	    branch = rev_branch_cvs (cvs, &cb->number);
	    rev_list_add_branch (rl, branch);
	}
    }
    rev_list_graft_branches (rl, cvs);
    rev_list_set_refs (rl, cvs);
    rev_list_patch_vendor_branch (rl);
    return rl;
}
