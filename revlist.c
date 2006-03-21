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

static void
rev_commit_dump (FILE *f, char *title, rev_commit *c, rev_commit *m)
{
    fprintf (f, "\n%s\n", title);
    while (c) {
	int	i;

	fprintf (f, "%c0x%x %s\n", c == m ? '>' : ' ',
		(int) c, ctime_nonl (&c->date));
	for (i = 0; i < c->nfiles; i++) {
	    fprintf (f, "\t%s", ctime_nonl (&c->files[i]->date));
	    dump_number_file (f, c->files[i]->name, &c->files[i]->number);
	    fprintf (f, "\n");
	}
	fprintf (f, "\n");
	c = c->parent;
    }
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

static int
rev_list_count (rev_list *head)
{
    int	count = 0;
    while (head) {
	count++;
	head = head->next;
    }
    return count;
}

static int
rev_commit_date_compare (const void *av, const void *bv)
{
    const rev_commit	*a = *(const rev_commit **) av;
    const rev_commit	*b = *(const rev_commit **) bv;
    int			t;

    /*
     * NULL entries sort last
     */
    if (!a) {
	if (!b)
	    return 0;
	return 1;
    }
    if (!b)
	return -1;
    /*
     * Newest entries sort first
     */
    t = -time_compare (a->date, b->date);
    if (t)
	return t;
    /*
     * Ensure total order by ordering based on file address
     */
    if ((uintptr_t) a->files[0] > (uintptr_t) b->files[0])
	return -1;
    if ((uintptr_t) a->files[0] < (uintptr_t) b->files[0])
	return 1;
    return 0;
}

static int
rev_commit_date_sort (rev_commit **commits, int ncommit)
{
    qsort (commits, ncommit, sizeof (rev_commit *),
	   rev_commit_date_compare);
    /*
     * Trim off NULL entries
     */
    while (ncommit && !commits[ncommit-1])
	ncommit--;
    return ncommit;
}

static int
rev_commit_has_file (rev_commit *c, rev_file *f)
{
    int	n;

    for (n = 0; n < c->nfiles; n++)
	if (c->files[0] == f)
	    return 1;
    return 0;
}

static rev_file *
rev_commit_find_file (rev_commit *c, char *name)
{
    int	n;

    for (n = 0; n < c->nfiles; n++)
	if (c->files[n]->name == name)
	    return c->files[n];
    return NULL;
}

static rev_commit *
rev_commit_build (rev_commit **commits, int ncommit)
{
    int	n, nfile;
    rev_commit	*commit;

    commit = calloc (1, sizeof (rev_commit) +
		     ncommit * sizeof (rev_file *));

    commit->date = commits[0]->date;
    commit->commitid = commits[0]->commitid;
    commit->log = commits[0]->log;
    nfile = 0;
    for (n = 0; n < ncommit; n++)
	if (commits[n]->nfiles > 0)
	    commit->files[nfile++] = commits[n]->files[0];
    commit->nfiles = nfile;
    return commit;
}

static rev_commit *
rev_commit_locate (rev_list *rl, rev_commit **commits, int ncommit)
{
    rev_ref	*h;
    rev_commit	*c;
    int		n;
    int		nseen, nfile;
    int		best = 0;
    rev_commit	*best_commit = NULL;
    rev_ref	*best_ref = NULL;
    rev_file	*f, *cf;

    for (n = 0; n < ncommit; n++)
	commits[n]->seen = 0;
    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	for (c = h->commit; c; c = c->parent) {
	    nseen = 0;
	    nfile = 0;
	    for (n = 0; n < ncommit; n++) {
		if (commits[n]->nfiles) {
		    ++nfile;
		    cf = commits[n]->files[0];
		    f = rev_commit_find_file (c, cf->name);
		    if (f &&
			cvs_number_compare (&cf->number, &f->number) == 0)
		    {
			commits[n]->seen = 1;
			++nseen;
		    }
		}
	    }
	    if (nseen == nfile)
		return c;
	    if (nseen > best) {
		best = nseen;
		best_commit = c;
		best_ref = h;
	    }
	    if (c->tail)
		break;
	}
    }
    fprintf (stderr, "Cannot locate commit.\n");
    fprintf (stderr, "\tbranch: %s\n", best_ref->name);
    fprintf (stderr, "\tlog: "); dump_log (stderr, best_commit->log);
    fprintf (stderr, "\n\tdate: %s\n", ctime_nonl (&best_commit->date));
    for (n = 0; n < ncommit; n++) {
	if (commits[n]->nfiles) {
	    cf = commits[n]->files[0];
	    f = rev_commit_find_file (best_commit, cf->name);
	    if (!f) {
		fprintf (stderr, "\t%s: missing", cf->name);
		dump_number_file (stderr, " want",
				  &cf->number);
		fprintf (stderr, "\n");
	    }
	    else if (cvs_number_compare (&cf->number, &f->number) != 0)
	    {
		fprintf (stderr, "\t%s: ", cf->name);
		dump_number_file (stderr, "found",
				  &commits[n]->files[0]->number);
		dump_number_file (stderr, " want",
				  &cf->number);
	    }
	}
    }
    return NULL;
}

static rev_commit *
rev_branch_merge (rev_ref **branches, int nbranch, rev_list *rl)
{
    int		nlive;
    int		n;
    rev_commit	*prev = NULL;
    rev_commit	*head, **tail = &head;
    rev_commit	**commits = calloc (nbranch, sizeof (rev_commit *));
    rev_commit	*commit;

    /*
     * Compute number of branches with remaining entries
     */
    nlive = 0;
    for (n = 0; n < nbranch; n++)
	if (!branches[n]->tail)
	    nlive++;
    /*
     * Initialize commits to head of each branch
     */
    for (n = 0; n < nbranch; n++)
	commits[n] = branches[n]->commit;
    /*
     * Walk down branches until each one has merged with the
     * parent branch
     */
    while (nlive > 0 && nbranch > 0) {
	nbranch = rev_commit_date_sort (commits, nbranch);

	/*
	 * Construct current commit
	 */
	commit = rev_commit_build (commits, nbranch);

	/*
	 * Step each branch
	 */
	for (n = nbranch - 1; n >= 0; n--) {
	    if (n == 0 || rev_commit_match (commits[0], commits[n])) {
		if (commits[n]->tail || commits[n]->parent == NULL)
		    nlive--;
		commits[n] = commits[n]->parent;
	    }
	}
	    
	*tail = commit;
	tail = &commit->parent;
	prev = commit;
    }
    /*
     * Connect to parent branch
     */
    nbranch = rev_commit_date_sort (commits, nbranch);
    if (nbranch)
    {
	*tail = rev_commit_locate (rl, commits, nbranch);
//	assert (*tail);
	if (!*tail) {
	    fprintf (stderr, "Lost branch point\n");
	    *tail = rev_commit_build (commits, nbranch);
	}
	else if (prev)
	    prev->tail = 1;
    }
    free (commits);
    return head;
}

/*
 * Locate position in tree cooresponding to specific tag
 */
static rev_commit *
rev_tag_search (rev_ref **tags, int ntag, rev_list *rl)
{
    rev_commit	**commits = calloc (ntag, sizeof (rev_commit *));
    rev_commit	*commit;
    int		n;

    for (n = 0; n < ntag; n++)
	commits[n] = tags[n]->commit;
    
    ntag = rev_commit_date_sort (commits, ntag);
    commit = rev_commit_locate (rl, commits, ntag);
    if (!commit)
	commit = rev_commit_build (commits, ntag);
    free (commits);
    return commit;
}

rev_list *
rev_list_merge (rev_list *head)
{
    int		count = rev_list_count (head);
    rev_list	*rl = calloc (1, sizeof (rev_list));
    rev_list	*l;
    rev_ref	*lh, *h;
    rev_ref	*lt, *t;
    rev_ref	**refs = calloc (count, sizeof (rev_commit *));
    int		nref;

    /*
     * Find all of the heads across all of the incoming trees
     * Yes, this is currently very inefficient
     */
    for (l = head; l; l = l->next) {
	for (lh = l->heads; lh; lh = lh->next) {
	    h = rev_find_head (rl, lh->name);
	    if (!h)
		rev_list_add_head (rl, NULL, lh->name, lh->degree);
	    else if (lh->degree > h->degree)
		h->degree = lh->degree;
	}
    }
    /*
     * Sort by degree so that finding branch points always works
     */
    rl->heads = rev_ref_sel_sort (rl->heads);
    for (h = rl->heads; h; h = h->next)
	fprintf (stderr, "head %s(%d)\n", h->name, h->degree);
    /*
     * Merge common branches
     */
    for (h = rl->heads; h; h = h->next) {
	fprintf (stderr, "merge head %s\n", h->name);
	/*
	 * Locate branch in every tree
	 */
	nref = 0;
	for (l = head; l; l = l->next) {
	    lh = rev_find_head (l, h->name);
	    if (lh)
		refs[nref++] = lh;
	}
	if (nref)
	    h->commit = rev_branch_merge (refs, nref, rl);
    }
    /*
     * Compute 'tail' values
     */
    rev_list_set_tail (rl);
    /*
     * Compute set of tags
     */
    for (l = head; l; l = l->next) {
	for (lt = l->tags; lt; lt = lt->next) {
	    t = rev_find_tag (rl, lt->name);
	    if (!t)
		rev_list_add_tag (rl, NULL, lt->name, lt->degree);
	    else if (lt->degree == t->degree)
		t->degree = lt->degree;
	}
    }
    rl->tags = rev_ref_sel_sort (rl->tags);
    /*
     * Find tag locations
     */
    for (t = rl->tags; t; t = t->next) {
	/*
	 * Locate branch in every tree
	 */
	nref = 0;
	for (l = head; l; l = l->next) {
	    lh = rev_find_tag (l, t->name);
	    if (lh)
		refs[nref++] = lh;
	}
	if (nref)
	    t->commit = rev_tag_search (refs, nref, rl);
	if (!t->commit)
	    fprintf (stderr, "lost tag %s\n", t->name);
    }
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
