#include "cvs.h"

static Node *table[4096];
static int entries;

static Node *hash_number(cvs_number *n)
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

static Node *find_number(cvs_number *n)
{
	cvs_number key = *n;
	Node *p;
	int hash;
	int i;

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
{
	v->node = hash_number(&v->number);
	if (v->node->v) {
		char name[CVS_MAX_REV_LEN];
		fprintf(stderr, "more than one delta with number %s\n",
			cvs_number_string(&v->node->number, name));
	} else {
		v->node->v = v;
	}
}

void hash_patch(cvs_patch *p)
{
	p->node = hash_number(&p->number);
	if (p->node->p) {
		char name[CVS_MAX_REV_LEN];
		fprintf(stderr, "more than one delta with number %s\n",
			cvs_number_string(&p->node->number, name));
	} else {
		p->node->p = p;
	}
}

void hash_branch(cvs_branch *b)
{
	b->node = hash_number(&b->number);
}

void clean_hash(void)
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
}

static int compare(const void *a, const void *b)
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
	int i;

	if (n == b->number.c) {
		if (n == 2) {
			a->next = b;
			return;
		}
		for (i = n - 2; i >= 0; i--)
			if (a->number.n[i] != b->number.n[i])
				break;
		if (i < 0) {
			a->next = b;
			return;
		}
	}
	if ((b->number.c & 1) == 0) {
		cvs_number num = b->number;
		Node *p = find_number(&num);
		if (p)
			p->next = b;
	}
}

void build_branches(void)
{
	Node **v = malloc(sizeof(Node *) * entries), **p = v;
	int i;

	for (i = 0; i < 4096; i++) {
		Node *q;
		for (q = table[i]; q; q = q->hash_next)
			*p++ = q;
	}
	qsort(v, entries, sizeof(Node *), compare);
	for (p = v + entries - 2 ; p >= v; p--)
		try_pair(p[0], p[1]);
	free(v);
}
