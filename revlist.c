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
 * Add head refs
 */

rev_ref *
rev_list_add_head (rev_list *rl, rev_commit *commit, char *name, int degree)
{
    rev_ref	*r;
    rev_ref	**list = &rl->heads;

    while (*list)
	list = &(*list)->next;
    r = calloc (1, sizeof (rev_ref));
    r->commit = commit;
    r->name = name;
    r->next = *list;
    r->degree = degree;
    *list = r;
    return r;
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

/*
 * We keep all file lists in a canonical sorted order,
 * first by latest date and then by the address of the rev_file object
 * (which are always unique)
 */

bool
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
	return true;
    if (t < 0)
	return false;
    if ((uintptr_t) af > (uintptr_t) bf)
	return true;
    return false;
}

bool
rev_commit_later (rev_commit *a, rev_commit *b)
{
    long	t;

    assert (a != b);
    t = time_compare (a->date, b->date);
    if (t > 0)
	return true;
    if (t < 0)
	return false;
    if ((uintptr_t) a > (uintptr_t) b)
	return true;
    return true;
}

static bool
commit_time_close (time_t a, time_t b)
{
    long	diff = a - b;
    if (diff < 0) diff = -diff;
    if (diff < commit_time_window)
	return true;
    return false;
}

/*
 * The heart of the merge operation; detect when two
 * commits are "the same"
 */
static bool
rev_commit_match (rev_commit *a, rev_commit *b)
{
    /*
     * Very recent versions of CVS place a commitid in
     * each commit to track patch sets. Use it if present
     */
    if (a->commitid && b->commitid)
	return a->commitid == b->commitid;
    if (a->commitid || b->commitid)
	return false;
    if (!commit_time_close (a->date, b->date))
	return false;
    if (a->log != b->log)
	return false;
    if (a->author != b->author)
	return false;
    return true;
}

#if UNUSED
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
#endif

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
	head->commit->tagged = true;
    }
}

#if 0
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
#endif

static rev_ref *
rev_ref_find_name (rev_ref *h, char *name)
{
    for (; h; h = h->next)
	if (h->name == name)
	    return h;
    return NULL;
}

static int
rev_ref_is_ready (char *name, rev_list *source, rev_ref *ready)
{
    for (; source; source = source->next) {
	rev_ref *head = rev_find_head(source, name);
	if (head) {
	    if (head->parent && !rev_ref_find_name(ready, head->parent->name))
		    return 0;
	}
    }
    return 1;
}

static rev_ref *
rev_ref_tsort (rev_ref *refs, rev_list *head)
{
    rev_ref *done = NULL;
    rev_ref **done_tail = &done;
    rev_ref *r, **prev;

//    fprintf (stderr, "Tsort refs:\n");
    while (refs) {
	for (prev = &refs; (r = *prev); prev = &(*prev)->next) {
	    if (rev_ref_is_ready (r->name, head, done)) {
		break;
	    }
	}
	if (!r) {
	    fprintf (stderr, "Error: branch cycle\n");
	    return NULL;
	}
	*prev = r->next;
	*done_tail = r;
//	fprintf (stderr, "\t%s\n", r->name);
	r->next = NULL;
	done_tail = &r->next;
    }
    return done;
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
    if (!a && !b)
	return 0;
    else if (!a)
	return 1;
    else if (!b)
	return -1;
#if 0
    /*
     * Entries with no files sort next
     */
    if (a->nfiles != b->nfiles)
	return b->nfiles - a->nfiles;
#endif
    /*
     * tailed entries sort next
     */
    if (a->tailed != b->tailed)
	return (int)a->tailed - (int)b->tailed;
    /*
     * Newest entries sort first
     */
    t = -time_compare (a->date, b->date);
    if (t)
	return t;
    /*
     * Ensure total order by ordering based on file address
     */
    if ((uintptr_t) a->file > (uintptr_t) b->file)
	return -1;
    if ((uintptr_t) a->file < (uintptr_t) b->file)
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

int
rev_commit_has_file (rev_commit *c, rev_file *f)
{
    int	i, j;

    if (!c)
	return 0;
    for (i = 0; i < c->ndirs; i++) {
	rev_dir	*dir = c->dirs[i];
	for (j = 0; j < dir->nfiles; j++)
	    if (dir->files[j] == f)
		return 1;
    }
    return 0;
}

#if UNUSED
static rev_file *
rev_commit_find_file (rev_commit *c, char *name)
{
    int	n;

    for (n = 0; n < c->nfiles; n++)
	if (c->files[n]->name == name)
	    return c->files[n];
    return NULL;
}
#endif

static rev_file **files = NULL;
static int	    sfiles = 0;

void
rev_commit_cleanup (void)
{
    if (files) {
	free (files);
	files = NULL;
	sfiles = 0;
    }
}

static rev_commit *
rev_commit_build (rev_commit **commits, rev_commit *leader, int ncommit)
{
    int		n, nfile;
    rev_commit	*commit;
    int		nds;
    rev_dir	**rds;
    rev_file	*first;

    if (ncommit > sfiles) {
	free (files);
	files = 0;
    }
    if (!files)
	files = malloc ((sfiles = ncommit) * sizeof (rev_file *));
    
    nfile = 0;
    for (n = 0; n < ncommit; n++)
	if (commits[n] && commits[n]->file)
	    files[nfile++] = commits[n]->file;
    
    if (nfile)
	first = files[0];
    else
	first = NULL;
    
    rds = rev_pack_files (files, nfile, &nds);
        
    commit = calloc (1, sizeof (rev_commit) +
		     nds * sizeof (rev_dir *));
    
    commit->date = leader->date;
    commit->commitid = leader->commitid;
    commit->log = leader->log;
    commit->author = leader->author;
    
    commit->file = first;
    commit->nfiles = nfile;

    memcpy (commit->dirs, rds, (commit->ndirs = nds) * sizeof (rev_dir *));
    
    return commit;
}

#if UNUSED
static rev_commit *
rev_ref_find_commit_file (rev_ref *branch, rev_file *file)
{
    rev_commit	*c;

    for (c = branch->commit; c; c = c->parent)
	if (rev_commit_has_file (c, file))
	    return c;
    return NULL;
}

static int
rev_commit_is_ancestor (rev_commit *old, rev_commit *young)
{
    while (young) {
	if (young == old)
	    return 1;
	young = young->parent;
    }
    return 0;
}
#endif

static rev_commit *
rev_commit_locate_date (rev_ref *branch, time_t date)
{
    rev_commit	*commit;

    for (commit = branch->commit; commit; commit = commit->parent)
    {
	if (time_compare (commit->date, date) <= 0)
	    return commit;
    }
    return NULL;
}

static rev_commit *
rev_commit_locate_one (rev_ref *branch, rev_commit *file)
{
    rev_commit	*commit;

    if (!branch)
	return NULL;

    for (commit = branch->commit;
	 commit;
	 commit = commit->parent)
    {
	if (rev_commit_match (commit, file))
	    return commit;
    }
    return NULL;
}

static rev_commit *
rev_commit_locate_any (rev_ref *branch, rev_commit *file)
{
    rev_commit	*commit;

    if (!branch)
	return NULL;
    commit = rev_commit_locate_any (branch->next, file);
    if (commit)
	return commit;
    return rev_commit_locate_one (branch, file);
}

static rev_commit *
rev_commit_locate (rev_ref *branch, rev_commit *file)
{
    rev_commit	*commit;

    /*
     * Check the presumed trunk first
     */
    commit = rev_commit_locate_one (branch, file);
    if (commit)
	return commit;
    /*
     * Now look through all branches
     */
    while (branch->parent)
	branch = branch->parent;
    return rev_commit_locate_any (branch, file);
}

rev_ref *
rev_branch_of_commit (rev_list *rl, rev_commit *commit)
{
    rev_ref	*h;
    rev_commit	*c;

    for (h = rl->heads; h; h = h->next)
    {
	if (h->tail)
	    continue;
	for (c = h->commit; c; c = c->parent) {
	    if (rev_commit_match (c, commit))
		return h;
	    if (c->tail)
		break;
	}
    }
    return NULL;
}

/*
 * Time of first commit along entire history
 */
static time_t
rev_commit_first_date (rev_commit *commit)
{
    while (commit->parent)
	commit = commit->parent;
    return commit->date;
}

/*
 * Merge a set of per-file branches into a global branch
 */
static void
rev_branch_merge (rev_ref **branches, int nbranch,
		  rev_ref *branch, rev_list *rl)
{
	int nlive;
	int n;
	rev_commit *prev = NULL;
	rev_commit *head = NULL, **tail = &head;
	rev_commit **commits = calloc (nbranch, sizeof (rev_commit *));
	rev_commit *commit;
	rev_commit *latest;
	rev_commit **p;
	time_t start = 0;

	nlive = 0;
	for (n = 0; n < nbranch; n++) {
		rev_commit *c;
		/*
		 * Initialize commits to head of each branch
		 */
		c = commits[n] = branches[n]->commit;
		/*
		* Compute number of branches with remaining entries
		*/
		if (!c)
			continue;
		if (branches[n]->tail) {
			c->tailed = true;
			continue;
		}
		nlive++;
		while (c && !c->tail) {
			if (!start || time_compare(c->date, start) < 0)
				start = c->date;
			c = c->parent;
		}
		if (c && (c->file || c->date != c->parent->date)) {
			if (!start || time_compare(c->date, start) < 0)
				start = c->date;
		}
	}

	for (n = 0; n < nbranch; n++) {
		rev_commit *c = commits[n];
		if (!c->tailed)
			continue;
		if (!start || time_compare(start, c->date) >= 0)
			continue;
		if (c->file)
			fprintf(stderr,
				"Warning: %s too late date through branch %s\n",
					c->file->name, branch->name);
		commits[n] = NULL;
	}
	/*
	 * Walk down branches until each one has merged with the
	 * parent branch
	 */
	while (nlive > 0 && nbranch > 0) {
		for (n = 0, p = commits, latest = NULL; n < nbranch; n++) {
			rev_commit *c = commits[n];
			if (!c)
				continue;
			*p++ = c;
			if (c->tailed)
				continue;
			if (!latest || time_compare(latest->date, c->date) < 0)
				latest = c;
		}
		nbranch = p - commits;

		/*
		 * Construct current commit
		 */
		commit = rev_commit_build (commits, latest, nbranch);

		/*
		 * Step each branch
		 */
		nlive = 0;
		for (n = 0; n < nbranch; n++) {
			rev_commit *c = commits[n];
			rev_commit *to;
			/* already got to parent branch? */
			if (c->tailed)
				continue;
			/* not affected? */
			if (c != latest && !rev_commit_match(c, latest)) {
				if (c->parent || c->file)
					nlive++;
				continue;
			}
			to = c->parent;
			/* starts here? */
			if (!to)
				goto Kill;

			if (c->tail) {
				/*
				 * Adding file independently added on another
				 * non-trunk branch.
				 */
				if (!to->parent && !to->file)
					goto Kill;
				/*
				 * If the parent is at the beginning of trunk
				 * and it is younger than some events on our
				 * branch, we have old CVS adding file
				 * independently
				 * added on another branch.
				 */
				if (start && time_compare(start, to->date) < 0)
					goto Kill;
				/*
				 * XXX: we still can't be sure that it's
				 * not a file added on trunk after parent
				 * branch had forked off it but before
				 * our branch's creation.
				 */
				to->tailed = true;
			} else if (to->file) {
				nlive++;
			} else {
				/*
				 * See if it's recent CVS adding a file
				 * independently added on another branch.
				 */
				if (!to->parent)
					goto Kill;
				if (to->tail && to->date == to->parent->date)
					goto Kill;
				nlive++;
			}
			commits[n] = to;
			continue;
Kill:
			commits[n] = NULL;
		}

		*tail = commit;
		tail = &commit->parent;
		prev = commit;
	}
    /*
     * Connect to parent branch
     */
    nbranch = rev_commit_date_sort (commits, nbranch);
    if (nbranch && branch->parent )
    {
	rev_ref	*lost;
	int	present;

//	present = 0;
	for (present = 0; present < nbranch; present++)
	    if (commits[present]->file) {
		/*
		 * Skip files which appear in the repository after
		 * the first commit along the branch
		 */
		if (prev && commits[present]->date > prev->date &&
		    commits[present]->date == rev_commit_first_date (commits[present]))
		{
		    fprintf (stderr, "Warning: file %s appears after branch %s date\n",
			     commits[present]->file->name, branch->name);
		    continue;
		}
		break;
	    }
	if (present == nbranch)
	    *tail = NULL;
	else if ((*tail = rev_commit_locate_one (branch->parent,
						 commits[present])))
	{
	    if (prev && time_compare ((*tail)->date, prev->date) > 0) {
		fprintf (stderr, "Warning: branch point %s -> %s later than branch\n",
			 branch->name, branch->parent->name);
		fprintf (stderr, "\ttrunk(%3d):  %s %s", n,
			 ctime_nonl (&commits[present]->date),
			 commits[present]->file ? " " : "D" );
		if (commits[present]->file)
		    dump_number_file (stderr,
				      commits[present]->file->name,
				      &commits[present]->file->number);
		fprintf (stderr, "\n");
		fprintf (stderr, "\tbranch(%3d): %s  ", n,
			 ctime_nonl (&prev->file->date));
		dump_number_file (stderr,
				  prev->file->name,
				  &prev->file->number);
		fprintf (stderr, "\n");
	    }
	} else if ((*tail = rev_commit_locate_date (branch->parent,
						  commits[present]->date)))
	    fprintf (stderr, "Warning: branch point %s -> %s matched by date\n",
		     branch->name, branch->parent->name);
	else {
	    fprintf (stderr, "Error: branch point %s -> %s not found.",
		     branch->name, branch->parent->name);

	    if ((lost = rev_branch_of_commit (rl, commits[present])))
		fprintf (stderr, " Possible match on %s.", lost->name);
	    fprintf (stderr, "\n");
	}
	if (*tail) {
	    if (prev)
		prev->tail = 1;
	} else 
	    *tail = rev_commit_build (commits, commits[0], nbranch);
    }
    for (n = 0; n < nbranch; n++)
	if (commits[n])
	    commits[n]->tailed = false;
    free (commits);
    branch->commit = head;
}

/*
 * Locate position in tree corresponding to specific tag
 */
static void
rev_tag_search(Tag *tag, rev_commit **commits, rev_list *rl)
{
	rev_commit_date_sort(commits, tag->count);
	tag->parent = rev_branch_of_commit(rl, commits[0]);
	if (tag->parent)
		tag->commit = rev_commit_locate (tag->parent, commits[0]);
	if (!tag->commit) {
		fprintf (stderr, "Unmatched tag %s\n", tag->name);
		/* AV: shouldn't we put it on some branch? */
		tag->commit = rev_commit_build(commits, commits[0], tag->count);
	}
	tag->commit->tagged = true;
}

static void
rev_ref_set_parent (rev_list *rl, rev_ref *dest, rev_list *source)
{
    rev_ref	*sh;
    rev_list	*s;
    rev_ref	*p;
    rev_ref	*max;

    if (dest->depth)
	return;

    max = NULL;
    for (s = source; s; s = s->next) {
	sh = rev_find_head (s, dest->name);
	if (!sh)
	    continue;
	if (!sh->parent)
	    continue;
	p = rev_find_head (rl, sh->parent->name);
	assert (p);
	rev_ref_set_parent (rl, p, source);
	if (!max || p->depth > max->depth)
	    max = p;
    }
    dest->parent = max;
    if (max)
	dest->depth = max->depth + 1;
    else
	dest->depth = 1;
}


#if UNUSED
static void
rev_head_find_parent (rev_list *rl, rev_ref *h, rev_list *lhead)
{
    rev_list	*l;
    rev_ref	*lh;

    for (l = lhead; l; l = l->next) {
	lh = rev_find_head (l, h->name);
	if (!lh)
	    continue;
	
    }
}
#endif

#if UNUSED
static int
rev_branch_name_is_ancestor (rev_ref *old, rev_ref *young)
{
    while (young) {
	if (young->name == old->name)
	    return 1;
	young = young->parent;
    }
    return 0;
}
#endif

#if UNUSED
static rev_ref *
rev_ref_parent (rev_ref **refs, int nref, rev_list *rl)
{
    rev_ref	*parent = NULL;
    rev_ref	*branch = NULL;
    int		n;
    rev_ref	*h;

    for (n = 0; n < nref; n++)
    {
	if (refs[n]->parent) {
	    if (!parent) {
		parent = refs[n]->parent;
		branch = refs[n];
	    } else if (parent->name != refs[n]->parent->name) {
		if (rev_branch_name_is_ancestor (refs[n]->parent, parent))
		    ;
		else if (rev_branch_name_is_ancestor (parent, refs[n]->parent)) {
		    parent = refs[n]->parent;
		    branch = refs[n];
		} else {
		    fprintf (stderr, "Branch name collision:\n");
		    fprintf (stderr, "\tfirst branch: ");
		    dump_ref_name (stderr, branch);
		    fprintf (stderr, "\n");
		    fprintf (stderr, "\tsecond branch: ");
		    dump_ref_name (stderr, refs[n]);
		    fprintf (stderr, "\n");
		}
	    }
	}
    }
    if (!parent)
	return NULL;
    for (h = rl->heads; h; h = h->next)
	if (parent->name == h->name)
	    return h;
    fprintf (stderr, "Reference missing in merge: %s\n", parent->name);
    return NULL;
}
#endif

rev_list *
rev_list_merge (rev_list *head)
{
    int		count = rev_list_count (head);
    rev_list	*rl = calloc (1, sizeof (rev_list));
    rev_list	*l;
    rev_ref	*lh, *h;
    Tag		*t;
    rev_ref	**refs = calloc (count, sizeof (rev_ref *));
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
//    rl->heads = rev_ref_sel_sort (rl->heads);
    rl->heads = rev_ref_tsort (rl->heads, head);
    if (!rl->heads)
	return NULL;
//    for (h = rl->heads; h; h = h->next)
//	fprintf (stderr, "head %s (%d)\n",
//		 h->name, h->degree);
    /*
     * Find branch parent relationships
     */
    for (h = rl->heads; h; h = h->next) {
	rev_ref_set_parent (rl, h, head);
//	dump_ref_name (stderr, h);
//	fprintf (stderr, "\n");
    }
    /*
     * Merge common branches
     */
    for (h = rl->heads; h; h = h->next) {
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
	    rev_branch_merge (refs, nref, h, rl);
    }
    /*
     * Compute 'tail' values
     */
    rev_list_set_tail (rl);

    free(refs);
    /*
     * Find tag locations
     */
    for (t = all_tags; t; t = t->next) {
	rev_commit **commits = tagged(t);
	if (commits)
	    rev_tag_search(t, commits, rl);
	else
	    fprintf (stderr, "lost tag %s\n", t->name);
	free(commits);
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
	    if (free_files && c->file)
		rev_file_mark_for_free (c->file);
	    free (c);
	}
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
//	    assert (time_compare (c->date, c->parent->date) >= 0);
	}
    }
}

/*
 * Generate a list of files in uniq that aren't in common
 */

static rev_file_list *
rev_uniq_file (rev_commit *uniq, rev_commit *common, int *nuniqp)
{
    int	i, j;
    int nuniq = 0;
    rev_file_list   *head = NULL, **tail = &head, *fl;
    
    if (!uniq)
	return NULL;
    for (i = 0; i < uniq->ndirs; i++) {
	rev_dir	*dir = uniq->dirs[i];
	for (j = 0; j < dir->nfiles; j++)
	    if (!rev_commit_has_file (common, dir->files[j])) {
		fl = calloc (1, sizeof (rev_file_list));
		fl->file = dir->files[j];
		*tail = fl;
		tail = &fl->next;
		++nuniq;
	    }
    }
    *nuniqp = nuniq;
    return head;
}

int
rev_file_list_has_filename (rev_file_list *fl, char *name)
{
    for (; fl; fl = fl->next)
	if (fl->file->name == name)
	    return 1;
    return 0;
}

/*
 * Generate a diff between two commits. Either may be NULL
 */

rev_diff *
rev_commit_diff (rev_commit *old, rev_commit *new)
{
    rev_diff	*diff = calloc (1, sizeof (rev_diff));

    diff->del = rev_uniq_file (old, new, &diff->ndel);
    diff->add = rev_uniq_file (new, old, &diff->nadd);
    return diff;
}

static void
rev_file_list_free (rev_file_list *fl)
{
    rev_file_list   *next;

    while (fl) {
	next = fl->next;
	free (fl);
	fl = next;
    }
}

void
rev_diff_free (rev_diff *d)
{
    rev_file_list_free (d->del);
    rev_file_list_free (d->add);
    free (d);
}

