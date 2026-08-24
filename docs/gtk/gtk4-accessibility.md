# Accessibility under GTK4

`wxUSE_ACCESSIBILITY` was a wxMSW-only setting: `include/wx/chkconf.h` turned it
off again anywhere else, and `wx/access.h` only ever declared the abstract
`wxAccessibleBase`. Turning it on for another port failed to compile, which is
the first thing this work established rather than assumed -- with
`--enable-accessibility --with-gtk=4`, `configure` succeeded and the build then
produced 96 errors across `src/generic/grid.cpp`, `src/generic/gridsel.cpp` and
`src/generic/datavgen.cpp`, all of them variations on `wxAccessible` being an
incomplete type.

This document is what the port did about that, what GTK4 can and cannot express,
and what is deliberately left undone.

## The two models are inverted

`wxAccessibleBase` is shaped like MSAA. An assistive technology asks a question
-- what is your name, your role, your state, how many children do you have --
and the application answers it, from whatever it happens to know at that moment.
Nothing is stored. An object nobody asks about costs nothing, which is why
`wxGridAccessible::GetChildCount()` can cheerfully answer `rows * cols`.

GTK4 inverted this when it dropped ATK. An accessible object now owns a
`GtkATContext` holding the answers, and the application pushes new ones in with
`gtk_accessible_update_property()`, `gtk_accessible_update_state()` and
`gtk_accessible_update_relation()`. The only questions GTK asks back are the
five vfuncs on `GtkAccessibleInterface`: the AT context, the platform states
(focusable / focused / active), the bounds, and the two tree-walk steps.

So a GTK4 backend for this API is a translation between a pull model and a push
model, and the only moments wx knows something might have changed are the
`wxAccessible::NotifyEvent()` calls the generic controls already make. That is
where `src/gtk/accessgtk.cpp` hangs almost everything.

## What GTK4 can express, and how that was established

Three things had to be true for this to be worth doing at all, because most of
what a `wxAccessible` describes -- a grid cell, a list item, a range of text --
has no window of its own, and GTK4's accessibility documentation talks about
widgets almost exclusively.

`docs/gtk/probes/gtk4-a11y-virtual-child.c` is a standalone program that answers
them. Build it with `cc -o probe gtk4-a11y-virtual-child.c $(pkg-config --cflags
--libs gtk4)` and run it as `GTK_A11Y=test ./probe`; on GTK 4.14.5 all seven
checks pass:

| question | answer |
| --- | --- |
| does a plain `GObject` implementing `GtkAccessible` get a working role? | yes |
| are properties pushed to it readable back? | yes |
| are states pushed to it readable back? | yes |
| can a widget name a non-widget as its first accessible child? | yes |
| does the sibling chain continue from there? | yes |
| does the child point back at the widget? | yes |
| can a bare widget's role be changed after construction? | yes -- but see below |
| does `get_first_accessible_child()` call its vfunc? | yes |
| does `get_next_accessible_sibling()` call its vfunc? | **no** |

`GTK_A11Y=test` is worth noting on its own: it makes GTK keep the whole
accessibility tree in process, with `gtk_test_accessible_check_property()` and
friends to read it back. No accessibility bus, no screen reader, nothing that
cannot run in CI.

### Where the tree walk actually comes from

`GtkAccessibleInterface` declares `get_first_accessible_child()` and
`get_next_accessible_sibling()` side by side, and only the first of them is
used. `gtk_accessible_get_first_accessible_child()` calls its vfunc;
`gtk_accessible_get_next_accessible_sibling()` ignores its own and returns
whatever `gtk_accessible_set_accessible_parent()` was given.

That decides the shape of the implementation. Making the child objects only as
far as a walk goes -- which is what the vfuncs invite, and what a wxGrid
reporting `rows * cols` children badly wants -- does not work, because the walk
never asks. The chain has to be built and stored before anything looks at it.

### A GTK bug found on the way

`gtk_accessible_update_next_accessible_sibling()` drops a reference on the
accessible *parent*. With stock widgets and no wx involved at all:

```c
GtkWidget* parent = gtk_drawing_area_new();      /* held by a window: refcount 1 */
gtk_accessible_set_accessible_parent(cell, GTK_ACCESSIBLE(parent), NULL);
                                                 /* refcount still 1 */
gtk_accessible_update_next_accessible_sibling(cell, GTK_ACCESSIBLE(sibling));
                                                 /* refcount 0 -- finalized */
```

GTK notices and complains -- *"has a parent GtkWindow during dispose. Parents
hold a reference, so this should not happen"* -- but by then the widget is gone.
Observed on GTK 4.14.5; not checked against newer GTK.

The workaround is to pass the parent and the sibling together to
`gtk_accessible_set_accessible_parent()`, which is safe -- and which is the only
way to build the chain anyway, given the paragraph above. The probe's last check
reproduces the bug and prints what it observed rather than asserting, since it
is a property of the GTK in use.

## What was implemented

`src/gtk/accessgtk.cpp`, `include/wx/gtk/access.h` and a small addition to
`src/gtk/win_gtk.cpp`.

* `wxAccessible` derives from `wxAccessibleBase` and owns a
  `wxGTKAccessibleImpl`, which is where all the GTK contact is.
* `NotifyEvent()` finds the window's `wxAccessible` and pushes whatever the
  event says changed. `wxACC_EVENT_OBJECT_CREATE`, `_REORDER` and
  `_PARENTCHANGE` rebuild the child objects; `_FOCUS` updates the active
  descendant; anything else refreshes one object.
* Child ids become `wxGtkAccChild`, a `GObject` implementing `GtkAccessible`.
  Nothing is built until something first asks the widget for its accessible
  children; at that point all of them are, back to front so each can be given
  its successor. The vfuncs answer lazily as well, so a GTK that starts
  honouring `get_next_accessible_sibling()` would get the cheaper behaviour for
  free.
* A child id may be answered by the object it belongs to or by an object of its
  own -- wxGrid uses both, answering `GetChildCount()` from
  `wxGridAccessible` but handing out a `wxGridCellAccessible` for anything about
  a particular cell. That object is transient: wxGrid rebuilds it whenever a
  different cell is asked about. So it is looked up again for every question and
  never stored.
* `NotifyEvent()` uses `GetOrCreateAccessible()`, not `GetAccessible()`. A
  control that describes itself does so by overriding `CreateAccessible()`, and
  under wxMSW the platform asks for the result when an assistive technology
  sends `WM_GETOBJECT`. GTK4 has no equivalent -- its accessibility data has to
  already be there when something comes looking -- so the first event a control
  reports is the moment the object gets built.
* `wxPizza` re-implements `GtkAccessible` so it can answer with those children.
  It is the widget behind every custom-drawn wx control, and nothing else can
  name an accessible child that is not a widget. When the window has no
  `wxAccessible`, or one that reports no children, it defers to `GtkWidget`'s
  own implementation, so ordinary widget children are unaffected.
* `GetLocation()` feeds `get_bounds()`, converted from wx's screen coordinates
  to the parent-relative ones GTK asks for.
* The *window's* own role is not set. `accessible-role` is a GObject property
  rather than an accessible attribute, and GTK refuses to change it once the
  widget has an AT context -- which a wx window already has by the time an
  application attaches a `wxAccessible`, so every attempt is a `g_critical()`
  and no change. The role's ARIA name goes into
  `GTK_ACCESSIBLE_PROPERTY_ROLE_DESCRIPTION` instead, which can be set at any
  time and is what ARIA offers for saying what something is when the role
  cannot. Child objects are unaffected: their contexts are created here, with
  the right role from the start.
* `GetFocus()` feeds two things: the `FOCUSED` platform state, which GTK pulls,
  and `GTK_ACCESSIBLE_RELATION_ACTIVE_DESCENDANT` on the widget, which is the
  ARIA way of saying which item inside a composite control currently stands in
  for it. The relation is what an AT actually notices, because GTK 4.14 has no
  way to announce that a platform state changed.

`configure` accepts `--enable-accessibility` for wxGTK4 against GTK 4.10 or
later and rejects it elsewhere; the default stays off everywhere but wxMSW.

Enabling it also made three wxGTK dataview renderers abstract, because
`wxDataViewRendererBase::GetAccessibleDescription()` is a pure virtual guarded by
`wxUSE_ACCESSIBILITY` that only the generic renderers implemented. The native
GTK text, bitmap and toggle renderers now implement it too.

## The mapping

`wxAccRole` and `GtkAccessibleRole` both descend from MSAA by way of ARIA, so
most roles map straight across. The ones with no counterpart -- `wxROLE_SYSTEM_CARET`,
`_CURSOR`, `_TITLEBAR`, `_BORDER`, `_GRIP`, `_WHITESPACE`, `_CLIENT` and a dozen
more that describe parts of a window GTK4 does not model as objects -- become
`GTK_ACCESSIBLE_ROLE_GENERIC` rather than being guessed at.

States map the same way and to the same principle: `BUSY`, `UNAVAILABLE`,
`EXPANDED`, `INVISIBLE`, `PRESSED`, `SELECTED`, `CHECKED`/`MIXED`, `READONLY`
and `MULTISELECTABLE` are pushed; everything else is left unsaid. An AT told
nothing about a state falls back on a sensible default. One told the wrong thing
does not.

## What is not implemented, and why

| `wxAccessibleBase` member | status |
| --- | --- |
| `GetName`, `GetDescription`, `GetKeyboardShortcut` | pushed as `LABEL`, `DESCRIPTION`, `KEY_SHORTCUTS` |
| `GetRole` | per child; for the window itself, as a role description |
| `GetState` | pushed, as far as GTK4 has names |
| `GetChildCount`, `GetChild` | answered by the tree-walk vfuncs |
| `GetParent` | answered by `get_accessible_parent()` |
| `GetLocation` | answered by `get_bounds()` |
| `GetFocus` | platform state and active-descendant relation |
| `GetValue` | not pushed: GTK4 has no value-text property; `GtkAccessibleText` (4.14) is the replacement and is not implemented yet |
| `HitTest` | no equivalent. GTK4 hit-tests from `get_bounds()` itself |
| `Navigate` | no equivalent. The tree is walked, not navigated by direction |
| `DoDefaultAction`, `GetDefaultAction` | no equivalent in GTK4's public API |
| `Select`, `GetSelections` | no equivalent; selection is a per-object state, not a list |
| `GetHelpText` | no equivalent; GTK4 has description but no separate help text |

Two limitations are GTK's rather than wx's:

* **Child list changes cannot be announced.** GTK 4.14 has no equivalent of
  MSAA's "the children changed": there is no `gtk_accessible_update_children()`.
  The port rebuilds its child objects when it is told the tree changed, and an
  AT sees the new ones the next time it walks.
* **Focus changes on a virtual child cannot be announced directly**, for the
  same reason -- platform states are pulled, not pushed. The active-descendant
  relation is the documented ARIA answer and is what the port uses.
* **A window's own accessible role cannot be set**, as above. The remaining way
  to get one would be for `wxPizza` to own its `GtkATContext` instead of
  `GtkWidget` -- it already re-implements the interface, so `get_at_context()`
  is available to it -- but GTK's own code reaches for `GtkWidget`'s private
  context in places, and putting a second one next to it is not a change to make
  in the widget behind every wx window without a reason better than a role
  string.

## Testing

`docs/gtk/probes/gtk4-a11y-wx-bridge.cpp` checks the translation end to end
against a build with accessibility enabled: that a `wxAccessible`'s name and
role reach the widget, that its child ids become accessible children in the
right order, that bounds arrive in the right coordinate space, that the widget
survives being described, and that a window with no `wxAccessible` is left with
GTK's own idea of its children. It finishes with a real wxGrid, walking its
twelve cells and headers and checking that a cell's value arrives:

```
the window's role reached GTK, as a description            yes
the window's name reached GTK                              yes
the window has an accessible first child                   yes
the walk reaches every child, and stops                    yes
each child's name reached GTK                              yes
each child's role reached GTK                              yes
each child's bounds arrive in client coordinates           yes
the widget survived being described                        yes
a wxGrid describes itself as a grid                        yes
every cell and header of a wxGrid is a child               yes
a wxGrid cell's value reaches GTK                          yes
a window with no wxAccessible reports no virtual children  yes
```

It is a probe rather than a test in `tests/` because it has to include both wx
and GTK headers, and `wx-config --cxxflags` does not name GTK's include paths:
they are private to the library build. Both probes exit non-zero on failure, so
they can be run from CI as they are, in a job configured with
`--enable-accessibility`.

`samples/access` also builds and runs under wxGTK now. Its "Query" command used
to walk the tree through MSAA's `IAccessible`; outside wxMSW it walks the
`wxAccessible` objects instead and prints what they report, which is exactly
what gets handed to GTK.
