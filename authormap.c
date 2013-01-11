/*
 * Manage a map from short CVS-syle names to DVCS-style name/email pairs.
 */

#include "cvs.h"

#define AUTHOR_HASH 1021

static cvs_author	*author_buckets[AUTHOR_HASH];

cvs_author *
fullname (char *name)
/* return the fullname structure corresponding to a specified shortname */
{
    cvs_author	**bucket = &author_buckets[((unsigned long) name) % AUTHOR_HASH];
    cvs_author	*a;

    for (a = *bucket; a; a = a->next)
	if (a->name == name)
	    return a;
    return NULL;
}

void
free_author_map (void)
/* discard author-map information */
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

int
load_author_map (char *filename)
/* load author-map information from a file */
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
	return 1;
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
	full = equal + 1;
	while (equal > line && equal[-1] == ' ')
	    equal--;
	*equal = '\0';
	name = atom (line);
	if (fullname (name)) {
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
	    free(a);
	    return 0;
	}
	email = angle + 1;
	while (full < angle && full[0] == ' ')
	    full++;
        while (angle > full && angle[-1] == ' ')
	    angle--;
	*angle = '\0';
	a->full = atom(full);
	angle = strchr (email, '>');
	if (!angle) {
	    fprintf (stderr, "%s: (%d) malformed email address '%s\n",
		     filename, lineno, name);
	    fclose (f);
	    free(a);
	    return 0;
	}
	*angle = '\0';
	a->email = atom (email);
	a->timezone = NULL;
	if (*++angle) {
	    while (isspace(*angle))
		angle++;
	    while (*angle != '\0') {
		char *end = angle + strlen(angle) - 1;
		if (isspace(*end))
		    *end = '\0';
		else
		    break;
	    }
	    a->timezone = atom(angle);
	}
	bucket = &author_buckets[((unsigned long) name) % AUTHOR_HASH];
	a->next = *bucket;
	*bucket = a;
    }
    fclose (f);
    return 1;
}

/* end */
