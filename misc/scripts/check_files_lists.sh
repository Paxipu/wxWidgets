#!/bin/bash

# build/bakefiles/files.bkl and build/cmake/files.cmake are generated from
# build/files by build/upmake, so a file added to build/files alone builds
# under some build systems and not others.
#
# build/upmake rewrites the list in the source tree wherever it is pointed,
# so a failure here leaves the lists regenerated: that is the fix, ready to
# be committed.

cd $(dirname "$0")/../..

rc=0

for f in build/bakefiles/files.bkl build/cmake/files.cmake ; do
    ./build/upmake build/files "$f" >/dev/null
    if ! git diff --quiet -- "$f" ; then
        echo "ERROR - $f was out of date with build/files"
        echo "        and has been regenerated; commit the result"
        rc=$((rc+1))
    fi
done

# Makefile.in is generated too, but by bakefile rather than upmake, and
# bakefile 0.2 needs Python 2 and so cannot be run on a current machine
# without a container. A source added to build/files by hand therefore
# reaches the CMake and bakefile lists and silently misses the one the
# autoconf build reads. Checking that every GTK source is named there costs
# nothing and catches that, without needing bakefile to say so.
for f in $(grep -oE "src/gtk/[a-z0-9_]+\.cpp" build/files | sort -u) ; do
    if ! grep -q "$(basename "$f")" Makefile.in ; then
        echo "ERROR - $f is in build/files but not in Makefile.in;"
        echo "        regenerate it with bakefile, or add its rules the way"
        echo "        the neighbouring sources have them"
        rc=$((rc+1))
    fi
done

exit $rc
