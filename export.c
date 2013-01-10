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

#include <openssl/sha.h>
#include "cvs.h"

static int mark;

int
export_init(void)
{
    mark = 0;
    return 0;
}

static char *sha1_to_hex(const unsigned char *sha1)
{
	static int bufno;
	static char hexbuffer[4][50];
	static const char hex[] = "0123456789abcdef";
	char *buffer = hexbuffer[3 & ++bufno], *buf = buffer;
	int i;

	for (i = 0; i < 20; i++) {
		unsigned int val = *sha1++;
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}
	*buf = '\0';

	return buffer;
}

void 
export_generation_hook(Node *node, void *buf, unsigned long len)
{
    char sha1_ascii[41];
    unsigned char sha1[20];

    SHA1(buf, len, sha1);
    strncpy(sha1_ascii, sha1_to_hex(sha1), 41);
    node->file->sha1 = atom(sha1_ascii);
    node->file->mark = ++mark;

    if (rev_mode == ExecuteExport)
    {
	printf("# %s\n", node->file->sha1);
	printf("blob\nmark :%d\ndata %zd\n", 
	       node->file->mark, len);
	fwrite(buf, len, sizeof(char), stdout);
	putchar('\n');
    }
}

static int
git_filename (rev_file *file, char *name, int strip)
{
    char    *attic;
    int	    l;
    int	    len;
    
    if (strlen (file->name) - strip >= MAXPATHLEN)
	return 0;
    strcpy (name, file->name + strip);
    while ((attic = strstr (name, "Attic/")) &&
	   (attic == name || attic[-1] == '/'))
    {
	l = strlen (attic);
	memmove (attic, attic + 6, l - 5);
    }
    len = strlen (name);
    if (len > 2 && !strcmp (name + len - 2, ",v"))
	name[len-2] = '\0';
    return 1;
}

#define GIT_CVS_DIR ".git-cvs"

static char *
git_cvs_file (char *base)
{
    char    *filename_buf = NULL;
    char    *filename;
    static int	id;
    
    if (id == 0)
    {
	if (mkdir (GIT_CVS_DIR, 0777) < 0 && errno != EEXIST) {
	    fprintf (stderr, "%s: %s\n", GIT_CVS_DIR, strerror (errno));
	    return NULL;
	}
    }
    //filename_buf = git_format_command ("%s/%s-%d",
    //				       GIT_CVS_DIR, base, id++);
    if (!filename_buf)
	return NULL;
    filename = atom (filename_buf);
    free (filename_buf);
    return filename;
}

extern const char *log_command;
static char *log_buf;
static size_t log_size;

static char *
git_log(rev_commit *commit)
{
    return commit->log;

#if 0
	char    *filename;
	char	*command;
	FILE    *f;
	int	n;
	size_t  size;
    
	filename = git_cvs_file ("log");
	if (!filename)
		return NULL;
	f = fopen (filename, "w+");
	if (!f) {
		fprintf (stderr, "%s: %s\n", filename, strerror (errno));
		return NULL;
	}
	if (fputs (commit->log, f) == EOF) {
		fprintf (stderr, "%s: %s\n", filename, strerror (errno));
		fclose (f);
		return NULL;
	}
	fflush (f);

	command = git_format_command ("%s '%s'", log_command, filename);
	if (!command)
	    return NULL;
	n = git_system (command);
	free (command);
	if (n != 0)
		return NULL;
	fflush (f);
	rewind(f);
	size = 0;
	while (1) {
		if (size + 1 >= log_size) {
			if (!log_size)
				log_size = 1024;
			else
				log_size *= 2;
			log_buf = xrealloc(log_buf, log_size);
		}
		n = fread(log_buf + size, 1, log_size - size - 1, f);
		if (!n)
			break;
		size += n;
	}
	fclose(f);
	log_buf[size] = '\0';
	return log_buf;
#endif
}

static int git_total_commits;
static int git_current_commit;
static char *git_current_head;

#define STATUS	stderr
#define PROGRESS_LEN	20

static void
git_status (void)
{
    int	spot = git_current_commit * PROGRESS_LEN / git_total_commits;
    int	s;

    fprintf (STATUS, "Save: %35.35s ", git_current_head);
    for (s = 0; s < PROGRESS_LEN + 1; s++)
	putc (s == spot ? '*' : '.', STATUS);
    fprintf (STATUS, " %5d of %5d\n", git_current_commit, git_total_commits);
    fflush (STATUS);
}

static int
git_commit(rev_commit *commit)
{
    cvs_author *author;
    char *full;
    char *email;
    char *log;
    unsigned char commit_sha1[20];

    if (!commit->sha1)
	return 0;

    log = git_log(commit);
    if (!log)
	return 0;

    author = fullname(commit->author);
    if (!author) {
//	fprintf (stderr, "%s: not in author map\n", commit->author);
	full = commit->author;
	email = commit->author;
    } else {
	full = author->full;
	email = author->email;
    }

    /* Not having i18n.commitencoding is the same as having utf-8 */
    //encoding_is_utf8 = is_encoding_utf8(git_commit_encoding);

    printf("commit\n");
    printf("# tree %s\n", commit->sha1);
    if (commit->parent)
	printf("parent %s\n", commit->parent->sha1);
    printf("author %s <%s> %lu +0000\n",
	       full, email, commit->date);
    printf("committer %s <%s> %lu +0000\n",
	       full, email, commit->date);
    printf("\n%s", log);

    //if (write_sha1_file(commit_text, size, commit_type, commit_sha1))
    //	return 0;

    commit->sha1 = atom(sha1_to_hex(commit_sha1));
    if (!commit->sha1)
	return 0;
    return 1;
}

static int
git_update_ref (char *sha1, char *type, char *name)
{
#if 0
    char    *command;
    int	    n;

    command = git_format_command ("git update-ref 'refs/%s/%s' '%s'",
				  type, name, sha1);
    if (!command)
	return 0;
    n = git_system (command);
    free (command);
    if (n != 0)
	return 0;
#endif
    return 1;
}

static char *
git_mktag (rev_commit *commit, char *name)
{
    char    *filename;
    FILE    *f;
    int     rv;
    char    *command;
    char    *tag_sha1;
    cvs_author *author;

    filename = git_cvs_file ("tag");
    if (!filename)
	return NULL;
    f = fopen (filename, "w");
    if (!f) {
	fprintf (stderr, "%s: %s\n", filename, strerror (errno));
	return NULL;
    }
    
    author = fullname (commit->author);
    if (author == NULL) {
      fprintf (stderr, "No author info for tagger %s\n", commit->author);
      return NULL;
    }

    rv = fprintf (f,
		"object %s\n"
		"type commit\n"
		"tag %s\n"
                "tagger %s <%s> %ld +0000\n"
		"\n",
		commit->sha1,
		name,
		author ? author->full : commit->author,
		author ? author->email : "",
		commit->date);
    if (rv < 1) {
	fprintf (stderr, "%s: %s\n", filename, strerror (errno));
	fclose (f);
	unlink (filename);
	return NULL;
    }
    rv = fclose (f);
    if (rv) {
	fprintf (stderr, "%s: %s\n", filename, strerror (errno));
	unlink (filename);
	return NULL;
    }

    command = git_format_command ("git mktag < '%s'", filename);
    if (!command) {
	unlink (filename);
	return NULL;
    }
    tag_sha1 = git_system_to_string (command);
    unlink (filename);
    free (command);
    return tag_sha1;
}

static int
git_tag (rev_commit *commit, char *name)
{
    char    *tag_sha1;

    tag_sha1 = git_mktag (commit, name);
    if (!tag_sha1)
	return 0;
    return git_update_ref (tag_sha1, "tags", name);
}

static int
git_head (rev_commit *commit, char *name)
{
    return git_update_ref (commit->sha1, "heads", name);
}

static int
git_commit_recurse (rev_ref *head, rev_commit *commit, int strip)
{
    Tag *t;
    
    if (commit->parent && !commit->tail)
	    if (!git_commit_recurse (head, commit->parent, strip))
		return 0;
    ++git_current_commit;
    git_status ();
    if (!git_commit (commit))
	return 0;
    for (t = all_tags; t; t = t->next)
	if (t->commit == commit)
	    if (!git_tag (commit, t->name))
		return 0;
    return 1;
}

static int
git_head_commit (rev_ref *head, int strip)
{
    git_current_head = head->name;
    if (!head->tail)
        if (!git_commit_recurse (head, head->commit, strip))
	    return 0;
    if (!git_head (head->commit, head->name))
	return 0;
    return 1;
}

static int
git_ncommit (rev_list *rl)
{
    rev_ref	*h;
    rev_commit	*c;
    int		n = 0;
    
    for (h = rl->heads; h; h = h->next) {
	if (h->tail)
	    continue;
	for (c = h->commit; c; c = c->parent) {
	    n++;
	    if (c->tail)
		break;
	}
    }
    return n;
}

int
git_rev_list_commit (rev_list *rl, int strip)
{
    rev_ref *h;

    git_total_commits = git_ncommit (rl);
    git_current_commit = 0;
    for (h = rl->heads; h; h = h->next)
	if (!git_head_commit (h, strip))
	    return 0;
    fprintf (STATUS, "\n");
//    if (!git_checkout ("master"))
//	return 0;
    return 1;
}

#define PROGRESS_LEN	20

void load_status (char *name)
{
    int	spot = load_current_file * PROGRESS_LEN / load_total_files;
    int	    s;
    int	    l;

    if (rev_mode == ExecuteGraph)
	return;
    l = strlen (name);
    if (l > 35) name += l - 35;

    fprintf (STATUS, "Load: %35.35s ", name);
    for (s = 0; s < PROGRESS_LEN + 1; s++)
	putc (s == spot ? '*' : '.', STATUS);
    fprintf (STATUS, " %5d of %5d\n", load_current_file, load_total_files);
    fflush (STATUS);
}

void load_status_next (void)
{
    if (rev_mode == ExecuteGraph)
	return;
    fprintf (STATUS, "\n");
    fflush (STATUS);
}

