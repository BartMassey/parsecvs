#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "cvs.h"

static Tag *table[4096];

Tag *all_tags;

static int tag_hash(char *name)
{
	uintptr_t l = (uintptr_t)name;
	int res = 0;
	while (l) {
		res ^= l;
		l >>= 12;
	}
	return res & 4095;
}

static Tag *find_tag(char *name)
{
	int hash = tag_hash(name);
	Tag *tag;
	for (tag = table[hash]; tag; tag = tag->hash_next)
		if (tag->name == name)
			return tag;
	tag = calloc(1, sizeof(Tag));
	tag->name = name;
	tag->hash_next = table[hash];
	table[hash] = tag;
	tag->next = all_tags;
	all_tags = tag;
	return tag;
}

/* the last argument is a sham */
void tag_commit(rev_commit *c, char *name)
{
	Tag *tag = find_tag(name);
	if (tag->last == this_file->name) {
		fprintf(stderr, "duplicate tag %s in %s, ignoring\n",
			name, this_file->name);
		return;
	}
	tag->last = this_file->name;
	if (!tag->left) {
		Chunk *v = malloc(sizeof(Chunk));
		v->next = tag->commits;
		tag->commits = v;
		tag->left = Ncommits;
	}
	tag->commits->v[--tag->left] = c;
	tag->count++;
}

rev_commit **tagged(Tag *tag)
{
	rev_commit **v = NULL;

	if (tag->count) {
		rev_commit **p = malloc(tag->count * sizeof(*p));
		Chunk *c = tag->commits;
		int n = Ncommits - tag->left;

		v = p;
		memcpy(p, c->v + tag->left, n * sizeof(*p));

		for (c = c->next, p += n; c; c = c->next, p += Ncommits)
			memcpy(p, c->v, Ncommits * sizeof(*p));
	}
	return v;
}

void discard_tags(void)
{
	Tag *tag = all_tags;
	all_tags = NULL;
	while (tag) {
		Tag *p = tag->next;
		Chunk *c = tag->commits;
		while (c) {
			Chunk *next = c->next;
			free(c);
			c = next;
		}
		free(tag);
		tag = p;
	}
}
