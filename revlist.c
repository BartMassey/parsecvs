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
rev_ref_add (rev_ref **list, rev_commit *commit, char *name, int head)
{
    rev_ref	*r;

    while (*list)
	list = &(*list)->next;
    r = calloc (1, sizeof (rev_ref));
    r->commit = commit;
    r->name = name;
    r->next = *list;
    r->head = head;
    *list = r;
}

void
rev_list_add_head (rev_list *rl, rev_commit *commit, char *name)
{
    rev_ref_add (&rl->heads, commit, name, 1);
}

void
rev_list_add_tag (rev_list *rl, rev_commit *commit, char *name)
{
    rev_ref_add (&rl->tags, commit, name, 0);
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
//    c->removing = a->removing | b->removing;
}

static void
rev_file_copy (rev_commit *src, rev_commit *dst)
{
    memcpy (dst->files, src->files,
	    (dst->nfiles = src->nfiles) * sizeof (rev_file *));
    dst->date = src->date;
    dst->commitid = src->commitid;
    dst->log = src->log;
//    dst->removing = src->removing;
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
	char	*d;
	int	i;

	d = ctime(&c->date);
	d[strlen(d)-1] = '\0';
	printf ("%c0x%x %s\n", c == m ? '>' : ' ',
		(int) c, d);
	for (i = 0; i < c->nfiles; i++) {
	    d = ctime(&c->files[i]->date);
	    d[strlen(d)-1] = '\0';
	    printf ("\t%s", d);
	    dump_number (c->files[i]->name, &c->files[i]->number);
	    printf ("\n");
	}
	printf ("\n");
	c = c->parent;
    }
}

static char *happy (rev_commit *prev, rev_commit *c)
{
    rev_file	*ef, *pf;
    int		pi;

    /*
     * Check for in-order dates
     */
    if (time_compare (prev->date, c->date) < 0)
	return "time out of order";
    
    if (c->nfiles) {
	ef = c->files[0];
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

static rev_commit *
rev_branch_merge (rev_commit *a, rev_commit *b)
{
    rev_commit	*ao = a, *bo = b;
    rev_commit	*head = NULL;
    rev_commit	**tail = &head;
    rev_commit	**patch = NULL;
    rev_commit	*c, *prev = NULL;
    rev_commit	*skip = NULL;
    char	*h;

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
	    rev_branch_mark_merged (a, b, c);
	    if (prev && (h = happy (prev, c)))
	    {
		*tail = c;
		printf ("not happy %s\n", h);
		rev_commit_dump ("a", ao, a);
		rev_commit_dump ("b", bo, b);
		rev_commit_dump ("c", head, c);
		abort ();
	    }
	    if (rev_commit_match (a, b)) {
		if (!rev_branch_find_merged (a, NULL))
		    rev_branch_mark_merged (a, NULL, c);
		if (!rev_branch_find_merged (b, NULL))
		    rev_branch_mark_merged (b, NULL, c);
		if (a->removing) a = a->parent;
		a = a->parent;
		if (b->removing) b = b->parent;
		b = b->parent;
	    } else {
		if (rev_commit_later (a, b)) {
		    if (!rev_branch_find_merged (a, NULL))
			rev_branch_mark_merged (a, NULL, c);
		    skip = b;
		    if (a->removing) a = a->parent;
		    a = a->parent;
		} else {
		    if (!rev_branch_find_merged (b, NULL))
			rev_branch_mark_merged (b, NULL, c);
		    skip = a;
		    if (b->removing) b = b->parent;
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
		if (prev)
		    c->date = prev->date;
		else
		    c->date = time (NULL);
		rev_branch_replace_merged (a, NULL, c);
		c->parent = parent;
//		c->removing = 1;
	    } else {
#if 1
		/*
		 * Make sure we land in-order on the new tree, creating
		 * a new node if necessary.
		 */
		if (prev)
		    while (c && time_compare (prev->date, c->date) < 0) {
			patch = &c->parent;
			c = c->parent;
		    }
#endif
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
	    if (prev)
		assert (time_compare (prev->date, c->date) >= 0);
	    if (a->removing) a = a->parent;
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

static int
rev_commit_validate (rev_commit *c)
{
    rev_commit	*p = c->parent;
    int		ei, pi;
    rev_file	*ef, *pf;

    if (!p)
	return 1;
    for (ei = 0; ei < c->nfiles; ei++) {
	ef = c->files[ei];
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
	if (h->next && h->next->commit != h->commit)
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

static rev_commit *
rev_branchpoint (rev_ref *r)
{
    rev_commit	*c;
    for (c = r->commit; c; c = c->parent)
	if (c->seen > 1)
	    break;
    return c;
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
rev_commit_file_depth (rev_commit *c, char *name)
{
    int	i;

    for (i = 0; i < c->nfiles; i++)
	if (c->files[i]->name == name)
	    return c->files[i]->number.c;
    return 100;
}

rev_list *
rev_list_merge (rev_list *a, rev_list *b)
{
    rev_list	*rl = calloc (1, sizeof (rev_list));
    rev_ref	*h, *ah, *bh, **hp;
    rev_commit	*c;
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
		fprintf (stderr, "can't order head %s %s\n",
			 h->name, h->next->name);
		fprintf (stderr, "a file: ");
		dump_number_file (stderr, a->heads->commit->files[0]->name,
				  &a->heads->commit->files[0]->number);
		fprintf (stderr, "\n");
		fprintf (stderr, "b file: ");
		dump_number_file (stderr, b->heads->commit->files[0]->name,
				  &b->heads->commit->files[0]->number);
		fprintf (stderr, "\n");
		hp = &h->next;
	    } else {
		*hp = h->next;
		h->next = h->next->next;
		(*hp)->next = h;
		hp = &rl->heads;
	    }
	} else {
	    hp = &h->next;
	}
    }
#if 0
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
#endif    
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
	bt = rev_find_tag (b, at->name);
	c = rev_branch_find_merged (at->commit, bt ? bt->commit : NULL);
	if (c)
	    rev_list_add_tag (rl, c, at->name);
    }
    for (bt = b->tags; bt; bt = bt->next) {
	at = rev_find_tag (a, bt->name);
	if (!at) {
	    c = rev_branch_find_merged (bt->commit, NULL);
	    if (c)
		rev_list_add_tag (rl, c, bt->name);
	}
    }
    /*
     * Compute 'tail' values
     */
    rev_list_set_tail (rl);
#if 0
    /*
     * Validate resulting tree by ensuring
     * that at each branch point, all but one of the
     * incoming branches has a change in depth in the first
     * file in the branch
     */
    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	for (c = h->commit; c && c->parent; c = c->parent) {
	    if (c->seen < c->parent->seen) {
		int	ed = rev_commit_file_depth (c,
						 c->files[0]->name);
		int	epd = rev_commit_file_depth (c->parent,
						 c->files[0]->name);
		if (epd >= ed && c->nfiles == c->parent->nfiles) {
		    if (c->parent->used > 0) {
			dump_rev_graph_begin ();
			dump_rev_graph_nodes (rl, "new");
			dump_rev_graph_nodes (a, "a");
			dump_rev_graph_nodes (b, "b");
			dump_rev_graph_end ();
			fflush (stdout);
			abort ();
		    }
		    c->parent->used = 1;
		    c->parent->user = c;
		}
	    }
	    if (c->tail)
		break;
	}
    }
#endif
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
