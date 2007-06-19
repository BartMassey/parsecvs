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
#include <assert.h>

#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

int
cvs_is_head (cvs_number *n)
{
    assert (n->c <= CVS_MAX_DEPTH); 
    return (n->c > 2 && (n->c & 1) == 0 && n->n[n->c-2] == 0);
}

int
cvs_same_branch (cvs_number *a, cvs_number *b)
{
    cvs_number	t;
    int		i;
    int		n;
    int		an, bn;

    if (a->c & 1) {
	t = *a;
	t.n[t.c++] = 0;
	return cvs_same_branch (&t, b);
    }
    if (b->c & 1) {
	t = *b;
	t.n[t.c++] = 0;
	return cvs_same_branch (a, &t);
    }
    if (a->c != b->c)
	return 0;
    /*
     * Everything on x.y is trunk
     */
    if (a->c == 2)
	return 1;
    n = a->c;
    for (i = 0; i < n - 1; i++) {
	an = a->n[i];
	bn = b->n[i];
	/*
	 * deal with n.m.0.p branch numbering
	 */
	if (i == n - 2) {
	    if (an == 0) an = a->n[i+1];
	    if (bn == 0) bn = b->n[i+1];
	}
	if (an != bn)
	    return 0;
    }
    return 1;
}

int
cvs_number_compare (cvs_number *a, cvs_number *b)
{
    int n = min (a->c, b->c);
    int i;

    for (i = 0; i < n; i++) {
	if (a->n[i] < b->n[i])
	    return -1;
	if (a->n[i] > b->n[i])
	    return 1;
    }
    if (a->c < b->c)
	return -1;
    if (a->c > b->c)
	return 1;
    return 0;
}

int
cvs_number_compare_n (cvs_number *a, cvs_number *b, int l)
{
    int n = min (l, min (a->c, b->c));
    int i;

    for (i = 0; i < n; i++) {
	if (a->n[i] < b->n[i])
	    return -1;
	if (a->n[i] > b->n[i])
	    return 1;
    }
    if (l > a->c)
	return -1;
    if (l > b->c)
	return 1;
    return 0;
}

int
cvs_is_branch_of (cvs_number *trunk, cvs_number *branch)
{
    cvs_number	n;

    if (branch->c > 2) {
	n = *branch;
	n.c -= 2;
	return cvs_same_branch (trunk, &n);
    }
    return 0;
}

int
cvs_number_degree (cvs_number *n)
{
    cvs_number	four;

    if (n->c < 4)
	return n->c;
    four = *n;
    four.c = 4;
    /*
     * Place vendor branch between trunk and other branches
     */
    if (cvs_is_vendor (&four))
	return n->c - 1;
    return n->c;
}

cvs_number
cvs_previous_rev (cvs_number *n)
{
    cvs_number	p;
    int		i;
    
    p.c = n->c;
    for (i = 0; i < n->c - 1; i++)
	p.n[i] = n->n[i];
    if (n->n[i] == 1) {
	p.c = 0;
	return p;
    }
    p.n[i] = n->n[i] - 1;
    return p;
}

cvs_number
cvs_master_rev (cvs_number *n)
{
    cvs_number p;

    p = *n;
    p.c -= 2;
    return p;
}

/*
 * Find the newest revision along a specific branch
 */

cvs_number
cvs_branch_head (cvs_file *f, cvs_number *branch)
{
    cvs_number	n;
    cvs_version	*v;

    n = *branch;
    /* Check for magic branch format */
    if ((n.c & 1) == 0 && n.n[n.c-2] == 0) {
	n.n[n.c-2] = n.n[n.c-1];
	n.c--;
    }
    for (v = f->versions; v; v = v->next) {
	if (cvs_same_branch (&n, &v->number) &&
	    cvs_number_compare (&n, &v->number) > 0)
	    n = v->number;
    }
    return n;
}

cvs_number
cvs_branch_parent (cvs_file *f, cvs_number *branch)
{
    cvs_number	n;
    cvs_version	*v;

    n = *branch;
    n.n[n.c-1] = 0;
    for (v = f->versions; v; v = v->next) {
	if (cvs_same_branch (&n, &v->number) &&
	    cvs_number_compare (branch, &v->number) < 0 &&
	    cvs_number_compare (&n, &v->number) >= 0)
	    n = v->number;
    }
    return n;
}

Node *
cvs_find_version (cvs_file *cvs, cvs_number *number)
{
    cvs_version *cv;
    cvs_version	*nv = NULL;

    for (cv = cvs->versions; cv; cv = cv->next) {
	if (cvs_same_branch (number, &cv->number) &&
	    cvs_number_compare (&cv->number, number) > 0 &&
	    (!nv || cvs_number_compare (&nv->number, &cv->number) > 0))
	    nv = cv;
    }
    return nv ? nv->node : NULL;
}

int
cvs_is_trunk (cvs_number *number)
{
    return number->c == 2;
}

/*
 * Import branches are of the form 1.1.x where x is odd
 */
int
cvs_is_vendor (cvs_number *number)
{
    if (number->c != 4) return 0;
    if (number->n[0] != 1)
	return 0;
    if (number->n[1] != 1)
	return 0;
    if ((number->n[2] & 1) != 1)
	return 0;
    return 1;
}

static void
cvs_symbol_free (cvs_symbol *symbol)
{
    cvs_symbol	*s;

    while ((s = symbol)) {
	symbol = s->next;
	free (s);
    }
}

static void
cvs_branch_free (cvs_branch *branch)
{
    cvs_branch	*b;

    while ((b = branch)) {
	branch = b->next;
	free (b);
    }
}

static void
cvs_version_free (cvs_version *version)
{
    cvs_version	*v;

    while ((v = version)) {
	version = v->next;
	cvs_branch_free (v->branches);
	free (v);
    }
}

static void
cvs_patch_free (cvs_patch *patch)
{
    cvs_patch	*v;

    while ((v = patch)) {
	patch = v->next;
	free (v->text);
	free (v);
    }
}

void
cvs_file_free (cvs_file *cvs)
{
    cvs_symbol_free (cvs->symbols);
    cvs_version_free (cvs->versions);
    cvs_patch_free (cvs->patches);
    free (cvs);
    clean_hash();
}

char *
cvs_number_string (cvs_number *n, char *str)
{
    char    r[11];
    int	    i;

    str[0] = '\0';
    for (i = 0; i < n->c; i++) {
	snprintf (r, 10, "%d", n->n[i]);
	if (i > 0)
	    strcat (str, ".");
	strcat (str, r);
    }
    return str;
}
