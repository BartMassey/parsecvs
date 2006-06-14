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
    char    *filename_buf;
    char    *filename;
    static int	id;
    
    if (id == 0)
    {
	if (mkdir (GIT_CVS_DIR, 0777) < 0 && errno != EEXIST) {
	    fprintf (stderr, "%s: %s\n", GIT_CVS_DIR, strerror (errno));
	    return NULL;
	}
    }
    filename_buf = git_format_command ("%s/%s-%d",
				       GIT_CVS_DIR, base, id++);
    if (!filename_buf)
	return NULL;
    filename = atom (filename_buf);
    free (filename_buf);
    return filename;
}

#define LOG_COMMAND "edit-change-log"

static char *
git_log_file (rev_commit *commit)
{
    char    *filename;
    FILE    *f;
    
    filename = git_cvs_file ("log");
    if (!filename)
	return NULL;
    f = fopen (filename, "w");
    if (!f) {
	fprintf (stderr, "%s: %s\n", filename, strerror (errno));
	return NULL;
    }
    if (fputs (commit->log, f) == EOF) {
	fprintf (stderr, "%s: %s\n", filename, strerror (errno));
	fclose (f);
	return NULL;
    }
    fclose (f);
#ifdef LOG_COMMAND
    {
	char	*command;
	int	n;
	command = git_format_command ("%s '%s'", LOG_COMMAND, filename);
	if (!command)
	    return NULL;
	n = git_system (command);
	free (command);
	if (n != 0)
	    return NULL;
    }
#endif
    return filename;
}

static FILE *git_update_data;
static char *git_update_data_name;

static int
git_start_switch (void)
{
    git_update_data_name = git_cvs_file ("switch");
    if (!git_update_data_name)
	return 0;
    git_update_data = fopen (git_update_data_name, "w");
    if (!git_update_data)
	return 0;
    return 1;
}

static int
git_end_switch (void)
{
    char    *command;
    int	    n;
    
    if (fclose (git_update_data) == EOF)
	return 0;
    command = git_format_command ("git-update-index --index-info < %s",
				  git_update_data_name);
    if (!command)
	return 0;
    n = git_system (command);
    free (command);
    if (n != 0)
	return 0;
    return 1;
}

static int
git_del_file (rev_file *file, int strip)
{
    /* avoid stack allocation to avoid running out of stack */
    static char    filename[MAXPATHLEN + 1];
    
    if (!git_filename (file, filename, strip))
	return 0;
    fprintf (git_update_data, 
	     "0 0000000000000000000000000000000000000000\t%s\n",
	     filename);
    return 1;
}

static int
git_add_file (rev_file *file, int strip)
{
    /* avoid stack allocation to avoid running out of stack */
    static char    filename[MAXPATHLEN + 1];
    
    if (!git_filename (file, filename, strip))
	return 0;
    fprintf (git_update_data, 
	     "%o %s\t%s\n",
	     file->mode, file->sha1, filename);
    return 1;
}

static int
git_update_file (rev_file *file, int strip)
{
    return git_add_file (file, strip);
}

typedef struct _cvs_author {
    struct _cvs_author	*next;
    char		*name;
    char		*full;
    char		*email;
} cvs_author;

#define AUTHOR_HASH 1021

static cvs_author	*author_buckets[AUTHOR_HASH];

static cvs_author *
git_fullname (char *name)
{
    cvs_author	**bucket = &author_buckets[((unsigned long) name) % AUTHOR_HASH];
    cvs_author	*a;

    for (a = *bucket; a; a = a->next)
	if (a->name == name)
	    return a;
    return NULL;
}

void
git_free_author_map (void)
{
    int	h;

    for (h = 0; h < AUTHOR_HASH; h++) {
	cvs_author	**bucket = &author_buckets[h];
	cvs_author	*a;

	while ((a = *bucket)) {
	    *bucket = a->next;
	    free (a);
	}
    }
}

static int
git_load_author_map (char *filename)
{
    char    line[10240];
    char    *equal;
    char    *angle;
    char    *email;
    char    *name;
    char    *full;
    FILE    *f;
    int	    lineno = 0;
    cvs_author	*a, **bucket;
    
    f = fopen (filename, "r");
    if (!f) {
	fprintf (stderr, "%s: %s\n", filename, strerror (errno));
	return 0;
    }
    while (fgets (line, sizeof (line) - 1, f)) {
	lineno++;
	if (line[0] == '#')
	    continue;
	equal = strchr (line, '=');
	if (!equal) {
	    fprintf (stderr, "%s: (%d) missing '='\n", filename, lineno);
	    fclose (f);
	    return 0;
	}
	*equal = '\0';
	full = equal + 1;
	name = atom (line);
	if (git_fullname (name)) {
	    fprintf (stderr, "%s: (%d) duplicate name '%s' ignored\n",
		     filename, lineno, name);
	    fclose (f);
	    return 0;
	}
	a = calloc (1, sizeof (cvs_author));
	a->name = name;
	angle = strchr (full, '<');
	if (!angle) {
	    fprintf (stderr, "%s: (%d) missing email address '%s'\n",
		     filename, lineno, name);
	    fclose (f);
	    return 0;
	}
	email = angle + 1;
        while (angle > full && angle[-1] == ' ')
	    angle--;
	*angle = '\0';
	a->full = atom(full);
	angle = strchr (email, '>');
	if (!angle) {
	    fprintf (stderr, "%s: (%d) malformed email address '%s\n",
		     filename, lineno, name);
	    fclose (f);
	    return 0;
	}
	*angle = '\0';
	a->email = atom (email);
	bucket = &author_buckets[((unsigned long) name) % AUTHOR_HASH];
	a->next = *bucket;
	*bucket = a;
    }
    fclose (f);
    return 1;
}

static int git_total_commits;
static int git_current_commit;
static char *git_current_head;
static int git_total_files;
static int git_current_file;

#define STATUS	stdout
#define PROGRESS_LEN	20

static void
git_status (void)
{
    int	spot = git_current_commit * PROGRESS_LEN / git_total_commits;
    int	s;

    fprintf (STATUS, "Save: %35.35s ", git_current_head);
    for (s = 0; s < PROGRESS_LEN + 1; s++)
	putc (s == spot ? '*' : '.', STATUS);
    fprintf (STATUS, " %5d of %5d\r", git_current_commit, git_total_commits);
    fflush (STATUS);
}

static void
git_spin (void)
{
//    fprintf (STATUS, "%5d of %5d\r", git_current_file, git_total_files);
//    fflush (STATUS);
}

/*
 * Create a commit object in the repository using the current
 * index and the information from the provided rev_commit
 */
static int
git_commit (rev_commit *commit)
{
    char    *command;
    char    *log;
    cvs_author	*author;
    char    *tree_sha1;
    char    *full;
    char    *email;
    char    *date;

    log = git_log_file (commit);
    if (!log)
	return 0;
    tree_sha1 = git_system_to_string ("git-write-tree --missing-ok");
    if (!tree_sha1)
	return 0;

    /*
     * Prepare environment for git-commit-tree
     */
    author = git_fullname (commit->author);
    if (!author) {
//	fprintf (stderr, "%s: not in author map\n", commit->author);
	full = commit->author;
	email = commit->author;
    } else {
	full = author->full;
	email = author->email;
    }
    date = ctime_nonl (&commit->date);
    setenv ("GIT_AUTHOR_NAME", full, 1);
    setenv ("GIT_AUTHOR_EMAIL", email, 1);
    setenv ("GIT_AUTHOR_DATE", date, 1);
    setenv ("GIT_COMMITTER_NAME", full, 1);
    setenv ("GIT_COMMITTER_EMAIL", email, 1);
    setenv ("GIT_COMMITTER_DATE", date, 1);
    if (commit->parent)
	command = git_format_command ("git-commit-tree '%s' -p '%s' < '%s'",
				      tree_sha1, commit->parent->sha1, log);
    else
	command = git_format_command ("git-commit-tree '%s' < '%s' 2>/dev/null",
				      tree_sha1, log);
    if (!command)
	return 0;
    commit->sha1 = git_system_to_string (command);
    free (command);
    if (!commit->sha1)
	return 0;
    return 1;
}

static int
git_switch_commit (rev_commit *old, rev_commit *new, int strip)
{
    rev_diff	    *diff = rev_commit_diff (old, new);
    rev_file_list   *fl;

    git_total_files = diff->ndel + diff->nadd;
    git_current_file = 0;
    git_start_switch ();
    for (fl = diff->del; fl; fl = fl->next) {
	++git_current_file;
	git_spin ();
	if (!rev_file_list_has_filename (diff->add, fl->file->name))
	    if (!git_del_file (fl->file, strip))
		return 0;
    }
    for (fl = diff->add; fl; fl = fl->next) {
	++git_current_file;
	git_spin ();
	if (rev_file_list_has_filename (diff->del, fl->file->name)) {
	    if (!git_update_file (fl->file, strip))
		return 0;
	} else {
	    if (!git_add_file (fl->file, strip))
		return 0;
	}
    }
    rev_diff_free (diff);
    if (!git_end_switch ())
	return 0;
    return 1;
}

static int
git_update_ref (rev_commit *commit, char *type, char *name)
{
    char    *command;
    int	    n;

    command = git_format_command ("git-update-ref 'refs/%s/%s' '%s'",
				  type, name, commit->sha1);
    if (!command)
	return 0;
    n = git_system (command);
    free (command);
    if (n != 0)
	return 0;
    return 1;
}

static int
git_tag (rev_commit *commit, char *name)
{
    return git_update_ref (commit, "tags", name);
}

static int
git_head (rev_commit *commit, char *name)
{
    return git_update_ref (commit, "heads", name);
}

static int
git_read_tree (rev_commit *commit)
{
    char    *command;
    int	    n;

    command = git_format_command ("git-read-tree '%s'", commit->sha1);
    if (!command)
	return 0;
    n = git_system (command);
    free (command);
    if (n != 0)
	return 0;
    return 1;
}

static int
git_commit_recurse (rev_ref *head, rev_commit *commit, rev_ref *tags, int strip)
{
    rev_ref *t;
    
    if (commit->parent) {
        if (commit->tail) {
	    if (!git_read_tree (commit->parent))
		return 0;
	} else {
	    if (!git_commit_recurse (head, commit->parent, tags, strip))
		return 0;
	}
    }
    ++git_current_commit;
    git_status ();
    if (!git_switch_commit (commit->parent, commit, strip))
	return 0;
    if (!git_commit (commit))
	return 0;
    for (t = tags; t; t = t->next)
	if (t->commit == commit)
	    if (!git_tag (commit, t->name))
		return 0;
    return 1;
}

static int
git_head_commit (rev_ref *head, rev_ref *tags, int strip)
{
    git_current_head = head->name;
    if (!head->tail)
        if (!git_commit_recurse (head, head->commit, tags, strip))
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

    git_load_author_map ("Authors");
    git_total_commits = git_ncommit (rl);
    git_current_commit = 0;
    for (h = rl->heads; h; h = h->next)
	if (!git_head_commit (h, rl->tags, strip))
	    return 0;
    fprintf (STATUS, "\n");
//    if (!git_checkout ("master"))
//	return 0;
    return 1;
}

