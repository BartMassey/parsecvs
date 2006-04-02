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
	c->author = v->author;
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
 * "Vendor branches" (1.1.x) are created by importing sources from
 * an external source. In X.org, this was from XFree86 and DRI. When
 * these trees are imported, cvs sets the 'default' branch in each ,v file
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
    rev_commit	*t, **tp, *v, **vp;
    rev_commit	*vlast;
    rev_commit	*tc;
    rev_ref	**h_p;
    int		delete_head;

    trunk = rl->heads;
    for (h_p = &rl->heads; (h = *h_p);) {
	delete_head = 0;
	if (h->commit && cvs_is_vendor (&h->commit->files[0]->number))
	{
	    /*
	     * Find version 1.2 on the trunk.
	     * This will reset the default branch set
	     * when the initial import was done.
	     * Subsequent imports will *not* set the default
	     * branch, and should be on their own branch
	     */
	    vendor = h;
	    vendor_p = h_p;
	    t = trunk->commit;
	    v = vendor->commit;
	    for (vlast = vendor->commit; vlast; vlast = vlast->parent)
		if (!vlast->parent)
		    break;
	    tc = NULL;
	    tp = &trunk->commit;
	    /*
	     * Find the latest trunk revision older than
	     * the entire vendor branch
	     */
	    while ((t = *tp))
	    {
		if (!t->parent || 
		    time_compare (vlast->files[0]->date,
				  t->parent->files[0]->date) >= 0)
		{
		    break;
		}
		tp = &t->parent;
	    }
	    if (t)
	    {
		/*
		 * If the first commit is older than the last element
		 * of the vendor branch, paste them together and
		 * nuke the vendor branch
		 */
		if (time_compare (vlast->files[0]->date,
				  t->files[0]->date) >= 0)
		{
		    delete_head = 1;
		}
		else
		{
		    /*
		     * Splice out any portion of the vendor branch
		     * newer than a the next trunk commit after
		     * the oldest branch commit.
		     */
		    for (vp = &vendor->commit; (v = *vp); vp = &v->parent)
			if (time_compare (v->date, t->date) <= 0)
			    break;
		    if (vp == &vendor->commit)
		    {
			/*
			 * Nothing newer, nuke vendor branch
			 */
			delete_head = 1;
		    }
		    else
		    {
			/*
			 * Some newer stuff, patch parent
			 */
			*vp = NULL;
		    }
		}
	    }
	    else
		delete_head = 1;
	    /*
	     * Merge two branches based on dates
	     */
	    while (t && v)
	    {
		if (time_compare (v->files[0]->date,
				  t->files[0]->date) >= 0)
		{
		    *tp = v;
		    tp = &v->parent;
		    v = v->parent;
		}
		else
		{
		    *tp = t;
		    tp = &t->parent;
		    t = t->parent;
		}
	    }
	    if (t)
		*tp = t;
	    else
		*tp = v;
	    if (!delete_head) {
		fprintf (stderr, "Vendor branch left with:\n");
		for (v = vendor->commit; v; v = v->parent) {
		    fprintf (stderr, "\t");
		    dump_number_file (stderr, v->files[0]->name,
				      &v->files[0]->number);
		    fprintf (stderr, "\n");
		}
	    }
	}
	if (delete_head) {
	    *h_p = h->next;
	    free (h);
	} else {
	    h_p = &(h->next);
	}
    }
//    fprintf (stderr, "%s spliced:\n", cvs->name);
//    for (t = trunk->commit; t; t = t->parent) {
//	dump_number_file (stderr, "\t", &t->files[0]->number);
//	fprintf (stderr, "\n");
//    }
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
	/*
	 * Find last commit on branch
	 */
	for (c = h->commit; c && c->parent; c = c->parent)
	    if (c->tail) {
		c = NULL;	/* already been done, skip */
		break;
	    }
	if (c) {
	    h->date = c->date;
	    /*
	     * Walk the version tree, looking for the branch location.
	     * Note that in the presense of vendor branches, the
	     * branch location may actually be out on that vendor branch
	     */
	    for (cv = cvs->versions; cv; cv = cv->next) {
		for (cb = cv->branches; cb; cb = cb->next) {
		    if (cvs_number_compare (&cb->number,
					    &c->files[0]->number) == 0)
		    {
			c->parent = rev_find_cvs_commit (rl, &cv->number);
			c->tail = 1;
			break;
		    }
		}
		if (c->parent)
		{
#if 0
		    /*
		     * check for a parallel vendor branch
		     */
		    for (cb = cv->branches; cb; cb = cb->next) {
			if (cvs_is_vendor (&cb->number)) {
			    cvs_number	v_n;
			    rev_commit	*v_c, *n_v_c;
			    fprintf (stderr, "Found merge into vendor branch\n");
			    v_n = cb->number;
			    v_c = NULL;
			    /*
			     * Walk to head of vendor branch
			     */
			    while ((n_v_c = rev_find_cvs_commit (rl, &v_n)))
			    {
				/*
				 * Stop if we reach a date after the
				 * branch version date
				 */
				if (time_compare (n_v_c->date, c->date) > 0)
				    break;
				v_c = n_v_c;
				v_n.n[v_n.c - 1]++;
			    }
			    if (v_c)
			    {
				fprintf (stderr, "%s: rewrite branch", cvs->name);
				dump_number_file (stderr, " branch point",
						  &v_c->files[0]->number);
				dump_number_file (stderr, " branch version",
						  &c->files[0]->number);
				fprintf (stderr, "\n");
				c->parent = v_c;
			    }
			}
		    }
#endif
		    break;
		}
	    }
	}
    }
}

/*
 * For each symbol, locate the appropriate commit
 */

static rev_ref *
rev_list_find_branch (rev_list *rl, cvs_number *number)
{
    cvs_number	n;
    rev_ref	*h;

    if (number->c < 2)
	return NULL;
    n = *number;
    h = NULL;
    while (n.c >= 2)
    {
	for (h = rl->heads; h; h = h->next) {
	    if (cvs_same_branch (&h->number, &n)) {
		break;
	    }
	}
	if (h)
	    break;
	n.c -= 2;
    }
    return h;
}

static void
rev_list_set_refs (rev_list *rl, cvs_file *cvs)
{
    rev_ref	*h;
    cvs_symbol	*s;
    rev_commit	*c;
    rev_ref	*t;
    
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
		if (!h->name) {
		    h->name = s->name;
		    h->degree = cvs_number_degree (&s->number);
		} else
		    h = rev_list_add_head (rl, h->commit, s->name,
					   cvs_number_degree (&s->number));
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
		    h = rev_list_add_head (rl, c, s->name,
					   cvs_number_degree (&s->number));
	    }
	    if (h)
		h->number = s->number;
	} else {
	    c = rev_find_cvs_commit (rl, &s->number);
	    if (c) {
		t = rev_list_add_tag (rl, c, s->name,
				      cvs_number_degree (&s->number));
		t->number = s->number;
	    }
	}
    }
    /*
     * Fix up unnamed heads
     */
    for (h = rl->heads; h; h = h->next) {
	cvs_number	n;
	rev_commit	*c;

	if (h->name)
	    continue;
	for (c = h->commit; c; c = c->parent) {
	    if (c->nfiles)
		break;
	}
	if (!c)
	    continue;
	n = c->files[0]->number;
	/* convert to branch form */
	n.n[n.c-1] = n.n[n.c-2];
	n.n[n.c-2] = 0;
	h->number = n;
	h->degree = cvs_number_degree (&n);
	/* compute name after patching parents */
    }
    /*
     * Link heads together in a tree
     */
    for (h = rl->heads; h; h = h->next) {
	cvs_number	n;

	if (h->number.c >= 4) {
	    n = h->number;
	    n.c -= 2;
	    h->parent = rev_list_find_branch (rl, &n);
	    if (!h->parent && ! cvs_is_vendor (&h->number))
		fprintf (stderr, "Warning: %s: branch %s has no parent\n",
			 cvs->name, h->name);
	}
	if (h->parent && !h->name) {
	    char	name[1024];

	    fprintf (stderr, "Warning: %s: unnamed branch from %s\n",
		     cvs->name, h->parent->name);
	    sprintf (name, "%s-UNNAMED-BRANCH", h->parent->name);
	    h->name = atom (name);
	}
    }
    /*
     * Link tags to containing branch
     */
    for (t = rl->tags; t; t = t->next)
	t->parent = rev_list_find_branch (rl, &t->number);
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
rev_list_dump_ref_parents (FILE *f, rev_ref *h)
{
    if (h) {
	rev_list_dump_ref_parents (f, h->parent);
	fprintf (f, "%s > ", h->name);
    }
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
	rev_list_dump_ref_parents (stderr, h->parent);
	dump_number_file (stderr, h->name, &h->number);
	fprintf (stderr, "\n");
	h = h->next;
    }
}

rev_list *
rev_list_cvs (cvs_file *cvs)
{
    rev_list	*rl = calloc (1, sizeof (rev_list));
    cvs_number	trunk_number;
    rev_commit	*trunk; 
    rev_commit	*branch;
    cvs_version	*cv;
    cvs_branch	*cb;
    rev_ref	*t;
    cvs_version	*ctrunk = NULL;

    /*
     * Locate first revision on trunk branch
     */
    for (cv = cvs->versions; cv; cv = cv->next) {
	if (cvs_is_trunk (&cv->number) &&
	    (!ctrunk || cvs_number_compare (&cv->number,
					    &ctrunk->number) < 0))
	{
	    ctrunk = cv;
	}
    }
    /*
     * Generate trunk branch
     */
    if (ctrunk)
	trunk_number = ctrunk->number;
    else
	trunk_number = lex_number ("1.1");
    trunk = rev_branch_cvs (cvs, &trunk_number);
    if (trunk) {
	t = rev_list_add_head (rl, trunk, atom ("master"), 2);
	t->number = trunk_number;
    }
    /*
     * Search for other branches
     */
//    printf ("building branches for %s\n", cvs->name);
    
    for (cv = cvs->versions; cv; cv = cv->next) {
	for (cb = cv->branches; cb; cb = cb->next)
	{
	    branch = rev_branch_cvs (cvs, &cb->number);
	    rev_list_add_head (rl, branch, NULL, 0);
	}
    }
    rev_list_patch_vendor_branch (rl, cvs);
    rev_list_graft_branches (rl, cvs);
    rev_list_set_refs (rl, cvs);
    rev_list_sort_heads (rl, cvs);
    rev_list_set_tail (rl);
    rev_list_free_dead_files (rl);
    rev_list_validate (rl);
    return rl;
}
