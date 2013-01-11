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

#include <limits.h>
#include <openssl/sha.h>
#include "cvs.h"

static int mark;

void
export_init(void)
{
    mark = 0;
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
export_blob(Node *node, void *buf, unsigned long len)
{
    char sha1_ascii[41];
    unsigned char sha1[20];

    SHA1(buf, len, sha1);
    strncpy(sha1_ascii, sha1_to_hex(sha1), 41);
    node->file->sha1 = atom(sha1_ascii);
    node->file->mark = ++mark;

    if (rev_mode == ExecuteExport)
    {
	printf("blob\nmark :%d\ndata %zd\n", 
	       node->file->mark, len);
	fwrite(buf, len, sizeof(char), stdout);
	putchar('\n');
    }
}

static int
export_filename (rev_file *file, char *name, int strip)
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

static int export_total_commits;
static int export_current_commit;
static char *export_current_head;

#define STATUS	stderr
#define PROGRESS_LEN	20

static void
export_status (void)
{
    int	spot = export_current_commit * PROGRESS_LEN / export_total_commits;
    int	s;

    fprintf (STATUS, "Save: %35.35s ", export_current_head);
    for (s = 0; s < PROGRESS_LEN + 1; s++)
	putc (s == spot ? '*' : '.', STATUS);
    fprintf (STATUS, " %5d of %5d\n", export_current_commit, export_total_commits);
    fflush (STATUS);
}

static void
export_commit(rev_commit *commit, char *branch, int strip)
{
    cvs_author *author;
    char *full;
    char *email;
    rev_file	*f;
    int		i, j;

    author = fullname(commit->author);
    if (!author) {
	full = commit->author;
	email = commit->author;
    } else {
	full = author->full;
	email = author->email;
    }

    printf("commit refs/heads/%s\n", branch);
    printf("mark :%d\n", ++mark);
    commit->mark = mark;
    if (commit->parent)
	printf("from :%d\n", commit->parent->mark);
    printf("author %s <%s> %lu +0000\n",
	       full, email, commit->date);
    printf("committer %s <%s> %lu +0000\n",
	       full, email, commit->date);
    printf("data %zd\n%s\n", strlen(commit->log), commit->log);

    for (i = 0; i < commit->ndirs; i++) {
	rev_dir	*dir = commit->dirs[i];
	
	for (j = 0; j < dir->nfiles; j++) {
	    char stem[PATH_MAX], *end;
	    f = dir->files[j];
	    strcpy(stem, f->name + strip);
	    end = stem + strlen(stem);
	    if (end > stem + 2 && end[-1] == 'v' && end[-2] == ',')
		end[-2] = '\0';
	    if (strcmp(stem, ".cvsignore") == 0)
	    {
		stem[1] = 'g';
		stem[2] = 'i';
		stem[3] = 't';
	    }	
	    printf("M %s :%d\n", stem, f->mark);
	}
    }
    printf ("\n");

}

static int
export_commit_recurse (rev_ref *head, rev_commit *commit, int strip)
{
    Tag *t;
    
    if (commit->parent && !commit->tail)
	    if (!export_commit_recurse (head, commit->parent, strip))
		return 0;
    ++export_current_commit;
    export_status ();
    export_commit (commit, head->name, strip);
    for (t = all_tags; t; t = t->next)
	if (t->commit == commit)
	    printf("reset refs/tags/%s\nmark :%d\n\n", t->name, commit->mark);
    return 1;
}

static int
export_ncommit (rev_list *rl)
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
export_commits (rev_list *rl, int strip)
{
    rev_ref *h;

    export_total_commits = export_ncommit (rl);
    export_current_commit = 0;
    for (h = rl->heads; h; h = h->next) 
    {
	export_current_head = h->name;
	if (!h->tail)
	    if (!export_commit_recurse (h, h->commit, strip))
		return 0;
	printf("reset refs/heads %s\nmark :%d\n\n", h->name, h->commit->mark);
    }
    fprintf (STATUS, "\n");
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

