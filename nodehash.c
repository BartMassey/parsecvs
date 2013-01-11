#include "cvs.h"

static Node *table[4096];
static int entries;

Node *head_node;

static Node *hash_number(cvs_number *n)
/* look up the node associated with a specifued CVS release number */
{
	cvs_number key = *n;
	Node *p;
	int hash;
	int i;

	if (key.c > 2 && !key.n[key.c - 2]) {
		key.n[key.c - 2] = key.n[key.c - 1];
		key.c--;
	}
	if (key.c & 1)
		key.n[key.c] = 0;
	for (i = 0, hash = 0; i < key.c - 1; i++)
		hash += key.n[i];
	hash = (hash * 256 + key.n[key.c - 1]) % 4096;
	for (p = table[hash]; p; p = p->hash_next) {
		if (p->number.c != key.c)
			continue;
		for (i = 0; i < key.c && p->number.n[i] == key.n[i]; i++)
			;
		if (i == key.c)
			return p;
	}
	p = calloc(1, sizeof(Node));
	p->number = key;
	p->hash_next = table[hash];
	table[hash] = p;
	entries++;
	return p;
}

static Node *find_parent(cvs_number *n, int depth)
/* find the parent node of the specified prefix of a release number */
{
	cvs_number key = *n;
	Node *p;
	int hash;
	int i;

	key.c -= depth;
	for (i = 0, hash = 0; i < key.c - 1; i++)
		hash += key.n[i];
	hash = (hash * 256 + key.n[key.c - 1]) % 4096;
	for (p = table[hash]; p; p = p->hash_next) {
		if (p->number.c != key.c)
			continue;
		for (i = 0; i < key.c && p->number.n[i] == key.n[i]; i++)
			;
		if (i == key.c)
			break;
	}
	return p;
}

void hash_version(cvs_version *v)
/* intern a version onto the node list */
{
	char name[CVS_MAX_REV_LEN];
	v->node = hash_number(&v->number);
	if (v->node->v) {
		fprintf(stderr, "more than one delta with number %s\n",
			cvs_number_string(&v->node->number, name));
	} else {
		v->node->v = v;
	}
	if (v->node->number.c & 1) {
		fprintf(stderr, "revision with odd depth (%s)\n",
			cvs_number_string(&v->node->number, name));
	}
}

void hash_patch(cvs_patch *p)
/* intern a patch onto the node list */
{
	char name[CVS_MAX_REV_LEN];
	p->node = hash_number(&p->number);
	if (p->node->p) {
		fprintf(stderr, "more than one delta with number %s\n",
			cvs_number_string(&p->node->number, name));
	} else {
		p->node->p = p;
	}
	if (p->node->number.c & 1) {
		fprintf(stderr, "patch with odd depth (%s)\n",
			cvs_number_string(&p->node->number, name));
	}
}

void hash_branch(cvs_branch *b)
/* intern a branch onto the node list */
{
	b->node = hash_number(&b->number);
}

void clean_hash(void)
/* discard the node list */
{
	int i;
	for (i = 0; i < 4096; i++) {
		Node *p = table[i];
		table[i] = NULL;
		while (p) {
			Node *q = p->hash_next;
			free(p);
			p = q;
		}
	}
	entries = 0;
	head_node = NULL;
}

static int compare(const void *a, const void *b)
/* total ordering of nodes by associated CVS revision number */
{
	Node *x = *(Node * const *)a, *y = *(Node * const *)b;
	int n, i;
	n = x->number.c;
	if (n < y->number.c)
		return -1;
	if (n > y->number.c)
		return 1;
	for (i = 0; i < n; i++) {
		if (x->number.n[i] < y->number.n[i])
			return -1;
		if (x->number.n[i] > y->number.n[i])
			return 1;
	}
	return 0;
}

static void try_pair(Node *a, Node *b)
{
	int n = a->number.c;

	if (n == b->number.c) {
		int i;
		if (n == 2) {
			a->next = b;
			b->to = a;
			return;
		}
		for (i = n - 2; i >= 0; i--)
			if (a->number.n[i] != b->number.n[i])
				break;
		if (i < 0) {
			a->next = b;
			a->to = b;
			return;
		}
	} else if (n == 2) {
		head_node = a;
	}
	if ((b->number.c & 1) == 0) {
		b->starts = 1;
		/* can the code below ever be needed? */
		Node *p = find_parent(&b->number, 1);
		if (p)
			p->next = b;
	}
}

void build_branches(void)
/* set head_node global and build branch links in the node list */ 
{
	Node **v = malloc(sizeof(Node *) * entries), **p = v;
	int i;

	for (i = 0; i < 4096; i++) {
		Node *q;
		for (q = table[i]; q; q = q->hash_next)
			*p++ = q;
	}
	qsort(v, entries, sizeof(Node *), compare);
	/* only trunk? */
	if (v[entries-1]->number.c == 2)
		head_node = v[entries-1];
	for (p = v + entries - 2 ; p >= v; p--)
		try_pair(p[0], p[1]);
	for (p = v + entries - 1 ; p >= v; p--) {
		Node *a = *p, *b = NULL;
		if (!a->starts)
			continue;
		b = find_parent(&a->number, 2);
		if (!b) {
			char name[CVS_MAX_REV_LEN];
			fprintf(stderr, "no parent for %s\n",
				cvs_number_string(&a->number, name));
			continue;
		}
		a->sib = b->down;
		b->down = a;
	}
	free(v);
}

/* end */
