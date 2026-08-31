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

exit $rc
