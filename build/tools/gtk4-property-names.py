#!/usr/bin/env python3
"""Report GObject property names wxGTK sets or reads that GTK4 does not have.

Properties are addressed by string. `g_object_set(obj, "no-month-change", ...)`
compiles whatever the object is and whatever GTK calls the property this year;
if the name is wrong the only symptom is a runtime warning nobody reads, and
the call quietly does nothing. `g_object_get()` is worse: it leaves the
destination untouched, so the caller returns whatever was already there.

Both happened in this port:

  * GtkSpinner's "active" became "spinning" in GTK4, so
    wxActivityIndicator::IsRunning() returned an uninitialized gboolean.

  * GtkCalendar's "no-month-change" was removed outright, so
    wxCAL_NO_MONTH_CHANGE silently did nothing.

Two checks are made, in decreasing confidence:

  1. Where the object is written as a GTK_FOO() cast, the property is looked up
     on that exact type and its ancestors. This is exact, and is what catches a
     rename to a name some other class still uses.

  2. Otherwise, the name is reported if it exists somewhere in GTK3 and nowhere
     in GTK4 at all. That misses renames like "active" -> "spinning", but it
     needs no type information.

Usage:  python3 build/tools/gtk4-property-names.py [path-to-wx-source]
Exit:   0 if clean, 1 if anything was found, 77 if the .gir files are not
        installed (Debian/Ubuntu: libgtk-3-dev, libgtk-4-dev).
"""

import glob
import os
import re
import sys
import xml.etree.ElementTree as ET

CORE = 'http://www.gtk.org/introspection/core/1.0'
C = 'http://www.gtk.org/introspection/c/1.0'
GIR_DIR = '/usr/share/gir-1.0'

GTK4_GIRS = ['Gtk-4.0', 'Gdk-4.0', 'Gsk-4.0', 'GObject-2.0', 'Gio-2.0',
             'Pango-1.0', 'GdkPixbuf-2.0', 'GLib-2.0']
GTK3_GIRS = ['Gtk-3.0', 'Gdk-3.0', 'GObject-2.0', 'Gio-2.0',
             'Pango-1.0', 'GdkPixbuf-2.0', 'GLib-2.0']

# Names that are deliberately not GTK properties, or are checked elsewhere.
IGNORE = set()


def load(girs):
    """(all property names, {type name: (parent, {properties})}, {GTK_FOO: type})."""
    every, types, macros = set(), {}, {}
    found = False

    for g in girs:
        path = os.path.join(GIR_DIR, g + '.gir')
        if not os.path.exists(path):
            continue
        found = True
        root = ET.parse(path).getroot()

        for cl in root.iter():
            if cl.tag.split('}')[-1] not in ('class', 'interface'):
                continue
            name = cl.get('name')
            if not name:
                continue
            props = set()
            for p in cl.findall('{%s}property' % CORE):
                if p.get('name'):
                    props.add(p.get('name'))
                    every.add(p.get('name'))
            types[name] = (cl.get('parent'), props)

            # GTK_SPINNER(x) etc: derive the cast macro from the C symbol
            # prefix and the type name, which is what GTK's own headers do.
            ctype = cl.get('{%s}type' % C)
            if ctype:
                macros[re.sub(r'(?<!^)(?=[A-Z])', '_', ctype).upper()] = name

    return (every, types, macros) if found else (None, None, None)


def has_property(types, typename, prop):
    """Does this type or any ancestor declare the property?"""
    seen = set()
    while typename and typename not in seen:
        seen.add(typename)
        entry = types.get(typename)
        if not entry:
            return None          # unknown type: cannot say
        if prop in entry[1]:
            return True
        typename = entry[0].split('.')[-1] if entry[0] else None
    return False


def gtk4_dead_lines(lines):
    """Line numbers compiled out under GTK4.

    Both #ifndef __WXGTK4__ and the #else of #ifdef __WXGTK3__ are dead there,
    since GTK4 builds define __WXGTK3__ as well.
    """
    dead, stack, depth = set(), [], 0

    def positive(idx):
        skip = 0
        for j in range(idx - 1, -1, -1):
            s = lines[j].strip()
            if re.match(r'#\s*endif\b', s):
                skip += 1
                continue
            m = re.match(r'#\s*(if|ifdef|ifndef)\b(.*)', s)
            if m:
                if skip:
                    skip -= 1
                    continue
                rest = m.group(2)
                return (('__WXGTK4__' in rest or '__WXGTK3__' in rest)
                        and not re.search(r'ifndef|!\s*defined', s))
        return False

    for i, line in enumerate(lines):
        s = line.strip()
        m = re.match(r'#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)', s)
        if m:
            kw, rest = m.group(1), m.group(2)
            if kw in ('if', 'ifdef', 'ifndef'):
                d = ((kw == 'ifndef' and '__WXGTK4__' in rest) or
                     (kw == 'if' and bool(
                         re.search(r'!\s*defined\s*\(\s*__WXGTK4__', rest))))
                stack.append(d)
                depth += d
            elif kw in ('else', 'elif'):
                if stack:
                    depth -= stack[-1]
                    now = (kw == 'else' and positive(i))
                    stack[-1] = now
                    depth += now
            elif kw == 'endif':
                if stack:
                    depth -= stack.pop()
        elif depth > 0:
            dead.add(i)
    return dead


CALL = re.compile(r'\b(g_object_get|g_object_set|g_object_notify|'
                  r'g_object_bind_property|g_object_class_find_property)\s*\(')
CAST = re.compile(r'^\s*(GTK_[A-Z0-9_]+|GDK_[A-Z0-9_]+|PANGO_[A-Z0-9_]+)\s*\(')
STRING = re.compile(r'"([a-z0-9]+(?:-[a-z0-9]+)*)"')


def first_argument_cast(text, call_end):
    """The cast macro naming the object, if the first argument is exactly one.

    Only the argument itself is looked at, never the surrounding lines: a
    g_object_set() on a cell renderer sitting under a GTK_CELL_LAYOUT() cast
    would otherwise be checked against the wrong type. A G_OBJECT() cast says
    nothing about the type and is deliberately not matched.
    """
    depth, arg = 1, []
    for ch in text[call_end:]:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
            if depth == 0:
                break
        elif ch == ',' and depth == 1:
            break
        arg.append(ch)
    arg = ''.join(arg)

    m = CAST.match(arg)
    if not m:
        return None

    # the cast must span the whole argument, i.e. its ")" is the last thing
    depth = 0
    for i, ch in enumerate(arg[m.end() - 1:], m.end() - 1):
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
            if depth == 0:
                return m.group(1) if not arg[i + 1:].strip() else None
    return None


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')

    every4, types4, macros4 = load(GTK4_GIRS)
    every3, _, _ = load(GTK3_GIRS)
    if not every4 or not every3:
        print("gtk4-property-names: .gir files not installed, nothing to check.\n"
              "On Debian/Ubuntu these come with libgtk-3-dev and libgtk-4-dev.")
        return 77

    files = sorted(glob.glob(os.path.join(src, 'src/gtk/**/*.cpp'), recursive=True) +
                   glob.glob(os.path.join(src, 'include/wx/gtk/**/*.h'), recursive=True))

    findings = []
    for path in files:
        lines = open(path, errors='replace').read().split('\n')
        dead = gtk4_dead_lines(lines)

        for i, line in enumerate(lines):
            if i in dead or line.lstrip().startswith(('//', '*')):
                continue

            context = '\n'.join(lines[max(0, i - 2):i + 1])
            if not CALL.search(line) and not CALL.search(context):
                continue

            # The type, if the first argument is written as a cast macro.
            typename = None
            call = CALL.search(context)
            if call:
                macro = first_argument_cast(context, call.end())
                if macro:
                    typename = macros4.get(macro)
            for m in STRING.finditer(line):
                prop = m.group(1)
                if prop in IGNORE:
                    continue

                if typename:
                    if has_property(types4, typename, prop) is False:
                        findings.append((path, i + 1, prop,
                                         "not a property of %s in GTK4" % typename,
                                         line.strip()))
                        continue

                if prop in every3 and prop not in every4:
                    findings.append((path, i + 1, prop,
                                     "exists in GTK3 and nowhere in GTK4",
                                     line.strip()))

    for path, num, prop, why, text in findings:
        print('%s:%d: "%s" %s\n    %s'
              % (os.path.relpath(path, src), num, prop, why, text[:110]))

    print("\nchecked %d files" % len(files))
    if findings:
        print("%d suspicious property name(s). Setting one of these does "
              "nothing and\nreading one leaves the destination untouched -- "
              "neither is a compile error." % len(findings))
        return 1

    print("no suspicious property names")
    return 0


if __name__ == '__main__':
    sys.exit(main())
