# cvs-fast-export
Copyright © 2013 Keith Packard, Bart Massey and Eric Raymond

You may want to look at <https://gitlab.com/esr/cvs-fast-export>,
which contains Eric Raymond's currently-maintained version of
`cvs-fast-export`. Probably this GitHub repo should be backed off
to before Eric's changes, when it was the latest known version
of `parsecvs`.

-----

This is the former canonical repo for `git
cvs-fast-export`, formerly `parsecvs`. This tool does what
its new name implies: exports `cvs` repositories in a format
suitable for `git fast-import`.

This tool is currently being maintained by Eric Raymond, who
has rewritten parts of it to support current `git` in a more
sensible way.

Below are a bunch of stale notes and old versions of README
material, preserved for historical reasons. You might just
want to ignore them.

> Bart Massey  
> bart@cs.pdx.edu  
> 2013-01-14

-----

Note: The standalone code currently finally did not run
properly in at least some instances. It is suspected that
this was due to changes in `libgit` that hadn't been tracked
in the `parsecvs` code. Fortunately, Eric Raymond's changes to
support `git fast-import` mooted that problem.

-----

Note: Building `parsecvs` formerly required a copy of libgit
and its header files to be on your system. For my Debian
system, that meant pulling the Git source and building
it. The Makefile variable GITDIR should be aimed at an
appropriate location if you ever try to build old code.

-----

This is the current canonical repo for `parsecvs`. It merges
three different Git repos:

  * The latest copy I could find of Keith Packard's original repo.
  * Kristian Hogsberg's repo from freedesktop.org. This was
    a fast-forward merge.
  * Ivan Zakharyaschev's repo from gitorious.org. This had two
    merge conflicts, which I hope I resolved in sensible ways.

I will commit (no pun intended) to maintaining this version
as the "one true branch" on github going forward. I will not
actively look for patches, but if people send them to me I
will look at getting them in. Further, if it breaks for me,
I will fix it and push my patches here.

I was in on the original design of `parsecvs`, and have gotten
a lot of use out of it over the years. Hopefully it will
remain useful for years to come.

>Bart Massey  
>bart@cs.pdx.edu   
>2011-02-15

-----

Here is the original `parsecvs` README. Much of this
information is out-of-date as of 2013, but I preserve it in
case it might be useful.

                                        Parsecvs
                                   keithp@keithp.com
                                      April, 2006

        This directory contains code which can directly read RCS ,v files and
        generate a git-style rev-list structure from them. Revision lists can be
        merged together to produce a composite revision history for an arbitrary
        collection of files.

        Optional behaviors are controlled by editing the source and recompiling.

        If arguments are supplied, `parsecvs` assumes they're all ,v files and reads
        them in. If no arguments are supplied, `parsecvs` reads filenames from stdin,
        one per line.

        Working features:

                Attic support. Files found in the Attic are not dealt with specially
                at all; they should be renamed in the output, and the terminal
                revision noted so that they don't appear in later revision. I think
                fixing this will be reasonably straightforward.

                Disjoint branch resolution. Branches occurring in a subset of the
                files are not correctly resolved; instead, an entirely disjoint
                history will be created containing the branch revisions and all
                parents back to the root. I'm not sure how to fix this; it seems
                to implicitly assume there will be only a single place to attach as
                branch parent, which may not be the case. In any case, the right
                revision will have a superset of the revisions present in the
                original branch parent; perhaps that will suffice.

                Connection to git. As mentioned above, the code doesn't actually
                connect to git yet, so while it can generate lovely graphs, it won't
                do anything useful. I think this is reasonably straight forward as
                well; we've got a revision history containing the necessary version
                of every file at each point in time. This could either be done by
                emitting git commands and sending them to a shell, or by linking
                against a git library and doing everything internally.

                Author translation. Just as git cvsimport does.


        Missing features:

                Reasonable command line syntax. The current lack of command line
                parsing should be fixed to align with the usual git tools.

                Testing. I'm sure there are plenty of additional bugs to be found;
                I've tested with valgrind and eliminated memory leaks and other
                errors.

