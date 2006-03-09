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

#ifndef _CVS_H_
#define _CVS_H_

#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define CVS_MAX_DEPTH	16

typedef struct _cvs_number {
    int			c;
    int 		n[CVS_MAX_DEPTH];
} cvs_number;

typedef struct _cvs_symbol {
    struct _cvs_symbol	*next;
    char		*name;
    cvs_number		*number;
} cvs_symbol;

typedef struct _cvs_branch {
    struct _cvs_branch	*next;
    cvs_number		*number;
} cvs_branch;

typedef struct _cvs_version {
    struct _cvs_version	*next;
    cvs_number		*number;
    time_t		date;
    char		*author;
    cvs_branch		*branches;
    cvs_number		*parent;	/* next in ,v file */
    char		*commitid;
} cvs_version;

typedef struct _cvs_patch {
    struct _cvs_patch	*next;
    cvs_number		*number;
    char		*log;
    char		*text;
} cvs_patch;

typedef struct {
    cvs_number		*head;
    cvs_number		*branch;
    cvs_symbol		*symbols;
    cvs_version		*versions;
    cvs_patch		*patches;
} cvs_file;

extern cvs_file     *this_file;

int yyparse (void);

cvs_number *
lex_number (char *);

time_t
lex_date (cvs_number *n);

#endif /* _CVS_H_ */
