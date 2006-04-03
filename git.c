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
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define MAXPATHLEN  10240

#define GIT_REV_LEN 128

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

static int
git_system (char *command)
{
    int	ret = 0;
    
//    printf ("\t%s\n", command);
#if 1
    ret = system (command);
    if (ret != 0) {
	fprintf (stderr, "%s failed\n", command);
    }
#endif
    return ret;
}

static int
git_unlink (char *file)
{
//    printf ("\trm '%s'\n", file);
#if 1
    if (unlink (file) != 0 && errno != ENOENT) {
	fprintf (stderr, "rm '%s' failed\n", file);
	return 0;
    }
#endif
    return 1;
}

static int
git_del_file (rev_file *file, int strip)
{
    char    filename[MAXPATHLEN + 1];
    char    command[MAXPATHLEN * 2 + 1];
    
    if (!git_filename (file, filename, strip))
	return 0;
    git_unlink (filename);
    snprintf (command, sizeof (command) - 1, "git-update-index --remove '%s'",
	      filename);
    if (git_system (command) != 0)
	return 0;
    return 1;
}

static int
git_ensure_path (char *filename)
{
    char    dirname[MAXPATHLEN + 1];
    char    *slash;
    struct stat	buf;

    strcpy (dirname, filename);
    slash = strrchr (dirname + 1, '/');
    if (slash) {
	*slash = '\0';
	if (stat (dirname, &buf) == 0) {
	    if (!S_ISDIR(buf.st_mode)) {
		fprintf (stderr, "%s: not a directory\n", dirname);
		return 0;
	    }
	} else if (errno == ENOENT) {
	    if (!git_ensure_path (dirname))
		return 0;
	    if (mkdir (dirname, 0777) != 0) {
		fprintf (stderr, "%s: %s\n", dirname, strerror (errno));
		return 0;
	    }
	} else {
	    fprintf (stderr, "%s: %s\n", dirname, strerror (errno));
	    return 0;
	}
    }
    return 1;
}

static int
git_checkout_file (rev_file *file, char *filename)
{
    char    command[MAXPATHLEN * 2 + 1];
    char    rev[CVS_MAX_REV_LEN];
    struct stat	buf;

    if (stat (file->name, &buf) != 0) {
	fprintf (stderr, "%s: %s\n", file->name, strerror (errno));
	return 0;
    }
    git_unlink (filename);
    cvs_number_string (&file->number, rev);
    git_ensure_path (filename);
    snprintf (command, sizeof (command) - 1, "co -kk -p%s '%s' > '%s' 2>/dev/null",
	      rev, file->name, filename);
    if (git_system (command) != 0) {
	fprintf (stderr, "%s failed\n", command);
	return 0;
    }
    if (chmod (filename, buf.st_mode & 0777) != 0) {
	fprintf (stderr, "%s: %s\n", filename, strerror (errno));
	return 0;
    }
    return 1;
}

static int
git_add_file (rev_file *file, int strip)
{
    char    filename[MAXPATHLEN + 1];
    char    command[MAXPATHLEN * 2 + 1];
    
    if (!git_filename (file, filename, strip))
	return 0;
    if (!git_checkout_file (file, filename))
	return 0;
    snprintf (command, sizeof (command) - 1, "git-update-index --add '%s'",
	      filename);
    if (git_system (command) != 0)
	return 0;
    return 1;
}

static int
git_update_file (rev_file *file, int strip)
{
    char    filename[MAXPATHLEN + 1];
    char    command[MAXPATHLEN * 2 + 1];
    
    if (!git_filename (file, filename, strip))
	return 0;
    if (!git_checkout_file (file, filename))
	return 0;
    snprintf (command, sizeof (command) - 1, "git-update-index '%s'",
	      filename);
    if (git_system (command) != 0)
	return 0;
    return 1;
}

#define GIT_CVS_DIR ".git-cvs"

static char *
git_cvs_file (char *base)
{
    char    filename_buf[MAXPATHLEN + 1];
    char    *filename;
    static int	id;
    
    if (id == 0)
    {
	if (mkdir (GIT_CVS_DIR, 0777) < 0 && errno != EEXIST) {
	    fprintf (stderr, "%s: %s\n", GIT_CVS_DIR, strerror (errno));
	    return NULL;
	}
    }
    snprintf (filename_buf, sizeof (filename_buf),
	      "%s/%s-%d", GIT_CVS_DIR, base, id++);
    filename = atom (filename_buf);
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
	char	command[MAXPATHLEN*2+1];
	snprintf (command, sizeof (command),
		  "%s '%s'", LOG_COMMAND, filename);
	if (git_system (command) != 0)
	    return NULL;
    }
#endif
    return filename;
}

static char *
git_tree_file (void)
{
    return git_cvs_file ("id");
}

static char *
git_commit_file (void)
{
    return git_cvs_file ("commit");
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
	    return 0;
	}
	*equal = '\0';
	full = equal + 1;
	name = atom (line);
	if (git_fullname (name)) {
	    fprintf (stderr, "%s: (%d) duplicate name '%s' ignored\n",
		     filename, lineno, name);
	    return 0;
	}
	a = calloc (1, sizeof (cvs_author));
	a->name = name;
	angle = strchr (full, '<');
	if (!angle) {
	    fprintf (stderr, "%s: (%d) missing email address '%s'\n",
		     filename, lineno, name);
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
	    return 0;
	}
	*angle = '\0';
	a->email = atom (email);
	bucket = &author_buckets[((unsigned long) name) % AUTHOR_HASH];
	a->next = *bucket;
	*bucket = a;
    }
    return 1;
}

static char *
git_load_file (char *file)
{
    FILE    *f;
    char    buf[1024];
    char    *nl;
    
    f = fopen (file, "r");
    if (!f) {
	fprintf (stderr, "%s: %s\n", file, strerror (errno));
	return NULL;
    }
    if (fgets (buf, sizeof (buf), f) == NULL) {
	fprintf (stderr, "%s: %s\n", file, strerror (errno));
	fclose (f);
	return NULL;
    }
    fclose (f);
    nl = strchr (buf, '\n');
    if (nl)
	*nl = '\0';
    return atom (buf);
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

    fprintf (STATUS, "              ");
    fprintf (STATUS, "%28.28s: ", git_current_head);
    for (s = 0; s < PROGRESS_LEN + 1; s++)
	putc (s == spot ? '*' : '.', STATUS);
    fprintf (STATUS, " %5d of %5d\r", git_current_commit, git_total_commits);
    fflush (STATUS);
}

static void
git_spin (void)
{
    fprintf (STATUS, "%5d of %5d\r", git_current_file, git_total_files);
    fflush (STATUS);
}

/*
 * Create a commit object in the repository using the current
 * index and the information from the provided rev_commit
 */
static int
git_commit (rev_commit *commit)
{
    char    command[MAXPATHLEN * 2];
    char    *log;
    char    *tree;
    char    *id;
    cvs_author	*author;
    char    *tree_sha1;
    char    *full;
    char    *email;
    char    *date;

    log = git_log_file (commit);
    if (!log)
	return 0;
    tree = git_tree_file ();
    if (!tree)
	return 0;
    id = git_commit_file ();
    snprintf (command, sizeof (command) - 1,
	      "git-write-tree --missing-ok > '%s'", tree);
    if (git_system (command) != 0)
	return 0;
    tree_sha1 = git_load_file (tree);
    if (!tree_sha1)
	return 0;

    /*
     * Prepare environment for git-commit-tree
     */
    author = git_fullname (commit->author);
    if (!author) {
	fprintf (stderr, "%s: not in author map\n", commit->author);
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
        snprintf (command, sizeof (command) - 1,
	          "git-commit-tree '%s' -p '%s' < '%s' > '%s'",
		  tree_sha1, commit->parent->sha1, log, id);
    else
        snprintf (command, sizeof (command) - 1,
	          "git-commit-tree '%s' < '%s' > '%s' 2> /dev/null",
		  tree_sha1, log, id);
    if (git_system (command) != 0)
	return 0;
    commit->sha1 = git_load_file (id);
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
    old = new;
    return 1;
}

static int
git_update_ref (rev_commit *commit, char *type, char *name)
{
    char    command[MAXPATHLEN * 2 + 1];

    snprintf (command, sizeof (command) - 1, "git-update-ref 'refs/%s/%s' '%s'",
	      type, name, commit->sha1);
    if (git_system (command) != 0)
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
    char    command[MAXPATHLEN*2+1];

    snprintf (command, sizeof (command), 
	      "git-read-tree '%s'", commit->sha1);
    if (git_system (command) != 0)
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
    } else {
	if (git_system ("git-init-db > /dev/null") != 0)
	    return 0;
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
git_checkout (char *name)
{
    char    command[MAXPATHLEN*2+1];

    snprintf (command, sizeof (command),
	      "git-reset --hard '%s'", name);
    if (git_system (command) != 0)
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
    if (!git_checkout ("master"))
	return 0;
    return 1;
}
