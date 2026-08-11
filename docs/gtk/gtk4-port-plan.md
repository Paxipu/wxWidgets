# wxGTK4 Port Plan

Status: **planning** — no code changes have been made yet. This document
inventories the current state of GTK4 support in wxWidgets and lays out a
phased roadmap for turning it into a real, CI-verified port.

Written 2026-08, based on a survey of the tree at the time.

## 1. Current state: this is not a blank slate

GTK4 is **not** a new top-level wx port (unlike Qt vs MSW). It's an in-tree
version variant of the existing `wx/gtk` port:

- `configure --with-gtk=4` and CMake's `wxTOOLKIT_OPTIONS=gtk4` both compile
  the same `src/gtk` / `include/wx/gtk` tree, selecting GTK4 behavior via
  preprocessor (`__WXGTK4__`, `__WXGTK3__`, `GTK_CHECK_VERSION`), not a
  separate `include/wx/gtk4/` directory. When GTK4 is selected, **both**
  `__WXGTK4__` and `__WXGTK3__` get defined (`configure.ac` ~line 2896-2982).
- Roughly 95 `__WXGTK4__` conditionals and 80 `__WXGTK3__` conditionals
  already exist across `src/gtk` and `include/wx/gtk` — someone got this
  compiling at some point (e.g. `window.cpp`, `toplevel.cpp`, `app.cpp`,
  `menu.cpp`, `textentry.cpp`, `mdi.cpp`, `dataview.cpp`, `dc.cpp`,
  `include/wx/gtk/private/gtk3-compat.h`).
- **But there is zero CI coverage.** `.github/workflows/ci.yml` only builds
  `gtk_version: 2` and defaults to `3`; `ci_cmake.yml` and `ci_mac_gtk.yml`
  hardcode `gtk3`. No workflow anywhere passes `--with-gtk=4`. The GTK4 path
  has had no automated pressure keeping it working against a fast-moving
  target (GTK4 itself is still evolving), so treat every existing
  `__WXGTK4__` branch as untrusted until it's actually built and run.
- At least one explicit, acknowledged gap exists in code today:
  `src/gtk/menu.cpp:994-996` — `GtkImageMenuItem` was removed in GTK4 and
  the replacement (`GtkMenuItem` + `GtkBox` + `GtkAccelLabel` + `GtkImage`)
  is marked `//TODO`, unimplemented.

**Recommendation: don't start from "GTK4 port," start from "why is the
existing GTK4 path broken and untested."** The fastest way to derisk the
whole project is to make it buildable and visible first (Phase 0 below),
then find out empirically how much of the scaffolding still holds.

**Update, first build attempt:** that empirical check has now been done —
see `docs/gtk/gtk4-status.md`. A clean `./configure --with-gtk=4` +
`make -k` against real GTK4 4.14 headers produces **1304 compiler errors
across ~80 files**, i.e. essentially the entire GTK-specific core library
fails to compile. This is not 80 unrelated bugs; it's a handful of
removed GTK3 APIs (`GtkContainer`, `GtkBin`, `gtk_widget_get_window`/
`GdkWindow`, `gtk_box_pack_start/end`, old style-properties API,
`GtkSelectionData`) whose absence cascades through shared code. The
`__WXGTK4__` conditionals already in the tree are necessary but nowhere
near sufficient — "already partially working" (as this document originally
characterized it, based on grep for `__WXGTK4__`) overstated the state;
correct to "scaffolded but never actually compiled against real GTK4
headers." See `gtk4-status.md` for the full breakdown and running
checklist; §3 below is retained for its narrative but `gtk4-status.md` is
now the source of truth for counts.

## 2. The three hard problems you named, reassessed against the code

### a. Signals — smaller than it sounds

GTK4 still uses GObject signals for the same things GTK3 does (e.g.
`notify::`, widget lifecycle signals). wx doesn't have a signal
abstraction layer to reimplement — `src/gtk/window.cpp` uses raw
`g_signal_connect`/`g_signal_connect_after` throughout, centralized in
`wxWindowGTK::PostCreation()` (~line 3164) and
`wxWindowGTK::ConnectWidget()` (line 4222), with a one-line forwarding
helper `GTKConnectWidget()` (line 3314). There's no macro system to
redesign.

What *does* need work: the specific **input signals** that pass raw
`GdkEventButton*`/`GdkEventKey*`/`GdkEventMotion*` structs by value
(`button_press_event`, `key_press_event`, `motion_notify_event`,
`scroll_event`, `enter_notify_event`, `leave_notify_event` — connected in
`ConnectWidget()`) don't exist at all in GTK4. GTK4 replaced them with
**`GtkEventController` objects** (`GtkEventControllerKey`,
`GtkGestureClick`, `GtkEventControllerMotion`, `GtkEventControllerScroll`,
`GtkEventControllerFocus`) attached to a widget rather than connected as
signals with event-struct arguments. This is a real, mechanical-but-wide
rewrite of the input layer — see Phase 3.

### b. Window/child structure — this is the real hard problem

You called this correctly. `wxWindowGTK` currently wraps up to two native
pointers (`m_widget`, and `m_wxwindow` — an inner `wxPizza`/`GtkFixed`-derived
container, see `src/gtk/win_gtk.cpp`), and wx's whole model of stacking,
positioning, popups, and hit-testing leans on **`GdkWindow`** — each
widget (or groups of widgets) can own a native `GdkWindow`, including
input-only and override-redirect subwindows for things like popups,
tooltips, and custom cursor regions. In-tree evidence of how deep this
goes: 38 files touch `GdkWindow` directly, 12 use
`gdk_window_new`/`reparent`/`set_events`/`GDK_WINDOW` macros.

GTK4 removed all of this. Only **toplevels** own a native surface (renamed
`GdkSurface`); ordinary widgets live purely in the GtkWidget tree with no
backing window of their own, positioned by layout/measure/allocate, not by
raw window geometry. There is no offscreen/input-only child window
mechanism to replicate the old behavior with. This is the piece that
needs a genuine design decision, not just a mechanical port — see Phase 2.

### c. Painting — mostly already solved, not reinvented

Good news, and it changes the shape of this problem: **wx's GTK backend
already routes all drawing through Cairo**, even in the current GTK3
build. `wxWindowDCImpl` (`src/gtk/dcclient.cpp`) sits on top of
`wxGTKCairoDC` (`src/gtk/dc.cpp`, wraps a `cairo_t*`), and
`wxGraphicsContext`'s implementation (`src/generic/graphicc.cpp`,
`wxCairoContext`) is Cairo-based too. The `"draw"` signal already hands
wx a `cairo_t*` for GTK3 (vs. `GdkEventExpose*`/`"expose_event"` for
GTK2), converging in `wxWindowGTK::GTKSendPaintEvents(cairo_t*)`
(`window.cpp:5769`).

GTK4 replaces the `"draw"` **signal** with a `snapshot` **vfunc**
(`GtkWidgetClass::snapshot`) that receives a `GtkSnapshot*` and produces a
`GskRenderNode` tree. But you are *not* required to build GSK nodes by
hand to keep drawing correct: `gtk_snapshot_append_cairo()` hands you a
`cairo_t*` recorded into the snapshot, so the existing
Cairo-based `wxGTKCairoDC`/`wxCairoContext` machinery can be reused almost
unchanged for a correctness-first port — swap "get `cairo_t*` from the
signal argument" for "get `cairo_t*` from `gtk_snapshot_append_cairo()`
inside an overridden `snapshot` vfunc." A GSK-native fast path (real
GPU-accelerated render nodes instead of a recorded Cairo surface) is a
legitimate later optimization, not a blocker for a working port. Treat it
as an explicit stretch goal so it doesn't balloon the critical path.

## 3. Inventory of concrete API deltas (starting point for Phase 1)

Grep counts against `src/gtk/*.cpp`, current tree — a floor, not a ceiling,
since some usages are already GTK2-only and dead under `__WXGTK3__`:

| Removed/changed GTK3 API | Files affected | GTK4 replacement |
|---|---|---|
| `gtk_container_add/remove/foreach`, `GTK_CONTAINER` | 26 files (`toolbar.cpp` 12, `private.cpp` 10, `settings.cpp` 8, `frame.cpp` 6, `minifram.cpp`/`notebook.cpp`/`statbox.cpp` 4 each, +16 more) | `gtk_widget_set_parent`, `gtk_widget_insert_after`, widget-specific setters (`gtk_window_set_child`, etc.) |
| `GdkWindow` (child/offscreen), `gdk_window_new/reparent/set_events` | 38 files touch `GdkWindow`; 12 call the removed constructors/mutators | `GdkSurface` (toplevel-only); no child-window equivalent — needs the Phase 2 design |
| `GdkEventButton/Key/Motion` structs by value | 14 files | `GtkEventController*` objects, opaque `GdkEvent*` accessors |
| `GtkImageMenuItem` | `src/gtk/menu.cpp` (already flagged TODO) | manual `GtkMenuItem` + `GtkBox` + `GtkImage` + `GtkAccelLabel` composition |
| `get_preferred_width/height` vfuncs (`src/gtk/win_gtk.cpp` `class_init`, line 296-297) | wxPizza (custom container backing every `wxWindow`) | single `measure()` vfunc — mechanical signature merge, good first Phase-2 exercise |
| `GtkTargetList`/`GtkSelectionData`-based DnD (`src/gtk/dnd.cpp`) | 1 subsystem | `GdkContentProvider`, `GtkDragSource`, `GtkDropTarget` |
| `GtkClipboard` (`src/gtk/clipbrd.cpp`) | 1 subsystem | `GdkClipboard` |

This table should become a tracked checklist (`docs/gtk/gtk4-status.md` or
a GitHub issue with checkboxes) during Phase 1, expanded file-by-file as
each is actually audited — the table above is a starting inventory, not a
finished one.

## 4. Phased roadmap

**Phase 0 — Restore visibility (weeks, not months). Partially done.**
The first build attempt (see update above and `gtk4-status.md`) showed
this isn't "fix whatever's bit-rotted" — it's 1304 errors from a genuinely
unbuilt path. Revised scope: an `allow-failure` CI job building
`--with-gtk=4` has been added so the error count is tracked over time
and visibly trends toward zero as later phases land, rather than waiting
for a fully green build before it's visible at all. Getting the job to
actually pass is now realistically a Phase 2/5 outcome, not a Phase 0 one.

**Phase 1 — Inventory & triage (1-2 months).**
Systematic widget-by-widget audit of every removed/deprecated GTK3→GTK4
API touchpoint, expanding the table in §3 into a tracked, file-by-file
checklist. Build with GTK4 headers only (no GTK3 fallback available) to
let the compiler enumerate the real gaps rather than relying on grep.
Produces no user-visible behavior, but this is the phase that turns "a
GTK4 port" from a vague multi-month fear into a scoped list of PRs — worth
front-loading before writing architectural code.

**Phase 2 — Window/child model (2-4 months, the critical design phase).**
Replace the `GdkWindow`-per-widget model with a scheme that works when
only toplevels have a `GdkSurface`:
- Reposition children of `wxPizza`/`m_wxwindow` purely through GtkFixed's
  `put`/`move` layout, not window reparenting.
- Redesign popups/tooltips/comboboxes that relied on override-redirect
  subwindows around `GtkPopover`/`GdkPopup`.
- Replace input-only `GdkWindow` hit-testing (custom cursor regions,
  drag handles) with `GtkEventController`-based hit-testing.
- Mechanical sub-task: migrate `get_preferred_width/height` → `measure()`
  across every custom widget class (start with `wxPizza` in
  `win_gtk.cpp` as the reference example — it already isolates the vfunc
  table in one `class_init()` function).

This is the phase that needs a genuine design write-up before coding
starts — plan to spend real time here before touching `window.cpp`.

**Phase 3 — Input/event model (1-2 months, can overlap Phase 2).**
Replace the raw-event signal connections in `ConnectWidget()` with
`GtkEventController` equivalents, keeping the adapter thin (mirror the
existing `GTKConnectWidget()` idiom) so the higher-level wx event
generation code that builds `wxMouseEvent`/`wxKeyEvent` objects barely
changes — only the "receive the native event" layer changes.

**Phase 4 — Painting (1 month).**
Switch `"draw"` signal handling to an overridden `snapshot` vfunc,
sourcing `cairo_t*` via `gtk_snapshot_append_cairo()`. Because
`wxGTKCairoDC`/`wxCairoContext` are already Cairo-based, this should be
close to a drop-in swap of where the `cairo_t*` comes from. Explicitly
defer GSK-native render-node fast paths to a later optimization pass.

**Phase 5 — Widget-by-widget remediation (2-3 months).**
Work the Phase 1 checklist: menu/menuitem, toolbar, notebook, dataview,
statbox, minifram, radiobox, and the other files flagged in §3 for
`GtkContainer` usage.

**Phase 6 — DnD & clipboard rewrite (1 month, parallelizable).**
Self-contained subsystem: `GdkContentProvider`/`GtkDragSource`/
`GtkDropTarget` replacing `src/gtk/dnd.cpp`'s target-list API;
`GdkClipboard` replacing `src/gtk/clipbrd.cpp`'s `GtkClipboard` API. Good
candidate to run as a separate concurrent workstream/branch since it
doesn't touch `window.cpp`.

**Phase 7 — Native dialogs & printing (ongoing, parallelizable).**
File chooser (likely least affected — already portal/native-dialog based
in most distros), print dialogs, message dialogs.

**Phase 8 — Stabilization.**
Flip the Phase 0 CI job from allow-failure to required; update
`docs/gtk/readme.txt` and `docs/gtk/install.md` to document GTK4 support
level; run the full wx test suite and samples under GTK4 on both X11 and
Wayland.

## 5. Sequencing note for a multi-month, mostly-solo effort

Phases 2 → 3 → 4 are one critical-path track: they all touch the core
`window.cpp`/`win_gtk.cpp` machinery and build on each other, so they
benefit from architectural continuity — ideally one continuous thread of
work rather than interleaved context-switches. Phases 6 and 7 are
genuinely separable (different files, no shared state) and are good
candidates to peel off into parallel branches/sessions without much
coordination overhead once Phase 1's inventory exists.

## 6. Recommended immediate next step

Phase 0 and Phase 1 are now done: `docs/gtk/gtk4-status.md` has the real,
compiler-verified error inventory (1304 errors / ~80 files, categorized by
root cause), and `.github/workflows/ci.yml` has an `allow-failure` GTK4
matrix entry (`build/tools/before_install.sh` now knows how to install
`libgtk-4-dev`) so the error count is tracked over time instead of being
invisible.

The next concrete step is the start of **Phase 2**: design the
`GdkWindow`→`GdkSurface` / child-widget-positioning replacement. This is
the item blocking the largest share of the error count
(`gtk_widget_get_window`, `GdkWindow`, and everything downstream of
`wxGetTopLevelGDK()` in `window.cpp`) and needs a written-down design —
specifically, how `wxPizza`/`m_wxwindow` will position children without
per-widget native windows, and what replaces override-redirect popups —
before any of the ~80 broken files can be fixed for real. Fixing files
piecemeal ahead of that design would mean redoing the work once it exists.
