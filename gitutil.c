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

int
git_system (char *command)
{
    int	ret = 0;
    
/*    printf ("\t%s\n", command); */
#if 1
    ret = system (command);
    if (ret != 0) {
	fprintf (stderr, "%s failed\n", command);
    }
#endif
    return ret;
}

char *
git_system_to_string (char *command)
{
    FILE    *f;
    char    buf[1024];
    char    *nl;

    f = popen (command, "r");
    if (!f) {
	fprintf (stderr, "%s: %s\n", command, strerror (errno));
	return NULL;
    }
    if (fgets (buf, sizeof (buf), f) == NULL) {
	fprintf (stderr, "%s: %s\n", command, strerror (errno));
	pclose (f);
	return NULL;
    }
    while (getc (f) != EOF)
	;
    pclose (f);
    nl = strchr (buf, '\n');
    if (nl)
	*nl = '\0';
    return atom (buf);
}

int
git_string_to_system (char *command, char *string)
{
    FILE    *f;

    f = popen (command, "w");
    if (!f) {
	fprintf (stderr, "%s: %s\n", command, strerror (errno));
	return -1;
    }
    if (fputs (string, f) == EOF) {
	fprintf (stderr, "%s: %s\n", command, strerror (errno));
	pclose (f);
	return -1;
    }
    pclose (f);
    return 0;
}

char *
git_format_command (const char *fmt, ...) 
{
    /* Guess we need no more than 100 bytes. */
    int n, size = 100;
    char *p, *np;
    va_list ap;

    if ((p = malloc (size)) == NULL)
	return NULL;

    while (1) {
	/* Try to print in the allocated space. */
	va_start(ap, fmt);
	n = vsnprintf (p, size, fmt, ap);
	va_end(ap);
	/* If that worked, return the string. */
	if (n > -1 && n < size)
	    return p;
	/* Else try again with more space. */
	if (n > -1)    /* glibc 2.1 */
	size = n+1; /* precisely what is needed */
	else           /* glibc 2.0 */
	size *= 2;  /* twice the old size */
	if ((np = realloc (p, size)) == NULL) {
	    free(p);
	    return NULL;
	} else {
	    p = np;
	}
    }
}
