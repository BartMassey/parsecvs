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

    while (*list)
	list = &(*list)->next;
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
    rev_branch	**p;

    for (p = &rl->branches; *p; p = &(*p)->next);
    b->next = *p;
    b->ent = ent;
    *p = b;
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

/*
 * We keep all file lists in a canonical sorted order,
 * first by latest date and then by the address of the rev_file object
 * (which are always unique)
 */

int
rev_file_later (rev_file *af, rev_file *bf)
{
    long	t;

    /*
     * When merging file lists, we should never see the same
     * object in both trees
     */
    assert (af != bf);

    t = time_compare (af->date, bf->date);

    if (t > 0)
	return 1;
    if (t < 0)
	return 0;
    if ((uintptr_t) af > (uintptr_t) bf)
	return 1;
    return 0;
}

int
rev_ent_later (rev_ent *a, rev_ent *b)
{
    long	t;

    assert (a != b);
    t = time_compare (a->date, b->date);
    if (t > 0)
	return 1;
    if (t < 0)
	return 0;
    if ((uintptr_t) a > (uintptr_t) b)
	return 1;
    return 0;
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
    int		an = a->nfiles;
    int		bn = b->nfiles;

    while (ai < an || bi < bn) {
	if (ai < an && bi < bn) {
	    af = a->files[ai];
	    bf = b->files[bi];
	    if (rev_file_later (af, bf)) {
		ef = af;
		ai++;
	    } else {
		ef = bf;
		bi++;
	    }
	} else if (ai < an) {
	    ef = a->files[ai];
	    ai++;
	} else {
	    ef = b->files[bi];
	    bi++;
	}
	e->files[ei++] = ef;
    }
    e->nfiles = ei;
    if (rev_ent_later (a, b)) {
	e->date = a->date;
	e->commitid = a->commitid;
	e->log = a->log;
    } else {
	e->date = b->date;
	e->commitid = b->commitid;
	e->log = b->log;
    }
}

static void
rev_file_copy (rev_ent *src, rev_ent *dst)
{
    memcpy (dst->files, src->files,
	    (dst->nfiles = src->nfiles) * sizeof (rev_file *));
    dst->date = src->date;
    dst->commitid = src->commitid;
    dst->log = src->log;
}

/*
 * Commits further than 60 minutes apart are assume to be different
 */
static int
commit_time_close (time_t a, time_t b)
{
    long	diff = a - b;
    if (diff < 0) diff = -diff;
    if (diff < 60 * 60)
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
    if (a->commitid && b->commitid)
	return a->commitid == b->commitid;
    if (a->commitid || b->commitid)
	return 0;
    if (!commit_time_close (a->date, b->date))
	return 0;
    if (a->log != b->log)
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

static void
rev_ent_dump (char *title, rev_ent *e, rev_ent *m)
{
    printf ("\n%s\n", title);
    while (e) {
	char	*d;
	int	i;

	d = ctime(&e->date);
	d[strlen(d)-1] = '\0';
	printf ("%c0x%x %s\n", e == m ? '>' : ' ',
		(int) e, d);
	for (i = 0; i < e->nfiles; i++) {
	    d = ctime(&e->files[i]->date);
	    d[strlen(d)-1] = '\0';
	    printf ("\t%s", d);
	    dump_number (e->files[i]->name, &e->files[i]->number);
	    printf ("\n");
	}
	printf ("\n");
	e = e->parent;
    }
}

static char *happy (rev_ent *prev, rev_ent *e)
{
    rev_file	*ef, *pf;
    int		pi;

    /*
     * Check for in-order dates
     */
    if (time_compare (prev->date, e->date) < 0)
	return "time out of order";
    
    if (e->nfiles) {
	ef = e->files[0];
	/* Make sure at least the leading file stays on the same cvs branch */
	for (pi = 0; pi < prev->nfiles; pi++) {
	    pf = prev->files[pi];
	    if (pf->name == ef->name) {
		cvs_number	pn;
		pn = pf->number;
		while (pn.c >= 2) {
		    if (cvs_same_branch (&pn, &ef->number))
			break;
		    pn.c -= 2;
		}
		if (!pn.c) {
		    if (!cvs_is_vendor (&ef->number) && !cvs_is_vendor (&pf->number))
			return "leading file leaves branch";
		}
		break;

	    }
	}
    }
    return 0;
}

static rev_ent *
rev_branch_merge (rev_ent *a, rev_ent *b)
{
    rev_ent	*ao = a, *bo = b;
    rev_ent	*head = NULL;
    rev_ent	**tail = &head;
    rev_ent	**patch = NULL;
    rev_ent	*e, *prev = NULL;
    rev_ent	*skip = NULL;
    char	*h;

    while (a && b)
    {
	e = rev_branch_find_merged (a, b);
	if (e)
	{
//	    printf ("0x%x + 0x%x == 0x%x\n", a, b, e);
	    a = b = NULL;
	}
	else
	{
	    e = calloc (1, sizeof (rev_ent) +
			(a->nfiles + b->nfiles) * sizeof (rev_file *));
//	    printf ("0x%x + 0x%x -> 0x%x\n", a, b, e);
	    rev_file_merge (a, b, e);
	    rev_branch_mark_merged (a, b, e);
	    if (prev && (h = happy (prev, e)))
	    {
		*tail = e;
		printf ("not happy %s\n", h);
		rev_ent_dump ("a", ao, a);
		rev_ent_dump ("b", bo, b);
		rev_ent_dump ("e", head, e);
		abort ();
	    }
	    if (rev_ent_match (a, b)) {
		if (!rev_branch_find_merged (a, NULL))
		    rev_branch_mark_merged (a, NULL, e);
		if (!rev_branch_find_merged (b, NULL))
		    rev_branch_mark_merged (b, NULL, e);
		a = a->parent;
		b = b->parent;
	    } else {
		if (rev_ent_later (a, b)) {
		    if (!rev_branch_find_merged (a, NULL))
			rev_branch_mark_merged (a, NULL, e);
		    skip = b;
		    a = a->parent;
		} else {
		    if (!rev_branch_find_merged (b, NULL))
			rev_branch_mark_merged (b, NULL, e);
		    skip = a;
		    b = b->parent;
		}
	    }
	}
	*tail = e;
	tail = &e->parent;
	prev = e;
    }
    if (!a)
	a = b;
    while (a)
    {
	e = NULL;
	if (a != skip) {
	    e = rev_branch_find_merged (a, NULL);
	    /*
	     * Make sure we land in-order on the new tree, creating
	     * a new node if necessary.
	     */
	    if (prev)
		while (e && time_compare (prev->date, e->date) < 0) {
		    patch = &e->parent;
		    e = e->parent;
		}
	}
	if (e)
	{
//	    printf ("0x%x == 0x%x\n", a, e);
	    if (prev)
		assert (time_compare (prev->date, e->date) >= 0);
	    a = NULL;
	}
	else
	{
	    e = calloc (1, sizeof (rev_ent) + a->nfiles * sizeof (rev_file *));
//	    printf ("0x%x -> 0x%x\n", a, e);
	    rev_file_copy (a, e);
	    rev_branch_mark_merged (a, NULL, e);
	    if (prev)
		assert (time_compare (prev->date, e->date) >= 0);
	    a = a->parent;
	    if (patch) {
		*patch = e;
	    }
	}
	*tail = e;
	tail = &e->parent;
	prev = e;
    }
    return head;
}

static rev_ent *
rev_branch_copy (rev_ent *a)
{
    return rev_branch_merge (a, NULL);
}

static void
rev_list_unique_add_branch (rev_list *rl, rev_ent *ent)
{
    rev_branch *branch;
    for (branch = rl->branches; branch; branch = branch->next)
	if (ent == branch->ent)
	    return;
    rev_list_add_branch (rl, ent);
}

static int
rev_ent_validate (rev_ent *e)
{
    rev_ent	*p = e->parent;
    int		ei, pi;
    rev_file	*ef, *pf;

    if (!p)
	return 1;
    for (ei = 0; ei < e->nfiles; ei++) {
	ef = e->files[ei];
	for (pi = 0; pi < p->nfiles; pi++) {
	    pf = p->files[pi];
	    if (ef->name == pf->name) {
		if (!cvs_same_branch (&ef->number, &pf->number)) {
		    if ((cvs_is_vendor (&ef->number) && cvs_is_trunk (&pf->number)) ||
			(cvs_is_vendor (&pf->number) && cvs_is_trunk (&ef->number)))
			break;
		    return 0;
		}
	    }
	}
    }
    return 1;
}

static int
head_loc (rev_ref *a, rev_list *rl)
{
    int	i = 0;
    rev_ref	*h;

    for (h = rl->heads; h; h = h->next) {
	if (h->name == a->name)
	    return i;
	i++;
    }
    return -1;
}

static int
head_order (rev_ref *a, rev_ref *b, rev_list *rl)
{
    int	al = head_loc (a, rl);
    int bl = head_loc (b, rl);

    if (al == -1 || bl == -1)
	return 0;
    return al - bl;
}

static rev_ent *
rev_branchpoint (rev_ref *r)
{
    rev_ent	*e;
    for (e = r->ent; e; e = e->parent)
	if (e->seen > 1)
	    break;
    return e;
}

void
rev_list_set_tail (rev_list *rl)
{
    rev_branch	*branch;
    rev_ent	*e;
    int		tail = 1;

    for (branch = rl->branches; branch; branch = branch->next) {
	if (branch->ent && branch->ent->seen) {
	    branch->tail = tail;
	    tail = 0;
	}
	for (e = branch->ent; e; e = e->parent) {
	    if (tail && e->parent && e->seen < e->parent->seen) {
		e->tail = 1;
		tail = 0;
	    }
	    e->seen++;
	}
    }
}

rev_list *
rev_list_merge (rev_list *a, rev_list *b)
{
    rev_list	*rl = calloc (1, sizeof (rev_list));
    rev_ref	*h, *ah, *bh, **hp;
    rev_ent	*e;
    rev_ref	*at, *bt;

    rl->watch = a->watch || b->watch;
    /* add all of the head refs */
    for (ah = a->heads; ah; ah = ah->next)
	rev_list_add_head (rl, NULL, ah->name);
    for (bh = b->heads; bh; bh = bh->next)
	if (!rev_find_head (rl, bh->name))
	    rev_list_add_head (rl, NULL, bh->name);
#if 0
    /*
     * Topologically sort heads
     */
    for (hp = &rl->heads; (h = *hp);)
    {
	int	ao, bo;
	if (!h->next)
	    break;
	ao = head_order (h, h->next, a);
	bo = head_order (h, h->next, b);
	if (ao > 0 || bo > 0) {
	    if (ao < 0 || bo < 0) {
		fprintf (stderr, "can't sort heads\n");
		break;
	    }
	    *hp = h->next;
	    h->next = h->next->next;
	    (*hp)->next = h;
	    hp = &rl->heads;
	} else {
	    hp = &h->next;
	}
    }
    /*
     * validate topo sort
     */
    for (ah = a->heads, h = rl->heads; ah && h;)
    {
	if (ah->name == h->name)
	    ah = ah->next;
	h = h->next;
    }
    assert (!ah);
    for (bh = b->heads, h = rl->heads; bh && h;)
    {
	if (bh->name == h->name)
	    bh = bh->next;
	h = h->next;
    }
    assert (!bh);
#endif    
    /*
     * Merge common branches
     */
//    fprintf (stderr, "merge branches...\n");
    for (h = rl->heads; h; h = h->next) {
	if (h->ent)
	    continue;
	ah = rev_find_head (a, h->name);
	bh = rev_find_head (b, h->name);
//	fprintf (stderr, "\tmerge branch %s\n", h->name);
	if (ah && bh)
	    h->ent = rev_branch_merge (ah->ent, bh->ent);
	if (h->ent)
	    rev_list_unique_add_branch (rl, h->ent);
    }
    for (h = rl->heads; h; h = h->next) {
	if (h->ent)
	    continue;
	if ((ah = rev_find_head (a, h->name)))
	    h->ent = rev_branch_copy (ah->ent);
	else if ((bh = rev_find_head (b, h->name)))
	    h->ent = rev_branch_copy (bh->ent);
	if (h->ent)
	    rev_list_unique_add_branch (rl, h->ent);
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
    rev_list_set_tail (rl);
    for (h = rl->heads; h; h = h->next) {
	rev_ent	*ht, *at = NULL, *bt = NULL;

/*	if (h->ent->used)
	continue; */
	h->ent->used = 1;
	ht = rev_branchpoint (h);
	if (!ht)
	    continue;
	ah = rev_find_head (a, h->name);
	bh = rev_find_head (b, h->name);
	if (ah)
	    at = rev_branchpoint (ah);
	if (bh)
	    bt = rev_branchpoint (bh);

	if (!at && !bt)
	    continue;
	
	if ((at && ht->date == at->date) ||
	    (bt && ht->date == bt->date))
	    continue;

	if (0) {
	    dump_rev_graph_begin ();
	    dump_rev_graph_nodes (rl, "new");
	    dump_rev_graph_nodes (a, "a");
	    dump_rev_graph_nodes (b, "b");
	    dump_rev_graph_end ();
	} else {
	    printf ("%s\n", h->name);
	    rev_ent_dump ("h", h->ent, ht);
	    rev_ent_dump ("a", ah->ent, at);
	    rev_ent_dump ("b", bh->ent, bt);
	}
	exit (0);
    }
    rev_branch_discard_merged ();
    return rl;
}

/*
 * Icky. each file revision may be referenced many times in a single
 * tree. When freeing the tree, queue the file objects to be deleted
 * and clean them up afterwards
 */

static rev_file *rev_files;

static void
rev_file_mark_for_free (rev_file *f)
{
    if (f->name) {
	f->name = NULL;
	f->link = rev_files;
	rev_files = f;
    }
}

static void
rev_file_free_marked (void)
{
    rev_file	*f, *n;

    for (f = rev_files; f; f = n)
    {
	n = f->link;
	free (f);
    }
    rev_files = NULL;
}

rev_file *
rev_file_rev (char *name, cvs_number *n, time_t date)
{
    rev_file	*f = calloc (1, sizeof (rev_file));

    f->name = name;
    f->number = *n;
    f->date = date;
    return f;
}

void
rev_file_free (rev_file *f)
{
    free (f);
}

static void
rev_ent_free (rev_ent *ent, int free_files)
{
    rev_ent	*e;

    while ((e = ent)) {
	ent = e->parent;
	if (--e->seen == 0)
	{
	    if (free_files) {
		int i;
		for (i = 0; i < e->nfiles; i++)
		    rev_file_mark_for_free (e->files[i]);
	    }
	    free (e);
	}
    }
}

void
rev_branch_free (rev_branch *branches, int free_files)
{
    rev_branch	*b;

    while ((b = branches)) {
	branches = b->next;
	rev_ent_free (b->ent, free_files);
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
rev_list_free (rev_list *rl, int free_files)
{
    rev_branch_free (rl->branches, free_files);
    rev_ref_free (rl->heads);
    rev_ref_free (rl->tags);
    if (free_files)
	rev_file_free_marked ();
    free (rl);
}
