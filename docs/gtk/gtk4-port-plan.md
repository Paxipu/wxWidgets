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

**Phase 2/3/4 — Window model, input, and painting: revised, merged, and
scoped down (was 4-7 months combined; now looks like weeks). See
`docs/gtk/gtk4-phase2-window-model-design.md` for the full design.**

The original three-phase split assumed the `GdkWindow`-per-widget model
needed a from-scratch redesign. Tracing actual call sites showed
otherwise: `wxPizza`'s child positioning is already windowless (no
`GdkWindow`-per-child, no reparenting anywhere except one narrow file —
see below), and the real `gtk_widget_get_window()`/`GdkWindow` usages
break down into six much smaller, mostly-*easier*-in-GTK4 buckets: "get
me a display" (trivial `gdk_display_get_default()` swap), event-source
identity matching (obsolete — GTK4 event controllers report their owning
widget directly, so the whole matching mechanism and its ~18 per-widget
overrides get *deleted*, not reimplemented), cursor setting (simpler,
`gtk_widget_set_cursor()`), paint/clip/size (folds directly into the
`draw`→`snapshot` migration — same call sites), Z-order (`gtk_widget_
insert_after/before`, with a small documented fidelity gap for
`wxWindow::Lower()` on toplevels), and coordinate translation
(`gtk_widget_translate_coordinates`). Recommend doing the former Phases
2-4 as one coordinated pass through `window.cpp`/`win_gtk.cpp` rather
than three sequential phases, since a given call site's fix usually
resolves items from more than one former phase at once. Order: (1)
`wxGetTopLevelGDK()` → `GdkDisplay*`, (2) `wxGetTopLevel()` →
`GdkSurface*` for the narrow toplevel/backend-interop call sites, (3)
event-controller input model, (4) `draw`→`snapshot` + clip/size, (5)
delete the now-dead `GTKGetWindow`/`GTKIsOwnWindow` machinery.

Two items are explicitly carved out as separate, parallelizable tracks
rather than blocking this pass:
- **`wxPopupWindow`** (`src/gtk/popupwin.cpp`) uses the now-removed
  `GTK_WINDOW_POPUP` type hint and needs a `GtkPopover`/`GdkPopup`-based
  rewrite — a real API-shape change (anchored+relative vs. absolute
  screen position), self-contained to one file and its few consumers.
- **`wxNativeContainerWindow`** (`src/gtk/nativewin.cpp`, the one file
  that actually reparents a raw `GdkWindow`, for embedding foreign
  windows by XID) has no GTK4 equivalent mechanism at all — GTK removed
  `GtkSocket`/`GtkPlug`-style embedding, and Wayland's security model
  mostly precludes it. This needs an explicit scope decision (drop it
  for GTK4 vs. investigate a portal-based replacement) rather than a
  technical fix — see the design doc §7.

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

The merged Phase 2/3/4 `window.cpp`/`win_gtk.cpp` pass is one
critical-path track — do it as one continuous thread of work rather than
interleaved context-switches, per the ordering in that section. `wxPopupWindow`,
`wxNativeContainerWindow`, Phase 6 (DnD/clipboard), and Phase 7 (native
dialogs) are all genuinely separable (different files, no shared state)
and are good candidates to peel off into parallel branches/sessions once
the core pass is underway.

## 6. Recommended immediate next step

Phase 0 and Phase 1 are done: `docs/gtk/gtk4-status.md` has the real,
compiler-verified error inventory (1304 errors / ~80 files), and CI now
tracks the GTK4 build (allow-failure) over time. The merged Phase 2/3/4
design is also done — `docs/gtk/gtk4-phase2-window-model-design.md` —
and revised the effort estimate for that critical-path work down
substantially (weeks, not months) once §7's scope decision on
`wxNativeContainerWindow` is made.

Next concrete step: start executing that design, in the order given in
its §8 — beginning with the `wxGetTopLevelGDK()` → `GdkDisplay*` swap as
the smallest, most mechanical first PR to validate the approach before
taking on the event-controller input model.
