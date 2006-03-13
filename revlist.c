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
 * Add head or tag refs
 */

void
rev_ref_add (rev_ref **list, rev_ent *ent, char *name, int head)
{
    rev_ref	*r;
    r = calloc (1, sizeof (rev_ref));
    r->ent = ent;
    r->name = name;
    r->next = *list;
    r->head = head;
    *list = r;
}

void
rev_list_add_head (rev_list *rl, rev_ent *ent, char *name)
{
    rev_ref_add (&rl->heads, ent, name, 1);
}

void
rev_list_add_tag (rev_list *rl, rev_ent *ent, char *name)
{
    rev_ref_add (&rl->tags, ent, name, 0);
}

void
rev_list_add_branch (rev_list *rl, rev_ent *ent)
{
    rev_branch	*b = calloc (1, sizeof (rev_branch));

    b->next = rl->branches;
    b->ent = ent;
    rl->branches = b;
}

static rev_ref *
rev_find_head (rev_list *rl, char *name)
{
    rev_ref	*h;

    for (h = rl->heads; h; h = h->next)
	if (h->name == name)
	    return h;
    return NULL;
}

static rev_ref *
rev_find_tag (rev_list *rl, char *name)
{
    rev_ref	*t;

    for (t = rl->tags; t; t = t->next)
	if (t->name == name)
	    return t;
    return NULL;
}

#if 0
static rev_ref *
rev_find_ref (rev_list *rl, char *name)
{
    rev_ref	*r;

    r = rev_find_head (rl, name);
    if (!r)
	r = rev_find_tag (rl, name);
    return r;
}
#endif

static void
rev_file_merge (rev_ent *a, rev_ent *b, rev_ent *e)
{
    int		ai = 0, bi = 0, ei = 0;
    rev_file	*af, *bf, *ef;

    while (ai < a->nfiles || bi < b->nfiles) {
	if (ai < a->nfiles && bi < b->nfiles) {
	    af = a->files[ai];
	    bf = b->files[bi];
	    if (time_compare (af->date, bf->date) > 0) {
		ef = af;
		ai++;
	    } else {
		ef = bf;
		bi++;
	    }
	} else if (ai < a->nfiles) {
	    ef = a->files[ai];
	    ai++;
	} else {
	    ef = b->files[bi];
	    bi++;
	}
	e->files[ei++] = ef;
    }
    e->nfiles = ei;
}

static void
rev_file_copy (rev_ent *src, rev_ent *dst)
{
    memcpy (dst->files, src->files,
	    (dst->nfiles = src->nfiles) * sizeof (rev_file *));
}

/*
 * Commits further than 5 minutes apart are assume to be different
 */
static int
commit_time_close (time_t a, time_t b)
{
    long	diff = a - b;
    if (diff < 0) diff = -diff;
    if (diff < 5 * 60)
	return 1;
    return 0;
}

/*
 * The heart of the merge operation; detect when two
 * commits are "the same"
 */
static int
rev_ent_match (rev_ent *a, rev_ent *b)
{
    /*
     * Very recent versions of CVS place a commitid in
     * each commit to track patch sets. Use it if present
     */
    if (a->files[0]->commitid && b->files[0]->commitid)
	return a->files[0]->commitid == b->files[0]->commitid;
    if (a->files[0]->commitid || b->files[0]->commitid)
	return 0;
    if (!commit_time_close (a->files[0]->date, b->files[0]->date))
	return 0;
    if (a->files[0]->log != b->files[0]->log)
	return 0;
    return 1;
}

/*
 * Find an existing rev_ent which has both a and b in common
 */

#define HASH_SIZE	9013

typedef struct _rev_merged {
    struct _rev_merged	*next;
    rev_ent		*a, *b, *e;
} rev_merged;

static rev_merged	*merged_buckets[HASH_SIZE];

static unsigned long
rev_merged_hash (rev_ent *a, rev_ent *b)
{
    unsigned long	h = (unsigned long) a ^ (unsigned long) b;

    return h % HASH_SIZE;
}

static rev_ent *
rev_branch_find_merged (rev_ent *a, rev_ent *b)
{
    rev_merged	*m;

    for (m = merged_buckets[rev_merged_hash(a,b)]; m; m = m->next)
	if (m->a == a && m->b == b)
	    return m->e;
    return NULL;
}

static void
rev_branch_mark_merged (rev_ent *a, rev_ent *b, rev_ent *e)
{
    rev_merged	**bucket = &merged_buckets[rev_merged_hash(a,b)];
    rev_merged	*m = calloc (1, sizeof (rev_merged));

    m->a = a;
    m->b = b;
    m->e = e;
    m->next = *bucket;
    *bucket = m;
}

static void
rev_branch_discard_merged (void)
{
    int		i;
    rev_merged	*m;
    rev_merged	*merged;

    for (i = 0; i < HASH_SIZE; i++) {
	merged = merged_buckets[i];
	merged_buckets[i] = NULL;
	while ((m = merged)) {
	    merged = m->next;
	    free (m);
	}
    }
}

static rev_merged *
rev_branch_get_merge (rev_ent *e)
{
    int		i;
    rev_merged	*m;
    rev_merged	*merged;
    
    for (i = 0; i < HASH_SIZE; i++) {
	merged = merged_buckets[i];
	for (m = merged; m; m = m->next)
	    if (m->e == e)
		return m;
    }
    return NULL;
}

static rev_ent *
rev_branch_merge (rev_ent *a, rev_ent *b)
{
    rev_ent	*head = NULL;
    rev_ent	**tail = &head;
    rev_ent	*e;

    while (a && b)
    {
	e = rev_branch_find_merged (a, b);
	if (e)
	{
	    a = b = NULL;
	}
	else
	{
	    e = calloc (1, sizeof (rev_ent) +
			(a->nfiles + b->nfiles) * sizeof (rev_file *));
	    rev_file_merge (a, b, e);
	    rev_branch_mark_merged (a, b, e);
	    if (rev_ent_match (a, b)) {
		a = a->parent;
		b = b->parent;
	    } else {
		if (time_compare (a->files[0]->date, b->files[0]->date) > 0)
		    a = a->parent;
		else
		    b = b->parent;
	    }
	}
	*tail = e;
	tail = &e->parent;
    }
    if (!a)
	a = b;
    while (a)
    {
	e = rev_branch_find_merged (a, NULL);
	if (e)
	{
	    a = NULL;
	}
	else
	{
	    e = calloc (1, sizeof (rev_ent) + a->nfiles * sizeof (rev_file *));
	    rev_file_copy (a, e);
	    rev_branch_mark_merged (a, NULL, e);
	    a = a->parent;
	}
	*tail = e;
	tail = &e->parent;
    }
    return head;
}

static rev_ent *
rev_branch_copy (rev_ent *a)
{
    return rev_branch_merge (a, NULL);
}

rev_list *
rev_list_merge (rev_list *a, rev_list *b)
{
    rev_list	*rl = calloc (1, sizeof (rev_list));
    rev_ref	*h, *ah, *bh;
    rev_branch	*branch;
    rev_ent	*e;
    rev_merged	*merged;
    rev_ent	*parent;
    rev_ref	*at, *bt;

    /* add all of the head refs */
    for (ah = a->heads; ah; ah = ah->next)
	rev_list_add_head (rl, NULL, ah->name);
    for (bh = b->heads; bh; bh = bh->next)
	if (!rev_find_head (rl, bh->name))
	    rev_list_add_head (rl, NULL, bh->name);
    
    /*
     * Walk the branches from each head, adding ents all the way down
     */
    for (h = rl->heads; h; h = h->next) {
	ah = rev_find_head (a, h->name);
	bh = rev_find_head (b, h->name);
	if (ah && bh) {
	    h->ent = rev_branch_merge (ah->ent, bh->ent);
	} else {
	    if (!ah) ah = bh;
	    if (ah)
		h->ent = rev_branch_copy (ah->ent);
	}
	if (h->ent) {
	    for (branch = rl->branches; branch; branch = branch->next)
		if (h->ent == branch->ent)
		    break;
	    if (!branch)
		rev_list_add_branch (rl, h->ent);
	}
    }
    /*
     * Find tag locations
     */
    for (at = a->tags; at; at = at->next) {
	bt = rev_find_tag (b, at->name);
	e = rev_branch_find_merged (at->ent, bt ? bt->ent : NULL);
	if (e)
	    rev_list_add_tag (rl, e, at->name);
    }
    for (bt = b->tags; bt; bt = bt->next) {
	at = rev_find_tag (a, bt->name);
	if (!at) {
	    e = rev_branch_find_merged (bt->ent, NULL);
	    if (e)
		rev_list_add_tag (rl, e, bt->name);
	}
    }
    /*
     * Compute 'tail' values
     */
    for (branch = rl->branches; branch; branch = branch->next)
	branch->ent->seen = 1;
    for (branch = rl->branches; branch; branch = branch->next) {
	for (e = branch->ent; e; e = e->parent) {
	    e->seen = 1;
	    if (e->parent && e->parent->seen) {
		e->tail = 1;
		break;
	    }
	}
    }
    rev_branch_discard_merged ();
    return rl;
}

static void
rev_ent_free (rev_ent *ent)
{
    rev_ent	*e;

    while ((e = ent)) {
	if (e->tail)
	    ent = NULL;
	else
	    ent = e->parent;
	free (e);
    }
}

void
rev_branch_free (rev_branch *branches)
{
    rev_branch	*b;

    while ((b = branches)) {
	branches = b->next;
	rev_ent_free (b->ent);
	free (b);
    }
}

static void
rev_ref_free (rev_ref *ref)
{
    rev_ref	*r;

    while ((r = ref)) {
	ref = r->next;
	free (r);
    }
}

void
rev_list_free (rev_list *rl)
{
    rev_branch_free (rl->branches);
    rev_ref_free (rl->heads);
    rev_ref_free (rl->tags);
    free (rl);
}
