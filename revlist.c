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
rev_ref_add (rev_ref **list, rev_commit *commit, char *name, int degree, int head)
{
    rev_ref	*r;

    while (*list)
	list = &(*list)->next;
    r = calloc (1, sizeof (rev_ref));
    r->commit = commit;
    r->name = name;
    r->next = *list;
    r->degree = degree;
    r->head = head;
    *list = r;
}

void
rev_list_add_head (rev_list *rl, rev_commit *commit, char *name, int degree)
{
    rev_ref_add (&rl->heads, commit, name, degree, 1);
}

void
rev_list_add_tag (rev_list *rl, rev_commit *commit, char *name, int degree)
{
    rev_ref_add (&rl->tags, commit, name, degree, 0);
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
rev_commit_later (rev_commit *a, rev_commit *b)
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
rev_file_merge (rev_commit *a, rev_commit *b, rev_commit *c)
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
	c->files[ei++] = ef;
    }
    c->nfiles = ei;
    if (rev_commit_later (a, b)) {
	c->date = a->date;
	c->commitid = a->commitid;
	c->log = a->log;
    } else {
	c->date = b->date;
	c->commitid = b->commitid;
	c->log = b->log;
    }
}

static void
rev_file_copy (rev_commit *src, rev_commit *dst)
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
rev_commit_match (rev_commit *a, rev_commit *b)
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
 * Find an existing rev_commit which has both a and b in common
 */

#define HASH_SIZE	9013

typedef struct _rev_merged {
    struct _rev_merged	*next;
    rev_commit		*a, *b, *c;
} rev_merged;

static rev_merged	*merged_buckets[HASH_SIZE];

static unsigned long
rev_merged_hash (rev_commit *a, rev_commit *b)
{
    unsigned long	h = (unsigned long) a ^ (unsigned long) b;

    return h % HASH_SIZE;
}

static rev_commit *
rev_branch_find_merged (rev_commit *a, rev_commit *b)
{
    rev_merged	*m;

    for (m = merged_buckets[rev_merged_hash(a,b)]; m; m = m->next)
	if (m->a == a && m->b == b)
	    return m->c;
    return NULL;
}

static void
rev_branch_mark_merged (rev_commit *a, rev_commit *b, rev_commit *c)
{
    rev_merged	**bucket = &merged_buckets[rev_merged_hash(a,b)];
    rev_merged	*m = calloc (1, sizeof (rev_merged));

    m->a = a;
    m->b = b;
    m->c = c;
    m->next = *bucket;
    *bucket = m;
}

static void
rev_branch_replace_merged (rev_commit *a, rev_commit *b, rev_commit *c)
{
    rev_merged	*m;

    for (m = merged_buckets[rev_merged_hash(a,b)]; m; m = m->next)
	if (m->a == a && m->b == b)
	    m->c = c;
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
rev_commit_dump (char *title, rev_commit *c, rev_commit *m)
{
    printf ("\n%s\n", title);
    while (c) {
	int	i;

	printf ("%c0x%x %s\n", c == m ? '>' : ' ',
		(int) c, ctime_nonl (&c->date));
	for (i = 0; i < c->nfiles; i++) {
	    printf ("\t%s", ctime_nonl (&c->files[i]->date));
	    dump_number (c->files[i]->name, &c->files[i]->number);
	    printf ("\n");
	}
	printf ("\n");
	c = c->parent;
    }
}

static rev_commit *
rev_branch_merge (rev_commit *a, rev_commit *b)
{
    rev_commit	*ao = a, *bo = b;
    rev_commit	*head = NULL;
    rev_commit	**tail = &head;
    rev_commit	**patch = NULL;
    rev_commit	*c, *prev = NULL;
    rev_commit	*skip = NULL;
    rev_commit	*preva = NULL, *prevb = NULL;
    time_t	alate, blate, late;

    alate = time_now;
    blate = time_now;
    late = time_now;
    while (a && b)
    {
	c = rev_branch_find_merged (a, b);
	if (c)
	{
//	    printf ("0x%x + 0x%x == 0x%x\n", a, b, c);
	    a = b = NULL;
	}
	else
	{
	    c = calloc (1, sizeof (rev_commit) +
			(a->nfiles + b->nfiles) * sizeof (rev_file *));
//	    printf ("0x%x + 0x%x -> 0x%x\n", a, b, c);
	    rev_file_merge (a, b, c);
	    if (time_compare (alate, blate) < 0)
		late = alate;
	    else
		late = blate;
	    rev_branch_mark_merged (a, b, c);
	    if (prev)
		assert (commit_time_close (late, prev->date));
	    if (time_compare (late, c->date) < 0)
	    {
		if (!head)
		    head = c;
		printf ("not happy\n");
		rev_commit_dump ("a", ao, a);
		rev_commit_dump ("b", bo, b);
		rev_commit_dump ("c", head, c);
		abort ();
	    }
	    preva = a;
	    prevb = b;
	    if (rev_commit_match (a, b)) {
		if (!rev_branch_find_merged (a, NULL))
		    rev_branch_mark_merged (a, NULL, c);
		if (!rev_branch_find_merged (b, NULL))
		    rev_branch_mark_merged (b, NULL, c);
		alate = a->date;
		a = a->parent;
		blate = b->date;
		b = b->parent;
	    } else {
		if (rev_commit_later (a, b)) {
		    if (!rev_branch_find_merged (a, NULL))
			rev_branch_mark_merged (a, NULL, c);
		    skip = b;
		    alate = a->date;
		    a = a->parent;
		} else {
		    if (!rev_branch_find_merged (b, NULL))
			rev_branch_mark_merged (b, NULL, c);
		    skip = a;
		    blate = b->date;
		    b = b->parent;
		}
	    }
	}
	*tail = c;
	tail = &c->parent;
	prev = c;
    }
    if (!a)
	a = b;
    while (a)
    {
	c = NULL;
	if (a != skip) {
	    c = rev_branch_find_merged (a, NULL);
	    while (c && time_compare (c->date, late) > 0)
		c = c->parent;
	    /*
	     * If this is pointing back into a merged entry,
	     * make sure there is at least one node on this
	     * branch without the added files.
	     * Replace the merge entry.
	     */
	    if (c && c->nfiles > a->nfiles && !head)
	    {
		rev_commit	*parent = c->parent;
		c = calloc (1, sizeof (rev_commit) + a->nfiles *
			    sizeof (rev_file *));
//		printf ("0x%x => 0x%x\n", a, c);
		rev_file_copy (a, c);
		rev_branch_replace_merged (a, NULL, c);
		c->parent = parent;
	    }
	    if (c)
	    {
		assert (time_compare (late, c->date) >= 0);
		if (c->parent)
		    assert (time_compare (c->date, c->parent->date) >= 0);
	    }
	}
	if (c)
	{
//	    printf ("0x%x == 0x%x\n", a, c);
	    if (prev)
		assert (time_compare (prev->date, c->date) >= 0);
	    a = NULL;
	}
	else
	{
	    c = calloc (1, sizeof (rev_commit) + a->nfiles * sizeof (rev_file *));
//	    printf ("0x%x -> 0x%x\n", a, c);
	    rev_file_copy (a, c);
	    rev_branch_mark_merged (a, NULL, c);
	    if (prev) {
		assert (time_compare (prev->date, c->date) >= 0);
	    }
	    late = a->date;
	    a = a->parent;
	    if (patch)
		*patch = c;
	}
	*tail = c;
	tail = &c->parent;
	prev = c;
    }
    return head;
}

static rev_commit *
rev_branch_copy (rev_commit *a)
{
    return rev_branch_merge (a, NULL);
}

void
rev_list_set_tail (rev_list *rl)
{
    rev_ref	*head;
    rev_commit	*c;
    int		tail;

    for (head = rl->heads; head; head = head->next) {
	tail = 1;
	if (head->commit && head->commit->seen) {
	    head->tail = tail;
	    tail = 0;
	}
	for (c = head->commit; c; c = c->parent) {
	    if (tail && c->parent && c->seen < c->parent->seen) {
		c->tail = 1;
		tail = 0;
	    }
	    c->seen++;
	}
    }
}

static int
rev_ref_len (rev_ref *r)
{
    int	l = 0;
    while (r) {
	l++;
	r = r->next;
    }
    return l;
}

static rev_ref *
rev_ref_sel (rev_ref *r, int len)
{
    rev_ref	*head, **tail;
    rev_ref	*a = r;
    rev_ref	*b;
    int		alen = len / 2;
    int		blen = len - alen;
    int		i;

    if (len <= 1)
	return r;

    /*
     * split
     */
    for (i = 0; i < alen - 1; i++)
	r = r->next;
    b = r->next;
    r->next = 0;
    /*
     * recurse
     */
    a = rev_ref_sel (a, alen);
    b = rev_ref_sel (b, blen);
    /*
     * merge
     */
    tail = &head;
    while (a && b) {
	if (a->degree < b->degree) {
	    *tail = a;
	    a = a->next;
	} else {
	    *tail = b;
	    b = b->next;
	}
	tail = &(*tail)->next;
    }
    /*
     * paste
     */
    if (a)
	*tail = a;
    else
	*tail = b;
    /*
     * done
     */
    return head;
}

static rev_ref *
rev_ref_sel_sort (rev_ref *r)
{
    rev_ref	*s;

    r = rev_ref_sel (r, rev_ref_len (r));
    for (s = r; s && s->next; s = s->next) {
	assert (s->degree <= s->next->degree);
    }
    return r;
}

rev_list *
rev_list_merge (rev_list *a, rev_list *b)
{
    rev_list	*rl = calloc (1, sizeof (rev_list));
    rev_ref	*h, *ah, *bh;
    rev_commit	*c;
    rev_ref	*at, *bt;

    rl->watch = a->watch || b->watch;
    /* add all of the head refs */
    for (ah = a->heads; ah; ah = ah->next) {
	int	degree = ah->degree;
	bh = rev_find_head (b, ah->name);
	if (bh && bh->degree > degree)
	    degree = bh->degree;
	rev_list_add_head (rl, NULL, ah->name, degree);
    }
    for (bh = b->heads; bh; bh = bh->next)
	if (!rev_find_head (rl, bh->name))
	    rev_list_add_head (rl, NULL, bh->name, bh->degree);
    b->heads = rev_ref_sel_sort (b->heads);
    /*
     * Merge common branches
     */
//    fprintf (stderr, "merge branches...\n");
    for (h = rl->heads; h; h = h->next) {
	if (h->commit)
	    continue;
	ah = rev_find_head (a, h->name);
	bh = rev_find_head (b, h->name);
//	fprintf (stderr, "\tmerge branch %s\n", h->name);
	if (ah && bh)
	    h->commit = rev_branch_merge (ah->commit, bh->commit);
    }
    for (h = rl->heads; h; h = h->next) {
	if (h->commit)
	    continue;
	if ((ah = rev_find_head (a, h->name)))
	    h->commit = rev_branch_copy (ah->commit);
	else if ((bh = rev_find_head (b, h->name)))
	    h->commit = rev_branch_copy (bh->commit);
    }
    /*
     * Find tag locations
     */
    for (at = a->tags; at; at = at->next) {
	int	degree;
	bt = rev_find_tag (b, at->name);
	c = rev_branch_find_merged (at->commit, bt ? bt->commit : NULL);
	degree = at->degree;
	if (bt && bt->degree > degree)
	    degree = bt->degree;
	if (c)
	    rev_list_add_tag (rl, c, at->name, degree);
    }
    for (bt = b->tags; bt; bt = bt->next) {
	at = rev_find_tag (a, bt->name);
	if (!at) {
	    c = rev_branch_find_merged (bt->commit, NULL);
	    if (c)
		rev_list_add_tag (rl, c, bt->name, bt->degree);
	}
    }
    /*
     * Compute 'tail' values
     */
    rev_list_set_tail (rl);
    rev_branch_discard_merged ();
    rev_list_validate (rl);
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
rev_commit_free (rev_commit *commit, int free_files)
{
    rev_commit	*c;

    while ((c = commit)) {
	commit = c->parent;
	if (--c->seen == 0)
	{
	    if (free_files) {
		int i;
		for (i = 0; i < c->nfiles; i++)
		    rev_file_mark_for_free (c->files[i]);
	    }
	    free (c);
	}
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
rev_head_free (rev_ref *head, int free_files)
{
    rev_ref	*h;

    while ((h = head)) {
	head = h->next;
	rev_commit_free (h->commit, free_files);
	free (h);
    }
}

void
rev_list_free (rev_list *rl, int free_files)
{
    rev_head_free (rl->heads, free_files);
    rev_ref_free (rl->tags);
    if (free_files)
	rev_file_free_marked ();
    free (rl);
}

void
rev_list_validate (rev_list *rl)
{
    rev_ref	*h;
    rev_commit	*c;
    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	for (c = h->commit; c && c->parent; c = c->parent) {
	    if (c->tail)
		break;
	    assert (time_compare (c->date, c->parent->date) >= 0);
	}
    }
}
