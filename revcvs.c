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

/*
 * Given a single-file tree, locate the specific version number
 */

static rev_commit *
rev_find_cvs_commit (rev_list *rl, cvs_number *number)
{
    rev_ref	*h;
    rev_commit	*c;
    rev_file	*f;

    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	for (c = h->commit; c; c = c->parent)
	{
	     f = c->files[0];
	     if (cvs_number_compare (&f->number, number) == 0)
		    return c;
	     if (c->tail)
		 break;
	}
    }
    return NULL;
}

/*
 * Construct a branch using CVS revision numbers
 */
static rev_commit *
rev_branch_cvs (cvs_file *cvs, cvs_number *branch)
{
    cvs_number	n;
    rev_commit	*head = NULL;
    cvs_version	*v;
    rev_commit	*c;
    cvs_patch	*p;

    n = *branch;
    n.n[n.c-1] = 0;
    while ((v = cvs_find_version (cvs, &n))) {
	c = calloc (1, sizeof (rev_commit) + sizeof (rev_file *));
	p = cvs_find_patch (cvs, &v->number);
	c->date = v->date;
	c->commitid = v->commitid;
	if (p)
	    c->log = p->log;
	if (v->dead)
	    c->nfiles = 0;
	else
	    c->nfiles = 1;
	/* leave this around so the branch merging stuff can find numbers */
	c->files[0] = rev_file_rev (cvs->name, &v->number, v->date);
	c->parent = head;
	head = c;
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
rev_list_patch_vendor_branch (rev_list *rl, cvs_file *cvs)
{
    rev_ref	*trunk = NULL;
    rev_ref	*vendor = NULL, **vendor_p = NULL;
    rev_ref	*h;
    rev_commit	*t, *v;
    rev_commit	*tp, *tc, *vc;
    rev_ref	**h_p;
    cvs_number	default_branch;

    if (cvs->branch.c == 3)
	default_branch = cvs->branch;
    else
	default_branch = lex_number ("1.1.1");

    for (h_p = &rl->heads; (h = *h_p); h_p = &(h->next)) {
	if (!trunk)
	    if (cvs_is_trunk (&h->commit->files[0]->number))
		trunk = h;
	if (!vendor)
	    if (cvs_same_branch (&h->commit->files[0]->number, &default_branch)) {
		vendor = h;
		vendor_p = h_p;
	    }
    }
    assert (trunk);
    assert (trunk != vendor);
    if (vendor) {
	tc = NULL;
	t = trunk->commit;
	/*
	 * Find the first commit to the trunk
	 * This will reset the default branch set
	 * when the initial import was done.
	 * Subsequent imports will *not* set the default
	 * branch, and should be on their own branch
	 */
	tc = NULL;
	t = trunk->commit;
	while (t && t->parent && t->parent->parent) {
	    tc = t;
	    t = t->parent;
	}
	tp = t->parent;
	/*
	 * Bracket the first trunk commit
	 */
	vc = NULL;
	v = vendor->commit;
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
	    /*
	     * No commits to trunk, merge entire vendor branch
	     * to trunk
	     */
	    trunk->commit = v;
	    while (v->parent)
		v = v->parent;
	    v->parent = t;
	    *vendor_p = vendor->next;
	    free (vendor);
	    vendor = NULL;
	}
    }
    if (vendor)
	vendor->name = atom ("VENDOR-BRANCH");
}

/*
 * Given a disconnected set of branches, graft the bottom
 * of each branch where it belongs on the parent branch
 */

static void
rev_list_graft_branches (rev_list *rl, cvs_file *cvs)
{
    rev_ref	*h;
    rev_commit	*c;
    cvs_version	*cv;
    cvs_branch	*cb;

    /*
     * Glue branches together
     */
    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	for (c = h->commit; c && c->parent; c = c->parent)
	    if (c->tail) {
		c = NULL;
		break;
	    }
	if (c) {
	    for (cv = cvs->versions; cv; cv = cv->next) {
		for (cb = cv->branches; cb; cb = cb->next) {
		    if (cvs_number_compare (&cb->number,
					    &c->files[0]->number) == 0)
		    {
			c->parent = rev_find_cvs_commit (rl, &cv->number);
			c->tail = 1;
		    }
		}
	    }
	}
    }
}

/*
 * For each symbol, locate the appropriate commit
 */

static void
rev_list_set_refs (rev_list *rl, cvs_file *cvs)
{
    rev_ref	*h;
    cvs_symbol	*s;
    rev_commit	*c;
    
    /*
     * Locate a symbolic name for this head
     */
    for (s = cvs->symbols; s; s = s->next) {
	c = NULL;
	if (cvs_is_head (&s->number)) {
	    for (h = rl->heads; h; h = h->next) {
		if (cvs_same_branch (&h->commit->files[0]->number, &s->number))
		    break;
	    }
	    if (h) {
		if (!h->name)
		    h->name = s->name;
		else
		    rev_list_add_head (rl, h->commit, s->name);
	    } else {
		cvs_number	n;

		n = s->number;
		while (n.c >= 4) {
		    n.c -= 2;
		    c = rev_find_cvs_commit (rl, &n);
		    if (c)
			break;
		}
		if (c)
		    rev_list_add_head (rl, c, s->name);
	    }
	} else {
	    c = rev_find_cvs_commit (rl, &s->number);
	    if (c)
		rev_list_add_tag (rl, c, s->name);
	}
    }
}

/*
 * Dead file revisions get an extra rev_file object which may be
 * needed during branch merging. Clean those up before returning
 * the resulting rev_list
 */

static void
rev_list_free_dead_files (rev_list *rl)
{
    rev_ref	*h;
    rev_commit	*c;

    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	for (c = h->commit; c; c = c->parent) {
	    if (c->nfiles == 0)
		rev_file_free (c->files[0]);
	    if (c->tail)
		break;
	}
    }
}

static int
rev_order_compare (cvs_number *a, cvs_number *b)
{
    if (a->c != b->c)
	return a->c - b->c;
    return cvs_number_compare (b, a);
}

static cvs_symbol *
cvs_find_symbol (cvs_file *cvs, char *name)
{
    cvs_symbol	*s;

    for (s = cvs->symbols; s; s = s->next)
	if (s->name == name)
	    return s;
    return NULL;
}

static int
cvs_symbol_compare (cvs_symbol *a, cvs_symbol *b)
{
    if (!a) {
	if (!b) return 0;
	return -1;
    }
    if (!b)
	return 1;
    return cvs_number_compare (&a->number, &b->number);
}

static void
rev_list_sort_heads (rev_list *rl, cvs_file *cvs)
{
    rev_ref	*h, **hp;
    cvs_symbol	*hs, *hns;

    for (hp = &rl->heads; (h = *hp);) {
	if (!h->next)
	    break;
	hs = cvs_find_symbol (cvs, h->name);
	hns = cvs_find_symbol (cvs, h->next->name);
	if (cvs_symbol_compare (hs, hns) > 0) {
	    *hp = h->next;
	    h->next = h->next->next;
	    (*hp)->next = h;
	    hp = &rl->heads;
	} else {
	    hp = &h->next;
	}
    }
    return;
    fprintf (stderr, "Sorted heads\n");
    for (h = rl->heads; h;) {
	fprintf (stderr, "\t");
	for (;;) {
	    hs = cvs_find_symbol (cvs, h->name);
	    if (hs)
		dump_number_file (stderr, h->name,
				  &hs->number);
	    else
		fprintf (stderr, "%s 1.1", h->name);
	    if (!h->next)
		break;
	    if (h->next->commit != h->commit)
		break;
	    fprintf (stderr, " ");
	    h = h->next;
	}
	fprintf (stderr, "\n");
	h = h->next;
    }
}

rev_list *
rev_list_cvs (cvs_file *cvs)
{
    rev_list	*rl = calloc (1, sizeof (rev_list));
    cvs_number	one_one;
    rev_commit	*trunk; 
    rev_commit	*branch;
    cvs_version	*cv;
    cvs_branch	*cb;

//    if (!strcmp (cvs->name, "/cvs/xorg/xserver/xorg/ChangeLog,v"))
//    if (!strcmp (cvs->name, "/cvs/xorg/xserver/xorg/ChangeLog,v"))
//	rl->watch = 1;
    /*
     * Generate trunk branch
     */
    one_one = lex_number ("1.1");
    trunk = rev_branch_cvs (cvs, &one_one);
    if (trunk)
	rev_list_add_head (rl, trunk, atom ("HEAD"));
    /*
     * Search for other branches
     */
//    printf ("building branches for %s\n", cvs->name);
    
    for (cv = cvs->versions; cv; cv = cv->next) {
	for (cb = cv->branches; cb; cb = cb->next)
	{
	    branch = rev_branch_cvs (cvs, &cb->number);
	    rev_list_add_head (rl, branch, NULL);
	}
    }
    rev_list_patch_vendor_branch (rl, cvs);
    rev_list_graft_branches (rl, cvs);
    rev_list_set_refs (rl, cvs);
    rev_list_sort_heads (rl, cvs);
    rev_list_set_tail (rl);
    rev_list_free_dead_files (rl);
    return rl;
}
