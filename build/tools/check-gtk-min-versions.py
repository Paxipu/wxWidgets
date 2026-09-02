#!/usr/bin/env python3
"""Fail if configure.ac and CMake disagree about the minimum GTK versions.

wx has two build systems, and each carries its own copy of "the oldest GTK we
support". Nothing connects them, so raising the floor in one and forgetting the
other produces a build that works for whoever uses that build system and breaks
for everyone else -- and the breakage is not a build error but a link-time or
run-time failure on an old GTK, which is exactly the kind that reaches a user
before it reaches a maintainer.

That is issue #194. This check is the automatic part of the answer: the numbers
stay written where each build system needs them, and drift is caught here.

Three things are compared:

  1. configure.ac's PKG_CHECK_MODULES minimum against toolkit.cmake's
     find_package() minimum, per GTK version.

  2. Every *other* mention of a GTK4 minimum in configure.ac against the one
     PKG_CHECK_MODULES asks for -- configure.ac has more than one place
     naming 4.10, and they can drift from each other without CMake being
     involved at all.

  3. GDK_VERSION_MIN_REQUIRED, which both build systems set for GTK4, against
     that same minimum. Announcing a floor of 4.10 to pkg-config while telling
     GDK the floor is something else would produce either warnings wx cannot
     act on or silence about ones it should hear.

"No minimum" is a legitimate answer -- configure.ac asks for plain gtk+-3.0 --
and has to be spelled the same way on both sides.

Exits 0 when everything agrees, 1 otherwise, printing what disagrees.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

CONFIGURE_AC = os.path.join(ROOT, "configure.ac")
TOOLKIT_CMAKE = os.path.join(ROOT, "build", "cmake", "toolkit.cmake")

# pkg-config module name -> the name used in messages
MODULES = {
    "gtk+-2.0": "GTK+ 2",
    "gtk+-3.0": "GTK+ 3",
    "gtk4": "GTK4",
}

# pkg-config module name -> the CMake variable guard it lives under
CMAKE_GUARD = {
    "gtk+-2.0": "WXGTK2",
    "gtk+-3.0": "WXGTK3",
    "gtk4": "WXGTK4",
}


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def configure_minimums(text):
    """{module: version or None} from configure.ac's PKG_CHECK_MODULES calls."""
    found = {}
    # PKG_CHECK_MODULES(GTK, [gtk4 >= 4.10], ...) and the unbracketed form
    # PKG_CHECK_MODULES(GTK, gtk+-3.0, ...) which means "any version".
    for m in re.finditer(
        r"PKG_CHECK_MODULES\(\s*GTK\s*,\s*\[?\s*"
        r"(?P<module>[a-z0-9+.-]+)"
        r"(?:\s*>=\s*(?P<version>[0-9.]+))?\s*\]?\s*,",
        text,
    ):
        module = m.group("module")
        if module in MODULES:
            found[module] = m.group("version")
    return found


def cmake_minimums(text):
    """{module: version or None} from toolkit.cmake's per-toolkit branches."""
    found = {}
    for module, guard in CMAKE_GUARD.items():
        # if(WXGTK4) ... set(gtk_min_version 4.10)   -- or set(gtk_min_version)
        m = re.search(
            r"(?:if|elseif)\(" + guard + r"\)(?P<body>.*?)(?=\n\s*(?:elseif|else|endif)\()",
            text,
            re.S,
        )
        if not m:
            continue
        v = re.search(r"set\(gtk_min_version\s*(?P<version>[0-9.]*)\s*\)", m.group("body"))
        if v:
            found[module] = v.group("version") or None
    return found


def other_gtk4_mentions(text):
    """Every '4.x' a gtk4 pkg-config expression names outside PKG_CHECK_MODULES."""
    versions = set()
    for m in re.finditer(r"gtk4\s*>=\s*(?P<version>[0-9][0-9.]*)", text):
        versions.add(m.group("version"))
    return versions


def gdk_min_required(text):
    """GDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_10 -> '4.10'."""
    m = re.search(r"GDK_VERSION_MIN_REQUIRED=GDK_VERSION_(?P<major>\d+)_(?P<minor>\d+)", text)
    if not m:
        return None
    return "%s.%s" % (m.group("major"), m.group("minor"))


def describe(version):
    return version if version else "no minimum"


def main():
    problems = []

    ac_text = read(CONFIGURE_AC)
    cmake_text = read(TOOLKIT_CMAKE)

    ac = configure_minimums(ac_text)
    cm = cmake_minimums(cmake_text)

    if not ac:
        problems.append(
            "configure.ac: found no PKG_CHECK_MODULES(GTK, ...) call at all -- "
            "this check can no longer see what it is meant to compare"
        )
    if not cm:
        problems.append(
            "build/cmake/toolkit.cmake: found no set(gtk_min_version ...) at all -- "
            "this check can no longer see what it is meant to compare"
        )

    # 1. the two build systems against each other
    for module in sorted(set(ac) | set(cm)):
        name = MODULES[module]
        if module not in ac:
            problems.append("%s: named in toolkit.cmake but not in configure.ac" % name)
        elif module not in cm:
            problems.append("%s: named in configure.ac but not in toolkit.cmake" % name)
        elif ac[module] != cm[module]:
            problems.append(
                "%s minimum differs: configure.ac says %s, toolkit.cmake says %s"
                % (name, describe(ac[module]), describe(cm[module]))
            )

    gtk4_min = ac.get("gtk4")

    # 2. configure.ac against itself
    if gtk4_min:
        for version in sorted(other_gtk4_mentions(ac_text)):
            if version != gtk4_min:
                problems.append(
                    "configure.ac asks pkg-config for gtk4 >= %s in one place and "
                    "gtk4 >= %s in another" % (gtk4_min, version)
                )

        # 3. the GDK target, in both files
        for path, text in (
            ("configure.ac", ac_text),
            ("build/cmake/toolkit.cmake", cmake_text),
        ):
            gdk = gdk_min_required(text)
            if gdk is None:
                problems.append("%s: sets no GDK_VERSION_MIN_REQUIRED for GTK4" % path)
            elif gdk != gtk4_min:
                problems.append(
                    "%s: GDK_VERSION_MIN_REQUIRED says %s but the GTK4 minimum is %s"
                    % (path, gdk, gtk4_min)
                )

    if problems:
        print("The two build systems disagree about the minimum GTK versions:")
        print()
        for p in problems:
            print("  %s" % p)
        print()
        print("Both have to be updated together. See issue #194.")
        return 1

    print("Minimum GTK versions agree between configure.ac and CMake:")
    for module in sorted(ac):
        print("  %-8s %s" % (MODULES[module], describe(ac[module])))
    if gtk4_min:
        print("  GDK_VERSION_MIN_REQUIRED matches, in both build systems")
    return 0


if __name__ == "__main__":
    sys.exit(main())
