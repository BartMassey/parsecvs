#include <limits.h>

#include "cvs.h"

typedef struct _entry {
	char *cvs_name;
	struct _entry *next;
	char *name;
	size_t len;
	struct cache_entry *ce;
} Hash_entry;

static Hash_entry *table[4096];

static int strip;

static int name_hash(char *name)
{
	uintptr_t l = (uintptr_t)name;
	int res = 0;
	while (l) {
		res ^= l;
		l >>= 12;
	}
	return res & 4095;
}

static char *convert(char *name)
/* convert the name of a CVS master to a corresponding "plain" name */
{
	static char path[PATH_MAX + 1];
	char *end, *p, *q;
	const int attic_len = strlen("Attic");
	size_t len = strlen(name);

	if (len >= PATH_MAX + strip)
		return NULL;

	len -= strip;
	memcpy(path, name + strip, len + 1);
	end = path + len;

	p = strrchr(path, '/');

	if (len > 2 && end[-2] == ',' && end[-1] == 'v') {
		end[-2] = '\0';
		end -= 2;
	}

	if (!p || p < path + attic_len)
		return path;

	q = p - attic_len;
	p++;
	if (memcmp(q, "Attic", attic_len) != 0)
		return path;

	if (q == path || q[-1] == '/') {
		while ((*q++ = *p++) != '\0')
			;
	}
	return path;
}

static Hash_entry *find_node(rev_commit *c)
{
	char *name = c->file ? c->file->name : NULL;
	char *real_name;
	Hash_entry *entry;
	size_t len;
	int hash;

	if (!name)
		return NULL;

	hash = name_hash(name);
	for (entry = table[hash]; entry; entry = entry->next)
		if (entry->cvs_name == name)
			return entry;

	real_name = convert(name);
	if (!real_name)
		return NULL;

	entry = calloc(1, sizeof(Hash_entry));
	if (entry == NULL)
	{
	    fprintf(stderr, "parsecvs: memory allocation failure\n");
	    exit(1);
	}
	entry->cvs_name = name;

	len = strlen(real_name);

	entry->len = len;
	entry->name = xmalloc(len + 1);
	memcpy(entry->name, real_name, len + 1);

	//entry->ce = xcalloc(1, cache_entry_size(len));
	//memcpy(entry->ce->name, real_name, len);
	//entry->ce->ce_flags = create_ce_flags(len);

	entry->next = table[hash];
	table[hash] = entry;
	return entry;
}

static int cache_broken;

static void delete_file(Hash_entry *entry)
{
    //remove_file_from_cache(entry->name);
    //cache_tree_invalidate_path(active_cache_tree, entry->name);
}

static int set_file(Hash_entry *entry, rev_file *file)
{
    //int options = ADD_CACHE_OK_TO_ADD | ADD_CACHE_OK_TO_REPLACE;

    //if (get_sha1_hex(file->sha1, entry->ce->sha1))
    //die("corrupt sha1: %s\n", file->sha1);

    //entry->ce->ce_mode = create_ce_mode(file->mode);
    //if (add_cache_entry(entry->ce, options))
    //	return error("can't add %s\n", entry->name);

    //cache_tree_invalidate_path(active_cache_tree, entry->name);

    return 0;
}

void delete_commit(rev_commit *c)
{
	Hash_entry *entry = find_node(c);
	if (entry && !cache_broken)
		delete_file(entry);
}

void set_commit(rev_commit *c)
{
	if (!cache_broken) {
		Hash_entry *entry = find_node(c);
		if (entry)
			cache_broken = set_file(entry, c->file);
	}
}

void reset_commits(rev_commit **commits, int ncommits)
{
    //discard_cache();
    //active_cache_tree = cache_tree();
    cache_broken = 0;
    while (ncommits-- && !cache_broken) {
	rev_commit *c = *commits++;
	if (c) {
	    Hash_entry *entry = find_node(c);
	    if (entry)
		cache_broken = set_file(entry, c->file);
	}
    }
}

#if 0
rev_commit *create_tree(rev_commit *leader)
{
	rev_commit *commit = xcalloc(1, sizeof (rev_commit));

	commit->date = leader->date;
	commit->commitid = leader->commitid;
	commit->log = leader->log;
	commit->author = leader->author;

	if (!cache_broken) {
		if (cache_tree_update(active_cache_tree, active_cache,
				      active_nr, 0))
		    cache_broken = error("writing tree");
	}

	if (!cache_broken)
		commit->sha1 = atom(sha1_to_hex(active_cache_tree->sha1));

	return commit;
}
#endif

void init_tree(int n)
{
#if 0
	git_config(git_default_config, NULL);
	strip = n;
#endif
}

void discard_tree(void)
{
	int i;
	//discard_cache();
	for (i = 0; i < 4096; i++) {
		Hash_entry *entry = table[i];
		while (entry) {
			Hash_entry *next = entry->next;
			free(entry->name);
			free(entry);
			entry = next;
		}
	}
}

/* end */
