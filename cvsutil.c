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

#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))

int
cvs_is_head (cvs_number *n)
{
    return (n->c > 2 && (n->c & 1) == 0 && n->n[n->c-2] == 0);
}

int
cvs_same_branch (cvs_number *a, cvs_number *b)
{
    cvs_number	t;
    int		i;
    int		n;

    if (a->c & 1) {
	t = *a;
	t.n[t.c++] = 0;
	return cvs_same_branch (b, &t);
    }
    if (a->c != b->c)
	return 0;
    n = a->c;
    for (i = 0; i < a->c - 1; i++)
	if (a->n[i] != b->n[i])
	    return 0;
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

cvs_number *
cvs_previous_rev (cvs_number *n)
{
    cvs_number	*p = calloc (1, sizeof (cvs_number));
    int		i;
    
    for (i = 0; i < p->c - 1; i++)
	p->n[i] = n->n[i];
    if (n->n[i] == 1) {
	free (p);
	return NULL;
    }
    p->n[i] = n->n[i] - 1;
    return p;
}

cvs_number *
cvs_master_rev (cvs_number *n)
{
    cvs_number *p = calloc (1, sizeof (cvs_number));

    *p = *n;
    p->c -= 2;
    return p;
}

/*
 * Find the newest revision along a specific branch
 */

cvs_number *
cvs_branch_head (cvs_file *f, cvs_number *branch)
{
    cvs_number	*n = calloc (1, sizeof (cvs_number));
    cvs_version	*v;

    *n = *branch;
    /* Check for magic branch format */
    if ((n->c & 1) == 0 && n->n[n->c-2] == 0) {
	n->n[n->c-2] = n->n[n->c-1];
	n->c--;
    }
    for (v = f->versions; v; v = v->next) {
	if (cvs_same_branch (n, v->number) &&
	    cvs_number_compare (n, v->number) > 0)
	    *n = *v->number;
    }
    return n;
}

cvs_number *
cvs_branch_parent (cvs_file *f, cvs_number *branch)
{
    cvs_number	*n = calloc (1, sizeof (cvs_number));
    cvs_version	*v;

    *n = *branch;
    n->n[n->c-1] = 0;
    for (v = f->versions; v; v = v->next) {
	if (cvs_same_branch (n, v->number) &&
	    cvs_number_compare (branch, v->number) < 0 &&
	    cvs_number_compare (n, v->number) >= 0)
	    *n = *v->number;
    }
    return n;
}

cvs_patch *
cvs_find_patch (cvs_file *f, cvs_number *n)
{
    cvs_patch	*p;

    for (p = f->patches; p; p = p->next)
	if (cvs_number_compare (p->number, n) == 0)
	    return p;
    return NULL;
}

cvs_version *
cvs_find_version (cvs_file *cvs, cvs_number *number)
{
    cvs_version *cv;
    cvs_version	*nv = NULL;

    for (cv = cvs->versions; cv; cv = cv->next) {
	if (cvs_same_branch (number, cv->number) &&
	    cvs_number_compare (cv->number, number) > 0 &&
	    (!nv || cvs_number_compare (nv->number, cv->number) > 0))
	    nv = cv;
    }
    return nv;
}

int
cvs_is_trunk (cvs_number *number)
{
    return number->c == 2;
}

int
cvs_is_vendor (cvs_number *number)
{
    int i;
    if (number->c != 4) return 0;
    for (i = 0; i < 3; i++)
	if (number->n[i] != 1)
	    return 0;
    return 1;
}
