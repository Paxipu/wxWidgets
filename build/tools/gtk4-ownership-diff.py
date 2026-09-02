#!/usr/bin/env python3
"""Report GTK functions wxGTK calls whose ownership rules changed in GTK4.

Ownership is the one thing about a GTK API that the compiler cannot check and
the headers do not say: whether a function hands you a reference to release, or
takes one from you, lives only in the GObject-introspection annotations. A
function whose signature is unchanged can quietly have swapped sides, and the
only symptom is a leak or a double free much later.

That is not hypothetical for this port. Two of the nastiest bugs found while
doing it were exactly this:

  * gtk_drop_target_async_new() is (transfer full) on its GdkContentFormats,
    so wx's own unref was one too many and every wxWindow with a drop target
    corrupted the heap on destruction.

  * GtkFileFilter was a GInitiallyUnowned under GTK3, so the floating
    reference gtk_file_filter_new() returned was sunk by
    gtk_file_chooser_add_filter(). Under GTK4 it is a plain GObject, the
    reference is real, add_filter() is (transfer none) -- and the unchanged
    wx code leaked one reference per filter.

So rather than waiting for the next one to crash, compare the annotations
directly. This reads the Gtk-3.0 and Gtk-4.0 .gir files, collects every
G-prefixed function named anywhere in src/gtk and include/wx/gtk, and reports
the ones where the two disagree about who owns what -- including the
GInitiallyUnowned-to-GObject change above, which no transfer annotation
mentions at all.

Differences that have been looked at are listed in REVIEWED below, so a run
that prints nothing new means nothing new has appeared. Run it after a GTK
update, or when adding calls to an unfamiliar part of the API.

Usage:  python3 build/tools/gtk4-ownership-diff.py [path-to-wx-source]
Exit:   0 if only reviewed differences remain, 1 otherwise, 77 if the .gir
        files are not installed (Debian/Ubuntu: libgtk-3-dev, libgtk-4-dev).
"""

import glob
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

CORE = 'http://www.gtk.org/introspection/core/1.0'
C = 'http://www.gtk.org/introspection/c/1.0'

GIR_DIR = '/usr/share/gir-1.0'

GTK4_GIRS = ['Gtk-4.0', 'Gdk-4.0', 'Gsk-4.0', 'GObject-2.0', 'Gio-2.0',
             'Pango-1.0', 'PangoCairo-1.0', 'GdkPixbuf-2.0', 'GLib-2.0',
             'GdkX11-4.0', 'GdkWayland-4.0', 'Graphene-1.0']
GTK3_GIRS = ['Gtk-3.0', 'Gdk-3.0', 'GObject-2.0', 'Gio-2.0',
             'Pango-1.0', 'PangoCairo-1.0', 'GdkPixbuf-2.0', 'GLib-2.0',
             'GdkX11-3.0']

# Differences already understood. Each entry is the function name; the comment
# says what was done about it.
REVIEWED = {
    # Handled: filectrl.cpp unrefs the filter under GTK4. Both halves of the
    # same change -- the type stopped being GInitiallyUnowned, and the call
    # that used to sink the floating reference stopped taking ownership.
    'gtk_file_filter_new',
    'gtk_file_chooser_add_filter',

    # Handled: calctrl.cpp calls g_date_time_unref() on the GDateTime GTK4
    # returns in place of GTK3's out-parameters.
    'gtk_calendar_get_date',

    # Not called by wx: it appears only in a comment in dataview.cpp.
    'gtk_tree_drag_source_drag_data_get',
}


def q(tag):
    return '{%s}%s' % (CORE, tag)


def load(girs):
    """Return (functions, types) read from the given .gir files."""
    funcs, kinds = {}, {}
    found = False

    for name in girs:
        path = os.path.join(GIR_DIR, name + '.gir')
        if not os.path.exists(path):
            continue
        found = True
        root = ET.parse(path).getroot()

        for el in root.iter():
            if el.tag.split('}')[-1] in ('class', 'interface', 'record'):
                if el.get('name'):
                    kinds[el.get('name')] = el.get('parent')

        for el in root.iter():
            if el.tag.split('}')[-1] not in ('function', 'method', 'constructor'):
                continue
            cid = el.get('{%s}identifier' % C)
            if not cid:
                continue

            rv = el.find(q('return-value'))
            rtype = rv.find(q('type')) if rv is not None else None
            params = []
            ps = el.find(q('parameters'))
            if ps is not None:
                for p in ps.findall(q('parameter')):
                    params.append((p.get('name'), p.get('transfer-ownership')))

            funcs[cid] = {
                'ret_transfer': rv.get('transfer-ownership') if rv is not None else None,
                'ret_type': rtype.get('name') if rtype is not None else None,
                'params': params,
            }

    return (funcs, kinds) if found else (None, None)


def was_floating(kinds, typename):
    """True if instances of this type are created with a floating reference."""
    seen = set()
    while typename and typename not in seen:
        seen.add(typename)
        parent = kinds.get(typename)
        if parent is None:
            return False
        if parent.split('.')[-1] == 'InitiallyUnowned':
            return True
        typename = parent.split('.')[-1]
    return False


def functions_used(src):
    used = set()
    files = (glob.glob(os.path.join(src, 'src/gtk/**/*.cpp'), recursive=True) +
             glob.glob(os.path.join(src, 'include/wx/gtk/**/*.h'), recursive=True))
    for f in files:
        text = open(f, errors='replace').read()
        for m in re.finditer(r'\b(g[a-z_]*_[a-z0-9_]+)\s*\(', text):
            used.add(m.group(1))
    return used, len(files)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')

    f4, k4 = load(GTK4_GIRS)
    f3, k3 = load(GTK3_GIRS)
    if not f4 or not f3:
        print("gtk4-ownership-diff: .gir files not installed, nothing to check.\n"
              "On Debian/Ubuntu these come with libgtk-3-dev and libgtk-4-dev.")
        return 77

    used, nfiles = functions_used(src)

    unreviewed = 0
    for name in sorted(used):
        a, b = f3.get(name), f4.get(name)
        if not a or not b:
            continue

        notes = []
        if a['ret_transfer'] != b['ret_transfer']:
            notes.append("returns (transfer %s) where GTK3 said (transfer %s)"
                         % (b['ret_transfer'], a['ret_transfer']))

        old = dict(a['params'])
        for pname, transfer in b['params']:
            if pname in old and old[pname] != transfer:
                notes.append("parameter '%s' is (transfer %s), was (transfer %s)"
                             % (pname, transfer, old[pname]))

        rt = (b['ret_type'] or '').split('.')[-1]
        if rt and b['ret_transfer'] == 'full':
            rt3 = (a['ret_type'] or b['ret_type']).split('.')[-1]
            if was_floating(k3, rt3) and not was_floating(k4, rt):
                notes.append("returns a %s, which was GInitiallyUnowned and is now "
                             "a plain GObject: the reference is real now, and "
                             "whatever used to sink it no longer does" % rt)

        if not notes:
            continue

        reviewed = name in REVIEWED
        if not reviewed:
            unreviewed += 1
        print("%s%s" % (name, "  [reviewed]" if reviewed else "  *** NEW ***"))
        for note in notes:
            print("    " + note)

    print("\nchecked %d functions named across %d files in src/gtk and "
          "include/wx/gtk" % (len(used), nfiles))

    if unreviewed:
        print("\n%d unreviewed difference(s). Each one is a place where code that "
              "compiles\nunchanged may now leak or double-free. Work out which, fix "
              "it, then add the\nfunction to REVIEWED in this script with a note "
              "saying what was done." % unreviewed)
        return 1

    print("no unreviewed differences")
    return 0


if __name__ == '__main__':
    sys.exit(main())
