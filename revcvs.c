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
rev_file_rev (cvs_file *cvs, cvs_number *n, time_t date, char *commitid, char *log)
{
    rev_file	*f = calloc (1, sizeof (rev_file));

    f->name = cvs->name;
    f->number = *n;
    f->date = date;
    f->log = log;
    f->commitid = commitid;
    return f;
}

/*
 * Given a single-file tree, locate the specific version number
 */

static rev_ent *
rev_find_cvs_ent (rev_list *rl, cvs_number *number)
{
    rev_branch	*b;
    rev_ent	*e;
    rev_file	*f;

    for (b = rl->branches; b; b = b->next)
	for (e = b->ent; e; e = e->parent)
	{
	     f = e->files[0];
	     if (cvs_number_compare (&f->number, number) == 0)
		    return e;
	}
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
    cvs_patch	*p;

    n = *branch;
    n.n[n.c-1] = 0;
    while ((v = cvs_find_version (cvs, &n))) {
	e = calloc (1, sizeof (rev_ent) + sizeof (rev_file *));
	p = cvs_find_patch (cvs, &v->number);
	e->date = v->date;
	e->commitid = v->commitid;
	if (p)
	    e->log = p->log;
	if (v->dead)
	    e->nfiles = 0;
	else
	    e->nfiles = 1;
	/* leave this around so the branch merging stuff can find numbers */
	e->files[0] = rev_file_rev (cvs, &v->number, v->date, v->commitid, e->log);
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
    rev_ent	*t, *v;
    rev_ent	*tp, *tc, *vc;
    rev_branch	**b_p;

    for (b_p = &rl->branches; (b = *b_p); b_p = &(b->next)) {
	if (cvs_is_trunk (&b->ent->files[0]->number))
	    trunk = b;
	if (cvs_is_vendor (&b->ent->files[0]->number)) {
	    vendor = b;
	    vendor_p = b_p;
	}
    }
    assert (trunk);
    assert (trunk != vendor);
    if (vendor) {
	tc = NULL;
	t = trunk->ent;
	/*
	 * Find the first commit to the trunk
	 * This will reset the default branch set
	 * when the initial import was done.
	 * Subsequent imports will *not* set the default
	 * branch, and should be on their own branch
	 */
	tc = NULL;
	t = trunk->ent;
	while (t && t->parent && t->parent->parent) {
	    tc = t;
	    t = t->parent;
	}
	tp = t->parent;
	/*
	 * Bracket the first trunk commit
	 */
	vc = NULL;
	v = vendor->ent;
	if (t && tp && v)
	{
	    while (v && time_compare (t->date, v->date) < 0) {
		vc = v;
		v = v->parent;
	    }
	    /*
	     * The picture now looks like this:
	     *
	     *	      tc
	     *	      |          vc
	     *        t          |
	     *        |          v
	     *        tp
	     *
	     * Hook it up so that it looks like:
	     *
	     *	     tc
	     *       |     /--- vc
	     *       t----/
	     *       |
	     *       v
	     *       |
	     *       tp
	     */
	
	    if (vc) {
		vc->tail = 1;
		vc->parent = t;
	    } else {
		*vendor_p = vendor->next;
		free (vendor);
		vendor = NULL;
	    }
	    t->parent = v;
	    while (v->parent)
		v = v->parent;
	    v->parent = tp;
	} else if (t && v) {
	    while (v->parent)
		v = v->parent;
	    v->tail = 1;
	    v->parent = t;
	}
    }
    /*
     * Set HEAD tag
     */
    rev_list_add_head (rl, trunk->ent, "HEAD");
    if (vendor)
	rev_list_add_head (rl, vendor->ent, "VENDOR-BRANCH");
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
	for (e = b->ent; e && e->parent; e = e->parent) {
	    if (e->tail) {
		e = NULL;
		break;
	    }
	}
	if (e) {
	    for (cv = cvs->versions; cv; cv = cv->next) {
		for (cb = cv->branches; cb; cb = cb->next) {
		    if (cvs_number_compare (&cb->number,
					    &e->files[0]->number) == 0)
		    {
			e->parent = rev_find_cvs_ent (rl, &cv->number);
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
		if (cvs_same_branch (&b->ent->files[0]->number, &s->number))
		{
		    e = b->ent;
		    break;
		}
	    if (!e) {
		cvs_number	n;

		n = s->number;
		while (n.c >= 4) {
		    n.c -= 2;
		    e = rev_find_cvs_ent (rl, &n);
		    if (e)
			break;
		}
	    }
	} else {
	    head = 0;
	    store = &rl->tags;
	    e = rev_find_cvs_ent (rl, &s->number);
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
    cvs_number	one_one;
    rev_ent	*trunk; 
    rev_ent	*branch;

//    if (!strcmp (cvs->name, "/cvs/xorg/xserver/xorg/ChangeLog,v"))
//	rl->watch = 1;
    /*
     * Generate trunk branch
     */
    one_one = lex_number ("1.1");
    trunk = rev_branch_cvs (cvs, &one_one);
    /*
     * Search for other branches
     */
    for (cv = cvs->versions; cv; cv = cv->next) {
	for (cb = cv->branches; cb; cb = cb->next) {
	    branch = rev_branch_cvs (cvs, &cb->number);
	    rev_list_add_branch (rl, branch);
	}
    }
    if (trunk)
	rev_list_add_branch (rl, trunk);
    rev_list_patch_vendor_branch (rl);
    rev_list_graft_branches (rl, cvs);
    rev_list_set_refs (rl, cvs);
    return rl;
}
