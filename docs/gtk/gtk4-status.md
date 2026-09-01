# wxGTK4 build status (tracked checklist)

This is the Phase 1 deliverable referenced by `docs/gtk/gtk4-port-plan.md`:
a compiler-verified inventory of what actually breaks when building against
real GTK4 headers, superseding the grep-based estimate in that document's
§3. Update this file as items get fixed; it's meant to be the running
source of truth, not a one-time report.

## How this was produced

**Build out of tree.** wxWidgets' `.gitignore` doesn't cover the
artifacts an in-tree build scatters through the source tree (and
upstream declined to add them, on the grounds that nobody builds
in-tree), so an in-tree build leaves ~1200 untracked files sitting on
top of your work, which makes `git status` useless for seeing what you
actually changed:

```
mkdir -p ../wxbuild-gtk4 && cd ../wxbuild-gtk4
../wxWidgets/configure --with-gtk=4 --disable-shared --without-opengl \
                       --disable-optimise --disable-stc --disable-tests \
                       --disable-uiactionsim
make -k -j4
```

Earlier updates in this file were produced with the same options run
in-tree; the out-of-tree build reproduces them exactly (verified:
identical diagnostic count *and* identical failing-target list), so the
numbers below are comparable across the switch.
Environment: Ubuntu 24.04, `libgtk-4-dev` 4.14.5, `libgtk-3-dev` 3.24.41
(both installed side by side; wx picks GTK4 via pkg-config `gtk4`).
`configure` succeeds cleanly and reports "Which GUI toolkit should
wxWidgets use? GTK+ 4" — the configure/build-system integration is fine.
`make -k` (keep-going) was used to surface every independent failure in
one pass rather than stopping at the first one.

## Headline finding

**This is not ~80 unrelated bugs. It is a small number of removed GTK3
APIs whose absence cascades through nearly the whole `src/gtk` tree**,
because the affected calls sit in widely-shared code
(`wxGetTopLevelGDK()` in `window.cpp`, `wxWindowGTK`'s base class in
`window.h`, generic container/child helpers). A clean `make -k -j4`
against GTK4 4.14 headers produces:

- **1304** `error:` diagnostics
- across **~80** distinct `.cpp`/`.h` translation units in `src/gtk`,
  `src/generic`, `src/aui`, `src/common`, `include/wx/gtk`
- essentially the *entire* GTK-specific core library fails to compile —
  only a handful of backend-agnostic files (netlib, baselib) build clean.

This means the ~95 existing `__WXGTK4__` conditionals in the tree (see
`gtk4-port-plan.md` §1) have **never been exercised against real GTK4
headers in this state** — they're necessary but nowhere near sufficient.
Treat "wxGTK4 already partially works" as false until this list is empty.

*(These are the baseline numbers from the very first build attempt. See
"Progress update" below for what's changed since — the raw count has
gone up, which is explained there and is expected, not a regression.)*

## Root-cause categories, by error volume

Deduplicated from the 1304 diagnostics, by the specific removed
identifier:

| Removed/changed API | Occurrences | GTK4 replacement | Plan phase |
|---|---:|---|---|
| `GTK_CONTAINER`, `gtk_container_add`, `gtk_container_remove` | 69 | `gtk_widget_set_parent`/widget-specific setters (no generic container) | Phase 2/5 |
| `GTK_BIN`, `gtk_bin_get_child` | 34 | widget-specific child accessors (`gtk_button_get_child`, etc.) — `GtkBin` removed entirely | Phase 2/5 |
| `gtk_widget_get_window` | 33 | none direct — only realized toplevels have a surface (`gtk_native_get_surface`); ordinary widgets have no window | **Phase 2 (the hard one)** |
| `gtk_widget_destroy` | 19 | implicit via `g_object_unref`/`gtk_window_destroy` — no generic widget destroy call | Phase 2/5 |
| `gtk_box_pack_start`, `gtk_box_pack_end` | 20 | `gtk_box_append`, `gtk_box_prepend` (no start/end distinction) | Phase 5 (mechanical) |
| `gdk_window_get_display` and other `gdk_window_*` | 10+ | `gdk_surface_get_display` etc. — `GdkWindow` type doesn't exist in GTK4 headers at all (0 hits found in `/usr/include/gtk-4.0/gdk/*.h`) | **Phase 2 (the hard one)** |
| `gtk_widget_get_toplevel` | 9 | `gtk_widget_get_root` (returns `GtkRoot*`, not `GtkWidget*` — needs a cast/rethink at each call site) | Phase 3 |
| `gtk_selection_data_get_length/target/data`, `gtk_drag_finish` | 22 | `GdkContentProvider`/`GdkContentDeserializer` — full DnD/selection rewrite | Phase 6 |
| `gtk_style_context_get`, `gtk_widget_style_get`, `GtkStateType`, `GTK_STATE_ACTIVE` | 19 | CSS-node-based `GtkStyleContext` state flags (`GtkStateFlags`) — old style-properties API gone | Phase 5 |
| `gtk_file_chooser_get_filename`, `gtk_file_chooser_set_filename` | 11 | GFile-based `gtk_file_chooser_get_file`/`set_file` | Phase 7 |
| `GTK_TOOL_BUTTON`, `GTK_RADIO_BUTTON`, and other deprecated type-check macros | ~10 | GTK4 dropped most `GTK_FOO(x)` cast macros for now-removed/renamed types | Phase 5 |
| `gdk/gdkx.h`, `gdk/gdkwayland.h` (fatal, wrong include path) | 8 files | moved to `gdk/x11/gdkx.h` / `gdk/wayland/gdkwayland.h` (backend-specific subdirs) — path rename is mechanical, but the code behind it still uses removed `GdkWindow` APIs (`GDK_WINDOW_XID` → `gdk_x11_surface_get_xid`), so fixing the `#include` alone does not clear these files | Phase 2 |

Long tail not itemized above (style-context conversions, `gtk_widget_get_preferred_height`
→ `measure()`, misc `-fpermissive` conversion errors) accounts for the
remaining ~150 diagnostics; these mostly resolve automatically once the
categories above are fixed, since they're downstream of the same missing
symbols in shared headers.

## What actually blocks everything else

**Update:** the design work is now done — see
`docs/gtk/gtk4-phase2-window-model-design.md`. Tracing the actual call
sites of `gtk_widget_get_window`/`GdkWindow` (and the ~18 per-widget
`GTKGetWindow()` overrides that depend on it) showed this isn't a single
opaque blocker but six distinct, mostly *easier*-in-GTK4 sub-problems
(display lookup, event-source identity, cursor setting, paint/clip,
Z-order, coordinate translation), none of which require redesigning how
child widgets are positioned — that part (`wxPizza`) already carries over
from GTK3 unchanged. `wxGetTopLevelGDK()` (`src/gtk/window.cpp:478`,
called from `bitmap.cpp`, `cursor.cpp`, `display.cpp`, `evtloop.cpp`,
`taskbar.cpp`, `utilsgtk.cpp`, plus `window.cpp` itself) turns out to be
mostly the "get me a display" pattern with a one-line fix
(`gdk_display_get_default()`), not a sign of deep window-per-widget
coupling. See the design doc for the full breakdown and the two items
that do need a real design/scope decision (`wxPopupWindow`,
`wxNativeContainerWindow`).

## Progress update: executing the design (this is expected to look worse before it looks better)

After landing the fixes below, a rebuild shows:

- **1760** `error:` diagnostics (up from 1304)
- **93** distinct failing files (up from ~80)
- **0** `fatal error:` diagnostics (down from **9**)

The error count going *up* is expected, not a regression: several fatal
errors (`gdk/gdkwayland.h`/`gdk/gdkx.h`: No such file or directory) were
aborting whole translation units — `window.cpp` itself hit a fatal error
on its very first `#include` chain (`wx/gtk/private/wayland.h` →
`wrapgdk.h`) and reported **zero** of its real errors as a result. Fixing
that central include (see below) let the compiler get past it and reveal
`window.cpp`'s true ~280-error surface for the first time, along with
similar unmasking in `app.cpp`, `toplevel.cpp`, `utilsgtk.cpp`, and
`include/wx/gtk/private/gtk2-compat.h`. Verified this wasn't a real
regression by diffing the before/after failing-file lists and checking
that none of the newly-visible errors trace back to the lines actually
changed (they're all pre-existing removed-API issues from the root-cause
table above, just previously hidden behind a fatal error). Net effect:
the error count is a worse *headline number* but a more *accurate* one —
every file's real error surface is now visible instead of some being
artificially truncated.

Concretely fixed and verified:

- **`include/wx/gtk/private/wrapgdk.h`**, the central header
  `<gdk/gdkx.h>`/`<gdk/gdkwayland.h>` get included from, now points at
  GTK4's `gdk/x11/gdkx.h`/`gdk/wayland/gdkwayland.h` subdirectories.
  Same fix applied to the handful of files that include these headers
  directly instead of through `wrapgdk.h` (`window.cpp`, `display.cpp`,
  `taskbar.cpp`, `src/unix/uiactionx11.cpp`/`utilsx11.cpp`).
  `src/gtk/renderer.cpp`'s occurrence is already dead code under
  `__WXGTK3__` (GTK2-only) and needed no change. This is the single
  biggest reason the "8 files, fatal, wrong include path" row in the
  table above is now fully resolved — 0 fatal errors anywhere.
- **`wxGetTopLevelGDK()`/`gtk_widget_get_window` "just get me a
  display" cases** (the design doc's bucket 1): new
  `wxGetTopLevelGdkDisplay()` in `window.cpp`
  (`gdk_display_get_default()` under `__WXGTK4__`, byte-for-byte
  unchanged behavior otherwise), swapped in across `cursor.cpp` (×3),
  `evtloop.cpp`, `utilsgtk.cpp`, `display.cpp`'s GTK4 `GetDisplay()`
  helper, `window.cpp`'s own `IsBackend()`, and
  `src/unix/utilsx11.cpp`'s `wxGetKeyStateGTK()`.
- **`wxGetPangoContext()`**: its no-toplevel-yet fallback used
  `GdkScreen` (removed in GTK4); now falls straight through to the same
  default-font-map fallback already used for console apps.
- **`wxGetMouseState()`/`wxGetMousePosition()`**: real GTK4
  implementations via `gdk_device_get_surface_at_position()`/
  `gdk_device_get_modifier_state()`, replacing the removed
  `gdk_device_get_position()`/`get_state()`. **Known, accepted fidelity
  gap**: GTK4 (deliberately, under Wayland) provides no API to query the
  pointer's true global screen position any more, only its position
  relative to whatever surface it's currently over. Not chasing an
  X11-only raw-Xlib workaround for this in the current pass — flagging
  it here the same way `wxNativeContainerWindow`'s gap is flagged, for a
  deliberate decision later rather than silently wrong behavior now.
- **`wxWindowGTK::IsBackend()`**: swapped its GTK4 default from a
  toplevel's (nonexistent) `GdkWindow` type to the default display's
  type — same backend-name prefix match (`"GdkWayland"`/`"GdkX11"`),
  functionally equivalent.
- `include/wx/gtk/evtloop.h`: `GdkEvent` forward-declared as `union`
  unconditionally; GTK4 declares it `struct`. Fixed with an
  `__WXGTK4__` conditional (commit `e80b8d4`).
- `3rdparty/nanosvg` submodule wasn't checked out locally, causing an
  unrelated `bmpsvg.cpp` failure — environment issue, not a GTK4
  incompatibility; resolved with `git submodule update --init`.
- `wxNativeContainerWindow` is now cleanly gated off under GTK4 via the
  existing `wxHAS_NATIVE_CONTAINER_WINDOW` feature macro rather than
  left to fail with a confusing compiler error (see
  `gtk4-upstream-summary.md` for the full reasoning) — `nativewin.cpp`
  compiles clean now.

**A caught-and-fixed regression, for the record**: the first pass of the
`wxGetTopLevelGDK()` swap missed two call sites in
`src/unix/utilsx11.cpp` (`wxGetKeyStateGTK()`'s `GdkKeymap` usage, also
needing its own GTK4 fix since `GdkKeymap` is removed too — replaced with
`GdkSeat`/`GdkDevice`-based lock-state queries) because the initial sweep
only grepped `src/gtk/*.cpp` call sites, not `src/unix/*.cpp` ones found
via the forward-declaration search. Caught by rebuilding and diffing
before/after error output rather than assuming the change was correct —
worth calling out as the reason every batch in this file gets a rebuild
before being called done.

## Progress update 2: `GtkContainer`/`GtkBin` removal, first pass

Also produced `docs/gtk/gtk4-phase3-input-model-design.md` (the
event-controller input model design, Phase 3) in between the two
progress updates in this file — see that document for why it stops at
design rather than also implementing, unlike the Phase 2 window-model
document. Decided to spend the next implementation pass on
`GtkContainer`/`GtkBin` instead, since it's mechanical, independently
compile-verifiable, and orthogonal to the input-model architecture
question (see that design doc's closing recommendation).

After this batch:

- **1730** `error:` diagnostics (down from 1760)
- **93** distinct failing files (unchanged — this batch reduced error
  *density* per file, not the count of touched files, since none of the
  5 files fixed were fully cleared)
- **0** fatal errors (unchanged)
- Verified no regressions: grepped the new build log for every API this
  batch introduced (`gtk_scrolled_window_set_child`,
  `gtk_frame_set_child`, `gtk_widget_unparent`, `gtk_widget_insert_after`,
  `gtk_widget_queue_allocate`, `ContainerWidgetAddChild`) — none appear
  in any error, and no `GTK_CONTAINER`/`GTK_BIN` errors remain in any of
  the 5 touched files.

Fixed: `window.cpp` (scrolled-window child, `Reparent()`'s generic
`gtk_widget_unparent()`, `RealizeTabOrder()`'s tab-order-via-physical-
reordering — flagged with a Z-order caveat needing runtime verification),
`statbox.cpp` (`GtkFrame` child, a redundant remove call, a
runtime-dead-under-GTK4 border-width fallback), `textctrl.cpp`
(scrolled-window child), `settings.cpp` (`ContainerWidget()`'s scratch
`GtkFixed` helper split GTK3/GTK4 behind a shared
`ContainerWidgetAddChild()`), `win_gtk.cpp` (`wxPizza::scroll()`'s
`gdk_window_scroll()` pixel-blit optimization, which has no GTK4
equivalent at all, replaced with a plain re-allocate + redraw — correct
since `size_allocate_child()` already positions children from
`m_scroll_x`/`m_scroll_y` on every allocation pass, just not
blit-optimized).

**New finding while surveying the remaining `GtkContainer`/`GtkBin`
occurrences**: not all of them are mechanical accessor renames. Two
files depend on GTK widget *types* that GTK4 removed entirely, not just
container/bin accessors on types that still exist:

- **`src/gtk/toolbar.cpp`**: built on `GtkToolbar`/`GtkToolItem`, both
  confirmed absent from GTK4 headers (0 hits grepping
  `/usr/include/gtk-4.0/gtk/*.h`). `wxToolBar`'s native GTK backend needs
  a real redesign — most likely composing a `GtkBox` of plain
  `GtkButton`s by hand, similar to what other GTK4 apps do, or falling
  back to wx's own generic toolbar implementation for this port. Not a
  Phase 5 mechanical fix; deserves its own design pass.
- **`src/gtk/radiobut.cpp`/`radiobox.cpp`**: built on `GtkRadioButton`,
  also confirmed absent from GTK4 (same check, 0 hits). GTK4's
  replacement is `GtkCheckButton` with `gtk_check_button_set_group()`
  for the mutual-exclusion grouping that used to be `GtkRadioButton`'s
  job. Same situation as toolbar.cpp — a real widget-backend redesign,
  not a rename.
- **`src/gtk/toplevel.cpp`**'s `GTKHandleRealized()` goes deeper than
  its one `gtk_container_forall()` call (titlebar detection, left
  unfixed since the function doesn't compile regardless): it also calls
  `gdk_window_set_decorations()`/`set_functions()`/`set_cursor()` on the
  toplevel's `GdkWindow`, none of which have direct GTK4 equivalents —
  window-manager decoration/function hints are handled fundamentally
  differently under GTK4's client-side-decoration model. Another
  deferred redesign, not attempted here.

These three join `taskbar.cpp` (`GtkStatusIcon`) and
`uiactionx11.cpp` (`GdkWindow`-based input synthesis) as confirmed
whole-subsystem redesigns rather than mechanical fixes — worth keeping
as a distinct tracked category since lumping them in with the
mechanical `GtkContainer`/`GtkBin` count understates how much design
work (as opposed to translation work) is actually left.

## Progress update 3: `button.cpp`, and the error-count trend so far

Fixed `button.cpp`'s 3 unconditional `gtk_bin_get_child` calls (`m_widget`
is always a plain `GtkButton` here — not the check/toggle/radio family,
which is its own more complicated case, see below — so
`gtk_button_get_child(GTK_BUTTON(m_widget))` is an unambiguous swap; one
call site was inside an existing but never-compiled `__WXGTK4__` branch
that still called the wrong function). Also fixed `GetDefaultSize()`,
which used the now-fully-removed `GtkButtonBox` to compute GTK's own
idea of the minimum default button size; simplified the GTK4 path to
just use the button's own CSS-driven preferred size directly (flagged
as not yet visually verified, same as other behavior-affecting — not
just renamed — fixes this session).

**1629** errors after this batch (0 fatal, 93 files, no regressions —
button.cpp's three fixed call sites don't reappear; its remaining
errors are different, not-yet-attempted issues: old style API,
`gtk_widget_get_can_default`/`set_can_default`/`grab_default`, i.e. the
default-button-highlighting mechanism, which also changed under GTK4).

Running trend this session, each step verified by full rebuild with no
regressions: 1304 (masked baseline) → 1760 (unmasked, accurate) → 1730
(`GtkContainer`/`GtkBin` batch 1) → 1641 (`gtk_widget_add_events`/touch
batch) → **1629** (`button.cpp`). About 130 real errors cleared since
the accurate baseline, alongside the two design documents (window model,
input model) and the `wxNativeContainerWindow`/`nativewin.cpp` fix.

**Next mechanical candidate, not yet started**: `gtk_widget_destroy()`
doesn't exist under GTK4 either (`gtk_window_destroy()` is the toplevel-
specific replacement already used in a couple of fixes above; plain
widgets are destroyed via `g_object_unref`/`gtk_widget_unparent`
instead, since destruction is implicit once nothing references a widget
and it's removed from its parent). ~20 files hit this
(`assertdlg_gtk.cpp`, `clipbrd.cpp`, `overlay.cpp`, `private.cpp`,
`utilsgtk.cpp`, `settings.cpp`, `window.cpp`, `control.cpp`,
`filepicker.cpp`, `infobar.cpp`, `msgdlg.cpp`, `menu.cpp`,
`radiobox.cpp`, `aboutdlg.cpp`, `toolbar.cpp`, and others) — each call
site needs a quick look at whether it's destroying a toplevel
(`gtk_window_destroy`) or a plain widget (`g_object_unref`/
`gtk_widget_unparent`), same judgment call already made correctly for
`button.cpp`'s scratch window and `settings.cpp`'s `ContainerWidget()`.

## Not yet attempted / explicitly deferred

- The remainder of the mechanical `GtkContainer`/`GtkBin` occurrences not
  covered by the first pass above, plus the rest of the root-cause table
  (DnD/clipboard rewrite, old style API, `gtk_box_pack_*`, file choosers,
  deprecated type-check macros) — left for the phases the port plan
  assigns them to.
- **Confirmed whole-subsystem redesigns** (not mechanical, need their own
  design pass, not attempted): `toolbar.cpp` (`GtkToolbar`/`GtkToolItem`
  removed), `radiobut.cpp`/`radiobox.cpp` (`GtkRadioButton` removed,
  replacement is `GtkCheckButton` grouping), `toplevel.cpp`'s
  `GTKHandleRealized()` (window-manager decoration/function/cursor hints
  work fundamentally differently under GTK4's CSD model), `minifram.cpp`
  (`gtk_event_box_new()` — `GtkEventBox` is also confirmed absent from
  GTK4 headers; `wxMiniFrame`'s custom title-bar/border drag handling was
  built entirely around it).
- **`gtk_widget_add_events()`/`set_events()`**: confirmed absent from GTK4
  headers entirely (event masks are meaningless once delivery goes
  through `GtkEventController` objects instead). Guarded out the 4
  call sites in files not otherwise blocked (`win_gtk.cpp`'s
  `wxPizza::New()` — which also needed `gtk_widget_set_has_window()`
  guarded out, since no widget owns its own window under GTK4, not just
  masks; `window.cpp`'s touch/touchpad-gesture mask setup; `textctrl.cpp`'s
  enter/leave mask). The two remaining occurrences
  (`minifram.cpp`, `toplevel.cpp`) are inside the whole-subsystem
  redesigns above and weren't worth touching in isolation.
  `window.cpp`'s raw touch-event pipeline
  (`touch_callback()`/`wxEmulate*Event()`/`wxEmit*TapEvent()`, all built
  on opaque-under-GTK4 `GdkEventTouch*`) is squarely part of the Phase 3
  event-controller migration and was guarded out as one ~150-line block
  rather than translated blind, for the same not-yet-runtime-testable
  reason given in `gtk4-phase3-input-model-design.md`. This batch alone
  took the error count from 1730 to **1641** (0 fatal, verified no
  regressions by grepping the new build log for every symbol this batch
  touched).
- `src/gtk/taskbar.cpp`: has its own, deeper problem beyond anything in
  this file — `GtkStatusIcon` doesn't exist in GTK4 at all (system tray
  icons need the StatusNotifierItem D-Bus protocol or a library like
  libayatana-appindicator; there's no direct API replacement). Fixed its
  include path for consistency but did not attempt the real fix; the
  file still fails to compile.
- `src/unix/uiactionx11.cpp` (`wxUIActionSimulator`'s X11 backend):
  fixed its include path and forward declarations for consistency, but
  its actual mouse/keyboard synthesis logic is built entirely on
  `GdkWindow`/`GDK_WINDOW_XID`, which has no GTK4 equivalent — a real,
  separate design problem, not attempted here. Currently excluded from
  the build entirely via `--disable-uiactionsim`, so this isn't blocking
  anything today.

## Progress update 8: `win_gtk.cpp`'s `measure()`/`size_allocate` vfunc migration

Picked up the `win_gtk.cpp` scope flagged as "a legitimate next
significant target" at the end of update 7. `wxPizza` (the
`GtkFixed`-derived widget backing every `wxWindow`) overrides several
`GtkWidgetClass`/`GtkContainerClass` vfuncs whose signatures or existence
changed under GTK4:

- `size_allocate`: GTK4 dropped `GtkAllocation*` in favor of separate
  `width`/`height`/`baseline` parameters (a widget no longer owns a
  window it needs to position). Split into two full function definitions
  — GTK4 keeps only the border-inset width calc and child-position loop;
  GTK3/GTK2 keep the original signature and the window-repositioning code
  intact. The repositioning code was the mechanism that let
  `BORDER_STYLES` decoration (simple/raised/sunken/theme borders, e.g. on
  `wxTextCtrl`/`wxListBox`) show through the parent window — already a
  known gap since wxPizza went windowless under GTK4 (update 7); this
  batch doesn't add new gap, just carries the existing one through the
  new vfunc shape with an explicit comment.
- `get_preferred_width`/`get_preferred_height`/`adjust_size_request`
  merged into one `measure()` vfunc. New `pizza_measure()` always reports
  a zero minimum unconditionally — GTK3's `GTK_IS_TOOL_ITEM` special case
  is dropped because `GtkToolItem` doesn't exist under GTK4 at all (no
  toolbar port exists yet, see the deferred `toolbar.cpp` item), so there
  is currently no way for a wxPizza to be inside one.
- `GtkContainerClass` (and therefore its `add`/`remove` vfunc slots)
  doesn't exist under GTK4 — `pizza_add()`/`pizza_remove()` are guarded
  out entirely rather than ported, since nothing can call
  `gtk_container_add()`/`remove()` generically any more; child add/remove
  already goes through `wxPizza::put()`/`RemoveChild()` directly. (Noted
  but not acted on: with these vfuncs gone, nothing automatically calls
  back into `wxPizza::m_children` bookkeeping if a child is unparented via
  raw `gtk_widget_unparent()` outside of `RemoveChild()` — `notebook.cpp`
  already does this even under GTK3 without observed issues, so treated
  as consistent with existing behavior, not a new bug introduced here.)
- `pizza_show`/`pizza_hide`: `gtk_widget_queue_draw_area()` (partial-rect
  invalidation) doesn't exist under GTK4; falls back to whole-parent
  `gtk_widget_queue_draw()`.
- `gtk_widget_size_allocate()` gained a `baseline` int parameter (`-1` =
  no baseline, matching prior behavior).
- `gtk_widget_is_toplevel()` doesn't exist under GTK4; `wxPizza::put()`
  now checks `GTK_IS_WINDOW()` instead.
- `gtk_style_context_get_border()` dropped its `GtkStateFlags` parameter
  under GTK4 — it queries the context's current state, which the existing
  `gtk_style_context_set_state()` call already set, so behavior is
  unchanged.

Verified via full rebuild (`make -k -j4`, whole tree): `win_gtk.cpp` went
from 1 remaining error to 0 (only deprecation warnings, expected — GTK4
still ships these style/allocation APIs but flags them in favor of the
CSS/snapshot render model that's the subject of the still-pending Phase 4
redesign). `win_gtk.cpp` dropped off the failing-files list entirely, no
new failing files introduced (diffed the full failing-file list against
the pre-batch build; zero regressions). Combined with the cursor.cpp
rewrite in update 9 below (verified in the same rebuild pass), the two
batches together moved the count to **1529 → 1494**.

**Same signature break found in 5 other files**, not fixed here because
each is already entangled in much larger, separately-scoped rewrites and
fixing just this one call wouldn't get them compiling anyway:
`renderer.cpp` and `settings.cpp` (both deep in the removed
`GtkStateType`/`gtk_style_context_get()` varargs/`GtkWidgetPath` style-API
family), `notebook.cpp` (removed `GtkContainer`, `gtk_box_set_child_packing`,
`gtk_widget_show_all`), `control.cpp` and `statbox.cpp` (removed
`gtk_widget_style_get`/`gtk_widget_get_preferred_width` and friends).
Worth revisiting once those files' broader rewrites are scoped.

## Progress update 9: `cursor.cpp` rewritten for GTK4's name/texture cursor API

`GdkCursorType` (the old X-cursor-font shape enum), `gdk_cursor_new_for_display()`,
`gdk_cursor_new_from_surface()`/`from_pixbuf()`, and `gdk_cursor_get_image()`
are all gone under GTK4 — `GdkCursor` can now only be constructed from a CSS
cursor-name string or a `GdkTexture`, and neither constructor takes a
`GdkDisplay*` any more (a cursor is display-independent in GTK4).

- `InitFromStock()`: added a GTK4-only path mapping each `wxStockCursor` to
  the closest standard CSS cursor keyword via
  `gdk_cursor_new_from_name(name, fallback)`. A handful of stock cursors
  with no CSS equivalent (paint brush/spraycan/pencil, the three
  mouse-button cursors, "point at scrollbar arrow") fall back to
  `"default"` — a known, minor, explicitly-documented fidelity gap, not yet
  runtime-verified (no linkable `test_gui` yet). `wxCURSOR_SIZENWSE` and
  `wxCURSOR_SIZENESW` now get their own correct diagonal-resize names
  instead of both collapsing to the old 4-way "move" glyph.
- `InitFromBitmap()`/`InitFromImage()`: build a `GdkTexture` via
  `gdk_texture_new_for_pixbuf()`, pass it to `gdk_cursor_new_from_texture()`.
- `GetHotSpot()`: `gdk_cursor_get_hotspot_x/y()` directly, instead of
  digging hotspot values out of the removed `gdk_cursor_get_image()`'s
  `GdkPixbuf` options.

One self-inflicted bug caught before commit: an early edit left a trailing
`gdk_cursor_new_from_pixbuf()` call sitting *after* the `#elif`/`#endif`
for the GTK3-version-gated branch, with no `__WXGTK4__` guard of its own —
unconditional C++ that compiled fine under GTK3 but broke GTK4 the moment
the file was touched again. Caught by the standalone-compile check, not by
inspection; a reminder that `#elif`/`#endif` chains don't imply the code
*after* the chain is still inside a branch.

Verified via standalone compile against real GTK4 4.14.5 and GTK3 3.24.41
headers (matching the actual build's flags): zero errors or warnings under
GTK4, zero errors under GTK3 (no regression). Confirmed via the subsequent
full whole-tree rebuild: `cursor.cpp` also dropped off the failing-files
list, no new failures anywhere else in the tree.

Session running total: 1760 → 1730 → 1641 → 1629 → 1598 → 1577 → 1569 →
1555 → 1529 → **1494** (win_gtk.cpp + cursor.cpp combined). Distinct
failing files: 84 → **82**.

## Progress update 10: small mechanical batch, plus a methodology fix and two newly-scoped deferred items

Picked off seven small, independent errors surfaced by the last full
rebuild (1-4 errors each) rather than one big file:

- **`gtk3-compat.h`**: restored the `GTK_STYLE_CLASS_*` string-constant
  macros (`BUTTON`, `CELL`, `EXPANDER`, `GRIP`, `INLINE_TOOLBAR`,
  `PANE_SEPARATOR`) as plain string shims — the macros were dropped along
  with the rest of the deprecated style-context header, but
  `gtk_style_context_add_class()` still takes a plain string, so this is a
  pure restoration, no behavior change.
- **`dockart.cpp`** (AUI docking splitter handle): needed an explicit
  `gtk3-compat.h` include to see the shim above (it only pulled in
  `wrapgtk.h`/`private.h`, neither of which includes it).
- **`control.cpp`**: `GetClassDefaultAttributes()` rewritten — GTK4 has no
  replacement for querying a widget's effective background as a single
  flat colour (backgrounds are `render_background()`-painted, possibly a
  gradient/image, under CSS), so `gtk_style_context_lookup_color(sc,
  "theme_bg_color", ...)` is used as an approximation, falling back to
  opaque white. Unlike the GTK3 code, this doesn't walk the parent chain,
  so a widget with no `theme_bg_color` of its own won't inherit an
  ancestor's background — a known, documented fidelity gap, not yet
  runtime-verified. Font description now comes from
  `pango_context_get_font_description(gtk_widget_get_pango_context())`
  instead of the removed `GTK_STYLE_PROPERTY_FONT` query (copied via
  `pango_font_description_copy()` since the context owns the original —
  `wxNativeFontInfo`'s destructor frees `description`, so handing it a
  borrowed pointer would double-free the context's copy).
  `GTKGetEntryMargins()` got the same `get_border()`/`get_padding()`
  state-parameter fix as `win_gtk.cpp` (update 8).
- **`app.cpp`**: `gtk_events_pending()` → `g_main_context_pending(nullptr)`
  (GTK4 removed the former; it was always just a thin wrapper around the
  latter). Does **not** fully fix `app.cpp` — see below.
- **`bitmap.cpp`**: `wxBitmap(const wxCursor&)` rebuilt on
  `gdk_cursor_get_texture()` + `gdk_pixbuf_get_from_texture()`, replacing
  the removed `gdk_cursor_get_image()`.
- **`checklst.cpp`**: `gtk_tree_view_column_cell_get_size()` dropped its
  leading `GdkRectangle*` parameter under GTK4.
- **`clrpicker.cpp`/`colordlg.cpp`**: their GTK4 branches (added in an
  earlier session batch) passed a `wxColour` directly where
  `gtk_color_chooser_set_rgba()` needs a `const GdkRGBA*` — fixed with
  `wxColourImpl::GTKGetRGBA()`, the same conversion the neighboring GTK3
  branches already used.

Verified via standalone compiles against real GTK4 4.14.5 and GTK3 3.24.41
headers (build's actual flags) for each file individually, then via a full
whole-tree rebuild.

**Methodology correction**: the failing-*files* diff used in updates 8-9
(grepping error lines for a leading file path) silently misattributes
errors to whichever header the bad line lives in, not the `.cpp`
translation unit that triggered it — so a `.cpp` file can vanish from that
list while its build target still fails. Caught here because `app.cpp`
looked "fixed" by that method despite `corelib_gtk_app.o` still failing
(via `threads.h`, see below). Switched to diffing the actual failed
**build targets** (`grep -oP` on `make`'s `[Makefile:NNNNN: target.o]
Error` lines) instead, which tracks what a rebuild actually produces.
Re-checked updates 8-9 against this method too: no discrepancy there,
both `win_gtk.cpp` and `cursor.cpp` genuinely dropped off. This doc's
future updates use the target-based method.

By that method: **78 → 72** failing build targets, zero regressions
(nothing newly failing). 6 targets fixed: `dockart.o`, `bitmap.o`,
`checklst.o`, `clrpicker.o`, `colordlg.o`, `control.o`. `app.o` correctly
still fails, for an unrelated reason (below). Diagnostic count: 1494 →
**1481**.

**Two newly-scoped deferred items found along the way**, both real
enough to need a design decision rather than a mechanical fix:

1. **`wxGtkStyleContext`/`wxGtkWidgetPath`** (`stylecontext.h` +
   `settings.cpp`): a synthetic-`GtkWidgetPath`-based mechanism for
   querying theme style info (colours, sizes) for widget *types* that are
   never actually instantiated — built on `GtkWidgetPath`,
   `gtk_style_context_new()`, parent-context chains, and sibling-path
   tricks (`AddTreeviewHeaderButton()`), none of which exist under GTK4 at
   all (`GtkWidgetPath` the type is simply gone). Blocks `statbox.cpp`,
   `notebook.cpp`, `renderer.cpp`, and parts of `settings.cpp` itself — a
   single shared fix would unblock several files at once, higher leverage
   than most remaining items, but GTK4's idiomatic replacement (build real
   scratch widgets instead of synthetic paths, the same idiom already used
   for `button.cpp`'s `GetDefaultSize()` earlier this session) needs to be
   worked out per `Add*()` method, and a couple of them
   (`AddMenu()`/`AddMenuItem()`) reference `GTK_TYPE_MENU`, which doesn't
   exist under GTK4 either and ties into the already-deferred `menu.cpp`
   rewrite. Candidate for the next design pass.
2. **`gdk_threads_enter()`/`gdk_threads_leave()`** (`wx/gtk/private/
   threads.h`, used via `wxGDKThreadsLock` in `textctrl.cpp`, `timer.cpp`,
   `toplevel.cpp`, plus direct calls in `app.cpp`): GTK4 removed the GDK
   thread-lock mechanism entirely, with no direct replacement — its
   threading model requires GTK calls to stay on the main thread, full
   stop. Stubbing these out as no-ops would compile, but would silently
   remove real thread-safety rather than just lose visual fidelity, for
   whichever of these 9 call sites are genuinely reached from a worker
   thread. Unlike the approximation gaps documented elsewhere in this doc,
   this isn't safe to guess at without checking each call site's actual
   threading context — left unfixed, `app.cpp` still fails to build
   because of it.

## Progress update 7: starting the `gtk_widget_get_window` sweep

Surveyed all ~51 remaining `gtk_widget_get_window` occurrences against
the six buckets from `gtk4-phase2-window-model-design.md`. Most are the
`GTKGetMainWindow()`/`GTKGetConnectWindow()`/`GTKFindWindow()`
event-routing family (obsolete only once Phase 3's event controllers
land) or paint/clip code tied to the `draw`→`snapshot` migration
(Phase 4) — not safe to stub blind, same reasoning as this session's
touch-event pipeline decision. Picked off the three genuinely
independent ones:

- **`cursor.cpp`**'s `SetGlobalCursor()`: the "cursor setting" bucket —
  `gtk_widget_set_cursor()` works directly on any widget under GTK4, no
  window needed, simpler than the GTK3 code, not just translated.
- **`display.cpp`**'s `GetFromWindow()`: already in the file's existing
  `__WXGTK4__` branch — `gdk_display_get_monitor_at_window()` →
  `gdk_display_get_monitor_at_surface()` via
  `gtk_widget_get_native()`/`gtk_native_get_surface()`.
- **`win_gtk.cpp`**'s `pizza_size_allocate()`/`pizza_realize()`: these
  repositioned wxPizza's own inset `GdkWindow` to make room for
  `BORDER_STYLES` decoration drawn on the parent window — but wxPizza is
  windowless under GTK4 (removed its `has_window` call earlier this
  session), so there's no window left to reposition. Guarded out with an
  explicit **known gap**: border rendering (simple/raised/sunken/theme
  borders on `wxTextCtrl`, `wxListBox`, etc.) is likely incomplete under
  GTK4 until this gets a real redesign as part of the painting
  migration — not runtime-verified.

**1555** errors after this batch (0 fatal, no regressions — confirmed
the specific targeted errors are gone in all three files).

**Real new scope discovered while verifying**, worth calling out since
it changes the picture for these three files: none of them are anywhere
near fully fixed.
- `display.cpp`'s existing `__WXGTK4__` branch assumed
  `gdk_display_get_n_monitors()`/`get_monitor()`/`get_monitor_at_point()`
  would carry over from GTK3 — they didn't; GTK4 replaced indexed
  monitor access with a `GListModel`-based `gdk_display_get_monitors()`.
  A real, moderate-sized rewrite (~8 call sites), not attempted here.
- `cursor.cpp` still needs `GdkCursorType` (the old X-cursor-font shape
  enum — `GDK_LEFT_PTR`, `GDK_WATCH`, etc., all gone) replaced with
  GTK4's named-cursor-string API (`"wait"`, `"grab"`, etc.) across
  roughly two dozen call sites.
- `win_gtk.cpp` needs the `measure()` vfunc migration predicted early
  this session (`get_preferred_width`/`get_preferred_height` vfuncs are
  gone, `GtkWidgetClass` has no `adjust_size_request` any more) *and*
  `GtkContainerClass`'s `add`/`remove` vfunc table, which doesn't exist
  either — wxPizza's `pizza_add()`/`pizza_remove()` need porting to
  GTK4's generic parent/child widget API. This is core, foundational
  code (wxPizza backs every `wxWindow`) and a legitimate next
  significant target, distinct from anything already on the deferred
  whole-subsystem list.

Session running total: 1760 → 1730 → 1641 → 1629 → 1598 → 1577 → 1569 →
**1555**.

## Progress update 6: `gtk_widget_get_toplevel()`, same shim pattern

Same treatment as `gtk_box_pack_start`/`pack_end`: added
`wx_gtk_widget_get_toplevel()` to `gtk3-compat.h`, `#define`'d over the
old name. `gtk_widget_get_root()` (the GTK4 replacement) returns a
`GtkRoot*` interface pointer instead of a `GtkWidget*`, and returns
`nullptr` for a not-yet-rooted widget instead of GTK3's confusing
"returns the widget itself" convention — checked every call site in the
tree first, and all of them already guard the result (`GTK_IS_WINDOW()`
checks, or an assumption it's parented by that point in dialog-creation
code), so this is a safe, arguably more-correct substitution. Added the
header include to `statusbr.cpp`, `popupwin.cpp`, `dialog.cpp`,
`textentry.cpp`; `dirdlg.cpp`/`filedlg.cpp` already had it.
`overlay.cpp`'s call site is left alone — that file has deeper,
already-documented blocking issues with no marginal benefit from this
fix alone, confirmed by rebuild (it's the only remaining
`gtk_widget_get_toplevel` error).

**1569** errors after this batch (0 fatal, no regressions). Session
running total: 1760 → 1730 → 1641 → 1629 → 1598 → 1577 → **1569**.

**Next candidate scoped but not started**: the `gdk_window_get_display`
occurrences remaining (`window.cpp`, `toplevel.cpp`, `settings.cpp`,
`minifram.cpp`, `cursor.cpp`, `popupcmn.cpp`, `assertdlg_gtk.cpp`) all
call it on a local `GdkWindow*` obtained via `gtk_widget_get_window()`
in the same function — meaning the real fix isn't a `gdk_window_get_display`
shim, it's part of the much bigger 50-occurrence `gtk_widget_get_window`
category that `gtk4-phase2-window-model-design.md` already scoped into
six buckets (display lookup, event-source identity, cursor setting,
paint/clip, Z-order, coordinate translation). Worth tackling as its own
focused pass through that design rather than picking off individual
`gdk_window_get_display` call sites, since each one needs the same
"what bucket does this actually belong to" judgment call already made
for `wxGetTopLevelGDK()`'s consumers.

## Progress update 5: `gtk_box_pack_start`/`gtk_box_pack_end` via a shared shim

Rather than porting each of the ~21 call sites individually,
`include/wx/gtk/private/gtk3-compat.h` (which already shims other
removed GTK3 APIs for GTK4 the same way) got a `wx_gtk_box_pack_start()`
helper `#define`'d over both `gtk_box_pack_start` and `gtk_box_pack_end`.
It maps `expand`/`fill`/`padding` to the box's actual orientation
(`hexpand`/`vexpand`, `halign`/`valign`, margins) and calls
`gtk_box_append()`. `gtk_box_pack_end`'s call sites in this codebase
never use GTK3's "stack backward from the end" multi-pack_end pattern —
each box has at most a handful of pack_end children that just need to
land after whatever was already packed — so mapping it to the same
`append()`, called in the original chronological order, gives the same
visual result for every case actually present here (flagged as worth
re-checking once `test_gui` can run). Added the header include to the 5
files that didn't already have it; `dataview.cpp` and `toplevel.cpp`
needed no changes at all since they already included it.

Caught one real bug while verifying: a pre-existing, never-compiled
`__WXGTK4__` branch in `assertdlg_gtk.cpp` called a 2-argument
`gtk_box_pack_end(box, button)` that never existed in any GTK version —
didn't match the new 5-argument macro either, producing an "undeclared"
error that looked like it came from the shim itself. Fixed by calling
`gtk_box_append()` directly there, since that line was already inside a
GTK4-only branch.

**1577** errors after this batch (0 fatal, no regressions — verified via
two rebuilds, the second confirming the `assertdlg_gtk.cpp` fix cleared
the last holdout). Session running total: 1760 → 1730 → 1641 → 1629 →
1598 → **1577**.

## Progress update 4: `gtk_widget_destroy()`, and three more deferred subsystems

Fixed the 9 files where `gtk_widget_destroy()`'s replacement was a safe,
type-specific call: `gtk_window_destroy()` for `GtkWindow` subclasses
(`assertdlg_gtk.cpp`, `utilsgtk.cpp`, `settings.cpp`, `control.cpp`,
`aboutdlg.cpp`), or unparenting for plain widgets
(`window.cpp`'s core `wxWindowGTK` destructor — used by every window in
the framework, needs a runtime `GTK_IS_WINDOW()` check since this one
function has to handle both toplevels and regular widgets correctly;
`filepicker.cpp`'s shared `GtkFileChooserButton`; `infobar.cpp`'s
buttons). `private.cpp` got the same `GetContainer()`/`AddToContainer()`
GTK3-vs-GTK4 split already used in `settings.cpp` (it hit both
`GTK_WINDOW_POPUP` and `GtkContainer` removal at once).

**1598** errors after this batch (0 fatal, 93 files, no regressions —
`window.cpp`'s destructor fix is flagged as core lifecycle code not yet
verified against a running app, same treatment as the earlier Tab-order
Z-order concern; exactly what `test_gui` under `xvfb-run` would catch
once it can link).

**Three more confirmed whole-subsystem redesigns found while surveying
the remaining call sites** (joining `toolbar.cpp`, `radiobut.cpp`/
`radiobox.cpp`, `minifram.cpp`, `taskbar.cpp`, `toplevel.cpp`'s WM
hints):
- **`clipbrd.cpp`**: connects `"selection_received"`/
  `"selection_clear_event"`, raw X11-selection signals that don't exist
  under GTK4 either — it's `GdkClipboard`/`GdkContentProvider` now.
  Squarely Phase 6, already scoped in the root-cause table; fixing just
  the destroy calls here would have had zero marginal benefit.
- **`overlay.cpp`**: 24 total errors including `GdkScreen`,
  `gtk_widget_get_toplevel`, and the `"draw"` signal — `wxOverlay`'s
  transparent-window GTK backend (used for rubber-band selection
  rendering) needs its own redesign.
- **`menu.cpp`**: `GtkMenu`/`GtkMenuItem` confirmed entirely absent from
  GTK4 headers (replaced by `GMenuModel`/`GtkPopoverMenu`) — bigger in
  practical impact than `GtkToolbar`'s removal, since menus (menu bars,
  context menus) are used almost everywhere. Not attempted; deserves its
  own design document before any code, same as the window/input models.

**Also found**: `gtk_dialog_run()` is removed under GTK4 (modal dialogs
are async now, no blocking call) — affects `msgdlg.cpp`, `print.cpp`,
and is *why* `assertdlg_gtk.cpp`/`utilsgtk.cpp` still don't fully
compile despite this batch's fixes to their `gtk_widget_destroy` calls.
A real behavioral redesign (needs a nested-mainloop-pump replacement,
wx already has infrastructure for this pattern elsewhere) — not
something to guess at without runtime verification, so left alone.

## Unit tests: the base test suite already builds and runs today

wxWidgets ships a real, extensive Catch2-based test suite in `tests/`
(the console `test` binary — strings, dates, containers, streams,
sockets, filesystem, config, XML, etc. — and the GUI-dependent
`test_gui` binary). The `3rdparty/catch` submodule wasn't checked out
locally, same situation as `3rdparty/nanosvg` earlier; initialized it
with `git submodule update --init 3rdparty/catch`.

**`test` (the console binary) links only `wxBase`/`wxNet`/`wxXml` — not
the GUI library — so it builds and runs today, completely independently
of the GTK4 corelib work above being finished.** Verified locally:
compiles with 0 errors, links, and running it executes **1,231,492
assertions across 444 test cases, with 430 passing**. The 14 failures
are all environmental, not port regressions: `numformatter.cpp`/
`intltest.cpp` need a locale with thousands-separator formatting (this
container only has `C`/`C.utf8`/`POSIX`), and `url.cpp` needs outbound
network access (no egress in this sandbox). None of this session's
changes touch `wxBase` at all, so this is expected, not a surprise —
but it's still a genuine, valuable regression check to have running
continuously as the port progresses, since a base-library regression
would be just as bad as a GTK4-specific one.

Wired this into CI (`.github/workflows/ci.yml`): the `gtk_version: 4`
matrix entry now has `base_tests_only: true`, which makes the "Building
tests" step build just the `test` target (the full build and
`failtest` both need the GUI library and would fail), and skips the
two GUI-only testing steps (`test_gui`, Xvfb). The existing "Testing"
step (`./test`) needed no changes — it was already GUI-independent —
so it now actually runs and reports real pass/fail counts for this
matrix entry instead of being skipped because an earlier step failed
outright.

**`test_gui` remains blocked** until enough of the GTK4 corelib
compiles to link it — that's the real milestone to watch for, since it
would unlock exercising this session's window/input-model work (the
Tab-order Z-order caveat, `wxGetMouseState`'s reduced fidelity, etc.)
under `xvfb-run`, which is available in this environment (confirmed via
`which Xvfb xvfb-run`) but has had nothing to test against so far.

## Progress update 11: three more one-line-per-file fixes, same `measure()` pattern recurring

Continuing to pick off small, independent errors (1-4 per file) from the
tail of the failing-target list rather than one big file:

- **`stattext.cpp`**: `gtk_label_set_line_wrap()` → `gtk_label_set_wrap()`
  is a plain rename with an identical signature, so it got a macro shim in
  `gtk3-compat.h` (`#define gtk_label_set_line_wrap(label, wrap)
  gtk_label_set_wrap(label, wrap)`) instead of touching either call site
  directly — needed an explicit `gtk3-compat.h` include, same as
  `dockart.cpp` in update 10.
- **`mdi.cpp`**: two independent breaks — `gtk_widget_get_preferred_height()`
  replaced with `gtk_widget_measure(widget, GTK_ORIENTATION_VERTICAL, -1,
  ...)` (same `measure()` unification as `win_gtk.cpp`/update 8);
  `gtk_box_reorder_child(box, child, 0)` replaced with
  `gtk_box_reorder_child_after(box, child, nullptr)` — GTK4 dropped the
  position-index API in favour of a sibling reference, and a `nullptr`
  sibling moves the child to the front, matching what position `0` did.
- **`activityindicator.cpp`**: `DoGetBestClientSize()` calls
  `GtkWidgetClass::get_preferred_width`/`get_preferred_height` directly
  through the vtable (bypassing `gtk_widget_get_preferred_size()`, which
  returns 0 for a hidden `GtkSpinner`) to get `GtkSpinner`'s real preferred
  size — both vfunc slots are gone under GTK4, unified into the same
  `measure()` vfunc already migrated for `wxPizza` in `win_gtk.cpp`, so
  this is now the third file using that exact pattern.

Investigated but did **not** fix, all found to be entangled in
already-deferred subsystems rather than independently fixable:
- `include/wx/gtk/private/gtk2-compat.h` (`gdk_device_get_window_at_position`,
  called from `window.cpp`'s raw `GdkEventMotion*` handler — part of the
  Phase 3 input-model rewrite, which the Phase 3 design doc deliberately
  stopped short of implementing this session).
- `include/wx/gtk/private/cairo.h` (`gdk_cairo_create(GdkWindow*)` — the
  paint-event-to-snapshot redesign, Phase 4, not started).
- `splash.cpp` (`gtk_window_set_type_hint`/`GDK_WINDOW_TYPE_HINT_SPLASHSCREEN`
  — the same WM-hints removal already flagged for `toplevel.cpp`).
- `dialog.cpp` and `utilsgtk.cpp`'s assert-dialog path (`gtk_grab_add`,
  `gtk_true`, `gdk_seat_ungrab`, `gtk_dialog_run` — all part of the
  already-deferred `gtk_dialog_run()`/modal-dialog redesign; fixing
  `gdk_seat_ungrab` alone wouldn't get either file compiling since
  `gtk_dialog_run()` a few lines later still blocks it).

Verified via standalone compiles against real GTK4 4.14.5 and GTK3 3.24.41
headers for each file, then a full whole-tree rebuild: **72 → 69** failing
build targets, zero regressions. Diagnostic count: 1481 → **1474**.

Session running total (diagnostic count): 1760 → 1730 → 1641 → 1629 →
1598 → 1577 → 1569 → 1555 → 1529 → 1494 → 1481 → **1474**. Failing build
targets (tracked from update 10 on, the more accurate metric): 78 → 72 →
**69**.

## Progress update 12: `infobar.cpp`, `spinbutt.cpp`, `calctrl.cpp`

Three more small independent fixes:

- **`infobar.cpp`**: `gtk_info_bar_get_content_area()` +
  `gtk_container_add()` → `gtk_info_bar_add_child()`, GTK4's direct
  replacement for "add this widget to the info bar's content area".
- **`spinbutt.cpp`**: `GtkSpinButton` no longer subclasses `GtkEntry`
  under GTK4 (it now just implements the `GtkEditable` interface), so
  `gtk_entry_set_width_chars()`/`set_max_width_chars()` moved to
  `gtk_editable_set_width_chars()`/`set_max_width_chars()`. Also picked up
  the same `gtk_style_context_get_padding()` state-parameter fix already
  applied elsewhere (`win_gtk.cpp`, `control.cpp`).
- **`calctrl.cpp`**: `GtkCalendar` moved to a `GDateTime`-based API —
  `gtk_calendar_select_month()` is gone, `gtk_calendar_select_day()` now
  takes a full `GDateTime*` instead of a bare day-of-month int, and
  `gtk_calendar_get_date()` returns a `GDateTime*` instead of filling in
  separate out-params. Built via `g_date_time_new_local()`/
  `g_date_time_unref()` rather than the newer
  `gtk_calendar_set_year()`/`set_month()`/`set_day()` individual setters —
  those are `GDK_AVAILABLE_IN_4_14` (this environment happens to have
  4.14.5, but that's not true of all GTK4), while `GDateTime` and
  `select_day()`/`get_date()` are `GDK_AVAILABLE_IN_ALL`. GLib's
  `GDateTime` months are 1-based, unlike `wxDateTime::Month`/GTK3's
  0-based convention here, so both call sites convert.

Investigated but not fixed, both entangled in already-deferred subsystems:
`dirdlg.cpp` (`gtk_native_dialog_run()` — same async-only modal redesign
as `gtk_dialog_run()`; the other two errors in this file wouldn't get it
compiling on their own) and `dataobj.cpp` (`GdkAtom` — type is gone
entirely under GTK4, part of the same clipboard/DnD data-format redesign
already flagged for `clipbrd.cpp`).

Verified via standalone compiles against real GTK4 4.14.5 and GTK3 3.24.41
headers, then a full whole-tree rebuild: **69 → 66** failing build
targets, zero regressions. Diagnostic count: 1474 → **1465**.

Session running total (diagnostic count): 1760 → 1730 → 1641 → 1629 →
1598 → 1577 → 1569 → 1555 → 1529 → 1494 → 1481 → 1474 → **1465**. Failing
build targets: 78 → 72 → 69 → **66**.

## Progress update 13: `tglbtn.cpp`, `bmpcbox.cpp`/`combobox.cpp`'s `GtkComboBox` entry access

More instances of the by-now-familiar `gtk_bin_get_child(GTK_BIN(...))`
pattern, fixed per-widget-type same as `button.cpp` earlier:

- **`tglbtn.cpp`**: `GtkToggleButton` subclasses `GtkButton`, so
  `gtk_button_get_child()` applies directly, same as `button.cpp`. A third
  call site (inside the removed-`GtkAlignment` codepath) was already
  correctly guarded under `#ifndef __WXGTK4__`, left untouched.
- **`bmpcbox.cpp`/`combobox.cpp`**: both create an entry-mode
  `GtkComboBox`; `gtk_combo_box_get_child(GTK_COMBO_BOX(combo))` is the
  matching per-type replacement, returning the embedded entry widget
  directly. `combobox.cpp` also needed two more fixes for that entry,
  same pattern as `spinbutt.cpp` (update 12) — `gtk_entry_set_width_chars()`/
  `gtk_entry_set_text()` moved to the `GtkEditable` interface
  (`gtk_editable_set_width_chars()`/`set_text()`).

`bmpcbox.cpp` has two more errors left **unfixed**:
`gdk_cairo_surface_create_from_pixbuf()` (gone under GTK4, no direct
replacement) and `gtk_widget_get_window()`, used together to build a
`cairo_surface_t` for a `CAIRO_GOBJECT_TYPE_SURFACE` tree-model column
rendered by `GtkCellRendererPixbuf` (the combo's dropdown item icons).
Whether that cell renderer's underlying "surface" property still exists
under GTK4, or needs a `GdkTexture`/`GIcon` instead, isn't something to
guess at without a way to verify rendering behavior — same caution
category as the already-deferred `cairo.h`/Phase 4 draw redesign, so left
as-is rather than risk a plausible-looking but wrong fix.

Verified via standalone compiles against real GTK4 4.14.5 and GTK3 3.24.41
headers, then a full whole-tree rebuild: **66 → 64** failing build
targets (`combobox.o`, `tglbtn.o` fixed; `bmpcbox.o` still fails on its 2
known-deferred errors, as expected), zero regressions. Diagnostic count:
1465 → **1455**.

Session running total (diagnostic count): 1760 → 1730 → 1641 → 1629 →
1598 → 1577 → 1569 → 1555 → 1529 → 1494 → 1481 → 1474 → 1465 → **1455**.
Failing build targets: 78 → 72 → 69 → 66 → **64**.

## Progress update 14: `gauge.cpp`, `fontpicker.cpp`

- **`gauge.cpp`**: `gtk_widget_get_preferred_width()`/`get_preferred_height()`
  → the `measure()` vfunc, same recurring pattern. `GTK_STATE_ACTIVE` (a
  `GtkStateType` value, entirely removed under GTK4) was being passed as
  the `state` argument to `GetDefaultAttributesFromGTKWidget()` — that
  parameter is already a no-op under GTK4 (see `control.cpp`, update 10:
  GTK4 has no per-state background query at all), so any value compiles,
  but substituted `GTK_STATE_FLAG_ACTIVE` (the surviving `GtkStateFlags`
  equivalent) for a defensible name over a magic constant.
- **`fontpicker.cpp`**: `gtk_font_button_get_font_name()`/`set_font_name()`
  moved to the `GtkFontChooser` interface (which `GtkFontButton`
  implements) as `gtk_font_chooser_get_font()`/`set_font()` — the getter
  returns a newly-allocated string, wrapped in the existing
  `wxGlibPtr<gchar>` RAII idiom already used elsewhere in this codebase.
  `gtk_font_button_set_show_style()`/`set_show_size()` (whether the
  button's own label includes style/size text) have **no discoverable
  GTK4 replacement** — `use_font`/`use_size` (unaffected, still exist)
  control a different thing, whether the label renders *using* the
  selected font/size rather than whether style/size text is appended to
  it. Left as a documented fidelity gap under `__WXGTK4__`, not guessed
  at; not yet runtime-verified.

Verified via standalone compiles against real GTK4 4.14.5 and GTK3 3.24.41
headers, then a full whole-tree rebuild: **64 → 62** failing build
targets, zero regressions. Diagnostic count: 1455 → **1447**.

Session running total (diagnostic count): 1760 → 1730 → 1641 → 1629 →
1598 → 1577 → 1569 → 1555 → 1529 → 1494 → 1481 → 1474 → 1465 → 1455 →
**1447**. Failing build targets: 78 → 72 → 69 → 66 → 64 → **62**.

## Progress update 15: the `GTKGetWindow()` family removed, plus display/icon-theme ports

First session in a fresh container. Worth recording for whoever picks
this up next: **the build tree does not survive**, only the git tree
does. Re-running the `configure` line at the top of this file and
`make -k -j4` reproduced update 14's numbers exactly (1447 errors, 0
fatal, 62 failing targets), which is a useful confirmation that the
recorded state is real and nothing was lost between sessions.

Also set up a **second, GTK3 configure tree** (`wxbuild-gtk3`,
`--with-gtk=3`, otherwise identical flags) purely so every file touched
can be syntax-checked against *both* real GTK4 4.14.5 and real GTK3
3.24.41 headers before being called done. Previous updates describe doing
this; having a standing GTK3 tree makes it one command instead of a
setup step, and it caught nothing this session, which is the point —
it's the check that says the `#ifdef` split is right.

And **switched the GTK4 build out of tree** as described above. Two
parallel build trees next to the source tree (`wxbuild-gtk4`,
`wxbuild-gtk3`) is the arrangement this port wants anyway: neither one
touches the source tree, so `git status` shows only real changes, and
the two toolkits' objects can't collide.

### The structural piece: `GTKGetWindow()` is obsolete, not portable

`gtk_widget_get_window` has been the headline blocker since the very
first table in this file, and the ~18 per-widget `GTKGetWindow()`
overrides were the largest single block of it. Both the Phase 2 and
Phase 3 design docs deferred them as "not safe to stub blind".

Tracing what the return value is actually *used* for settles it, and the
answer is that the whole mechanism has no purpose left under GTK4:

- **`GTKIsOwnWindow()`**, one of its two consumers, has **no callers
  anywhere in the tree.** It's dead code.
- **`GTKSetCursorForAllWindows()`**, the other, exists solely to push a
  `GdkCursor` onto each of those windows one at a time. Under GTK4
  `gtk_widget_set_cursor()` sets the cursor for a widget *and all its
  children by inheritance* in a single call — which is exactly the
  simplification already made and accepted for `cursor.cpp`'s
  `SetGlobalCursor()` back in update 7.

So the family is compiled out under GTK4 rather than given an invented
meaning in a toolkit where widgets have no windows, and
`GTKSetCursor()`/`GTKUpdateCursor()` get direct `gtk_widget_set_cursor()`
implementations. `GTKUpdateCursor()`'s trailing "prod the native widget
into restoring its own cursor" loop goes too: it only existed because
GTK3 native widgets set cursors on their private `GdkWindow`s, and under
GTK4 they set them on themselves, where a cursor set on an *ancestor*
doesn't override them in the first place.

This is a behaviour-affecting decision, not a rename, and **cursor
handling is exactly the sort of thing that needs runtime verification
once `test_gui` links** — flagging it here in the same spirit as the
Tab-order Z-order and `wxGetMouseState` caveats. But it is a reasoned
removal with evidence, not a blind stub, and it unblocks a dozen files
that were otherwise capped.

### Two latent runtime bugs found, neither of which the compiler reports

Both are the same shape and worth watching for in the remaining files,
because **they compile cleanly and would only fail at runtime**: a
`GTK_FOO()` cast to a type the widget no longer derives from, where
`GtkFoo` itself still exists under GTK4 so nothing is undeclared.

- `spinctrl.cpp` cast a `GtkSpinButton` to `GtkEntry` for
  `gtk_entry_set_alignment()`. `GtkSpinButton` is not a `GtkEntry`
  subclass under GTK4 (it implements `GtkEditable` instead), but
  `GtkEntry` and that function both still exist, so it compiled.
- `checkbox.cpp` cast a `GtkCheckButton` to `GtkToggleButton` for the
  active/inconsistent accessors. Same situation: `GtkCheckButton` no
  longer derives from `GtkToggleButton` under GTK4, but `GtkToggleButton`
  is still there.

Grepping for `GTK_ENTRY(`, `GTK_TOGGLE_BUTTON(`, `GTK_BUTTON(` and
friends on widgets whose GTK4 class hierarchy changed is a cheap
sweep that would likely find more of these, and none of them will ever
show up in the error count.

### A methodology note on the error count itself

gcc reports an undeclared identifier **only once per function**. In
`spinctrl.cpp`'s `DoGetSizeFromTextSize()` this hid a second
`gtk_entry_set_width_chars()` call, on the line restoring the original
width, which the build log never mentioned. So "fix every error the log
lists for this file" is not the same as "fix this file" — after editing,
re-grep the function for the old symbol rather than trusting the
diagnostic list to be exhaustive.

### Shims added to `gtk3-compat.h`

Four API changes were each about to get their fourth or fifth
copy-pasted per-call-site fix, so they're now shimmed centrally
alongside the existing `gtk_box_pack_start()`/`gtk_widget_get_toplevel()`
ones:

- `gtk_widget_get_preferred_width()`/`get_preferred_height()`/
  `get_preferred_height_for_width()` → `gtk_widget_measure()`. This is
  the pattern that had already recurred in `win_gtk.cpp` (update 8),
  `mdi.cpp`/`activityindicator.cpp` (update 11) and `gauge.cpp`
  (update 14).
- `gtk_box_reorder_child()` → `gtk_box_reorder_child_after()`, walking
  the box's children to turn the index into a sibling reference (and
  skipping the child being moved while counting, as GTK3 did).
- The `GtkEntry` → `GtkEditable` text/width/alignment moves. This one is
  shimmed in the *opposite* direction — GTK4 spelling at the call sites,
  mapped back to `GtkEntry` for older GTK — because `GtkSpinButton`
  genuinely isn't a `GtkEntry` under GTK4, so the call sites have to use
  the `GtkEditable` names.
- `wx_gtk_widget_remove_from_parent()` for `gtk_container_remove()`.
  Deliberately **not** a blanket `gtk_container_remove` shim: for the
  simple multi-child containers (`GtkBox`, `wxPizza`) it is exactly
  `gtk_widget_unparent()`, but for single-child containers like
  `GtkScrolledWindow` and `GtkFrame`, which keep their own pointer to
  the child, unparenting directly would leave that pointer dangling.
  The helper is documented as unusable for those.

### Files ported

- **`display.cpp`** — the `GListModel` monitor rewrite scoped but not
  started in update 7. Beyond the mechanical part, three APIs have no
  GTK4 equivalent: `gdk_display_get_monitor_at_point()` (replaced by
  scanning the monitor list, which also subsumes the containment check
  the old code needed because that function returned the merely
  *nearest* monitor); `gdk_monitor_is_primary()` and the whole primary
  monitor concept (dropped the `IsPrimary()` override, the base class
  already treats monitor 0 as primary — which is also the new fallback
  in `GetFromWindow()`); and `gdk_monitor_get_workarea()`. That last one
  is a **real fidelity gap**: it's gone because the work area can't be
  determined under Wayland, but it survives in the X11 backend as
  `gdk_x11_monitor_get_workarea()`, so it's used when running under X11
  and falls back to full geometry otherwise — meaning `GetClientArea()`
  wrongly includes panels and docks under Wayland. Also implemented
  `GetRawPPI()`, which the never-compiled GTK4 branch simply lacked.
- **`artgtk.cpp`/`mimetype.cpp`** — the GTK4 icon theme API. Lookups
  return a `GtkIconPaintable`, meant to be *drawn* rather than turned
  into pixels, so both files go via the file the icon was loaded from
  (which also handles resource-backed icons for free: there's no path
  and `g_file_get_path()` returns null, exactly what the callers want).
  Note that the GTK4 lookup **never fails**, falling back to the
  "missing image" icon, so `gtk_icon_theme_has_icon()` has to be checked
  first to preserve the "not found" result. GTK4 also removed the named
  icon sizes and `gtk_icon_size_lookup()`; since `artgtk.cpp` uses named
  sizes throughout as a way of *naming pixel sizes*, a `wxGtkIconSize`
  abstraction (real `GtkIconSize` for GTK3, plain pixels for GTK4, using
  GTK3's default values) keeps all the closest-size and art-client
  mapping logic unchanged. **Known gap**: `CreateIconBundle()` used
  `gtk_icon_theme_get_icon_sizes()` to ask which sizes an icon really
  exists in, and GTK4 has nothing equivalent, so it now requests a fixed
  set of standard sizes and lets the lookup scale.
- **`spinctrl.cpp`**, **`frame.cpp`**, **`collpane.cpp`** — the new
  shims plus, for collpane, `gtk_expander_set_child()` and dropping the
  `gtk_expander_get_spacing()` term (GTK4 has no spacing property; the
  gap is CSS and can't be queried).
- **`checkbox.cpp`** — `GtkCheckButton`'s own API, plus creating the
  label widget explicitly, since the one made by
  `gtk_check_button_new_with_label()` is an unreachable internal detail
  under GTK4 and wx needs a real `GtkLabel` to style and hide. Uses
  `gtk_check_button_set_child()`, which unlike everything else used here
  is `GDK_AVAILABLE_IN_4_8` rather than `_IN_ALL` — noted because
  update 12 deliberately preferred `_IN_ALL` for `calctrl.cpp`; there
  is no `_IN_ALL` way to own the label, so this is a considered
  exception rather than an oversight.
- **`aboutdlg.cpp`** — `gtk_about_dialog_set_logo()` takes a
  `GdkPaintable` now, so the pixbuf gets wrapped in a `GdkTexture`. The
  HiDPI hand-drawing workaround is skipped under GTK4: it's built on
  `gtk_container_forall()` and the `"draw"` signal, and a texture
  carries its own scale anyway.
- **`fontdlg.cpp`** — all three `GtkFontSelectionDialog` uses are the
  fallback taken when `gtk_check_version(3,2,0)` fails, unreachable
  under GTK4, so simply compiled out.
- **`statbox.cpp`** — `gtk_frame_set_label_align()` lost its `yalign`
  parameter and the style getters lost their state parameter. Clears all
  of the file's *own* errors, but it still fails via `stylecontext.h`
  (see below).
- **`addremovectrl.h`** — its toolbar appearance tweak is skipped under
  GTK4: it styles the `GtkToolbar` behind `wxToolBar`, which has no GTK4
  backend yet, and uses style context junction sides, gone with the rest
  of the pre-CSS styling API.

### Also ported

- **`private.cpp`** — `GetRadioButtonWidget()`'s scratch widget, used only
  for theme queries, becomes a plain `GtkCheckButton` (GTK4 has no
  `GtkRadioButton`; a radio button is a grouped check button there).
- **`splash.cpp`** — window type hints are gone, see the Wayland section
  below.
- **`utilsx11.cpp`** — `GDK_MOD1_MASK` → `GDK_ALT_MASK`;
  `wxQueryWMspecSupport()` switched to the plain X11 implementation
  already present in the file (see the Wayland section);
  `gtk_show_uri_on_window()`, which the never-compiled GTK4 branch here
  invented, replaced with GTK4's fire-and-forget `gtk_show_uri()`. That
  last one **loses failure detection**: it reports errors by showing its
  own dialog rather than to the caller, so the xdg-open fallback can no
  longer run on failure without risking opening the URL twice on success.
- **`statusbr.cpp`** — the size grip is **not shown** under GTK4, a
  deliberate deferral rather than a fix; see the commit and the deferred
  list.

### On Wayland, and why some fixes are still X11-specific

Worth stating explicitly, since "port to GTK4" and "be ready for
Wayland" are related but not the same goal, and a couple of changes in
this update look like they point the wrong way.

**They don't, in the one case where it matters most.** `utilsx11.cpp` is
an X11 backend file by construction — `Display*`, `XSync()`,
`XInternAtom()`, `wxGetFullScreenMethodX11()` — and its
`wxQueryWMspecSupport()` asks whether the window manager advertises an
EWMH hint in `_NET_SUPPORTED`, a protocol which simply has no Wayland
counterpart. Both the old and new implementations are X11-only: the GDK
helper that was being used, `gdk_x11_screen_supports_net_wm_hint()`,
lives in `gdk/x11/` too. Nothing became *more* X11-bound; a GDK X11 call
whose signature changed incompatibly was swapped for a raw Xlib one. Its
only caller is additionally guarded by a runtime `wxGTKImpl::IsX11()`
check (and is currently `!defined(__WXGTK4__)` anyway), so under Wayland
it is never reached.

**`display.cpp`'s `GetClientArea()` is the case where the concern is
real**, and the tradeoff was made deliberately. GTK4 removed
`gdk_monitor_get_workarea()` *because* the work area can't be known
under Wayland — the compositor doesn't tell clients where panels and
docks are, by design. It survives only as the X11-specific
`gdk_x11_monitor_get_workarea()`. The options were to lose the work area
on every backend, or keep it where it's still knowable and degrade where
it isn't. This takes the latter: correct under X11, full monitor
geometry under Wayland. That is a **Wayland fidelity gap, not a solved
problem** — `wxDisplay::GetClientArea()` will overlap panels there.

More generally, GTK4 is the right target for being future-proof, but it
does not make these gaps go away; it mostly makes them *visible*, by
removing the X11-shaped APIs that used to paper over them. The running
list of things that are strictly worse under Wayland than under X11:

| Gap | Where | Status |
|---|---|---|
| No global pointer position, only position relative to the surface under it | `wxGetMousePosition()`/`wxGetMouseState()`, `utilsgtk.cpp` | Accepted, documented (earlier update) |
| No work area, so client area includes panels/docks | `wxDisplay::GetClientArea()`, `display.cpp` | X11 keeps real behaviour, Wayland degrades (this update) |
| No window type hints, so a splash screen isn't marked as one for the WM | `splash.cpp`, and `toplevel.cpp`'s deferred WM hints | Accepted, documented (this update) |
| No shaped windows at all | `wxNonOwnedWindow::SetShape`, `nonownedwnd.cpp` | Newly scoped, needs a decision (this update) |
| WM decoration/function hints work through CSD instead | `toplevel.cpp`'s `GTKHandleRealized()` | Deferred whole-subsystem redesign |
| Input synthesis has no equivalent | `uiactionx11.cpp` | Deferred; X11-only by nature, excluded from the build today |

None of these are reasons to prefer GTK3 — they're the same gaps GTK3
apps hit the moment they run on Wayland, just surfaced at compile time
instead of silently misbehaving. But a wxGTK4 that is *only* correct
under XWayland would miss the point of the exercise, so it's worth
keeping this table honest as the port continues.

### Numbers

Every batch verified by a full `make -k -j4` and a failing-**target**
diff (the update 10 methodology), with zero regressions at each step:
1447 → 1410 → 1386 → 1350 → 1329 → 1322 → **1312** diagnostics; failing
build targets 62 → 59 → 58 → 54 → 52 → 49 → **48**. Fourteen targets
fully cleared: `display.o`, `frame.o`, `spinctrl.o`, `collpane.o`,
`addremovectrl.o`, `artgtk.o`, `aboutdlg.o`, `mimetype.o`, `checkbox.o`,
`fontdlg.o`, `private.o`, `splash.o`, `utilsx11.o`, `statusbr.o`.

### What the next session should probably pick up

`wxGtkStyleContext`/`GtkWidgetPath` (`stylecontext.h`), already scoped in
update 10, is now clearly **the highest-leverage remaining item**: it is
the *sole* remaining blocker for `statbox.cpp` after this session's fix,
and it also blocks `notebook.cpp`, `renderer.cpp` and parts of
`settings.cpp`. Nothing else remaining unblocks four files at once.

Everything else near the top of the list is entangled in an
already-deferred subsystem: `msgdlg.cpp` and `dirdlg.cpp` need the
`gtk_dialog_run()` async-modal redesign, `scrolbar.cpp`/`slider.cpp`/
`srchctrl.cpp` need Phase 3's event controllers, `image_gtk.cpp` and
`cairo.h` need Phase 4's snapshot migration, `textentry.cpp` is three
easy fixes plus one Phase 3 one (`gtk_entry_im_context_filter_keypress`),
and `hyperlink.cpp` needs the `colour.cpp` `GdkColor` work scoped below.

Two genuinely new deferred items found this session:

- **`nonownedwnd.cpp`'s `gdk_window_shape_combine_region()`** (3 call
  sites). GTK4 removed shaped windows outright — there is no
  replacement, as compositors handle transparency instead.
  `wxNonOwnedWindow::SetShape` is public API, so this needs a real
  decision (report failure? approximate with an alpha-masked
  CSS/snapshot render?) rather than a mechanical fix.
- **The status bar size grip** (`statusbr.cpp`), disabled rather than
  ported this session. `gtk_window_begin_resize_drag()`/`begin_move_drag()`
  became `gdk_toplevel_begin_resize()`/`begin_move()`, which need the
  pointer position in *surface* coordinates plus the `GdkDevice`, so this
  is blocked on the same Phase 2 coordinate-translation and Phase 3
  input-device questions as everything else in those buckets. Cheap to
  revisit once either lands.

## Progress update 16: decision to drive for a link, and Phase 3 started

### The deadlock, and the decision

Worth stating plainly, because it now governs the whole port:

- Runtime verification needs `test_gui` to **link**.
- Linking needs **all** remaining targets to compile.
- The remaining targets are exactly the behaviour-heavy subsystems
  (input, painting, menus, toolbar, clipboard) that most **need**
  runtime verification.

The Phase 3 design doc explicitly stopped short of implementing for that
reason, which was right at the time but is self-perpetuating: meanwhile
the pile of never-executed behaviour changes keeps growing — cursor
handling, tab-order Z-order, the `wxWindowGTK` destructor, icon sizing,
the checkbox label, and everything in update 15.

**Decision: drive for a link.** Implement the remaining subsystems
GTK4-idiomatically but accept that they're unverified, flagging each,
so that `test_gui` links and the real Catch2 GUI suite can run under
`xvfb-run`. Getting a test harness executing is now worth more than
getting any individual subsystem provably right, because it's what
retroactively validates everything already landed.

### Done: `event.h` (the single most-included blocker)

`wx/gtk/private/event.h` is included by **75 translation units** and was
contributing 50 errors to each one that got far enough to see it, so it
came first. Now 0, with GTK3 unaffected.

`GdkEventButton`/`GdkEventMotion`/`GdkEventCrossing` don't exist under
GTK4 — there's one opaque `GdkEvent` plus accessors, all of which are
`GDK_AVAILABLE_IN_ALL` (`gdk_event_get_modifier_state()`,
`gdk_event_get_time()`, `gdk_event_get_position()`). So `InitMouseEvent()`
stops being a template over event-struct types and becomes a plain
function.

**The coordinate handling gets genuinely better, not just different.**
GTK+ 3 event structs carried coordinates relative to whichever
`GdkWindow` the event landed on, which is why the old code needs a
correction for no-window widgets that own a `GdkWindow` covering part of
their area. GTK4 events report a position relative to the *surface*, but
the event controllers that deliver them hand out coordinates **already
relative to the widget they're attached to** — so those are passed in
rather than dug out of the event. That's more accurate and sidesteps the
surface-to-widget translation entirely, which is the concrete form of
the Phase 2 doc's claim that the input model is *easier* under GTK4.
The no-window correction is dropped: it can't arise when no widget has a
window.

### Next, fully scoped: `window.cpp`'s mouse pipeline

This is the immediate next unit and the analysis is done, so it can be
executed directly. `wxWindowGTK::GTKConnectWidget()` (`window.cpp:4380`
onwards) connects the GTK+ 3 signals; under GTK4 each becomes a
controller added with `gtk_widget_add_controller()`:

| GTK+ 3 signal | GTK4 controller and signal |
|---|---|
| `button_press_event` / `button_release_event` | `GtkGestureClick`, `pressed`/`released`; call `gtk_gesture_single_set_button(..., 0)` to get all buttons |
| `motion_notify_event` | `GtkEventControllerMotion`, `motion` |
| `enter_notify_event` / `leave_notify_event` | `GtkEventControllerMotion`, `enter`/`leave` |
| `scroll_event` | `GtkEventControllerScroll` (`GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES`), `scroll` |
| `key_press_event` / `key_release_event` | `GtkEventControllerKey`, `key-pressed`/`key-released` |
| `popup_menu` | removed outright; needs a separate decision |

Three pieces of GTK+ 3 machinery **should be deleted rather than
ported**, which is why the GTK4 versions come out shorter:

1. **The double-click filter.** `WindowButtonPressCallback()` peeks the
   event queue (`gdk_event_peek()`) to suppress the surplus button-down
   GDK sends before a double click, and GTK2 additionally pokes
   `display->button_click_time[]` to defeat triple clicks.
   `GtkGestureClick` reports `n_press` directly (1/2/3), so all of this
   goes away and the down/dclick mapping reads off `n_press`.
2. **`EventAlreadyProcessed()`.** It memcmp's the event against the last
   one seen to deduplicate the same event being delivered to several
   widgets in the hierarchy. GTK4's controllers have explicit
   capture/bubble propagation phases, which is the supported way to
   express this, so the hack shouldn't be carried over.
3. **The `GDK_BUTTON1_MASK` touchscreen fixup**, which mutates
   `gdk_event->state` in place — impossible on an opaque event, and
   unnecessary since the state is read fresh via the accessor.

`SetLastMouseEvent` needs only a `GdkEvent*` constructor under GTK4
(the two struct-typed ones collapse into one). The three synthesized
call sites around `window.cpp:3947-3975` pass their own coordinates and
map over directly.

Everything wx-level must be preserved as-is: `AdjustEventButtonState()`,
`FindWindowForMouseEvent()` re-targeting and the event object/id reset
after it, the focus-on-left-down rule, and right-down generating
`WXSendContextMenuEvent()` with screen coordinates.

After the mouse pipeline, `window.cpp`'s remaining blockers are the key
events (`GdkEventKey`, 13 errors, same controller treatment via
`GtkEventControllerKey`), the paint path (Phase 4, `draw` → `snapshot`),
and a small tail of signature changes
(`gtk_css_provider_load_from_data()` lost its length argument,
`gtk_widget_size_allocate()` gained `baseline`).

## Newly-scoped deferred item: `colour.cpp`'s `GdkColor`/`GetColor()` removal

Investigated while looking for the next small fix; turned out much bigger
than its 4-error count in `colour.cpp` itself suggests. `wxColourRefData`
keeps a full `GdkColor` (`m_gdkColor`) alongside `GdkRGBA` even under
GTK3, and the *actual* implementation of `Red()`/`Green()`/`Blue()`/
`operator==` all read from `m_gdkColor`, not `m_gdkRGBA` — so this isn't
just an unused legacy field, it's load-bearing for core colour-component
access. `GdkColor` the type doesn't exist under GTK4 at all.

Worse, `wxColourImpl::GetColor()` (returns `const GdkColor*`) is called
from roughly a dozen other files: `dcclient.cpp` (`gdk_gc_set_foreground`/
`background`, old `GdkGC`-based drawing — likely already GTK2-only dead
code under GTK3+, but needs checking per call site, not assuming),
`textctrl.cpp`, `window.cpp`, `dataview.cpp`, `listbox.cpp`, `cursor.cpp`,
`clrpicker.cpp`, `colordlg.cpp`. Each call site needs individually
classifying as either dead-under-GTK3+ (safe to leave broken/unreachable)
or needing a real `GdkRGBA`-based rewrite — not something to batch-fix
blindly, since `Red()`/`Green()`/`Blue()` correctness (rounding behavior
converting `GdkRGBA`'s 0.0-1.0 floats to bytes) matters everywhere colour
values get compared or cached. A proper fix would restructure
`wxColourRefData`/`wxColourImpl` with a GTK4-only branch backed solely by
`GdkRGBA`, then audit and fix each `GetColor()` caller — a bounded but
real subsystem task, not attempted here. Candidate for a future batch
alongside the `wxGtkStyleContext` rewrite (update 10).

## Progress update 15: `wxGtkStyleContext` rebuilt on real widgets

The deferred item scoped in update 10, now done. Full write-up in
`docs/gtk/gtk4-stylecontext-design.md`; the short version is that GTK4
removed `GtkWidgetPath`, `gtk_style_context_new()`, `set_path()`,
`set_parent()` and the `gtk_style_context_get()` varargs query, so the
class was rebuilt to query theme values from a real (never shown, never
realized) widget hierarchy, descending to interior CSS nodes by walking
actual children.

Findings were established by **running probe programs against real GTK4**
rather than reading headers — GTK4 4.14.5 and `xvfb` are both available
here. The probes are committed under `docs/gtk/probes/` so they can be
re-checked against a different GTK4 rather than taken on trust. Three
mattered:

- Interior nodes (`header`/`tabs`/`tab`, `trough`/`slider`, `check`) are
  reachable as real child widgets, so a synthetic path segment maps
  directly onto a child descent.
- Ancestry genuinely affects resolution — a label reads white standalone
  but dark inside a button — so the hierarchy has to be really parented.
  This ruled out the obvious shortcut of creating each node standalone,
  which would have compiled and silently produced wrong colours.
- Widgets attached with `gtk_widget_set_parent()` are **not** freed with
  their parent, so the destructor unparents explicitly, deepest first.

**Impact: 62 → 46 failing build targets, zero regressions.** Sixteen files
fixed at once, well beyond the six predicted when this was scoped: the
class declaration mentions `GtkWidgetPath` directly, so `stylecontext.h`
was failing to compile and poisoning every translation unit that
transitively included it, not just the files that call the class.
Diagnostic count 1447 → **1227**.

Fixed: `statbox.o`, `generic_infobar.o`, `artgtk.o`, `collpane.o`,
`generic_statusbr.o`, `aboutdlg.o`, `checkbox.o`, `display.o`, `fontdlg.o`,
`frame.o`, `spinctrl.o`, `mimetype.o`, `private.o`, `splash.o`,
`utilsx11.o`, `addremovectrl.o`.

Two queries degrade and one improves, all documented in the design doc:
`Bg()`/`Border()` lose their exact property queries and now approximate via
`gtk_style_context_lookup_color()` (routed through one shared helper,
`wxGTKLookupThemeColour()`, also used by `control.cpp` so the gap has a
single implementation); `GetScrollbarWidth()` gains accuracy by using
`gtk_widget_measure()` instead of summing `min-width` node by node.
Foreground colours are unaffected and remain exact.

`AddMenu()`/`AddMenuItem()` still reference types GTK4 has in no form and
fall back to `GtkPopover` + a `GtkButton` named `modelbutton` until
`menu.cpp` is ported.

## Regression tests for GTK's own behaviour

The port rests on assumptions about GTK4 itself — widget tree shape, style
resolution rules, widget ownership — that a toolkit upgrade can change
silently. The failure mode is not a crash but a wrong number: if
`GtkNotebook`'s interior nodes are renamed, `wxGtkStyleContext` keeps
returning metrics, just the wrong ones.

`build/tools/gtk4-invariants.c` pins these down as assertions and runs in
CI on the GTK4 job (new "Checking GTK4 platform invariants" step). It needs
only GTK, not libwx, so it runs regardless of how far the port currently
builds — which matters, because `test_gui` still cannot link and normal GUI
tests remain unavailable. Once `test_gui` links this should move into the
regular suite.

It asserts **structure, not pixel values**: exact metrics and colours are
theme-dependent, so asserting them would fail whenever CI's theme differs
rather than when something is genuinely wrong. Checks that are inherently
theme-dependent (whether a theme defines `theme_bg_color` and friends)
report but do not fail. 12 checks currently pass against GTK 4.14.5.

Notably it asserts the *absence* of things too — `GtkFrame` having no
`border` child, an empty `GtkNotebook` having no `tab` node — because the
implementation deliberately relies on both. Each check was verified to
actually fail when the condition it guards is violated (renamed node,
gained node, leaked widget), so these are not tests that can only pass.

## Progress update 16: the Phase 3 input model, implemented

`docs/gtk/gtk4-phase3-input-model-design.md` stopped at design on purpose.
This is its execution, in the order it recommended — keyboard, scroll,
motion/enter-leave, buttons — each verified and committed separately.

**`window.cpp`: 206 → 110 errors**, GTK3 verified unaffected after every
step. Whole-tree diagnostics 1227 → **1133** across the four batches, no
regressions in any of them. The failing-target count stays at 46 throughout,
because `window.cpp` doesn't drop off that list until *all* of its errors are
gone; the remaining 110 are `GdkWindow` (Phase 2) and paint-model work, not
input.

### The shape of the port

GTK4 replaced per-widget signals carrying concrete `GdkEventFoo*` structs
with controller objects whose signals hand over the values directly. The
translation logic in wx is large and entirely value-based, so rather than
duplicate it per backend, each path collects what it needs into a small value
type or passes primitives, leaving the shared logic untouched:

- **Keyboard** (`GtkEventControllerKey`): `wxGTKKeyEventData`, fields named to
  match `GdkEventKey`'s, so ~250 lines of keysym translation needed only a
  mechanical rename. `m_imKeyEvent`/`GTKIMFilterKeypress()` use a
  `wxGTKNativeKeyEvent` typedef (`GdkEventKey` / opaque `GdkEvent`) rather
  than being duplicated.
- **Scroll** (`GtkEventControllerScroll`): GTK4 has no scroll-direction
  concept at all; everything is deltas, discrete clicks included. GTK3's
  smooth-scroll branch became the only path needed, factored into
  `wxGTKProcessScrollDeltas()` and shared.
- **Motion + enter/leave** (`GtkEventControllerMotion`, one controller, three
  signals).
- **Buttons** (`GtkGestureClick`).

### Resolved: the `EventAlreadyProcessed()` question

Design doc §2 left open whether this needed a GTK4 equivalent. It does not,
and the reason is structural rather than incidental: it guarded against GTK3
propagating one native event up the widget hierarchy so several wxWindows saw
it, and a controller only ever fires for the widget it is attached to. It is
deliberately absent from every GTK4 input path.

### Measured rather than assumed

Two findings came from running code against real GTK4, not from reading docs,
and both changed the implementation:

1. **`gtk_widget_set_parent()` vs. type-specific setters** — irrelevant here
   but see update 15.
2. **Gesture claim/deny** — the risk the design doc named as riskiest. Real
   clicks injected with XTest under Xvfb
   (`docs/gtk/probes/gtk4-gesture-semantics.c`) showed that on a widget with
   its own gesture, *not* claiming delivers the press but **never the
   release**, because the native gesture claims the sequence and cancels
   ours. GTK3 delivered both unconditionally, so any wx code pairing press
   with release would have broken silently. The port claims exactly when wx
   handles the press, reproducing GTK3's TRUE/FALSE semantics; `CAPTURE`
   phase was rejected despite fixing the release because it leaves wx unable
   to stop a native control acting at all.

### Behavioural differences accepted, with reasons

None of these are translations that were "close enough" — each is a place
where GTK4 genuinely cannot do what GTK3 did:

- `key-released` returns `void` (GTK3's `key_release_event` returned
  `gboolean`), so a wx handler can no longer suppress GTK's own processing of
  a key *release*. Presses are unaffected.
- An unhandled press on a *native control* yields no release event (above).
  Ordinary wx windows are unaffected, nothing competing for the sequence.
- `GtkEventControllerMotion::leave` carries no coordinates, unlike
  `GdkEventCrossing`; they are recovered from the controller's current event,
  falling back to the origin.
- The `GDK_CROSSING_NORMAL` filter is gone — a motion controller is not sent
  grab-synthesised crossings unless created with `SCOPE_CAPTURE`.
- `gdk_event_request_motions()` is gone — GTK4 has no motion hints and
  compresses motion events internally.

**None of this is runtime-verified beyond the gesture probe.** `test_gui`
still cannot link, so the event *plumbing* is confirmed to compile and the
gesture *semantics* are confirmed by injection, but nothing has exercised
wx's own handlers end to end.

## Progress update 17: `window.cpp`'s non-input remainder

With the input model done (update 16), the rest of `window.cpp` turned out to
be a long tail of unrelated removals rather than another subsystem. **206 →
51 errors** overall for this file; whole-tree 1133 → 1084 at the last full
rebuild, no regressions, GTK3 verified clean after every batch.

Handled, grouped by what GTK4 replaced them with:

**A different call**
- `gtk_im_context_set_client_window()` → `set_client_widget()`.
- `GDK_MOD1_MASK` → `GDK_ALT_MASK`.
- `gtk_widget_size_allocate()` gained a baseline parameter;
  `gtk_scrolled_window_new()` lost its adjustments;
  `gtk_css_provider_load_from_data()` lost its `GError**`.
- `gtk_scrolled_window_set_shadow_type()` → `set_has_frame()`.

**A constant** — these are the interesting ones, because the replacement is
"the answer is now always X", which changes behaviour rather than spelling:
- `gtk_widget_get_has_window()` is always false: no widget owns a window under
  GTK4. Made a shim rather than three `#ifdef`s, since call sites use it to
  decide whether coordinates need translating through a child window, which
  there they never do.
- `gtk_widget_get/set_double_buffered()`: always on, no toggle, so
  `wxWindow::SetDoubleBuffered()` is a no-op under GTK4.
- `gtk_widget_set_redraw_on_allocate()`: gone, GTK4 always redraws on resize,
  so `wxFULL_REPAINT_ON_RESIZE` can't be turned off.

**Nothing at all**
- **Pointer grabs.** `gdk_seat_grab()`, `gdk_pointer_grab()` and
  `gtk_grab_add()` are all gone, deliberately: GTK4 grabs implicitly, a
  gesture that claims a pointer sequence keeping it until the sequence ends.
  `DoCaptureMouse()`/`DoReleaseMouse()` therefore keep only their bookkeeping.
  This covers the dominant use — tracking a drag between button-down and
  button-up — but **not** capturing outside a pointer sequence (from a hover
  or a timer), which has no GTK4 equivalent at all.
- **`GDK_MOD5_MASK`.** GTK4 trimmed the modifier enum to named modifiers, so
  the convention of reporting AltGr as Ctrl+Alt cannot be detected. Left as a
  gap rather than substituted, since a plausible-looking guess here would
  misbehave on European keyboard layouts specifically.
- **`GtkShadowType`'s in/out distinction**, per above.
- **`DoPopupMenu()`.** `GtkMenu`, the `gtk_menu_popup*()` family,
  `GtkMenuPositionFunc` and `gtk_main_iteration()` are all gone, and the modal
  spin-the-loop idiom has no popover equivalent. Needs the `menu.cpp` rewrite
  it is already deferred behind, so the GTK4 path reports failure — checkable
  behaviour rather than a silent no-op.

### A guard bug worth recording

The earlier touch-event batch left an `#ifndef __WXGTK4__` that opened before
an anonymous namespace and closed before its brace, so under GTK4 the
namespace was never opened while its closing brace remained. It surfaced only
as a stray-brace error whose message pointed at the brace rather than the
guard. Fixed at the guard. Worth noting because conditional compilation that
spans a scope boundary fails in a way the compiler describes unhelpfully, and
this file now has a lot of such guards.

### What's left in `window.cpp`

The remaining ~51 errors are no longer a tail: ~30 are `GdkWindow`-based
geometry, invalidation and clipping (`gdk_window_get_origin/get_width`,
`gdk_window_invalidate_rect`, `gdk_window_get_clip_region`,
`gtk_cairo_should_draw_window`), which belong to the **Phase 4 draw →
snapshot redesign** that has not been started and has no design document yet.
The rest are assorted single items (`GtkBindingSet` → `GtkShortcut`,
`gdk_device_warp`, `gdk_window_raise/lower`, `gtk_widget_style_get`).

`window.cpp` will therefore not compile — and `window.o` will not leave the
failing-target list — until Phase 4 is designed and implemented. That is now
the single largest blocker in the port.

## Progress update 18: Phase 4 started -- wxPizza paints

The paint model is no longer just designed. `wxPizza` renders through a
snapshot vfunc, and the design's central bet held: `gtk_snapshot_append_cairo()`
yields a cairo context in widget-relative coordinates, so `GTKSendPaintEvents()`
and everything downstream needed **no** coordinate changes. This is what keeps
Phase 4 from becoming a rewrite of `wxGraphicsContext` and every `wxDC`
operation.

Done:

- **`pizza_snapshot()`** (`win_gtk.cpp`) replaces the `draw` signal. Two
  structural differences: a class vfunc carries no user data where the signal
  carried the `wxWindow`, so the owner is recorded on the widget and looked up;
  and children were previously drawn by chaining through the parent's handler,
  so each is now snapshotted explicitly.
- **Border painting** moved into wx's own paint path. GTK3 drew it from the
  *parent's* draw signal, but the stroke always fell just inside the child's
  own bounds, so the parent-relative rectangle becomes a child-relative one at
  the origin and the ordering (after the content) is preserved. **This closes
  the `BORDER_STYLES` gap open since update 8.**
- **Freeze/thaw** becomes a flag the snapshot checks, since there is no draw
  handler left to block.
- **`wxGtkImage`** gained a snapshot vfunc (see below).

`window.cpp`: **206 → 44 errors**. Whole-tree 1133 → 1067, no regressions.

### An assumption I stated and then had to correct

The design doc said `dc.cpp`, `graphicc.cpp`, `overlay.cpp` and
`image_gtk.cpp` were "expected to be mostly mechanical once the `cairo_t` is
flowing, since they consume a `cairo_t` rather than producing one -- but this
is an assumption, not yet verified". Checked, and it is **partly wrong**:

- `dc.cpp` and `graphicc.cpp` do mostly consume, **but both call
  `gdk_cairo_create(gdk_get_default_root_window())`** to build a context for
  `wxScreenDC`. GTK4 has no root window and no backend-independent way to
  obtain a cairo context for the screen at all.

  **Decided and done.** The scope decision was the X11 fallback: X11 still has
  a root window whether or not GDK exposes one, and `cairo_xlib_surface_create()`
  can be pointed straight at it, which is what `gdk_cairo_create()` did anyway.
  So `wxScreenDC` works there for both reading the screen and drawing on it,
  and Wayland keeps only the size -- there is no screen to read and no desktop
  to draw on, by design. Measured against GTK 4.14 and 4.22 in
  `probes/gtk4-screen-readback.c`, and covered by `tests/graphics/screendc.cpp`
  on every port. What made this worth doing rather than documenting is the way
  it failed: a screenshot came back black with no error at all.
- `image_gtk.cpp` overrides `GtkImageClass::draw`, i.e. another draw→snapshot
  migration. That part is done, but the file still does not compile: `wxGtkImage`
  derives from `GtkImage` by embedding its struct, and under GTK4 both
  `GtkImage` and `GtkImageClass` are opaque, so the subclass cannot be
  declared. Needs deriving from `GtkWidget` directly or wrapping a
  `GtkPicture`. **New deferred item.**

### Fidelity gaps added by this phase

- **Update regions are gone.** GTK4 gives a widget no damage information and
  removed partial invalidation, so `wxWindow::GetUpdateRegion()` reports the
  whole client area and `wxPaintDC`'s clip to it is a no-op. Correctness holds
  (repainting more is safe) and GTK4's renderer culls by diffing render nodes
  instead.

  **Less severe than first documented.** wxGTK3 already collapsed any
  multi-rectangle damage to a single bounding rectangle before an application
  saw it — `GTKSendPaintEvents()` takes `cairo_clip_extents()` and builds
  `m_updateRegion` from one rect — and in practice GTK3 tended to damage the
  whole window regardless. So this narrows an already-coarse guarantee rather
  than removing a precise one. See the Phase 4 design doc section 3.
- **Freezing a native control does nothing.** GTK3 intercepted the draw signal
  ahead of the widget's own handler; a GTK4 snapshot vfunc cannot be
  intercepted from outside, so only `wxPizza` widgets can be frozen.

### Still unverified, and now most acutely

Rendering is exactly what compiling cannot check. Whether borders appear in
the right place, whether children paint in the right order, whether anything
appears at all -- none of it is confirmed. `test_gui` still cannot link, and
the remaining blockers for that are `dc.cpp`, `graphicc.cpp`, `overlay.cpp`
and `image_gtk.cpp`, i.e. the two new deferred items above plus `overlay.cpp`.

## Progress update 19: working the tail toward a link

With Phase 4 started, the goal shifted to *linking* — which needs every
remaining file to compile, not just the interesting ones. Failing build
targets **46 → 42**, whole-tree diagnostics 1067 → **1040**, no regressions.

Newly compiling in full: `colour.o`, `scrolbar.o`, `taskbar.o`, `utilsgtk.o`.

### One shim, six files

`gtk_dialog_run()` and `gtk_native_dialog_run()` blocked `assertdlg_gtk`,
`dirdlg`, `filedlg`, `msgdlg`, `print` and `utilsgtk`. GTK4 removed both
because its dialogs are asynchronous — the caller connects to `::response`
instead of blocking — but wx's API is synchronous, so the blocking has to
live somewhere. The shims reproduce what `gtk_dialog_run()` did internally:
modal, present, spin a nested main loop until it responds.

Deliberately a plain `GMainLoop` rather than `wxGUIEventLoop`: the assert
dialog calls this exactly when wx's own event-loop machinery may not be
usable, which is the situation it exists to survive.

### The gesture finding paying off

`scrolbar.cpp` tracked thumb drags with button press/release on the
`GtkRange` and deferred its `THUMBRELEASE`/`CHANGED` events through
`event_after` so handlers could set the scroll position afterwards.

`GtkRange` has its own click gesture. A bubble-phase gesture that doesn't
claim would have received the press and **never the release** — the trap
measured in `docs/gtk/probes/gtk4-gesture-semantics.c`. Written the obvious
way, every thumb-release event would have vanished silently. The port uses
`GTK_PHASE_CAPTURE` (sees both without claiming, so `GtkRange` still drags)
and `g_idle_add()` for the deferral.

### Two corrections to earlier entries in this document

- **The `GdkColor` item was over-scoped.** Update at "Newly-scoped deferred
  item: colour.cpp" recorded it as touching roughly a dozen files, because
  that many call `GetColor()`. Only **three** are live under GTK4
  (`colour.cpp`, `textctrl.cpp`, `hyperlink.cpp`); the rest sit in
  `!__WXGTK3__` blocks and have been dead there all along. Counting call
  sites rather than *live* call sites made the task look far worse than it
  was and kept it deferred longer than it deserved. Now done.
- **Update regions**: severity corrected in place, see Phase 4 design §3.

### Latent bugs surfaced, not caused

Two things GTK4 exposed that were already wrong:

- `taskbar.cpp`'s no-`GtkStatusIcon` fallback declared
  `SetIcon(const wxIcon&, ...)` while the header has taken `wxBitmapBundle`
  for some time. That branch had apparently never been compiled; GTK4 is
  simply the first configuration to select it.
- An `#ifndef __WXGTK4__` from the touch batch spanned an anonymous
  namespace's opening but not its closing brace (fixed in update 17).

### New gaps recorded

- `hyperlink.cpp`: the `visited-link-color` style property is gone — link
  colouring moved to CSS, applied to the element rather than exposed as
  queryable. Uses the hard-coded fallback the GTK3 path already had, so a
  theme with custom visited-link colours is not followed.
- `textctrl.cpp`: `GtkTextTag`'s `foreground-gdk`/`background-gdk` gave way
  to the `-rgba` variants.

### What actually gates the samples

Six deferred subsystems dominate what is left, and each is a rewrite rather
than a translation: `toolbar` (141 errors), `toplevel` (138), `clipbrd` (62),
`radiobox` (58), `dataview` (55), `menu` (53). Linking needs all of them, so
they set the timeline — the remaining small files are comparatively quick but
do not, by themselves, get anything to link.

## Progress update 19: working down the tail toward a link

With `window.cpp` no longer the bottleneck, the goal shifted to reducing the
*failing-target count*, since linking `test_gui` — and therefore any runtime
verification at all — needs every one of them to compile, not just the
interesting ones.

**Failing targets 46 → 39; diagnostics 1133 → 998.** No regressions in any
batch; GTK3 checked after each.

Now compiling fully: `colour.o`, `scrolbar.o`, `taskbar.o`, `utilsgtk.o`,
`dialog.o`, `msgdlg.o`, `dirdlg.o`.

### The highest-leverage piece: `gtk_dialog_run()`

Six files were blocked on `gtk_dialog_run()`/`gtk_native_dialog_run()`
(`assertdlg_gtk`, `dirdlg`, `filedlg`, `msgdlg`, `print`, `utilsgtk`). GTK4
removed both because its dialogs are asynchronous — the caller connects to
`::response` instead of blocking — but wx's API is synchronous
(`wxDialog::ShowModal()` returns the result), so the blocking has to exist
somewhere. The shims in `gtk3-compat.h` reproduce what `gtk_dialog_run()` did
internally: make the dialog modal, show it, spin a nested main loop until it
responds.

Deliberately a plain `GMainLoop` rather than `wxGUIEventLoop`: the assert
dialog calls this precisely when wx's own event loop machinery may not be in a
usable state.

### Two corrections to earlier entries in this document

- **`GdkColor`'s removal was scoped far too widely.** Update 14 recorded it as
  touching about a dozen files because that is how many call `GetColor()`.
  Only **three** are actually rejected by GTK4 (`colour.cpp`, `textctrl.cpp`,
  `hyperlink.cpp`); every other caller sits inside a `!__WXGTK3__` block and
  has been dead code under GTK4 all along. The scoping counted call sites
  rather than live ones, which made the task look far more forbidding than it
  was — it took one batch.
- **The update-region loss was overstated** — see the correction in update 18
  and the Phase 4 design doc.

### A latent bug this surfaced

`taskbar.cpp`'s no-`GtkStatusIcon` fallback declared
`SetIcon(const wxIcon&, ...)` while the header has taken `wxBitmapBundle` for
some time. That branch appears never to have been compiled by any
configuration; GTK4 is simply the first to select it. Not a GTK4 issue at all,
just one this port happened to expose.

### Capability losses recorded this batch

All cases where GTK4 deliberately took control away from applications, so
there is nothing to work around:

- `wxSTAY_ON_TOP` on message dialogs — `gtk_window_set_keep_above()` is gone;
  stacking is the compositor's business.
- `wxDD_SHOW_HIDDEN` — whether hidden files are listed is the user's choice
  (Ctrl+H or the chooser's menu), not the application's.
- Mouse capture outside a pointer sequence, and freezing a native control
  (updates 17 and 18).
- `hyperlink.cpp`'s visited-link colour — link colouring moved to CSS, applied
  to the element rather than exposed as a queryable property.

### What is left, and what sets the timeline

Of the 39, roughly a dozen sit at 1–5 errors and should fall the same way.
The timeline is set by six genuine subsystem rewrites, not by the tail:

| File | Errors | Why |
|---|---|---|
| `toolbar.cpp` | 141 | `GtkToolbar`/`GtkToolItem` removed outright |
| `toplevel.cpp` | 138 | WM hints, `GdkWindow` geometry |
| `clipbrd.cpp` | 62 | `GdkAtom`/selection model replaced by `GdkClipboard` |
| `radiobox.cpp` | 58 | `GtkRadioButton` removed |
| `dataview.cpp` | 55 | cell renderers, `GdkWindow` |
| `menu.cpp` | 53 | `GMenuModel`/`GtkPopoverMenu` |

`test_gui` cannot link until all of them are done, so **nothing in this port
is runtime-verified yet** and that remains the largest risk, not the error
count.

---

## Progress update 20: the menu subsystem, rewritten

**998 -> 944 diagnostics, 39 -> 38 failing targets, no regressions.**
`menu.cpp` went from 53 errors to zero. That is one file out of the six
subsystem rewrites that gate the link, and the first of them to be done.

Full design: `docs/gtk/gtk4-phase-menu-design.md`. Probe:
`docs/gtk/probes/gtk4-menu-actions.c`.

### Not a port, a replacement

Every GTK type `menu.cpp` was built on is gone: `GtkMenu`, `GtkMenuBar`,
`GtkMenuItem` and all its subclasses, `GtkSeparatorMenuItem`,
`GtkTearoffMenuItem`, `GtkAccelGroup`, and the whole `gtk_menu_popup_at_*()`
family. GTK4 menus are declarative -- a `GMenuModel` describes the structure,
`GAction`s carry the behaviour, and `GtkPopoverMenu`/`GtkPopoverMenuBar` are
views that render a model. There are no per-item widgets at all.

So the GTK3 backend's central data structure, `wxMenuItem::m_menuItem` (a
`GtkWidget*`), has no counterpart. Everything downstream of that had to be
re-derived rather than translated.

### The finding that made it tractable

Before writing anything I checked whether per-item `GtkWidget*`s had leaked out
of `menu.cpp`. They had not:

* `wxMenuItem::GetMenuItem()` returning `GtkWidget*` is referenced **only
  inside `src/gtk/menu.cpp`**. The other matches in the tree
  (`framecmn.cpp`, `accel.cpp`, `event.h`, `accel.h`) are a *different*
  `GetMenuItem()` that returns `wxMenuItem*`.
* `wxMenu::m_menu`, `m_owner` and `m_accel` are used outside `menu.cpp` in
  exactly one place, `wxWindowGTK::DoPopupMenu()`, which was already
  `#ifdef`ed out for GTK4.

That is what turned this from "needs a compatibility shim" into "rewrite the
backend freely". Worth stating because the opposite result would have changed
the plan for the whole file, and it is not something to assume.

### Measured, not assumed

Seven mechanics were probed against GTK 4.14.5 under Xvfb before any code was
written. The load-bearing one:

> a `GtkShortcutController` on the frame, holding a `GtkShortcut` whose action
> is a `GtkNamedAction`, **does** resolve names against an action group
> installed with `gtk_widget_insert_action_group()` on that same frame.

It was not obvious that it would, and menu accelerators under GTK4 have no
other route. If it had failed, accelerators would have had to be routed through
wx's own key handling instead, which is a materially different design.

A second probe corrected something I had written down wrongly. My first note on
per-item bitmaps said `GMenuItem` accepts "only a `GIcon`, not an arbitrary
`wxBitmap` surface", and I had it queued as a fidelity gap. That is wrong:
**`GdkTexture` implements `GIcon`**, so `wxBitmap -> GdkPixbuf -> GdkTexture`
is a perfectly good menu icon and `SetBitmap()` keeps working. One line of test
code was enough to check; had I not, the port would have dropped a feature that
did not need dropping.

### Rebuild rather than patch

`GMenu` copies a `GMenuItem`'s attributes on insertion -- there is no
"change item 3's label". Mutation means remove-then-insert. And because
separators are modelled as *sections*, wx item position N is not model
position N.

Rather than maintain a wx-position-to-model-path mapping across every insert,
remove and separator change, every structural change calls
`wxMenu::GTKRebuildModel()`, which regenerates the model and the actions from
the wx item list. Menus have tens of items; this is O(n) per edit and it
removes a whole class of index-mapping bugs. State-only changes (`Enable()`,
`Check()`) still go straight to the `GAction`, so an open menu stays
responsive.

A pleasant consequence: the GTK3 backend's hand-rolled "look at the previous
item, then the next item" radio-group joining logic disappears. Radio runs are
just recomputed on each rebuild.

### Capability losses

- **`wxEVT_MENU_HIGHLIGHT` is not emitted.** GTK3 had `select`/`deselect` per
  item widget. A `GMenuModel` has no concept of a highlighted item and
  `GtkPopoverMenu` does not publish the `GtkModelButton`s it builds.
- **`wxEVT_MENU_OPEN`/`wxEVT_MENU_CLOSE` only fire for popup menus.** There wx
  owns the `GtkPopoverMenu` and watches its `show`/`closed` signals.
  `GtkPopoverMenuBar` creates its drop-down popovers internally and does not
  expose them. `UpdateUI()` on menu open is likewise popup-only.
- **Disabling one radio item disables its whole group**, because a GTK4 radio
  group is a single action and the enabled state lives on the action.
- **`wxMENU_TEAROFF` and `wxMB_DOCKABLE`** are accepted and ignored;
  `GtkTearoffMenuItem` and `GtkHandleBox` are both gone.
- **Stock accelerators.** `gtk_stock_lookup()` is gone, so `wxID_COPY` no
  longer picks up GTK's default `Ctrl+C` by itself. Explicit accelerators in
  the item label still work.

Two things GTK4 makes *harder* were worked around rather than dropped: a
`GMenuModel` submenu item has no action, so both `EnableTop()` and
`Enable()` on a submenu item re-emit that entry as a plain item bound to a
never-enabled action -- the label still shows, greyed, and does nothing.

### `PopupMenu()` works again

`wxWindow::PopupMenu()` had been reporting failure since the Phase 2 batch. It
is now implemented: a `GtkPopoverMenu` parented on the invoking window, with a
nested `GMainLoop` to preserve the documented "blocks until dismissed"
contract -- the same trick already used for `gtk_dialog_run()`.

### Tests

Four checks added to `build/tools/gtk4-invariants.c`, which CI runs before the
build: named-action resolution through an inserted action group, the `accel`
attribute round-tripping through a `GMenu`, a stateful action reporting the
activated target, and a live menu bar surviving its model being emptied and
refilled. All four were verified against negative controls -- deliberately
breaking each mechanism does make its check fail.

As with everything else in this port, these assert GTK's own behaviour. None of
the wx code above is runtime-verified yet, and cannot be until the remaining
five rewrites let `test_gui` link.

### What is left

| File | Errors | Why |
|---|---|---|
| `toolbar.cpp` | 141 | `GtkToolbar`/`GtkToolItem` removed outright |
| `toplevel.cpp` | 138 | WM hints, `GdkWindow` geometry |
| `clipbrd.cpp` | 62 | `GdkAtom`/selection model replaced by `GdkClipboard` |
| `radiobox.cpp` | 58 | `GtkRadioButton` removed |
| `dataview.cpp` | 55 | cell renderers, `GdkWindow` |

Plus the ~12-file tail at 1-5 errors each.

---

## Progress update 21: `toplevel.cpp`, and what GTK4 simply won't do

**944 -> 829 diagnostics, 38 -> 35 failing targets, no regressions.**
`toplevel.cpp` went from 138 errors to zero. `app.cpp` and `timer.cpp` fell out
with it, from a shared header fix. Two of the six subsystem rewrites are done.

Unlike `menu.cpp`, this was not one replaced abstraction but a long tail of
individually small removals -- and the interesting part is how many of them
have **no replacement at all**, by design.

### Things GTK4 took away from applications on purpose

Each of these is a deliberate decision that the compositor, not the client,
owns the behaviour. Under Wayland most were never really the client's to
begin with. There is nothing to work around, so each is `#ifdef`ed out with a
comment saying why:

| Removed | wx feature affected |
|---|---|
| `gtk_window_move()`, `gtk_window_get_position()` | `Move()` has no effect; `wxMoveEvent` reports what wx was *asked* for, not where the window is |
| `gtk_window_set_position()` | `wxCENTRE_ON_PARENT` for dialogs |
| `gtk_window_set_keep_above()` | `wxSTAY_ON_TOP` |
| `gtk_window_set_skip_taskbar_hint()` | `wxFRAME_NO_TASKBAR` |
| `gtk_window_set_focus_on_map()` | `ShowWithoutActivating()` behaves as plain `Show()` |
| `gtk_window_set_type_hint()` | dialog/utility window hints |
| `gdk_window_set_functions()` | `EnableCloseButton()` beyond client side decorations |
| `gtk_window_set_geometry_hints()` | maximum size and resize increments |
| `gtk_window_set_icon_list()` | `SetIcons()` -- window icons come from the .desktop file now |
| `gtk_grab_add()` | `AddGrab()`, now expressed as window modality |
| `configure-event` | position tracking |
| X11 property notifications | `_NET_FRAME_EXTENTS` tracking |

Maximum size deserves a note: `wxWindowBase::ConstrainSize()` still clamps
sizes wx sets itself, so `SetMaxSize()` is honoured for programmatic resizing.
What is lost is stopping the *user* dragging the window bigger. Minimum size
survives as a plain `gtk_widget_set_size_request()`.

### The frame extents machinery is gone, and that is a real loss

GTK3 wx learns its window manager decoration size by asking for
`_NET_FRAME_EXTENTS` and waiting for the X11 property notification, deferring
the initial `gtk_widget_show()` until the answer arrives so the window is not
visibly resized a moment after appearing.

GTK4 delivers no X11 property notifications to applications at all. With no
notification there is nothing to defer *for*, so both halves come out: a GTK4
wxTopLevelWindow never learns its own decoration size and `m_decorSize` stays
zero.

How much this matters depends on who draws the decorations. Where GTK draws
them itself -- client side decorations, the common case now --
`HasClientDecor()` already made the compensation zero, so nothing changes.
Where a traditional window manager draws them, `GetSize()`/`SetSize()` on a
TLW will be off by the frame thickness. `wxGetFrameExtents()` itself still
works and `wxSystemSettings` still uses it for `wxSYS_FRAMESIZE_X` and
friends; it is only the TLW's own arithmetic that no longer consults it.

I initially wrote in a comment that the timeout handler "becomes the only path"
to the extents. That was wrong and is corrected in the code: the timeout is
only ever started by the deferred show, so removing the latter makes the
former dead too.

### Things that did map cleanly

* `configure-event`'s DPI half -> `notify::scale-factor`, so `WXNotifyDPIChange()`
  still fires. Only the position half is lost.
* `window-state-event` -> `GdkToplevel`'s `notify::state`. GTK reports only the
  new state, so the previous one is kept in the window to reconstruct the
  changed mask the existing logic wants.
* `focus-in`/`focus-out-event` -> `notify::is-active` (already added in an
  earlier batch, wired up here).
* `delete-event` -> `close-request`.
* `gtk_container_forall()` searching for the header bar -> `gtk_window_get_titlebar()`,
  which is what the search was looking for all along.
* `gtk_window_resize()` -> `gtk_window_set_default_size()`, which applies to a
  visible window too.
* `gdk_window_get_state()` -> `gtk_window_is_maximized()`.
* The RGBA visual dance in `SetTransparent()` -> nothing needed: GTK4 surfaces
  always support alpha.
* `gdk_threads_enter()`/`leave()` -> no-ops. The GDK threads API was removed
  outright; its replacement is the rule it was deprecated in favour of, that
  GTK calls happen on the main thread and other threads use `g_idle_add()`.
  This is in `wx/gtk/private/threads.h`, which is why `app.cpp` and `timer.cpp`
  came along.

### Two bugs I introduced and caught before committing

Worth recording because both would have compiled and misbehaved silently:

1. I first wrote the GTK4 `gtk_window_set_decorated()` call where the GTK3
   `gdk_window_set_decorations()` had been -- which is *after* the block that
   zeroes `m_gdkDecor` once GTK is drawing the decorations itself. That would
   have undecorated every client-side-decorated window. It has to happen before
   that block, from the style-derived value.
2. `EnableCloseButton()` I first implemented by re-calling `GTKHandleRealized()`.
   That would have re-connected the `notify::state` signal on every call, and
   would have read the already-zeroed `m_gdkDecor` anyway. It now updates the
   header bar's decoration layout directly.

The zeroing of `m_gdkDecor` turns out to serve two purposes -- neutering the
GTK3 setter *and* keying `GetCachedDecorSize()` on "no window manager
decorations" -- which is why it is kept rather than skipped under GTK4.

### What is left

| File | Errors | Why |
|---|---|---|
| `toolbar.cpp` | 141 | `GtkToolbar`/`GtkToolItem` removed outright |
| `clipbrd.cpp` | 62 | `GdkAtom`/selection model replaced by `GdkClipboard` |
| `radiobox.cpp` | 58 | `GtkRadioButton` removed |
| `dataview.cpp` | 55 | cell renderers, `GdkWindow` |

Plus the tail of files at 1-25 errors each. `test_gui` still cannot link, so
none of the above is runtime-verified.

---

## Progress update 22: `toolbar.cpp`

**829 -> 685 diagnostics, 35 -> 34 failing targets, no regressions.**
`toolbar.cpp` went from 141 errors to zero -- the largest single file in the
port. Three of the six subsystem rewrites are done.

### Another whole widget family, gone

`GtkToolbar`, `GtkToolItem`, `GtkToolButton`, `GtkToggleToolButton`,
`GtkRadioToolButton`, `GtkSeparatorToolItem` and `GtkMenuToolButton` were all
removed. GTK4's answer is that a toolbar was never a special kind of container:
it is a `GtkBox` carrying the `toolbar` style class, holding ordinary buttons.

| wx | GTK4 |
| --- | --- |
| the toolbar | `GtkBox` + the `toolbar` style class |
| normal tool | `GtkButton` with the `flat` class |
| check tool | `GtkToggleButton` |
| radio tool | `GtkToggleButton` grouped with `gtk_toggle_button_set_group()` |
| separator | `GtkSeparator` |
| stretchable separator | `GtkSeparator`, opacity 0, `hexpand` set |
| control tool | the control's own widget, directly in the box |
| dropdown tool | a box holding the button and an arrow `GtkToggleButton` |
| right click | `GtkGestureClick` limited to the secondary button |
| mouse enter/leave | `GtkEventControllerMotion` |

### The behaviour difference that would have bitten silently

GTK3's `gtk_radio_tool_button_new()` **activates the first button of a group by
itself**. The GTK3 wx code knows this and has a comment saying so, calling
`tool->Toggle(true)` purely to bring its own flag into line with what GTK
already did.

GTK4's grouped toggle buttons do **not** do this. A probe confirmed it: a
freshly grouped set has nothing selected. So the port now activates the first
radio tool explicitly, both in GTK and in wx.

This is the kind of difference that compiles perfectly and produces a toolbar
where a radio group starts with nothing selected -- and the existing comment
in the code would have actively misled anyone reading it. It is now pinned as a
CI invariant, phrased as the negative ("a grouped toggle button is NOT active
by default") so that GTK reverting would also be caught.

### `wxGtkImage` cannot be used here either

`GtkImage` is final under GTK4, so `wxGtkImage` -- which derives from it
specifically so it can pick the right bitmap variant for the scale factor and
enabled state at draw time -- has no GTK4 form. This is the same blocker
already recorded for `menu.cpp`.

The choice it made is now made eagerly instead: `wxToolBarTool::SetImage()`
picks the variant and hands it to a plain `GtkImage` as a `GdkTexture`. The
consequence is that `DoEnableTool()` has to call `SetImage()` as well, since
the disabled bitmap is no longer chosen lazily when the widget draws.

### Smaller consequences

* **Tool content is ours to build.** There is no `GtkToolbarStyle` deciding
  icons vs text vs both, and no "is important" flag controlling whether a label
  shows beside the icon in a horizontal layout. A button contains exactly the
  widgets put in it, so `wxTB_NOICONS`, `wxTB_TEXT` and `wxTB_HORZ_LAYOUT` are
  honoured by rebuilding each button's child. Changing the toolbar style or a
  tool's label now rebuilds that content.
* **No overflow arrow.** `DoGetBestSize()` loses its workaround -- the GTK3
  code disabled the arrow around the measurement because `GtkToolbar` otherwise
  reported only the arrow's size, and called that "gross". A box just reports
  its children.
* **`wxTB_DOCKABLE` does nothing.** `GtkHandleBox` was already gone in GTK3.19.7.
* **Inline toolbar styling is gone.** `wxAddRemoveCtrl` used
  `GTK_STYLE_CLASS_INLINE_TOOLBAR` plus style context junction sides to make its
  toolbar look like GNOME's; neither survived the move to CSS styling, so it
  gets a plain toolbar.
* **Insertion by position takes a walk.** `GtkBox` only inserts relative to a
  sibling, so a helper walks the children to find the one at `pos - 1`. That the
  walk order matches insertion order is now also a CI invariant.

### What is left

| File | Errors | Why |
|---|---|---|
| `clipbrd.cpp` | 62 | `GdkAtom`/selection model replaced by `GdkClipboard` |
| `radiobox.cpp` | 58 | `GtkRadioButton` removed |
| `dataview.cpp` | 55 | cell renderers, `GdkWindow` |

Plus the tail of smaller files. `test_gui` still cannot link, so none of this
is runtime-verified.

---

## Progress update 23: `radiobox.cpp`

**685 -> 627 diagnostics, 34 -> 33 failing targets, no regressions.**
`radiobox.cpp` went from 58 errors to zero. Four of the six subsystem rewrites
are done.

### One type fewer, rather than a replacement

`GtkRadioButton` was not replaced under GTK4, it was *merged*: a
`GtkCheckButton` with a group **is** a radio button. So unlike the menu and the
toolbar, there was no new model to design here -- the mapping is
`GtkRadioButton` -> `GtkCheckButton`, with grouping done by pointing each
button at the first one instead of threading a `GSList` through successive
construction calls.

Two consequences worth noting:

* `GtkCheckButton` no longer derives from `GtkToggleButton`, so the state is
  read with `gtk_check_button_get_active()` and the signal to watch is
  `toggled` rather than `clicked`.
* It owns its label internally -- there is no child `GtkLabel` to reach through
  `gtk_bin_get_child()`. `GetString()`/`SetString()` go through
  `gtk_check_button_get_label()`/`set_label()`, and the places that used to
  disable or restyle the label widget separately simply don't need to: the
  button dims and themes its own label.

### The ordering question I checked rather than assumed

`wxRadioBox::Create()` activates the first button as soon as it is made, and
only then creates and groups the remaining ones. Under GTK3 that is fine
because the group is a `GSList` passed forward. Under GTK4 a button *joins* an
existing group afterwards, which raises the question of whether joining resets
the group's selection.

A probe mirroring the construction order exactly confirmed it does not: the
first button is still active after the others join, and exactly one is active.
Worth checking rather than assuming, because the failure would have been a
radio box that silently starts with nothing selected -- the same class of bug
found in `toolbar.cpp`, and it would have looked identical.

### Event plumbing

* `key-press-event` -> `GtkEventControllerKey`. The handler navigates between
  buttons with the arrow keys and forwards Tab to the parent; only the way GTK
  hands over the key changed, so the logic was factored into one function both
  versions call.
* `focus-in-event`/`focus-out-event` -> `GtkEventControllerFocus`, whose
  `enter`/`leave` signals carry no event and return nothing.
* **`size-allocate` is gone entirely.** The GTK3 code cached each button's
  rectangle as it changed, purely so `GetItemFromPoint()` could hit-test it.
  There is no such signal under GTK4, so the rectangle is computed on demand
  with `gtk_widget_compute_bounds()` -- which is where it was actually needed
  all along, and drops a cache that had to be kept in sync.
* `GtkShadowType` is gone, so `wxNO_BORDER` adds the `flat` style class to the
  frame rather than setting a shadow type.

### A recurring pattern in this port

The GTK3 build check caught a stray brace that left `extern "C"` closed one
function early -- the third time in this port that keeping both builds green
after every batch has caught a preprocessor or brace mistake the GTK4 build
happily accepted. Compiling both configurations is not redundancy here; the
`#ifdef` interleaving makes it the actual test.

### What is left

Two subsystem rewrites, plus a tail that is now the bulk of it:

| File | Errors |
|---|---|
| `clipbrd.cpp` | 62 |
| `dataview.cpp` | 55 |
| `renderer.cpp` | 50 |
| `minifram.cpp` | 48 |
| `window.cpp` | 44 |
| `dnd.cpp` | 41 |
| `evtloop.cpp` | 37 |

then `textctrl.cpp`, `anybutton.cpp`, `notebook.cpp` and a dozen smaller ones.
`test_gui` still cannot link, so none of this is runtime-verified.

---

## Progress update 24: the clipboard, and a deadlock that isn't documented

**627 -> 574 diagnostics, 33 -> 31 failing targets, no regressions.**
`clipbrd.cpp` (62 errors) and `dataobj.cpp` both went to zero. Five of the six
subsystem rewrites are done.

### `wxDataFormat` mapped better than expected

GTK4 removed `GdkAtom`; a clipboard format is a MIME type string. That sounds
like a type change with comparison consequences, but an atom *was* an interned
string, and `g_intern_string()` returns the same canonical pointer for equal
strings. So `NativeFormat` becomes `const char*` and every existing
compare-by-pointer keeps working untouched.

The one casualty is the `wxDataFormat(const char*)` constructor, which under
GTK4 would have the same signature as `wxDataFormat(NativeFormat)`. It is
dropped there; `SetId(NativeFormat)` interns whatever it is handed, so passing
an ordinary literal still works. The visible difference is that
`wxDataFormat("text/html")` now classifies as `wxDF_HTML` rather than
`wxDF_PRIVATE`, which is arguably more correct but is a change.

### The finding worth keeping

`wxClipboard::GetData()` is synchronous and `GdkClipboard` reads are not, so
the two are bridged with a nested main loop -- the same device already used for
modal dialogs and popup menus here. What is not obvious, and not documented:

> The `GInputStream` from `gdk_clipboard_read_finish()` must be drained
> **asynchronously**. When the clipboard is locally owned -- an application
> reading back what it just copied, the common case -- the writer feeding that
> stream is our own `GdkContentProvider`, running on the same main context.
> A blocking `g_output_stream_splice()` therefore deadlocks against it.

Worse, the deadlock is unrecoverable from inside. A `g_timeout_add()` watchdog
on the nested loop **never fires**, because the loop is blocked inside the read
callback and never reaches the point where it would dispatch the timeout. The
first version of the probe hung until an external `timeout` killed it.

`g_output_stream_splice_async()`, completing into the same nested loop, works.

`build/tools/gtk4-invariants.c` now asserts the working pattern end to end --
offer, advertise, read back through a nested loop, compare the bytes -- with a
watchdog so a future GTK breaking it fails CI rather than hanging it. Only the
good pattern is asserted, deliberately: proving the deadlock in CI would mean
hanging CI.

A second trap, this one merely a documented annotation that is easy to misread:
`gdk_content_provider_new_union()` takes ownership of the providers passed to
it. Unreffing them afterwards is a use-after-free that surfaces much later, as
a crash inside `gdk_content_provider_ref_formats()`.

### A bug of my own, caught by checking rather than assuming

My format-collection helper hardcoded `wxDataObject::Get`. That is right when
*offering* data, but `GetData()` needs `wxDataObject::Set` -- the formats the
object can *accept*. They are different lists. Using the providing direction
when receiving compiles perfectly and makes every paste silently find nothing.
Found by comparing against the GTK3 path instead of trusting the symmetry.

### Deliberate behaviour changes

* **Data is serialised eagerly.** GTK3 offered it lazily through a
  `selection_get` callback; GTK4's equivalent means subclassing
  `GdkContentProvider`. Copying a large object now costs that copy up front,
  but the bytes cannot go stale under a data object the application mutates or
  destroys after copying.
* **`Flush()` returns false.** `gtk_clipboard_store()` is gone; whether the
  contents outlive the application is the clipboard manager's decision.
* **`IsSupportedAsync()` got cheaper.** Under GTK3 it meant a round trip to the
  X server for the `TARGETS` atom. GTK4 already knows the available formats, so
  the answer is built immediately and only the event delivery stays deferred.

### What is left

| File | Errors |
|---|---|
| `dataview.cpp` | 55 |
| `renderer.cpp` | 50 |
| `minifram.cpp` | 48 |
| `window.cpp` | 44 |
| `dnd.cpp` | 41 |
| `evtloop.cpp` | 37 |

then `textctrl.cpp`, `anybutton.cpp`, `notebook.cpp`, `filedlg.cpp`,
`overlay.cpp` and a dozen smaller ones. `dnd.cpp` shares the format model just
established but uses the separate `GtkDragSource`/`GtkDropTarget` controllers.
`test_gui` still cannot link, so none of this is runtime-verified.

---

## Progress update 25: `wxGtkImage` gets a GTK4 form

**574 -> 551 diagnostics, 31 -> 29 failing targets, no regressions.**
`image_gtk.cpp` and `anybutton.cpp` (21 errors) both went to zero.

### Factoring a workaround that had appeared three times

`GtkImage` is a final type under GTK4, so `wxGtkImage` -- which derives from it
in order to pick the right bitmap variant for the scale factor and enabled
state *at the moment it draws* -- has no GTK4 form, and `image.h` could not
even be included.

`menu.cpp` and `toolbar.cpp` each worked around that locally, both open-coding
the same "choose the variant, push it in as a `GdkTexture`" logic.
`anybutton.cpp` needed it too, with `statbmp.cpp`, `notebook.cpp` and
`dataview.cpp` queued behind it. Three occurrences was enough: the lazy choice
is now made eagerly in one place, with callers re-pushing when the state it
depends on changes.

The interface is deliberately close to the GTK3 one so that call sites differ
by as little as possible -- `image->Set(b)` becomes `wxGtkImage::Set(image, b)`
and `WX_GTK_IS_IMAGE()` still works. `SetDisabled()` is part of it because
deriving a disabled bitmap from the normal one when the bundle has none is
exactly what the GTK3 class did while drawing, and would otherwise have been
reinvented at each call site as well.

`anybutton.cpp` then fell in a single pass, which is the evidence that
factoring rather than copying a third time was right.

One HiDPI detail is captured in the helper rather than left to each caller:
given only a texture, `GtkImage` lays it out at its *device* pixel size, so it
draws twice too large on a scaled display unless the logical pixel size is set
explicitly.

### Buttons hold one child

GTK4 removed `gtk_button_get_image()` and `gtk_button_set_image()` as well. A
button has exactly one child there, so an image shown beside a label is a box
we assemble; two local helpers keep the rest of `anybutton.cpp` reading as it
did, finding the image rather than asking for it. The image is reparented
rather than recreated when the content is rebuilt, so a bitmap already on it
survives a label change.

`gtk_button_set_image_position()` went with them and has no replacement: the
image sits where we put it, before the label, so `wxLEFT` is honoured and the
other positions are not.

### A note on ordering

`dnd.cpp` was going to be next but was deferred. It is a full model
replacement on the scale of `menu.cpp` -- `GtkDragSource`/`GtkDropTarget`
controllers, `GdkDrop`, and a *second* asynchronous-to-synchronous bridge, for
both `GetData()` and `DoDragDrop()` -- and is better started fresh than left
half-finished.

`renderer.cpp` is deferred for the opposite reason: it looks mechanical
(`GtkStateType` -> `GtkStateFlags`) but the constants do not map by value --
`GTK_STATE_INSENSITIVE` is 4 while `GTK_STATE_FLAG_INSENSITIVE` is 8 -- so a
substitution done quickly would compile and silently render the wrong states.
Notably the existing GTK3 code already casts one to the other in places, which
is worth looking at properly rather than preserving by reflex.

### What is left

| File | Errors |
|---|---|
| `dataview.cpp` | 55 |
| `renderer.cpp` | 50 |
| `minifram.cpp` | 48 |
| `window.cpp` | 44 |
| `dnd.cpp` | 41 |
| `evtloop.cpp` | 37 |
| `textctrl.cpp` | 21 |
| `notebook.cpp` | 19 |

plus `filedlg.cpp`, `overlay.cpp` and a handful of smaller ones. `evtloop.cpp`
needs the `gtk_main` family replaced with `GMainContext`. `test_gui` still
cannot link, so none of this is runtime-verified.

---

## Progress update 26: `renderer.cpp`, and a regression my own shim caused

**551 -> 495 diagnostics, 29 -> 28 failing targets, no regressions.**
`renderer.cpp` went from 50 errors to zero.

### A suspicion I had, and had to withdraw

Update 25 flagged this file as a trap because `GtkStateType` and `GtkStateFlags`
do not map by value -- `GTK_STATE_INSENSITIVE` is 4 while
`GTK_STATE_FLAG_INSENSITIVE` is 8, and 4 is `GTK_STATE_FLAG_SELECTED` -- and
noted that the existing code "already casts one to the other in places", with
the implication that it might be a latent bug.

That was wrong, and the file deserves better. It funnels every conversion
through a single `stateTypeToFlags[]` table. The raw `GtkStateFlags(state)`
casts belong to a *different* idiom in the same file, where `state` is an int
accumulating `GTK_STATE_FLAG_*` bits and the cast is exactly right. Both
conventions are correct.

That changed the fix. Rather than rewrite every drawing function to accumulate
flags, the enumeration is restored in the compatibility header, which preserves
the existing careful design and leaves the one conversion point where it was.

### The real work: style properties are gone

`gtk_widget_style_get()` and `gtk_style_context_get_style_property()` are
removed under GTK4, and so is the varargs `gtk_style_context_get()` used to
read CSS `min-width`/`min-height`. Those supplied `expander-size`,
`handle_size` and the check/radio indicator sizes.

They are now obtained the way GTK itself obtains them: by measuring a real
widget. `GtkPaned`'s handle is its `separator` node, an expander arrow is a
`GtkExpander`, and the indicators are the check button's indicator node.

The pre-3.20 fallback branches, which read style properties, are compiled out:
GTK4 is always newer than 3.20, so they were unreachable as well as
uncompilable.

### The trap that made this worth doing carefully

**Grouping a `GtkCheckButton` is what turns its `check` CSS node into a
`radio` node.** A probe confirmed it.

`wxGTKPrivate::GetRadioButtonWidget()` had already been stubbed for GTK4 as a
plain, ungrouped check button, carrying a comment saying the group did not
matter because the widget only supplies fonts and colours. That was true when
it was written and stopped being true the moment something measured the
indicator -- it would have returned check box metrics, silently, with no error
anywhere. The widget is now genuinely grouped and the comment says why.

Four invariants added: the ungrouped node is `check`, the grouped node is
`radio`, an unrealized check button still measures non-zero, and an expander
measures non-zero as the `expander-size` replacement.

### A regression I introduced, and what caught it

The state-taking style getters (`gtk_style_context_get_color()` and friends)
lost their state argument under GTK4. I first restored the GTK3 spelling as
**function-like macros taking three arguments**.

That broke `statbox.cpp`, which had already been ported to the two-argument
form: a three-parameter macro rejects a two-argument call outright. The file
went from green to red.

What is worth recording is how it surfaced. Total diagnostics fell from 551 to
498 in that build -- a count-only check would have reported pure improvement.
Diffing the set of **failed targets** showed `statbox.o` appearing where it had
not been before. This is the third time that habit has caught something an
error count would have hidden.

The fix is C++ overloads rather than macros: they accept both arities, so
ported and unported call sites coexist, which is the actual situation in a
port done file by file.

### What is left

| File | Errors |
|---|---|
| `dataview.cpp` | 55 |
| `minifram.cpp` | 48 |
| `window.cpp` | 44 |
| `dnd.cpp` | 41 |
| `evtloop.cpp` | 37 |
| `textctrl.cpp` | 21 |
| `notebook.cpp` | 19 |

plus `filedlg.cpp`, `overlay.cpp` and a handful of smaller ones. `test_gui`
still cannot link, so none of this is runtime-verified.

---

## Progress update 27: `minifram.cpp`

**495 -> 447 diagnostics, 28 -> 27 failing targets, no regressions.**
`minifram.cpp` went from 48 errors to zero.

### Two things the GTK3 code used that GTK4 has no form of

`wxMiniFrame` draws its own title bar and borders and implements dragging
itself. Both halves needed rebuilding, for different reasons.

**Somewhere to draw.** The GTK3 code puts a `GtkEventBox` between the window
and its contents, drawing the decorations in the box's `draw` handler --
the comment there says it uses an event box rather than `m_widget` because
otherwise "setting the resize cursor has no effect".

Under GTK4 neither half of that reasoning survives: `GtkEventBox` is gone, and
no widget needs one anyway since input arrives through controllers rather than
through a widget's own window. But there is still nowhere to draw, because GTK4
has no `draw` signal on an arbitrary widget.

The replacement is a `GtkOverlay` whose main child is a `GtkDrawingArea`, with
the frame's contents laid over it. That gets both halves right at once: the
decorations are painted underneath, and because the contents are inset by the
border and title margins, the uncovered edges are exactly the region where
clicks should reach the decorations.

One correction was needed after writing it: a `GtkOverlay` takes its size from
its *main* child, which here is a drawing area with no natural size, so the
frame requested nothing at all. `gtk_overlay_set_measure_overlay()` makes it
measure the contents instead.

**Moving the window.** The GTK3 code has two paths: under Wayland it calls
`gtk_window_begin_move_drag()` and lets the compositor do it, and otherwise it
grabs the pointer and calls `gtk_window_move()` on every motion event.

The second path has no GTK4 form at all -- there are no pointer grabs and no
way to position a window -- so the compositor-driven drag, previously the
Wayland-only path, is now the only one. It is `gdk_toplevel_begin_move()` and
`gdk_toplevel_begin_resize()` on the frame's `GdkToplevel`, started from a
`GtkGestureClick`. There is also no button-release handler any more: the drag
ends at the compositor rather than being tracked here.

Consequently `m_isDragMove` and `m_dragOffset` are `#ifdef`ed out of the header
under GTK4, which is how the missing-member errors first pointed at this.

### Smaller pieces

* `gdk_window_raise()` -> `gtk_window_present()`.
* `gdk_window_set_cursor()` with `GDK_BOTTOM_RIGHT_CORNER` ->
  `gtk_widget_set_cursor_from_name(widget, "se-resize")`.
* `gdk_window_invalidate_rect()` in `SetTitle()` -> queueing a draw on the
  drawing area.
* The decoration drawing, which was duplicated across the GTK2 and GTK3 draw
  callbacks, is factored into one function all three versions call. Adding a
  third copy for GTK4 was the alternative.

### What is left

| File | Errors |
|---|---|
| `dataview.cpp` | 55 |
| `window.cpp` | 44 |
| `dnd.cpp` | 41 |
| `evtloop.cpp` | 37 |
| `textctrl.cpp` | 21 |
| `notebook.cpp` | 19 |

plus `filedlg.cpp`, `overlay.cpp` and a handful of smaller ones. `test_gui`
still cannot link, so none of this is runtime-verified.

---

## Progress update 28: `evtloop.cpp`, and a capability GTK4 removes outright

**447 -> 411 diagnostics, 27 -> 26 failing targets, no regressions.**
`evtloop.cpp` went from 37 errors to zero.

### The loop itself

`gtk_main()`, `gtk_main_quit()`, `gtk_main_level()`, `gtk_main_iteration()` and
`gtk_events_pending()` are all gone; they were only ever thin wrappers, and the
replacements are the GLib primitives underneath:

| GTK3 | GTK4 |
| --- | --- |
| `gtk_main()` | `g_main_loop_run()` on a `GMainLoop` |
| `gtk_main_quit()` | `g_main_loop_quit()` on the innermost loop |
| `gtk_main_level()` | a stack of running loops kept here |
| `gtk_main_iteration()` | `g_main_context_iteration()` |
| `gtk_events_pending()` | `g_main_context_pending()` |

The stack matters. `DoRun()` deliberately quits its *enclosing* loop on the way
out, so that an `Exit()` issued for an outer loop while an inner one is running
gets noticed; `gtk_main_quit()` found that enclosing loop implicitly. With
explicit `GMainLoop`s there is nothing implicit to find, hence `gs_mainLoops`.

One signature change worth noting: `gtk_main_iteration()` returned whether the
loop had been asked to quit, while `g_main_context_iteration()` returns whether
it dispatched anything -- a different question. `Dispatch()` therefore answers
the original question directly, from `m_shouldExit`, rather than passing the
new return value through as if it meant the same thing.

### The capability that is simply gone

`DoYieldFor()` implements `wxYieldFor(category)`: process only events in the
requested categories now and put the rest back. Under GTK3 it does that by
calling `gdk_event_handler_set()` to take over the global event stream, sorting
each native event into a `wxEventCategory`, dispatching the wanted ones and
re-queuing the others.

**GTK4 has no interception point at all.** This was checked rather than
assumed, because it is a significant loss:

* `gdk_event_handler_set()` -- removed.
* `gdk_display_get_event()` -- removed.
* `GdkSurface` has no `event` signal.
* `GdkEvent` is opaque, so events could not be examined or copied even if they
  could be obtained.

Curiously `gdk_display_put_event()` *does* survive, so events could be put
back -- but with no way to take them out or see them first, that is moot.

So under GTK4 a yield processes **every** pending native event rather than only
those in the requested categories. Category filtering still applies to wx
events, which the base class handles. What is lost is deferring *native* ones:
`wxYieldFor(wxEVT_CATEGORY_UI)` no longer keeps user input from reaching
windows during the yield, so an application that yields inside a long operation
to stay repainted will now also see clicks and keystrokes it previously would
not have.

This is the largest behavioural gap in the port so far that has no workaround
available. Anything that needs input blocked during a yield has to do it at the
wx level -- disabling windows, or a `wxWindowDisabler` -- rather than relying on
the yield to do it.

### Two things that came out with it

`GTKIsSameAsLastEvent()` and the `m_lastEvent` buffer compare native events
byte-wise, which an opaque `GdkEvent` does not permit. Its only caller,
`EventAlreadyProcessed()` in `window.cpp`, is already GTK3-only -- the GTK4
input path handles propagation through gesture claiming instead -- so both are
`#ifdef`ed out rather than reinvented.

`StoreGdkEventForLaterProcessing()` and `m_queuedGdkEvents` likewise: with no
events to intercept there is nothing to store.

### What is left

| File | Errors |
|---|---|
| `dataview.cpp` | 55 |
| `window.cpp` | 44 |
| `dnd.cpp` | 41 |
| `textctrl.cpp` | 21 |
| `notebook.cpp` | 19 |

plus `filedlg.cpp`, `overlay.cpp` and a handful of smaller ones. `test_gui`
still cannot link, so none of this is runtime-verified.

---

## Progress update 29: `window.cpp` finished

**411 -> 367 diagnostics, 26 -> 25 failing targets, no regressions.**
The 44 errors left in `window.cpp` after the Phase 3 input work are gone.

### Focus events were not merely broken, they were absent

`focus-in-event` and `focus-out-event` are gone under GTK4 and nothing had
replaced them here, so `wxEVT_SET_FOCUS` and `wxEVT_KILL_FOCUS` would never
have fired at all. That is not something an error count shows -- the file had
44 errors either way -- and it is the sort of thing that only surfaces when
something tries to run.

A `GtkEventControllerFocus` now reports both. Its signals carry no event and
return nothing, so `GTKHandleFocusIn()`/`Out()` are simply called. The
controller is remembered on the widget with `g_object_set_data()` so that
`GTKDisableFocusOutEvent()`, which blocks the handler by function pointer, can
still find something to block.

### Removals with no replacement

* **`gdk_window_get_origin()`.** GTK4 does not tell a client where its window
  is on screen, so `ClientToScreen()`/`ScreenToClient()` are toplevel relative.
  Same root cause as the positioning loss recorded for `toplevel.cpp`.
* **`gdk_device_warp()`.** `WarpPointer()` is a no-op. This mostly formalises
  reality: it already did nothing under GTK3 with the Wayland backend, which is
  why the file carries a hand-rolled Wayland implementation -- itself built on
  `GdkWindow` and so GTK3-only now.
* **`gdk_window_raise()`/`lower()`.** There are no child windows to reorder and
  toplevel stacking belongs to the compositor. `Raise()` presents a toplevel;
  `Lower()` does nothing.
* **Binding sets.** The Ctrl-PageUp/Down conflict between scrolled windows and
  notebooks was resolved by removing the scrolled window's bindings. GTK4
  replaced binding sets with `GtkShortcut`, and a class's shortcuts cannot be
  removed from outside it, so that conflict is back.
* **Style properties**, again: the default button's `default_border` and the
  scrolled window's `scrollbar-spacing`. Neither is needed now -- GTK4 draws
  the default indication inside the widget's own allocation and overlays
  scrollbars rather than reserving space beside them -- so both become zero.

### One thing that came out better

GTK3 needs three handlers to track scrollbar dragging, one of them connected
and immediately blocked: `GtkRange` consumes the button release, so the
thumb-release event has to be deferred via `event_after` until after
`GtkRange`'s own handler has run.

A single `GtkGestureClick` in the **capture** phase sees the press and the
release before `GtkRange` does, so the deferral is unnecessary and the three
handlers collapse to two straightforward callbacks.

### The obstacle that cost the most time, and it was mundane

`GdkSurface` had never been added to the stand-in types in `defs.h`. The
accessor declarations returning it were therefore ill-formed, and the compiler
reported the *member functions* as "not declared in this scope" at their call
sites rather than complaining about the type. Several rounds of looking at the
wrong file followed.

Worth noting for `dnd.cpp`, which uses `GdkDrop` and `GdkDrag` and will need
the same stand-ins.

### What is left

| File | Errors |
|---|---|
| `dataview.cpp` | 55 |
| `dnd.cpp` | 41 |
| `textctrl.cpp` | 21 |
| `notebook.cpp` | 19 |

plus `filedlg.cpp`, `overlay.cpp` and a handful of smaller ones. `dnd.cpp` is
the last full model replacement. `test_gui` still cannot link, so none of this
is runtime-verified.

---

## Progress update 30: drag and drop -- the last model replacement

**367 -> 317 diagnostics, 25 -> 24 failing targets, no regressions.**
`dnd.cpp` went from 41 errors to zero.

With this every subsystem identified at the start as needing a rewrite rather
than a translation is done: menus, toplevel windows, toolbars, radio boxes, the
clipboard and now drag and drop.

### What was replaced

| GTK3 | GTK4 |
| --- | --- |
| `gtk_drag_dest_set()` + four signals | a `GtkDropTargetAsync` controller |
| `GdkDragContext` | `GdkDrop` receiving, `GdkDrag` sending |
| `GtkTargetList` | `GdkContentFormats` |
| `GtkSelectionData` | an asynchronous read of the drop |
| `gtk_drag_begin()` | `gdk_drag_begin()` with a `GdkContentProvider` |
| `gtk_drag_finish()` | `gdk_drop_finish()` |

### One structural difference worth stating

GTK3's `gtk_drag_dest_set()` could be told to supply **no** formats and **no**
actions -- the comment in the GTK3 code explains that this is deliberate, so
that wx can react individually in `drag_motion` and `drag_drop` and allow
dropping on only a small area.

`GtkDropTargetAsync` requires both when it is constructed. It is therefore
given the data object's accepted formats and every action wx might return, and
the per-position decision stays where it was: the handlers return the action to
use, or none, for each motion. The behaviour is preserved; only the place the
defaults are declared moved.

### A complication that disappeared

`GdkDrop` reports only the set of actions the source offers. There is no
separate "suggested action", which GTK3 had and which was the source of the
most confused code in this file: the suggestion was always `wxDragCopy` even
when a move was wanted, so `GTKFigureOutSuggestedAction()` had to weigh it
against the actions mask, and carried a disabled block with a comment from the
original author saying he did not understand the code below it.

Choosing directly from the offered set is simpler, and that block goes with it.

### The third asynchronous bridge

Dropped data no longer arrives attached to the drop; it has to be read. So
`wxDropTarget::GetData()`, which is synchronous, drives an asynchronous read
over a nested main loop -- the same device as `wxClipboard::GetData()`,
`wxDropSource::DoDragDrop()`, modal dialogs and popup menus.

It reuses what the clipboard probe established, including the part that is not
documented anywhere: the stream must be drained with
`g_output_stream_splice_async()`. The blocking version deadlocks against a
locally owned source and no watchdog can recover, because the loop is stuck
inside the read callback. That finding has now paid for itself twice.

### Capability lost: custom drag icons

`wxDropSource::SetIcon()` has no effect. The GTK3 implementation creates an
override-redirect window, draws the icon into it and shapes it with
`gtk_widget_shape_combine_region()`; none of those exist under GTK4. GTK
derives a drag icon from the content provider instead, so a drag still shows
something, just not the application's bitmap.

### What is left

No model replacements remain -- everything below is ordinary porting:

| File | Errors |
|---|---|
| `dataview.cpp` | 55 |
| `textctrl.cpp` | 21 |
| `notebook.cpp` | 19 |

plus `filedlg.cpp`, `overlay.cpp` and a handful of smaller ones. `test_gui`
still cannot link, so none of this is runtime-verified -- which remains the
largest risk in the port, unchanged since the beginning.

*(Update 31 below finishes all of these; two of them turned out not to be
ordinary porting after all.)*

## Progress update 31: the library compiles

Every remaining file is ported. `make` on the GTK4 configuration produces no
failing targets and no errors, from 78 failing targets when this started.

Two more model replacements turned up in this batch, neither of which was on
the list because both were hiding behind files that had failed earlier:

**Popup windows.** GTK4 removed `GTK_WINDOW_POPUP` and `gtk_window_move()`
together, so there is no way left to create a toplevel the application decides
the position of. `GtkPopover` is the only widget that still gets a surface of
its own which may extend past its toplevel, so `wxPopupWindow` is built on one.
A popover is placed by pointing at a rectangle in its parent's coordinates
rather than by being moved.

The placement was measured rather than assumed --
`docs/gtk/probes/gtk4-popover-placement.c`. A popover centers itself on the
pointing rectangle and, with `GTK_POS_BOTTOM`, puts its top edge at that
rectangle's bottom. Giving the rectangle the popup's own width both
left-aligns it and cancels out the popover's padding, which would otherwise
have to be measured separately. Two consequences: a `wxPopupWindow` must now
have a parent, and click-outside dismissal moved to where it belongs -- a
plain `wxPopupWindow` leaves the popover's autohide off, `wxPopupTransientWindow`
turns it on and learns about the dismissal from `::closed`. That last part
replaces `gtk_grab_add()`, `gdk_seat_grab()` and `gdk_pointer_grab()`, all of
which are gone.

**Drawing outside a paint handler.** `gdk_cairo_create()` is gone and a
`GdkSurface` cannot be drawn to except from GTK's own snapshot vfunc. So
`wxWindowDC` and `wxClientDC` keep reporting the right size -- which is what
most of their users actually want -- and their drawing goes to a scratch
context. `wxClientDCImpl::CanBeUsedForDrawing()` says so, exactly as it
already did for Wayland under GTK3. `wxPaintDC` is unaffected.

### The native overlay got simpler, not harder

`src/gtk/overlay.cpp` was a stack of things GTK4 removed: a popup toplevel for
save-under behaviour, `gtk_window_move()` to place it, RGBA visual selection,
and a 1x1 input shape to make it click-through. GTK4 needs none of them,
because it renders a toplevel as a single scene: a `GtkDrawingArea` added last
to the target's `wxPizza` is drawn on top of everything else in it, and
`gtk_widget_set_can_target(false)` makes it click-through exactly rather than
approximately. This also makes the native overlay the right choice on every
backend rather than only on Wayland, since the generic implementation reads the
window's pixels back and no GTK4 backend can do that.

### Measuring instead of reconstructing

Twice in this batch the GTK3 code reconstructed a size by summing CSS
properties node by node, using `gtk_style_context_get()` which GTK4 removed.
Both times measuring a real widget answered the same question and accounted
for every node by construction:

- `wxNotebook::CalcSizeFromPage()` measures a scratch notebook twice, once
  with the tab strip shown and once hidden. The two differences are exactly
  the frame around the page and the room the smallest tab needs -- the two
  quantities the GTK3 code computes separately.
- `GetScrollbarWidth()` in `settings.cpp` had already been done this way, and
  fixing the surrounding `#if` chain in this batch revealed that its GTK2
  fallback was never excluded, so it overwrote the measured width on every
  GTK4 build. That code had not been reached by the compiler before.

### Fixed-arity macro shims, for the third time

`gtk_file_chooser_set_current_folder()` kept its name in GTK4 while changing
what it takes. A two-argument macro shim broke `dirdlg.cpp`, which was already
calling it the GTK4 way. This is the same mistake as the `statbox.cpp`
regression in update 21 and the reason `gtk_scrolled_window_new()` was made
variadic earlier in this batch.

The three file-chooser functions which share a name with a real GTK4 function
are now overload sets reached through a forwarding variadic macro, so both
spellings compile and neither is silently reinterpreted. Where a shim's name
does *not* exist in GTK4 at all -- `set_filename`, `get_filenames`,
`list_filters` and the rest -- a plain macro is safe, because no GTK4 code can
be calling it.

### Capabilities dropped in this batch

| Feature | Why |
|---|---|
| `wxDataViewCtrl` drag and drop | See below |
| `wxFileDialog` preview (`wxFD_PREVIEW`) | Removed from `GtkFileChooser` along with the `selection-changed` signal, and the generic dialog wxGTK4 now uses does not implement it either -- so the flag is still ignored, on both counts |
| ~~`wxFileDialog` extra control~~ | **Recovered.** `gtk_file_chooser_set_extra_widget()` is indeed gone, but wxGTK4 no longer uses GTK's file dialog at all -- the generic one is a `wxDialog` and hosts an extra control like every other port that uses it |
| Shaped windows (`wxNonOwnedWindow::SetShape`) | `gdk_window_shape_combine_region()` has no replacement; GTK4's answer is transparency |
| `wxChoice::SetColumns()` | The popup's grid layout, and the wrap-width property controlling it, are gone |
| ~~`wxScreenDC` drawing and read-back~~ | **Recovered on X11.** GDK exposes no root window, but X11 has one and cairo can be pointed at it directly; on Wayland there is genuinely no screen to read |

`wxFileButton` and `wxDirButton` fall back to the generic implementation
because `GtkFileChooserButton` was removed -- but that is a plain button which
opens a file dialog, which is what GTK's own migration guide tells applications
to do now, and what wx already did for `wxFLP_SAVE`.

The dialogs those buttons open are generic under GTK4 as well, and that is
worth being explicit about because it is not only a deprecation question.
`GtkFileChooser` is deprecated, but its replacement `GtkFileDialog` does not
reliably open at all: on a machine whose portal produces no dialog it builds
its window, never shows it, and `ShowModal()` does not return -- no error, no
warning, no timeout. The portal is running and answers; it simply shows
nothing, so there is nothing an application can test for beforehand. Measured
in `probes/gtk4-filedialog-portal-hang.c`, and on wx itself: a `wxDirDialog`
on the native path hung wherever a session bus was present.

So `wxFileDialog` and `wxDirDialog` join `wxInfoBar`, `wxFileCtrl`,
`wxDataViewCtrl`, `wxComboBox` and `wxBitmapComboBox` on the implementations wx
already had. A deprecation warning is a message to the port's authors; a dialog
that never opens is a hang for the user.

### The one deliberate gap: dataview drag and drop

`GtkTreeView`'s own drag and drop, which `wxDataViewCtrl`'s model joins through
`GtkTreeDragSource` and `GtkTreeDragDest`, was rebuilt on `GdkContentProvider`
and `GValue`. The source half maps over cleanly. The destination half does not:
what GTK4 puts in that `GValue` for an application-defined MIME type is not
specified anywhere, and wx needs the raw bytes and their format, which is
exactly what a `GValue` of an unknown type cannot be asked for.

Rather than guess, `EnableDragSource()` and `EnableDropTarget()` return false,
which is the documented way for a port to report that it cannot do this.
Everything else about `wxDataViewCtrl` works, and `wxDropTarget`/`wxDropSource`
on the control as a whole are unaffected -- those go through `dnd.cpp`.

### Deriving from an opaque GObject type

`GtkCellRendererPixbuf`'s instance and class structs became private, so
`wxCellRendererPixbuf` could no longer be a C++ class with it as a base:
neither its size nor its layout is known at compile time. The sizes are asked
for with `g_type_query()` at run time instead and wx's one added field lives at
the end of the instance, addressed by an offset. This is the standard way of
deriving from an opaque GObject type and compiles identically under GTK3.

`GtkCellRenderer::render` also became `::snapshot`. wx renderers draw with
Cairo, and `gtk_snapshot_append_cairo()` hands out a context which renders into
the snapshot, so the existing drawing code needed no changes beyond being given
one. Where wx delegates to a GTK renderer, the snapshot is passed through so
that renderer draws into the same scene -- which is also why the RTL mirroring
had to move from `cairo_scale()` to `gtk_snapshot_scale()`.

### Where this leaves the port

The library compiles. It has never run. Every design decision above is
justified by the API and, where it was checkable without running wx, by a probe
or a CI invariant -- but the number of behaviours that can only be confirmed by
looking at a window is now the whole of the remaining risk.

Next: link `test_gui`, then the samples.

## Progress update 32: it runs

`test_gui` links, starts, and runs the suite. This is the first time any of
this port has been executed, and running it immediately found things that no
amount of compiling could:

| | GTK criticals per full run |
|---|---|
| First run | ~20,000 |
| After the scrollbar and size-event fixes | ~4,500 |
| After the CSS and version-guard fixes | 22 |
| Now | 1 |

The one that remains is GtkComboBox complaining about its own internal
`GtkStack` during the combobox selection test.

The suite reaches the rich text control before aborting on heap corruption,
with **18 failed assertions across 15 test cases** to that point. That is the
honest measure of where the port stands: it works well enough to run several
hundred test cases, and is not finished.

### The bug class the compiler cannot see

`G_CALLBACK()` casts a handler's type away. Every place this port changed a
signal name for GTK4 without checking the new signal's *signature* is a
silent, undiagnosable landmine, and two of them were:

- `toplevel.cpp` connected to GTK4's `map`, whose handler takes no event,
  while keeping GTK3's three-parameter `map-event` handler. The wxWindow
  pointer arrived in the wrong argument and the first window shown crashed.
- `notebook.cpp` nearly repeated it two commits later, caught only because
  the mistake was fresh.

Anything connecting a GTK4 signal in this port should be read with the
signature in hand, not just the name.

### The one that was worst

`gtk_check_version()` reports a version *mismatch*, not a version *ordering*:
it requires the major version to match exactly, so under GTK4 it answers
"incompatible" to every GTK 3.x requirement. This codebase asks it about sixty
times, always meaning "do we have at least this GTK3 feature level" -- so
every one of those guards was quietly running its oldest fallback.

That is how `GTKApplyWidgetStyle()` came to emit a raw Pango font description
as CSS, which GTK4's parser rejects, meaning **no wxWindow's font was being
applied at all**. It compiled, it ran, and it silently did the wrong thing. It
is shimmed centrally now, and both halves of the real function's behaviour are
recorded in `gtk4-invariants` so the shim cannot outlive its reason.

There is no telling what else in those sixty guards was affected; each is
worth a look.

### Model changes found only by running

| What | Why it could not be caught by compiling |
|---|---|
| `GtkScrollbar` is not a `GtkRange` | `GTK_RANGE()` compiles and fails at run time; every scrollbar operation was a no-op, so scrolling could not have worked |
| `GtkWidget` has no `size-allocate` signal | The connection compiles; no control was getting `wxEVT_SIZE` |
| `GtkEntry`'s editing signals belong to its private `GtkText` | Connecting to the entry compiles and never fires |
| `GtkCellRendererPixbuf` has `texture`, not `surface` -- and not `paintable` either | Attribute names are strings |
| GTK4's CSS parser requires a trailing `;` and a unit on the font size | Stylesheets are strings |

The last two are pinned by new checks in `gtk4-invariants`, now at 38 checks,
0 failed, and isolated in `docs/gtk/probes/gtk4-css-parser.c`.

### Next

1. The heap corruption in the rich text control -- the only thing stopping a
   complete run.
2. The 18 failing assertions, which are concentrated in the graphics and grid
   tests.
3. A sweep of the sixty `gtk_check_version()` guards, now that they evaluate
   the way they were written to.

## Progress update 33: the heap corruption, and a shim that was not reaching

`test_gui` gets much further. **Correction to what this section first
claimed:** it does *not* run to completion. 490 test cases are registered and
only 326 ran -- the run was still being cut short, by a later crash, and
Catch2's fatal-error handler prints a totals line that looks exactly like a
normal summary. Reading "test cases: 326" as the whole suite was wrong; it has
to be checked against `--list-tests`. See update 34 for where it actually got
to.

| | Test cases (of 490) | Assertions |
|---|---|---|
| Before | aborted in the rich text control, 15 cases failing | 18 failing |
| After this batch | 326 run, 309 passed, **17 failed** | 24907 run, **32 failed** |

### The heap corruption had nothing to do with rich text

`gtk_drop_target_async_new()` is annotated `(transfer full)` on its
`GdkContentFormats` argument: it takes ownership. `GtkRegisterWidget()` unreffed
the formats afterwards anyway, dropping the last reference and freeing them
while the drop target still held the pointer. Destroying the widget then
unreffed freed memory.

So **every wxWindow with a drop target was corrupting the heap on
destruction.** It surfaced in `wxRichTextCtrl` only because those tests create
and destroy a control 27 times in a row -- the heaviest widget churn in the
suite -- so that is where the damage first crossed the threshold glibc notices.
The abort came out of an unrelated `operator new`, in `setUp()`, pointing at a
control that was innocent.

Two things made it read as worse than it was:

- **The detection point is not the corruption point.** `malloc_consolidate():
  unaligned fastbin chunk detected` names the allocation that tripped over the
  damage, never the write that caused it.
- **The run hung rather than aborting.** Catch2's `SIGABRT` handler allocates
  in order to format its message, and glibc is still holding the malloc arena
  lock when it aborts, so the handler deadlocks. A hang at that point should be
  read as "already aborted", not as an infinite loop.

`valgrind` named it in one run, against the single test case, in under a
minute. The other GTK4 ownership transfers the port introduced --
`gdk_content_provider_new_union()` (takes the providers) and `gdk_drag_begin()`
(does not take the content) -- were rechecked against the GIR annotations and
are both right.

**The general lesson: ownership annotations are not in the headers.** A
`(transfer full)` argument compiles identically to a borrowed one, so the only
way to check is `/usr/share/gir-1.0/*.gir`, which is authoritative and
installed. Any GTK4 call this port introduces that takes or returns a
refcounted object is worth grepping there.

### The version shim was only reaching the files that opted in

Update 32 shimmed `gtk_check_version()` in `gtk3-compat.h`. But a call site
gets this wrong by *not* being adapted, so an opt-in header cannot catch it:
ten files call `gtk_check_version()` without including that header, and all ten
were still inverting. It has moved to `wrapgtk.h`, which everything gets.

Six of the ten were compiled out under GTK4 anyway. The four that were live:

| | What it was doing |
|---|---|
| `fontdlg.cpp` | Took neither branch, so `m_widget` was never assigned and `g_object_ref()` ran on it. **wxFontDialog could not work at all.** |
| `utilsx11.cpp` | `wxGetKeyState()` returned `false` for every key, unconditionally. |
| `control.cpp` | Emitted `style-updated`, which GTK4 removed, on every `wxControl::SetFont()` before realization. |
| `gauge.cpp`, `spinbutt.cpp` | Never applied the CSS that gives those controls their requested size. |

None of these is caught by the suite as it stands, which is worth stating
plainly: they were found by reading, and the run neither confirms nor denies
them.

### Next

1. The segfault after the summary line -- the suite completes and then dies on
   the way out, so something in shutdown is unsound. It does not reproduce on
   a single test case.
2. The 17 failing cases: paint/refresh delivery (`Window::Refresh`,
   `ClipperTestCase`, `ClippingBoxTestCase`), focus and event propagation, grid
   cursor, and text control positioning.
3. An ASAN build, which is being set up now. Everything above argues for it:
   the corruption fixed here was found only because valgrind happened to be
   affordable on one test case, and ASAN is cheap enough to leave on for the
   whole suite.

## Progress update 34: ASAN, and two more of the same bug

An ASAN build (`../wxbuild-asan`, same configure line plus
`-fsanitize=address`) is now the primary way to run the suite. It was worth
turning on immediately rather than waiting for the port to be finished: the
drop target bug in update 33 was found only because valgrind happened to be
affordable against a single test case, and ASAN is cheap enough to leave on for
the whole run.

It paid for itself on the first run.

| | Test cases (of 490) | Assertions |
|---|---|---|
| Update 33 | 326 | 24907 |
| After the frame clock fix | 375 | 25576 |
| After the pixbuf ownership fix | **445** | **39720** |

Failures are 20 cases / 38 assertions at that point. The run still does not
finish: it now aborts in `wxPersistTLW` on a GTK *internal* invariant,
`gtk_css_node_validate: assertion failed: (cssnode->parent == NULL)`, preceded
by `Unable to present ... unknown auxiliary child surface`. That is a widget
being left parented when GTK expects otherwise, and is the next thing to chase.

### Reading a truncated run as a finished one

Catch2 prints a totals line from its fatal-error handler that is
indistinguishable from the one it prints after a clean run. Update 33 read
"test cases: 326" as the whole suite; it was 326 of 490, cut short by a crash.
**Always compare against `--list-tests`.** The same mistake would have hidden
every one of the fixes below, because each of them only shows up as the run
getting *further*.

### The frame clock outlives the window

`GTKHandleRealized()` connects two `layout` handlers to the toplevel's
GdkFrameClock with `this` as the user data, and nothing disconnects them. Under
GTK3 the clock belonged to the toplevel's GdkWindow and died with it; under
GTK4 it belongs to the GdkSurface and outlives the wxWindow, so the handlers
keep firing against freed memory. ASAN named it precisely -- a wild-pointer
read of `m_needSizeEvent`, from a clock still driving a window destroyed
several test cases earlier.

This is a general shape worth looking for elsewhere in the port: **anything
GTK3 cleaned up by destroying the GdkWindow now needs doing explicitly**,
because the object that used to own it is gone.

### The same ownership bug, twice more

`wxBitmap(GdkPixbuf*)` stores the pixbuf without taking a reference -- it takes
ownership, which is why the GTK3 branch immediately below it does not unref.
The GTK4 branch unreffed anyway, leaving `m_pixbufNoMask` dangling for
`ConvertToImage()` to read through.

That is the same mistake as the drop target's content formats in update 33, in
unrelated code, written at a different time. Three instances now. The lesson
from update 33 stands and should be applied as a sweep rather than one bug at a
time: **ownership is invisible to the compiler and absent from the headers**,
and the only authority is `/usr/share/gir-1.0/*.gir` -- or, for wx's own
constructors, reading what they do with the pointer.

### The ASAN re-run, and what it ruled out

With the frame clock and pixbuf fixes in, the full suite under ASAN reports
**zero memory errors** across all 445 test cases it reaches. The abort at that
point is GTK asserting on its own state, not a memory error, which is a useful
negative result: nothing is being read after free up to there.

`wxPersistTLW` aborts on its own, so it is cheap to iterate on. Three attempts
did not move it, all recorded here so they are not repeated:

- Maintaining `wxPizza::m_children` (nothing did under GTK4). Real leak, real
  stale bookkeeping, committed on its own merits -- **not** this abort.
- Routing detach through `gtk_fixed_remove()` instead of
  `gtk_widget_unparent()`. Correct per GTK4's rules; no effect here.
- Removing a destroyed toplevel from its parent pizza's list. The test does
  create a parented `wxFrame` and `delete` it, so this looked exact. No effect.

What is known: the abort follows a `Gtk-WARNING` naming a **GtkWindow** where
the layout manager expects a child, and under `G_DEBUG=fatal-warnings` the
backtrace contains **no wx frames at all** -- it is entirely inside GTK's
allocate pass, driven from the main loop. So the bad state is established
earlier and only detected there, and the next step is to find which GtkWindow
and who parented it, not to keep guessing at destruction paths.

### Narrowing the abort with a reproducer

Guessing at destruction paths got nowhere; a 30-line reproducer got somewhere
immediately. Bisecting what the test does, one step at a time, isolated the
trigger to **a `wxFrame` with a parent, shown, then `Iconize()`d** -- dropping
any one of the three makes it survive.

From there a conditional breakpoint on `gtk_widget_set_parent()`, taken when
the parent is a `GtkWindow`, named the caller outright:
`wxGtkStyleContext::AddWidget()`.

That code builds a real widget tree to read style values off, and its comment
argued `gtk_widget_set_parent()` was safe "because these widgets are never
realized or size-allocated". That holds for every node except the first, which
is always a `GtkWindow` -- and **a GtkWindow is a toplevel GTK tracks and
validates whether or not it is ever shown**. Every style query in the port was
therefore a latent abort. Fixed by using `gtk_window_set_child()` for that one
case.

**Correction: that fix does not fix this abort.** The reproducer survived the
one run taken after the change, and that was read as a fix. It is not -- the
reproducer is *flaky*, and repeating it says so plainly:

| | Aborted |
|---|---|
| Without the change | 16 / 20 |
| With the change | 13 / 20 |

At n=20 that difference is noise. The change is kept because it is the correct
API regardless -- `gtk_widget_set_parent()` on a GtkWindow is wrong under GTK4,
and the code's own comment justified it with an assumption ("never realized or
size-allocated") that is false for a toplevel -- but **its effect on this abort
is nil as far as anything measured shows.**

Two lessons, the second more expensive than the first:

- **Check whether a reproducer is deterministic before using it to judge a
  fix.** A single passing run of a 65%-failure test is not evidence, and three
  earlier "this didn't help" conclusions in this section rest on single runs
  too -- they are weaker than they read.
- The `gtk_widget_set_parent()` call it found was still a real defect, so the
  bisect-and-breakpoint technique did work. It just found a different bug from
  the one being hunted.

A second attach path still emits the same warning once, and it is not a
`gtk_widget_set_parent()` call -- a breakpoint on that with a GtkWindow parent
never fires. GTK4 attaches popovers as "auxiliary children" outside the layout
manager, which is the wording the warning uses, and no popover shows up on this
path either. That is where it stands.

### Making the flaky one deterministic

The abort is now reproducible on demand, in about fifteen seconds, by
`docs/gtk/probes/wx-stylecontext-abort.cpp`. Getting there mattered more than
any single theory, because every "this didn't help" conclusion before it was
a coin toss.

What made it deterministic was **giving the main loop real elapsed time**. The
CSS validation GTK queues runs on a later frame clock pass; with only
`wxYield()` the process usually exited first, which is the whole of the
"intermittent" behaviour (13 aborts in 20). Adding `wxMilliSleep()` between
yields takes it to 8 in 8. Valgrind does the same thing by accident -- it
aborts 3 times in 3 there, purely from the slowdown, while reporting **no
invalid read or write at all**. So this is not memory corruption.

### `Iconize()` was a red herring

The failing test calls `Iconize()`, so that is what the reproducer was built
around. It is not needed: with the style queries alone and no `Iconize()`
anywhere, it still aborts 6 times out of 6.

The actual trigger, narrowed one variable at a time:

| | Aborts |
|---|---|
| One shown frame | 0 / 4 |
| Child frame **with a wx parent**, shown | **4 / 4** |
| Frame never shown | 0 / 4 |
| Second frame with **no** parent, shown | 0 / 5 |

So it takes a *parented*, *shown* `wxTopLevelWindow` existing while
`wxGtkStyleContext` builds and tears down its scratch widget tree. Two
unrelated toplevels are fine, so it is wx's handling of the parented one that
matters, not the window count. That also explains the reach of this: any style
query can abort an app in that state, which is why it stops the suite rather
than one test.

Ruled out, each re-tested against the deterministic reproducer rather than a
single run:

- Memory corruption (valgrind clean).
- `AddWidget()` populating a widget before attaching it, leaving a queued root
  validation -- reordering changed nothing.
- Tearing down the scratch toplevel -- leaking it deliberately changed nothing.
- A `GtkWindow` being passed as the *child* of `gtk_widget_set_parent()` -- a
  conditional breakpoint on exactly that never fires.

### The trigger, finally, is two things

Narrowing kept removing conditions until almost nothing was left. The abort
needs only:

1. a `wxTopLevelWindow` that has a **wx parent** and has been **shown**, and
2. enough elapsed time in the main loop for a frame clock pass.

| | Aborts |
|---|---|
| One shown frame | 0 / 5 |
| Child frame **with a wx parent**, shown | **5 / 5** |
| Parent frame never shown | 0 / 5 |
| Second frame with **no** wx parent, shown | 0 / 5 |
| As above, with **no style queries at all** | **5 / 5** |

Two unrelated toplevels are fine, so it is wx's handling of the *wx-parented*
one that matters, not the window count.

Everything else this hunt was built around turned out to be incidental --
`Iconize()`, `wxPersistenceManager`, and then the system colour queries, which
looked essential right up until removing them changed nothing. Each of those
was a real observation about the failing test; none of them was the cause.

Ruled out, each re-tested against the deterministic probe rather than one run:

- Memory corruption (valgrind clean while aborting 3/3).
- `wxPizza` tracking the toplevel in `m_children` without parenting it:
  removing the tracking entirely changes nothing.
- Creating and destroying a scratch `GtkWindow` per style query: sharing one
  across queries changes nothing, and the queries are not needed anyway.
- wx painting from inside GTK's snapshot vfunc. This one *is* real and worth
  fixing on its own -- `wxSystemSettings::GetColour()` builds and destroys a
  whole widget tree, rooted at a real `GtkWindow`, and a `gdb` breakpoint shows
  it being called from inside `pizza_snapshot()` -- but suppressing the paint
  does not stop the abort.
- `gtk_widget_set_parent()` with a `GtkWindow` as the child: a conditional
  breakpoint on exactly that never fires.

### Found: `gtk_widget_insert_after()` parents, it does not reorder

`RealizeTabOrder()` reproduces GTK3's `gtk_container_set_focus_chain()` by
physically reordering children with `gtk_widget_insert_after()`. Under GTK4
that call **sets the widget's parent**; it is not a reordering primitive. Run
over every focusable entry in `GetChildren()`, it therefore parented a child
`wxTopLevelWindow` -- which `wxPizza::put()` goes out of its way *not* to make
a GTK child -- into the parent's pizza, from `OnInternalIdle()`. A `GtkWindow`
with a CSS parent aborts the next time GTK validates it as a root.

Dumping the parent's widget tree says the whole thing in six lines:

```
GtkWindow
  GtkBox
    wxPizza
      GtkWindow      <-- a toplevel, parented inside its parent's client area
        GtkBox
          wxPizza
```

**`test_gui` now runs to completion: all 490 test cases, 470 passed, 20
failed.** That is the first genuinely complete run of this port.

#### What actually found it

Three false leads each came from taking the failing test at face value:
`Iconize()`, `wxPersistenceManager`, and the system colour queries were all
things the test did near the crash, and none was involved. Four more theories
died against the reproducer. What worked was mechanical, not clever:

1. **Reduce until conditions stop mattering.** Removing `Iconize()`, then the
   style queries, left two conditions: a shown, wx-parented toplevel, and
   elapsed time.
2. **Make it deterministic before judging anything.** 13-in-20 became 5-in-5
   once the main loop got real elapsed time.
3. **Ask the object, not the code.** A `notify::parent` handler on the child
   window with `G_BREAKPOINT()` named `RealizeTabOrder()` in one run, after
   breakpoints on `gtk_widget_set_parent()` and `gtk_fixed_put()` had both
   come back empty -- they were false negatives, because the parenting went
   through `gtk_widget_insert_after()`.

The transferable warning is the GTK4 one: **`gtk_widget_insert_after()` and
`gtk_widget_insert_before()` are parenting calls.** Anywhere this port uses
them to express ordering, it is also asserting ownership.

## Progress update 35: layout, coordinates, the frame clock, and focus

`test_gui` went from **470 passed / 20 failed** to **484 passed / 6 failed**
over this batch. Five findings did nearly all of it, and each of them is a GTK4
change that compiles cleanly and only shows up at runtime.

### wxPizza's layout code was never running

`gtk_widget_allocate()` dispatches `measure()` and `size_allocate()` to a
widget's `GtkLayoutManager` *instead of* its class vfuncs when it has one, and
`GtkFixed` -- wxPizza's base class -- installs a `GtkFixedLayout`. So
`pizza_measure()` and `pizza_size_allocate()`, which position every wx child
window, were dead code for the whole port, and `GtkFixedLayout` was laying
children out instead: at the origin, at their own measured size, which for a
wxPizza is 0x0.

Nothing about this is diagnosable from the code -- the vfuncs are assigned in
`class_init` exactly as under GTK3, and GTK simply never asks for them.
`gtk_widget_set_layout_manager(widget, nullptr)` in `wxPizza::New()` is the fix;
`put()` then cannot use `gtk_fixed_put()` either, since that reaches through the
layout manager. See `docs/gtk/probes/gtk4-layout-manager.c`.

This alone took the 83 repaint timeouts in the suite to zero.

### Invalidation does not reach children

GTK4 caches the render node each widget last produced, and invalidating one
widget does not invalidate its children. GTK3's `gdk_window_invalidate_rect()`
took an `invalidate_children` flag which wx always passed TRUE, so wx's contract
is that refreshing a region repaints everything inside it. `Refresh()` now says
it once per widget. Caching the node tree and replaying it on the GPU is the
whole point of the new rendering model -- this is not a workaround for GTK being
lazy.

### Layout and painting happen on the frame clock

This is the one with the widest blast radius. GTK4 lays out and paints in the
frame clock's phases, not synchronously, so:

* `wxWindow::Update()` was reduced to a `gtk_widget_queue_draw()`, which is not
  what it promises. Running the main loop does not help either: `wxYield()`
  returns long before the clock next ticks. It now asks for a frame and pumps
  until the clock reports one, bounded by a deadline so a window that is never
  presented cannot hang it.
* `ClientToScreen()`/`ScreenToClient()` asked GTK where each widget was, which
  is stale from the moment a window is shown, hidden or moved until the next
  frame. wx knows where it put its own children regardless, so it now walks its
  own parent chain; only the toplevel's client offset still comes from GTK.

### Window positioning is available after all, on X11

GTK4 removed `gdk_window_get_origin()` and `gtk_window_move()` together,
because a Wayland client is neither told where its window is nor allowed to
choose. X11 still permits both, and `XMoveWindow()` is exactly what GDK3 called
underneath `gtk_window_move()`. Without it wx reported a position nothing had
applied, so `ClientToScreen()` and `GetPosition()` disagreed. Both are still
no-ops under Wayland, which is all that is possible there.

### can-focus no longer means what wx means by it

GTK4 split GTK3's single `can-focus` flag in two and kept the old name for the
half wx does *not* mean:

* **`focusable`** -- whether the widget itself can take the focus. This is what
  `gtk_widget_grab_focus()` requires and what GTK3's `can-focus` meant. Defaults
  to FALSE for everything except the controls that are focusable by nature.
* **`can-focus`** -- whether the focus may enter the widget *or any of its
  children*. Defaults to TRUE for every widget.

So every `gtk_widget_set_can_focus()` call wx inherited became a no-op and every
`gtk_widget_get_can_focus()` test answered TRUE, which is why no wxWindow could
take the focus at all. `wx_gtk_widget_set_focusable()` in `gtk3-compat.h` now
carries the distinction.

Note it must set *only* `focusable` under GTK4: also clearing `can-focus` would
stop the focus reaching the children of the widgets wxTopLevelWindow marks as
unfocusable, which GTK3's single flag never did.

#### The consequence that cannot be fixed

Once wx windows are focusable, a GTK4 behaviour GTK3 did not have becomes
visible: when the widget holding the focus is destroyed, GTK4 does not simply
drop the focus. It remembers that the window needs one and hands it to the next
focusable widget added -- including one added before the next frame. A freshly
created wxWindow can therefore receive a `wxEVT_SET_FOCUS` wx never asked for.

There is no way out at GTK level: `gtk_window_set_focus(NULL)` does not cancel
it, and a widget with `can-focus = FALSE` is skipped by the restore but then
cannot be focused explicitly either. `build/tools/gtk4-invariants.c` pins all of
this, including that last point as a negative control.

Two test cases (`Window::Focus`, `wxDVC::SingleSelection`) fail because of it,
and only when run after a test that destroys a focused control -- both pass in
isolation. That is the trade for focus working at all, which it previously did
not.

### Other fixes in this batch

* **wxTextEntry::GetInsertionPoint()** -- `GtkEntry` does not report its caret.
  Its `GtkEditable` implementation answers out of the selection, and
  inconsistently: `gtk_editable_get_position()` returns the selection's *end*
  and the `"cursor-position"` property its *start*. The `GtkText` delegate
  underneath answers as GTK3 did, so ask that.
* **wxSearchCtrl** -- GTK4's `GtkSearchEntry` is not a `GtkEntry` any more, so
  none of the entry API this class is built on exists on it. It now uses the
  plain `GtkEntry` fallback the file has carried since GTK 3.0.
* **wxButton/wxToggleButton** -- `gtk_button_set_label()` *replaces* the
  button's child under GTK4, so calling it after the image had been arranged
  threw the image away.
* **wxSlider** -- `"format-value"` became `gtk_scale_set_format_value_func()`.
* **Paste** -- GTK4's paste is asynchronous; `wxTextCtrl::Paste()` now reads the
  clipboard synchronously through wxClipboard, as wx's contract requires.
* **Menu radio items** -- the first item of a radio group is checked by default,
  which nothing does for a `GAction`: a group is a stateful action whose state
  happens to match one member's target, and it starts matching none.

## Progress update 36: a yield has to settle the frame clock

`test_gui` is now at **488 passed / 2 failed of 490**, and runs in **3m20s
rather than 25 minutes**.

The runtime collapse and most of the remaining failures had one cause.
`wxYield()` no longer settled the interface: GTK3 laid out from idle handlers,
which running the main loop dispatched, so a yield did settle it -- that is what
wx callers have always assumed, and what the `wxYield(); // let GTK layout the
control` comments throughout the test suite are asking for. GTK4 lays out in the
frame clock's phases, which running the main loop does not advance. So a window
shown, hidden, resized or re-filled just before a yield was still stale when it
returned, and `PositionToCoords()`, `HitTest()`, scrollbar ranges and the rest
all answered out of date, with nothing indicating it.

`wxGUIEventLoop::DoYieldFor()` now asks each mapped toplevel's clock for a frame
and pumps until it has produced one, bounded so that a window which is never
presented cannot hang a yield. The suite got seven times faster because the
waits that used to time out now simply succeed.

### The focus restore, resolved

The GTK4 behaviour described in the previous section -- focus handed to the next
focusable widget added after the one holding it was destroyed -- turned out to
be recognizable, even though it cannot be declined at GTK level. Every wxWindow
carries a creation serial; when a focused one is destroyed the serial of that
moment is remembered, and a focus arriving at a window created after it, which
wx did not ask for, is the restore rather than anything real.

Two details make it work rather than merely move the problem:

* **Do not take the focus back.** `gtk_window_set_focus(nullptr)` does not
  cancel the pending move; it only makes GTK look for somewhere else to put the
  focus, which is the *next* window created. wx leaves GTK's idea of the focus
  alone and simply does not report it, ignoring the matching focus-out later.
* **Report it if wx later asks for it.** A declined window is still GTK's focus
  widget, so grabbing the focus for it produces no further focus-in;
  `SetFocus()` would have looked like it did nothing.

The mark is one-shot and is dropped at the end of the event loop turn, so it can
never suppress a focus change the user actually made.

### The two that are left

* **`wxDVC::SingleSelection`** -- and only when run after other tests. A
  `GtkTreeView` selects its cursor row when it gains the focus, and the focus it
  gains here is GTK's restore, which wx declines but cannot take back: the
  selection is made by GTK during the grab, before wx sees the focus-in at all.

  The selection-on-focus half is *not* a GTK4 regression -- a probe against the
  GTK3 build does exactly the same thing, `SetFocus()` on a wxDataViewCtrl
  leaving one row selected -- so there is nothing to fix in the dataview code.
  Nor can the focus be kept away from it: the restore lands on a `GtkTreeView`
  because that widget is focusable by its own class default, whatever wx does
  with the `focusable` flag, and marking it `can-focus = FALSE` would exclude it
  from the restore only by making it unfocusable outright.

  So this one is a second-order effect of a GTK4 behaviour that cannot be
  declined, sitting on top of wxGTK behaviour that is unchanged. Recorded
  rather than fixed.
* **`TextCtrl::HitTestSingleLine/Scrolled`** -- the horizontal scroll offset gap
  recorded earlier: a single-line entry's scroll position is not reachable
  through GTK4's API, so hit testing past the visible text is off. (Since
  closed -- see progress update 47.)

## Progress update 37: the samples run

All **76 samples build and run** under GTK 4.14, which is what all of this was
for. Two things had to be fixed to get there, neither of them large:

* `wxTaskBarIconBase::IsAvailable()` was left undefined. The whole of
  `src/gtk/taskbar.cpp` is compiled out under GTK4 -- `GtkStatusIcon`, which the
  class is built on, was removed with no replacement, since the freedesktop
  system tray protocol is not something GTK implements any more -- and the stub
  that remains did not cover this one function. Every program that so much as
  mentions wxTaskBarIcon failed to link. It now returns false.
* `samples/widgets/native.cpp` handed a `GtkMenu` to a `GtkMenuButton`. Under
  GTK4 that takes a `GMenuModel`, and wxMenu is built out of one, so it can be
  passed directly.

Spot checks with a screen capture confirm the samples render properly: the
widgets sample's tree, static boxes, log window and menu bar; the richtext
sample's toolbar, images, styled text and style list; the AUI demo's docked
panes, notebooks and toolbars.

## Progress update 38: the ownership audit

Whether a GTK function hands you a reference to release, or takes one from you,
is the one thing about the API the compiler cannot check and the headers do not
say: it lives only in the GObject-introspection annotations, in
`/usr/share/gir-1.0/*.gir`. A function whose signature is unchanged can have
swapped sides between GTK3 and GTK4, and the symptom is a leak or a double free
much later, never a compile error.

Two of the nastiest bugs in this port were exactly that, so rather than wait for
the next one, compare the annotations directly.
`build/tools/gtk4-ownership-diff.py` reads the Gtk-3.0 and Gtk-4.0 `.gir` files,
collects every G-prefixed function named anywhere in `src/gtk` and
`include/wx/gtk`, and reports the ones where the two disagree.

**Of the 1813 functions wxGTK calls, exactly four changed.** That is the useful
part of the result: the surface that needed checking turned out to be tiny and
is now known, rather than suspected.

| function | change | outcome |
| --- | --- | --- |
| `gtk_file_filter_new` | was `(transfer none)`, now `(transfer full)` -- and `GtkFileFilter` stopped being a `GInitiallyUnowned` | **real leak, fixed** |
| `gtk_file_chooser_add_filter` | `filter` was `(transfer full)`, now `(transfer none)` | the other half of the same change |
| `gtk_calendar_get_date` | returns a `GDateTime` the caller owns, in place of out-parameters | already handled in `calctrl.cpp` |
| `gtk_tree_drag_source_drag_data_get` | returns a `GdkContentProvider` the caller owns | not called; appears only in a comment |

The file filter is worth spelling out because nothing about it is visible in the
code. Under GTK3 `GtkFileFilter` derived from `GInitiallyUnowned`, so the
reference `gtk_file_filter_new()` returned was floating and
`gtk_file_chooser_add_filter()` sank it; the filter had exactly one owner and
callers correctly did not unref. Under GTK4 it derives from `GtkFilter`, which
is a plain `GObject`. The reference is real, `add_filter()` is now
`(transfer none)`, and the unchanged wx code leaked one reference per filter --
every time a wxFileCtrl's wildcard was set. No annotation on `add_filter()`
alone would have shown this: it takes the two halves together.

The checker runs in CI next to the invariants program and lists the four
differences as reviewed, so a run that prints nothing new means nothing new has
appeared -- after a GTK update, or when the port starts calling an unfamiliar
part of the API. Its own negative control is that removing an entry from the
reviewed list makes it fail.

### Not GTK4's doing

The scan also turned up two leaks that are identical under GTK3 --
`gtk_entry_completion_new()` in `textentry.cpp` and `gtk_list_store_new()` in
`bmpcbox.cpp` both create an object the widget then only takes its own reference
to, and wx never releases its own. They are pre-existing upstream bugs rather
than port regressions, and are left alone here so this branch does not change
GTK3 behaviour, but they are worth reporting separately.

## Progress update 43: the second round of bug reports

Seven more bugs came back from the human eyeballing the samples. Three had
already been fixed between the build being tested and this one; one was the
documented behaviour; three were real and are fixed here, and finding them
turned up two more nobody had reported.

### One press, several wxEVT_LEFT_DOWN

> aui sample: seem not to be able to dock or undock panes

A `GtkEventControllerKey` lives on the focus widget and fires only for it,
which is why the key handlers dropped GTK3's `EventAlreadyProcessed()` guard
and were right to. A `GtkGestureClick` is not like that. In the default BUBBLE
phase a press nobody claims -- which is exactly what an unhandled press is --
is offered to every ancestor in turn, each with its own gesture, and wx's
callback then re-targets it with `FindWindowForMouseEvent()`. One physical
click reached the same wxWindow once per ancestor.

Measured with a wxWindow inside a wxFrame: `LEFT_DOWN` twice, `LEFT_UP` once.
Two downs for one press breaks anything that toggles state on a click.

The guard is back for the pointer, keyed on the `GdkEvent` and holding a
reference to it so the next event cannot be allocated at the same address and
be mistaken for it.

### The frame that lost its client area

Fixing the presses was not enough: floating a pane still filled the log with
`invalid unclassed pointer in cast to 'wxPizza'` and often segfaulted in
`wxWindow::AddChildGTK()`.

`wx_gtk_widget_remove_from_parent()` unparented the child whatever its parent
was. Its own comment said not to use it on a single-child container, which
keeps its own pointer to the child -- and `wxMiniFrame::Create()` did exactly
that, on the `GtkWindow`, while rearranging the frame's contents into a
`GtkOverlay`. GtkWindow's child pointer was left dangling, and the
`gtk_window_set_child()` a few lines later unparented it a second time. By
then `m_mainWidget` was in the overlay, so it was torn straight back out of
it, taking the frame's client area with it.

**A wxAuiFloatingFrame is a wxMiniFrame under wxGTK**, so every pane that tried
to float was reparented into a frame whose `m_wxwindow` pointed at a destroyed
widget.

The helper now asks the container to let go -- `GtkWindow`, `GtkButton`,
`GtkFrame`, `GtkScrolledWindow`, `GtkExpander` and `GtkPopover` each get their
`set_child(nullptr)` -- rather than leaving it to each call site to remember.
wxAnyButton was unparenting a GtkButton's child the same way.

### Properties addressed by string

Two found while chasing the above, neither reported, both silent:

* `wxActivityIndicator::IsRunning()` read GtkSpinner's `"active"`, which GTK4
  renamed to `"spinning"`. `g_object_get()` on a property that does not exist
  is a runtime warning that leaves the destination untouched, so the function
  returned whatever was on the stack. It calls `gtk_spinner_get_spinning()`
  now, which cannot be got wrong this way.
* `wxCAL_NO_MONTH_CHANGE` did nothing: GtkCalendar's `"no-month-change"` was
  removed outright, so the month and year arrows stay usable under GTK4.
  `EnableMonthChange()` says so by returning false instead of claiming success.

`build/tools/gtk4-property-names.py` makes the class checkable. Where the
object is written as a `GTK_FOO()` cast it looks the property up on that exact
type and its ancestors -- which is what catches a rename to a name some other
class still uses -- and otherwise reports names that exist in GTK3 and nowhere
in GTK4. Only the call's own first argument is considered: taking casts from
nearby lines produced two false positives, a GtkLabel checked as a GtkButton
and a cell renderer checked as a GtkCellLayout.

### Already fixed, and not a bug

The menu sample's scrolling assert, the anim sample's close button and its
about box were all fixed in the previous round, between the build that was
tested and this one; re-running each against the current tree confirms it. The
`Gdk-CRITICAL`s on entering a panel went with the same commit that gave the
mouse wheel a position -- verified here by driving the pointer across the
widgets sample with XTest and finding the log clean.

"Set inactive bitmap" remains what it was: the documented behaviour, since the
sample starts playing immediately and the inactive bitmap is what shows when it
is not.

### Two leaks that were not the port's doing

The ownership audit turned up `gtk_entry_completion_new()` in `textentry.cpp`
and `gtk_list_store_new()` in `bmpcbox.cpp` both leaking a reference -- the
widget takes one of its own, and wx never dropped the one it had. Identical
under GTK3, so they were reported separately rather than folded into the port,
and then fixed on request.

### Next
2. Style queries from inside the snapshot vfunc (above): wx builds and destroys
   a `GtkWindow` per `GetColour()` call, during painting. Not this bug, but not
   right either.
3. The two remaining test failures, both described above.

## Progress update 39: the components the reduced configure line was hiding

Every build recorded above used the reduced configure line from "How this
was produced" -- `--without-opengl --disable-stc --disable-uiactionsim`,
in a container that also had no gstreamer or WebKitGTK development
packages installed. A plain `../configure --with-gtk=4` turns all of that
back on, and then seven more files fail to compile. None of them is a
regression: they are the parts of the tree the port had never actually
built.

- **`src/stc/ScintillaWX.cpp`**: `GdkKeymap` is gone. The Ctrl-shortcut
  fallback that walks keyboard groups looking for a Latin letter now uses
  `gdk_display_map_keycode()`, which returns every (group, level) entry
  for a keycode in one call instead of being queried one at a time.

- **`src/unix/uiactionx11.cpp`**: the file update 7 above deferred. Its
  two `GdkWindow` uses turned out to be small: the input-focus workaround
  takes the XID from the top level's `GdkSurface` via `GtkNative`, and
  the pointer-position check uses
  `gdk_device_get_surface_at_position()`, added to the
  `wx_gdk_device_get_window_at_position` shim in `gtk2-compat.h` (which
  had been left expanding to a nonexistent function under GTK4).

- **wxWebView** was the one genuine library-version problem rather than a
  port gap. `configure.ac` had no GTK4 branch and so selected
  `webkit2gtk-4.1`, the GTK3 build of WebKitGTK, whose headers refer to
  `GtkContainer`, `GtkAction` and `GdkEventKey` and therefore cannot be
  included in a GTK4 program at all. GTK4 needs `webkitgtk-6.0`, which is
  a separate API version, not just a rebuild.

  The migration itself was mostly mechanical and was driven off a
  mechanical diff of the two GIR files (`WebKit2-4.1.gir` versus
  `WebKit-6.0.gir`) rather than by chasing compiler errors, which is what
  turned up the two signal signature changes -- `context-menu` lost its
  `GdkEvent` parameter and `script-message-received` now delivers a
  `JSCValue` instead of a `WebKitJavascriptResult`. A callback signature
  mismatch is not a compile error, so neither would have been caught by
  building. The substantive changes are that website data and proxy
  settings moved from `WebKitWebContext` to the new
  `WebKitNetworkSession`, that `webkit_web_view_new_with_context()` and
  `_with_related_view()` became construct-only properties, and that the
  deprecated JavaScriptCore C API is not part of the GTK4 API, so script
  results are read through JSC's GObject API instead.

- **The wxWebView web process extension** was the one place with no
  mechanical answer. The whole WebKitDOM binding set it was written
  against was removed from WebKitGTK, with no replacement: the supported
  way to inspect a page from a web process extension is now to run
  JavaScript in it. All seven of its D-Bus methods were reimplemented
  over `jsc_context_evaluate()` on the page's main frame. The D-Bus
  architecture is unchanged and still worth having -- it is what makes
  `GetPageSource()` and friends synchronous, which
  `webkit_web_view_evaluate_javascript()` in the UI process is not.

- **wxMediaCtrl's GStreamer backend** and **wxGLCanvas** hit the same
  wall from opposite directions: both need a native window to hand to
  something outside GTK (a `GstVideoOverlay` window handle, an EGL
  surface), and under GTK4 only top levels have one. The GTK3 code
  already had to solve this for Wayland, where subsurfaces made
  per-widget windows unavailable too, so in both cases the GTK4 path is
  the existing Wayland path generalized: take the top level's surface and
  confine the content to the widget's area, computed with
  `gtk_widget_compute_bounds()` plus `gtk_native_get_surface_transform()`
  for the CSD shadow offset.

  Two GTK4 signals had to be replaced along the way: `size-allocate` is
  gone, so wxGLCanvas forwards its own `wxEVT_SIZE` to a new
  `wxGLCanvasUnixImpl::OnSizeChanged()`, and wxMediaCtrl repositions from
  `Move()`; and `map-event` is gone, but plain `map` is emitted late
  enough for the top level's surface to exist.

### GLX cannot be used under GTK4

GLX needs the window to have been created with the visual matching the
chosen fbconfig. GTK4 has no `GdkVisual`, no `GdkScreen` to look one up
on, and no per-widget window to apply one to, so the `parent-set`
emission hook in `src/gtk/glcanvas.cpp` that did this has nothing left to
hook. `wxGLBackend::Init()` therefore returns the EGL backend
unconditionally under GTK4 and `wxGLCanvas::PreferGLX()` has no effect
there. This is a property of the two APIs, not something left undone.

### Known limit: X11 and the top level surface

On Wayland this design is correct: the canvas gets its own `wl_surface`
as a subsurface of the top level's, positioned over the widget, and the
compositor clips it. On X11 there is no equivalent, so the EGL surface is
the top level's X window and a `wxGLCanvas` that does not fill its window
will draw outside its own bounds. Confining it would mean creating a
child X window with `XCreateWindow()` behind GTK's back -- new machinery
rather than a port of existing code, and aimed at the backend that is on
its way out. Deliberately not done.

## Progress update 40: gspell was linking GTK3 into the GTK4 build

Found while checking what the newly-built libraries actually link against,
not by anything failing to compile. `configure.ac` gated the spell
checking dependency on `$WXGTK3 = 1` -- the identical oversight as the
webkit block in update 39, since `WXGTK3` is 1 for GTK4 too. The result
was that **every one of the sixteen libraries linked both libgtk-4 and
libgtk-3**, `src/gtk/textctrl.cpp` was compiled with
`-I/usr/include/gtk-3.0` ahead of `-I/usr/include/gtk-4.0`, and
`wxTextCtrl::EnableProofCheck()` handed a GTK4 `GtkTextView*` to
`gspell_text_view_get_from_gtk_text_view()`, a GTK3 function. That
compiles because `GtkTextView` is an opaque struct with the same name in
both.

gspell has no GTK4 version. Its successor, libspelling, is not a drop-in
replacement: `spelling_text_buffer_adapter_new()` takes a
`GtkSourceBuffer`, and `libspelling-1` requires `gtksourceview-5`.
wxTextCtrl's multi-line widget is a plain `GtkTextView` over a plain
`GtkTextBuffer`, and its single-line widget is a `GtkEntry`, for which
libspelling has nothing at all -- there is no equivalent of `GspellEntry`.
So the adapter cannot be used here.

What libspelling does offer, and all wx now uses, is the dictionary:
`SpellingChecker` answers "is this word spelled correctly" and "what did
they mean", independently of any view. `wxTextCtrlSpellCheck` in
`textctrl.cpp` does the rest:

- Multi-line: word boundaries come from `GtkTextIter`, so Pango's word
  breaking is reused rather than reimplemented, and misspellings get a
  `GtkTextTag` with `PANGO_UNDERLINE_ERROR`.
- Single-line: a `GtkEntry` has no tags, so the same underline is applied
  as a `PangoAttrList` over byte ranges via `gtk_entry_set_attributes()`.
- Tokens that aren't words are not offered to the dictionary at all, so
  `42` and `x86` don't come back as errors.
- A per-control `spelling_checker_new()` rather than
  `spelling_checker_get_default()`, because setting the language on the
  shared default would change it for every other user of the library in
  the process.

Verified at runtime rather than only by building: with the text "The
quick brown fox hlepd zzzqqx 42 x86 over the lazy dog", exactly `hlepd`
and `zzzqqx` come back underlined; replacing the text re-scans; disabling
removes the marks; and the entry gets a non-null attribute list. `ldd`
now reports no libgtk-3 in any of the sixteen libraries.

### Not done

Grammar checking has no backend here and never did under GTK3 either.

The corrections context menu was left out of the first cut of this work
and is added in update 41 below.

### Caveat on these two updates

All of this was built and tested against GTK4 only. The GTK2/GTK3 paths
are preserved behind `#ifdef` and were not rebuilt, so they are unverified
by construction rather than by test.

## Progress update 41: the corrections menu, and a GtkEntry bug

Finishes the spell checking of update 40 by adding back the part gspell
gave us for free: right-clicking a misspelling offers corrections, "Add
to Dictionary" and "Ignore".

The GTK3 way of doing this is not available. GTK4 removed
"populate-popup" along with GtkMenu -- a text widget's context menu is a
GtkPopoverMenu built from the GMenuModel given to `set_extra_menu()`, and
**nothing of ours runs while it is up**, which the comment above
`gtk_textctrl_populate_popup()` in `textctrl.cpp` already noted for a
different reason. So the menu cannot be filled on demand: the
corrections have to be computed and installed *before* the popover is
built.

That is done from the secondary click which is about to open it, using a
GtkGestureClick in the **capture** phase so it runs ahead of the widget's
own handler for the same press. For the keyboard paths (Menu, Shift+F10),
which have no pointer position at all, the menu is instead kept up to
date from `notify::cursor-position`. That would be far too expensive if
it looked up corrections on every cursor movement, so it first asks the
underline tag whether the cursor is even on a misspelling -- which is
free, the tag already recording exactly that -- and only then goes to the
dictionary.

Applying a correction goes through a `GSimpleActionGroup` inserted on the
widget as "spelling", the menu items carrying the replacement as their
action target. The word being corrected is remembered as a pair of
GtkTextMarks rather than offsets, so it survives the buffer changing
underneath the open menu.

### GtkEntry has no hit testing under GTK4

The multi line case hit tests the pointer exactly, with
`gtk_text_view_window_to_buffer_coords()` and
`gtk_text_view_get_iter_at_location()`. There is no equivalent for a
GtkEntry: `gtk_entry_get_layout()` and `gtk_entry_get_layout_offsets()`
were removed in GTK4, the text being laid out by an internal GtkText the
entry delegates to, and nothing public replaces them. So entries get no
gesture at all and rely on the cursor path above, which works because
GtkText moves the cursor to the click before showing its menu, firing the
notify we are watching for while there is still time to install the menu.

### A GTK bug worth knowing about

`gtk_entry_set_extra_menu(entry, NULL)` -- the documented way to clear the
menu -- makes GTK 4.23.2 print

    g_object_ref: assertion 'G_IS_OBJECT (object)' failed

on **every** call, because the setter refs the model without checking it
first. Reproduced in twenty lines of plain GTK with no wx involved;
`gtk_text_view_set_extra_menu(view, NULL)` is unaffected, so this is
specific to GtkEntry. It is harmless, `g_object_ref()` returning null
after complaining, but it would appear in the log of every application
using a single line wxTextCtrl with spell checking, roughly once per
cursor movement. Worked around by using an empty GMenu rather than null
to mean "nothing to offer", which contributes no items to the popover and
stays out of the buggy path. Worth reporting upstream.

### Verified

A test drives the menu through the cursor path for both widget kinds and
checks what actually comes out: that no menu is offered away from a
misspelling; that on "hlepd" the model contains `spelling.correct` items
including "helped", plus `spelling.add` and `spelling.ignore`; that
activating `spelling.correct` rewrites exactly that word and leaves the
rest of the text alone ("a hlepd word" becomes "a helped word"); that
`spelling.ignore` makes the word stop being flagged; that the capture
phase gesture is installed on the text view and deliberately not on the
entry; and that a wxTE_PASSWORD entry is not checked or underlined at
all. All of it runs clean, with no GLib criticals.

Two bugs of our own turned up while testing this. Reading the entry's
text at a stale cursor offset ran `g_utf8_offset_to_pointer()` off the
end of the string, the text and the cursor position being separate
properties which are not updated together; both places that indexed by
character offset now check the length first.

## Progress update 42: the first bug reports

The port having been eyeballed by a human for the first time, five bugs came
back. Four were real and are fixed; one turned out to be the documented
behaviour. They are collected in `docs/gtk/probes/wx-gtk4-reported-bugs.cpp`,
one mode each, because every one of them needs real input or real window
manager messages to reproduce -- injected with XTest and X client messages,
the way `gtk4-gesture-semantics.c` already did.

### The window that would not close

> I click the "x" and the application doesn't exit. File/Exit works, though.

The one that took the longest to find, and the most interesting. It only
happened once the anim sample's animation had been **stopped**: with it
playing, the close button worked. The application was not hung -- its menus
opened, its update-UI handlers ran -- it simply ignored the close button.

Closing a top level window is deferred. `wxTopLevelWindowBase::Destroy()`
appends the window to `wxPendingDelete` and leaves the actual deletion to
idle time, and it deliberately does not even hide the window if it is the
last visible one, on the grounds that hiding it might stop idle events from
arriving. Under GTK4 they had already stopped: wx's idle source removes
itself when there is no idle work left and is re-armed by `WakeUpIdle()`,
which nothing calls for an event handled entirely inside a GTK callback.

GTK3 covered this with a one-shot emission hook on `GtkWidget::event`, which
GTK4 does not have -- and the port had left `wx_add_idle_hooks()` with
nothing at all to install. `GdkSurface::event` is the replacement: one level
below the widgets, emitted for every event GDK delivers to a surface
whatever widget it ends up at, and hookable. For re-arming an idle handler
that is exactly as good, since all it needs to know is that something
happened.

The animation timer was what hid this: while it ran, idle time kept coming.

### The mouse wheel

Scrolling asserted in `wxRound()`, "argument out of supported range". A GTK4
`GdkScrollEvent` carries its deltas and no position -- the pointer has not
moved -- so `gdk_event_get_position()` returns false for one, and writes NaN
into both out parameters rather than leaving them alone. NaN compares false
against both bounds of the range check.

The position such an event happened at is where the pointer is, so ask the
seat. `wxGTKImpl::GetEventPosition()` now does that for any event without a
position of its own, which is also what the leave handler's comment claimed
it was doing while doing the opposite.

### The about box

It opened once and never again. `wxAboutBox()` caches the dialog and cleared
that cache from a "response" handler; GTK4's `GtkAboutDialog` derives from
`GtkWindow` rather than `GtkDialog` and has no such signal, so
`g_signal_connect()` failed at runtime and carried on. Meanwhile an
unhandled "close-request" destroys the window, so the cache pointed at freed
memory and the second call wrote its properties into it. A "destroy" handler
replaces the response one under GTK4.

### The dialogs that would not close either

Found while chasing the first bug: the colour, font, file and directory
dialogs ignored the window manager's close button entirely, and being modal,
so did the application for as long as they were up.

GTK4 gives one of two things for "close-request", never both:

| handler returns | window survives | "response" emitted |
| --- | --- | --- |
| GTK3 delete-event, TRUE | yes | **yes** |
| GTK4 close-request, TRUE | yes | no |
| GTK4 close-request, FALSE | no | yes |

`wxDialog::ShowModal()` connected `gtk_true()`, wanting the first row and
getting the second: the dialog stayed, and nothing ended the modal loop. It
now calls `Close()` from that handler instead, which for a modal dialog
means `EndModal(wxID_CANCEL)`. Only the dialogs that build their `m_widget`
themselves ever reach it -- for every other one wxTopLevelWindowGTK's own
handler runs first and stops the emission.

### The animation that filled its control

The background colour of a wxAnimationCtrl had no visible effect once an
animation was loaded, because GTK4's `GtkImage` scales what it is given up
to the size of the widget -- it is documented as being for icons. GTK3's
drew it at its natural size, centred. The anim sample's 32x32 throbber, in a
control the sizer had made 100x100, was being scaled up by three.

`gtk-image-natural-size.c` measures all three cases by reading the corner
pixel off the screen, over a green background:

| | corner | centre |
| --- | --- | --- |
| GTK 3.24.41 `GtkImage` | #00ff00 background | #ff0000 image |
| GTK 4.14.5 `GtkImage` | #ff0000 image | #ff0000 image |
| GTK 4.14.5 `GtkPicture`, content-fit SCALE_DOWN | #00ff00 background | #ff0000 image |

So the control uses a `GtkPicture` with `GTK_CONTENT_FIT_SCALE_DOWN`, which
is GtkImage's old behaviour: natural size, centred, scaled down only when
the image does not fit.

This is worth remembering beyond wxAnimationCtrl: **any GTK4 `GtkImage`
given something larger than an icon will stretch it**, including the one
`wxGtkImage` uses under GTK4. wxStaticBitmap is the case that shows: a 32x32
bitmap in a control sized 100x100 fills the control, corner pixel and all,
where it should sit in the middle of the control's background. It is not
fixed here -- it needs the same GtkPicture treatment, one level down in
`wxGtkImage`, where the button, toolbar and menu users of that class also
have to be checked -- and it was not reported, since nothing anyone looked
at yet has a static bitmap bigger than its bitmap.

### And the suite

`test_gui` is unchanged at **488 passed / 2 failed of 490**, the two being
the `wxDVC::SingleSelection` and `TextCtrl::HitTest` failures already
described. (`wxTopLevel::Show` fails as well if the suite is run without
`wxUSE_XVFB=1`: the test only skips its `IsActive()` check when that
variable says there is no window manager.) The `grab-notify` criticals a
text entry with completion used to print for itself are gone too: the signal
went with GTK4's grabs, and nothing says when a GtkEntryCompletion popup is
up any more, so wxTE_PROCESS_ENTER now stays on while it is.

### Not a bug

"Set inactive bitmap" appearing to do nothing is the documented behaviour:
the inactive bitmap is what the control shows when it is *not* playing, and
the anim sample starts playing immediately. The probe's "anim" mode confirms
it appears as soon as the animation is stopped.

### What the test suite could not have caught

The window-close bug is a good illustration of a blind spot. Every probe
mode here except "quiet" calls `wxYield()` while waiting, and yielding runs
idle processing by hand -- which is precisely the thing that had stopped
happening on its own. `test_gui` yields constantly, so it passed throughout.
Removing the hook again makes only the "quiet" mode fail.

## Progress update 44: the caret, and a warning that was never wx's

Three more reports came in after a real application -- wxMaxima -- was built
against the port and used. All three are about the caret sample; two were
real, deep bugs and the third turned out to belong to GTK.

### The caret drew into a single pixel

> Caret sample: The caret is invisible

`wxCaret` draws through a `wxClientDC`, and under GTK4 a `wxClientDC` cannot
reach the screen: there is no `GdkWindow` to draw on outside a snapshot, so
`CanBeUsedForDrawing()` returns false and the caret has to go through
`wxOverlay` instead, which captures what is drawn as a cairo group and shows
it in a widget of its own. That much was already in place. What was not is
that the DC underneath had no real surface: it was built on
`wxGraphicsContext::Create()`, whose context is a **1x1 measuring surface**,
and a 1x1 clip discards everything drawn outside that one pixel. So the
group the overlay captured was empty every time.

That is a different failure from "the drawing is not shown", and it is worth
stating separately: **a wxDC that cannot draw to the screen still has to be
able to draw**. `wxWindowDCImpl` and `wxClientDCImpl` now build their
graphics context on an image surface the size of the window
(`wxGTKSetSurfaceGraphicsContext()` in `dc.cpp`), so anything drawn on them
lands somewhere real and `wxOverlay`, `wxCaret` and `wxDragImage` can pick it
up. Measured on a standalone probe, the same overlay went from 0 red pixels
to 7197.

### The caret that never blinked

With the drawing fixed the caret was permanently on. The loop:

* `wxCaretSuspend` hides the caret at the top of a paint handler and shows it
  again at the bottom, so that the window does not draw over it. That is what
  the caret sample does, and what any window drawing under a caret must do.
* Hiding and showing the caret each call `Refresh()`, which redraws the
  overlay and queues a draw of it.
* Under GTK4 `gtk_widget_queue_draw()` on a child marks every ancestor as
  needing to be drawn too, so the wxPizza above it re-runs its snapshot --
  which is where wx generates `wxEVT_PAINT`.
* ... so the paint handler runs again. And `DoShow()` restarts the blink
  timer every time, so the timer never reached its 500 ms.

`wxCaretSuspend` has an empty implementation for exactly this situation,
selected by `wxHAS_CARET_USING_OVERLAYS`, but nothing has ever defined that
macro. `wx/generic/caret.h` now defines it for GTK4, where
`wxOverlay::Create()` always returns a native implementation and the caret
therefore never draws on the window it belongs to. Suspending it is not
merely unnecessary there, it is what stopped it blinking.

### The caret that stayed where it was first put

Then the caret blinked, but always in the position it had been created at.
`wxPizza::move()` records the new position in wxPizza's own child list and
deliberately does *not* queue a resize:

```
// normally a queue-resize would be needed here, but we know
// wxWindowGTK::DoMoveWindow() will take care of it
```

which is true for every caller except one. The overlay is not a wxWindow, so
nothing queued the resize on its behalf and the new position never reached
the widget's allocation. `PositionOverlay()` now calls
`gtk_widget_queue_allocate()` itself after a move -- and only after a move,
which is what the `m_placedRect` bookkeeping there is for: repositioning on
every blink would repaint the whole window twice a second for nothing.

`Reset()` also no longer hides the overlay. With no surface the widget draws
nothing anyway, and a widget that has just been shown has no allocation until
the next layout pass, which is what produced

> Trying to snapshot GtkDrawingArea 0x... without a current allocation

when an overlay was reset and drawn on again from inside a paint handler.

### The slider warning is GTK's

> caret sample: Warning at startup
> GtkGizmo 0x... (slider) reported min width -2, but sizes must be >= 0

This one is not wxWidgets' at all, and the reporter's guess that GTK3 shows it
too is close: it is not a wx bug on either. It reproduces here with a theme
whose `slider` node has no minimum size and no border --

```css
slider { min-width: 0; min-height: 0; border: 0; }
```

-- and it reproduces in a **plain GTK4 program with no wxWidgets in it**: a
`GtkScrolledWindow` with a label in it and its scrollbar policy set to
`ALWAYS` prints the same four warnings, two scrollbars measured in two
orientations each. The negative number comes out of GtkRange's own slider
measurement.

There is nothing wx can do about it, in either direction. It cannot avoid
triggering it -- `wxSYS_VSCROLL_X` has to measure a scrollbar, and any
scrolled window measures its own -- and it cannot suppress it either: GTK4
logs through `g_log_structured()`, which does not consult the handlers
`g_log_set_handler()` installs, and taking over the writer with
`g_log_set_writer_func()` is an application's decision, not a library's.

What it can do is record it, so the next person to see it does not go looking
in wx. `gtk4-invariants.c` grew a `test_scrollbar_metric()` that measures an
unparented `GtkScrollbar` -- the thing `GetScrollbarWidth()` in `settings.cpp`
relies on -- and then applies that CSS and checks that GTK still reports the
negative size for a scrolled window of its own. It takes over the log writer
to count the warnings rather than print them, which is a thing a test program
may do.

### Found while testing: a container hears about its children's focus

Not reported, not fixed, and recorded here so it is not rediscovered from
scratch. A wxPanel with a focused control inside it prints

> window wxPanel@0x... ("panel") lost focus even though it didn't have it

in a debug build, and receives a `wxEVT_SET_FOCUS` and a `wxEVT_KILL_FOCUS`
of its own that GTK3 never sent it. GTK3's `focus-in-event` went to the
widget that took the focus. GTK4 has no such event: the port uses a
`GtkEventControllerFocus`, whose `::enter` fires when the focus enters **the
widget or any of its descendants**, and whose `::leave` fires only when it
leaves that whole subtree. So an ancestor is told the focus arrived, is not
told when it moves on to a child, and is told it left much later -- by which
time `gs_currentFocus` is somebody else, which is the message above.

The obvious filter -- ask which widget actually has the focus and ignore the
notification if it belongs to a different wxWindow -- does not work, because
**none of that is settled when `::enter` is emitted**. Measured at that
point, on GTK 4.14.5:

| | at `::enter` |
| --- | --- |
| `gtk_event_controller_focus_contains_focus()` | TRUE |
| `gtk_event_controller_focus_is_focus()` | FALSE |
| `gtk_widget_has_focus()` | FALSE |
| `GTK_STATE_FLAG_FOCUSED` | not set |
| `gtk_root_get_focus()` | NULL |

`is_focus()` is no use even later: for a `GtkEntry` it is FALSE, because the
widget holding the focus is the `GtkText` inside it -- and wxTextCtrl's
`m_focusWidget` is the entry. Any real fix has to decide after the fact,
either at idle or by listening to the root's `notify::focus-widget` instead
of to per-widget controllers, which is a redesign of GTK4 focus dispatch
rather than a filter. `test_focus_controller_scope()` in
`gtk4-invariants.c` pins all of the above down, so the day GTK settles the
focus before `::enter` the check says so.

### Checks

`gtk4-invariants` is at **56 checks, 0 failed**.

## Progress update 45: the third round of bug reports

Ten reports this time, from a pass over the collpane, comboboxes, dataview
and debugrpt samples. Four were real port bugs, one was an upstream bug in
generic code, four were the samples behaving as written, and one is still
open pending a retest. Chasing them turned up two more nobody had reported.

### A container hears about its children's focus

> DataViewCtrl sample: warnings on stderr
> window wxPanel ("panel") lost focus even though it didn't have it

Recorded as a known gap in update 44 and fixed here, since it turns out to
show up in ordinary use rather than only in a contrived probe. GTK3 sent
`focus-in-event` to the widget that took the focus; GTK4 has no such event,
and `GtkEventControllerFocus::enter` fires for the focus entering **the
widget or any of its descendants**. So every container above the focused
control was told the focus had arrived, none of them was told when it moved
on to a child, and each was told it had left much later -- by which time
`gs_currentFocus` was somebody else.

The measurements in update 44 rule out filtering it where it arrives. What
was left was to decide afterwards, and the toplevel's `notify::focus-widget`
is exactly where GTK settles it: it is emitted immediately after the
enter/leave burst, in the same operation, and `gtk_window_get_focus()` is
finally set by then. Each wxWindow marks the widget it watches with a
pointer back to itself, and the innermost marked widget at or above the real
focus widget names the wxWindow the focus belongs to. That handles the
`GtkEntry`/`GtkText` case, which is what ruled out the simpler tests.

`::leave` still drives focus-out -- GTK keeps its focus widget when a window
is merely deactivated, so that leave is the only notice wx gets of it -- but
only for a window wx has recorded something about.

Measured on the dataview sample: switching between all four tabs and back
several times now produces nothing at all on stderr.

### A click in a tree view arrived a hundred pixels lower

> DataViewCtrl sample: checkboxes in MyListModel cannot be checked

A GTK4 event's position is relative to the **surface**, while a cell area is
relative to the tree view. Under GTK3 the two were the same thing: the event
carried coordinates relative to the GdkWindow it had arrived on, which was
the tree view's bin window. `gtk_wx_cell_renderer_activate()` was mixing
them, so `ActivateCell()` tested the click against a point well below where
it happened:

```
DVHIT checksize=18x18 pos=(15,77) hit=0
```

The check box is 18x18 and the click was 77 pixels down. Converting through
`gtk_native_get_surface_transform()` and `gtk_widget_compute_point()` gives
`rel=(8,8)`, inside the box.

Notably **without** `gtk_tree_view_convert_widget_to_bin_window_coords()` on
top of that: GTK4's tree view has no bin window any more and hands out cell
areas relative to itself, so applying it subtracted the header height a
second time. That intermediate version still missed, 24px too high, which is
worth knowing for the next person converting coordinates around GtkTreeView.

This affects every activatable custom renderer, not just the check box one.

### gtk_render_arrow() draws nothing

> comboboxes sample: most drop-down arrow boxes lack their drop-down-arrow
> and don't work

GTK4 still declares `gtk_render_arrow()`, among its deprecated functions, so
`wxRendererGTK::DrawDropArrow()` went on calling it and went on compiling.
It puts nothing on the surface any more: a GTK4 theme gives the arrow of a
combo box a node of its own and paints it as an icon.

| through a button style context | pixels painted |
| --- | --- |
| `gtk_render_arrow()` | 0 |
| `pan-down-symbolic`, snapshotted | 42 |

So every wxComboCtrl and wxOwnerDrawnComboBox had a blank square where its
drop-down button should be. The "don't work" half was a consequence rather
than a second bug: the buttons were live all along, there was just nothing
drawn to suggest the blank square could be clicked. `DrawDropArrow()` now
looks up `pan-down-symbolic`, snapshots it with the style context's
foreground colour so it follows a dark theme, and draws the render node into
the cairo target.

The neighbouring deprecated calls are worth recording too, since the obvious
conclusion from the above is the wrong one. `gtk_render_check()`,
`gtk_render_option()` and `gtk_render_expander()` also draw nothing *through
a bare widget context* -- but wx descends to the real `check`, `radio` or
`expander` node first, and GTK4's themes paint those through the node's
`background-image`, so they come out correctly. `gtk_render_background()`,
`gtk_render_frame()` and `gtk_render_focus()` still draw. The arrow was the
only one wx was asking the wrong way for.

### A window's default widget outlives itself

Not reported: found while looking at something else, and firing on every
dialog with a default button. Closing one printed five criticals:

```
Gtk-CRITICAL gtk_widget_is_ancestor: assertion 'GTK_IS_WIDGET (widget)' failed
Gtk-CRITICAL gtk_widget_is_ancestor: assertion 'GTK_IS_WIDGET (widget)' failed
Gtk-CRITICAL gtk_widget_remove_css_class: assertion 'GTK_IS_WIDGET (widget)' failed
Gtk-CRITICAL gtk_widget_queue_draw: assertion 'GTK_IS_WIDGET (widget)' failed
GLib-GObject-CRITICAL g_object_notify: assertion 'G_IS_OBJECT (object)' failed
```

**GTK4's `GtkWindow` keeps a plain pointer to its default widget.** It takes
no reference to it, and unparenting that widget does not clear the pointer.
So destroying a dialog's default button leaves the window pointing at freed
memory, and the next unparent of any sibling walks into it:
`gtk_widget_unparent()` asks whether the default widget is inside the widget
going away. Instrumenting `wxPizza::remove()` makes the moment unambiguous:

```
PZREM w=0x...5f0 default=0x...5f0 ok=1     <- the default widget is removed
PZREM w=0x...150 default=0x...5f0 ok=0     <- same pointer, no longer a widget
```

`wx_gtk_widget_forget_in_root()` now tells the window before the widget
goes, for the widget itself and for anything below it, from both places a
child is detached. The focus widget is handled the same way -- GTK4 does
clear that one itself, but doing it here as well costs nothing and keeps the
two from drifting apart, which matters now that the focus resolution above
reads `gtk_window_get_focus()`.

### Not the port

Four reports came down to samples doing what they say, and the GTK3 build
was the arbiter in each case:

* **collpane, collapsing does not move the control below.** Identical on GTK
  3.24.41. The pane is added with proportion 1, so it keeps the space
  whether collapsed or not, and `MyFrame` has no
  `EVT_COLLAPSIBLEPANE_CHANGED` handler to re-fit the frame.
* **collpane, "Press to align right" does nothing.** In the main window it
  has no handler at all -- `MyFrame`'s event table has no `EVT_BUTTON` entry
  for it. The one in the Test dialog works correctly under GTK4: the text
  control moves to the right of its grid cell, diff bbox `(15,181)-(407,215)`.
* **debugrpt, nine vertical lines.** `m_numLines = 10` and the paint handler
  draws that many. It exists to be crashed by "Report for paint handler",
  which sets it to zero.
* **debugrpt, ListLoadedDLLs cycles.** `for(;;)` in the sample: pick a
  library, see its details, back to the list. Cancel and the window
  manager's close button both exit it correctly.

And one was an upstream bug in generic code rather than in the port:
`src/generic/dbgrptg.cpp` created the contents of two `wxStaticBoxSizer`s
with the dialog as parent, which `wxStaticBoxSizer::CheckIfNonBoxChild()`
complains about in a debug build -- on GTK3 just the same. Fixed by
parenting them to the `wxStaticBox`.

### A trap worth naming

Twice in one day, a modal dialog that had come up somewhere unexpected made
unrelated things look broken. In a window-manager-less X server a
`wxLogMessage` box lands at 0,0, behind the main window, while still holding
the modal grab -- so every button in the dialog underneath silently does
nothing, and the obvious conclusion is that the buttons are broken. Both
"the align-right button does nothing" and a stretch of this session were
that. If a control appears inert, count the windows first.

### And the suite

`test_gui` is unchanged at **488 passed / 2 failed of 490**, the two being
the `wxDVC::SingleSelection` and `TextCtrl::HitTest` failures already
described. One pre-existing GTK critical remains in the suite, unrelated to
any of the above and unchanged across all three runs today:
`gtk_stack_remove: assertion 'gtk_widget_get_parent (child) == GTK_WIDGET
(stack)' failed`, in `EventPropagationTestCase::DocView`.

`gtk4-invariants` is at **58 checks, 0 failed**.

## Progress update 46: what the CI actually said

The fork's workflows had never run: GitHub disables Actions on a fork until
someone turns them on, and every upstream workflow is filtered to
`branches: [master]` anyway. With both fixed, the port branch got its first
full CI pass across Unix, MSW, MSW-cross, CMake, Mac and the code checks --
about forty jobs, none of which the port had ever been measured against.

The headline is the one that was least certain before: **MSW builds the port
cleanly**. Every build step of every MSW job -- vs2022 Debug/Release x64,
Debug Win32, Release arm64 and vs2026 Release x64 -- succeeded, library,
samples and both test binaries alike. The 546 lines the port adds to shared,
non-GTK files compile under MSVC without a single warning being promoted, so
the largest unknown in "what happens when this is proposed upstream" is now
answered.

What did fail split cleanly into four kinds, and none of them was GTK4 code.

### Two functions that only exist for GTK+ 3 and 4

The wxGTK2 job builds `src/gtk/*.cpp` too, with `-Werror`, and it caught two
places where the port left something behind that GTK+ 2 does not use:

* `wxGetMouseState()` still fetched a `GdkWindow` that only the GTK+ 3 branch
  needs, because the port had replaced the `gdk_window_get_display(window)`
  call that used to consume it under GTK+ 2 with `wxGetTopLevelGdkDisplay()`.
* `wxGTKProcessScrollDeltas()` was factored out of the `GDK_SCROLL_SMOOTH`
  case so the GTK4 scroll controller could share it -- but that case is
  itself `#if GTK_CHECK_VERSION(3,4,0)`, so under GTK+ 2 the function has no
  caller at all.

Neither is reachable in a GTK+ 2 build, so neither could show up in any
amount of GTK4 testing. This is the whole argument for keeping the GTK+ 2 job
in the matrix while a port like this is in flight.

### A warning only clang gives

`src/gtk/artgtk.cpp` grew a compatibility layer for the icon sizes GTK4
removed, with a `wxGtkGetIconTheme()` in each half. Both of its callers are
inside `#ifdef __WXGTK4__`, so the GTK+ 3 copy is unused -- and clang says so
while gcc does not. gcc does not warn about an unused `inline` function; clang
does. One more reason the matrix has a clang job.

### A CI job that could not start

The wxGTK4 job itself failed before it configured anything, in "Install
CCache", on every run. The ccache action derives its cache key from the job
name, and the name was

    Ubuntu 24.04 wxGTK 4 (WIP port, see docs/gtk/gtk4-status.md)

GitHub rejects a cache key containing a comma. So the one job in the matrix
that exercises the port had been silently dead since it was added. Renamed to
`Ubuntu 24.04 wxGTK 4 - WIP port`, with a comment saying why the name has to
stay free of punctuation.

### Three tests that described wxGTK rather than wxWidgets

The MSW GUI test run failed 3 of 507 cases -- all three of them tests this
port added, none of them wx code:

* **`RadioBox::Focus`** expected `wxWindow::FindFocus()` to return the box.
  Under wxMSW `wxRadioBox` is a `wxCompositeWindow` and its `SetFocus()`
  forwards to one of the `wxRadioButton`s inside it, so the focus is *in* the
  box, not *on* it. Under wxQt it is neither: `FindFocus()` returns null even
  though the focus event still arrives. The test now accepts the focus being
  anywhere inside the box, and skips the question entirely under wxQt.

* **`wxMDIParentFrame::ShowFullScreen`** expected the `wxMenuBar` object to be
  hidden. That is how wxGTK implements `wxFULLSCREEN_NOMENUBAR`; wxMSW calls
  `SetMenu(hwnd, nullptr)` and leaves the bar shown. The bug the test was
  written for (#74) was idle processing undoing the full screen switch, so
  the test now checks `IsFullScreen()` everywhere and the bar's visibility
  only under wxGTK.

* **`wxTextCtrl::KeyEventsWhenFocused`** counted two key up events where it
  expected one. Simulated input is asynchronous and a release synthesized by
  an earlier test can still be in flight -- which is exactly why
  `KeyboardEventTestCase::setUp()` discards events before it starts counting.
  The test now does the same, and waits for the release instead of assuming
  one `wxYield()` sees it.

The general shape is worth stating: a test written while fixing a GTK4 bug
will tend to encode *how GTK does it* rather than *what wx promises*, and
nothing on the GTK side can catch that. Only a run on another port can.

### And a baseline, because "is this ours?" needed an answer

The wxQt job failed its GUI tests too, and there was no way to tell from the
outside whether that was the port's doing. So the same machine built and ran
`test_gui` twice, once from `origin/master` and once from the port branch:

| | test cases | failed | assertions failed |
|---|---|---|---|
| `origin/master`, wxQt | 493 | 4 | 7 |
| port branch, wxQt, before | 498 | 5 | 8 |
| port branch, wxQt, after | 498 | 4 | 7 |

The four failures are the same four in every column -- `measuring`,
`listbasetest`, `treectrltest` and `wxTopLevel::Show` -- and they fail on
unmodified upstream master just as they do here. The fifth was
`RadioBox::Focus`, ours, now fixed. The port adds **zero** wxQt regressions,
and that is a measurement rather than an opinion.

Building an upstream-master tree next to the branch turns out to be cheap and
worth doing whenever a failure appears on a port the work never touched.

### The suite

`test_gui` under GTK4 is unchanged at **488 passed / 2 failed of 490** with
`wxUSE_XVFB=1` set -- the two being the `wxDVC::SingleSelection` and
`TextCtrl::HitTest` failures described earlier. Without that variable
`wxTopLevel::Show` fails as well, on wxGTK and wxQt alike, because the test
only skips its `IsActive()` check when it knows there is no window manager.
Worth remembering before reading anything into a local run.

### The GTK4 job's first run, and what it had never covered

With the cache key fixed, the wxGTK4 job ran for the first time -- and failed
to build, on a file no GTK4 build here had ever compiled:

    src/unix/mediactrl.cpp:1055: error: 'gtk_widget_set_double_buffered'
                                        was not declared in this scope

The cause is a gap in the local configuration rather than anything subtle. The
GTK4 build used for development was `--disable-shared --without-opengl
--disable-stc`, on a machine with no GStreamer development packages, so three
whole libraries had never been through a GTK4 compiler:

* `wxgl` -- `glcanvas.cpp`, `glegl.cpp`, `glx11.cpp` and the two Unix files
  the port modified,
* `wxstc`, including the `ScintillaWX.cpp` change the port made,
* `wxmedia`, both GStreamer backends.

The CI job builds all of them. Reproducing it needed only a second build
directory configured the way CI configures, plus `libgstreamer1.0-dev`,
`libgstreamer-plugins-base1.0-dev` and the Mesa development packages; with
those, the failure appeared immediately and the fix could be checked locally.

The fix is the one already applied to `mediactrl_gstplayer.cpp`, mirrored into
the other GStreamer backend. Which of the two gets built depends on
`wxUSE_GSTREAMER_PLAYER`, which needs `gstreamer-player-1.0` from
plugins-bad -- absent on the CI image and absent here, so CI builds the
`mediactrl.cpp` one and the port had only ever ported the other. Both now:

* `gtk_widget_set_double_buffered()` is gone in GTK4 (a no-op since GTK+ 3.14)
  and the control sets `m_noExpose`, so nothing paints over the video anyway;
* there is no `draw` signal to connect to, so the realize handler sets a
  render rectangle instead;
* `wxGtkGetIdFromWidget()` hands GStreamer the *top level's* surface, because
  an ordinary GTK4 widget has no native window -- so the video has to be
  confined to the control's area with
  `gst_video_overlay_set_render_rectangle()`, and repositioned from `Move()`
  whenever the control moves or is resized. This is the same shape as the
  GTK+ 3 Wayland path, where subsurfaces don't exist either.

With that, the CI configuration -- shared, OpenGL, wxSTC, wxMedia, all samples
and both test binaries -- builds clean under GTK4. The lesson is worth keeping
for any port of this size: a development build configured for fast iteration
is a *subset* of what CI compiles, and the difference is invisible until CI
runs. Configuring one build directory the way CI does, once, is cheap.

### A crash only CI could find

With the build fixed, the wxGTK4 job got as far as running `test_gui` -- and
the run ended at test case 270 of 503 with

    ./controls/textctrltest.cpp:87: FAILED:
    due to a fatal error condition:
      SIGSEGV - Segmentation violation signal

    #0  wxWindow::GTKSendSizeEventIfNeeded()
    #1  frame_clock_layout_after ()
    ...
    #11 g_main_context_iteration ()
    #12 wxGUIEventLoop::DoYieldFor(long)

A frame clock "layout" handler firing with a `this` that had been freed --
the very thing `GTKDisconnectFrameClock()` exists to prevent. It reproduced
immediately in the CI-shaped build directory and never in the development
one, because the two link differently (shared versus static) and Catch runs
the test cases in a different order as a result.

The disconnect was written as

    if (GdkFrameClock* const clock = gtk_widget_get_frame_clock(m_widget))
        g_signal_handlers_disconnect_by_data(clock, this);

and `gtk_widget_get_frame_clock()` only answers while the widget is still
rooted. By the time a window is being destroyed it can already return null
while the clock itself is alive and still holding handlers whose user data is
the window about to be freed -- so both the unrealize call and the destructor
backstop quietly did nothing. Nothing warns about this: the disconnect
"succeeds" by finding no clock at all.

The fix is to stop asking GTK and remember the answer: `wxWindowGTK` now
holds the `GdkFrameClock*` it connected to, with a weak pointer on it so the
field clears itself if the clock dies first, and disconnects from that. A
re-realize that hands out a different clock now also releases the old one,
which the previous code could not do either.

With that, `test_gui` under GTK4 in the CI configuration runs to completion:
**502 passed / 1 failed of 503**, the one being `TextCtrl::HitTestSingleLine`
`Scrolled`. `wxDVC::SingleSelection` passes in this configuration -- the
selection-on-focus effect described earlier depends on which test ran before
it, and this link order does not trigger it.

The general point, again: this bug was in the port from the beginning, in
code every wxWindow runs, and no amount of running the development build
would have shown it. It took a differently linked build to change the test
order.

## Progress update 47: the last failing test, and how it was reachable after all

`wxTextCtrl::HitTest()` for a single-line control was the one thing left
failing under GTK4, and it had been written off. The reasoning was sound as
far as it went: `gtk_entry_get_layout_offsets()` is gone, the layout belongs
to the private `GtkText` inside the `GtkEntry`, and GTK4 has no getter for how
far the text has been scrolled. Hit testing a scrolled entry was therefore off
by the scroll amount, and the limitation went into the table of things the
port cannot do.

It turns out GTK4 does report it, just not as a scroll offset.
`gtk_text_compute_cursor_extents()` (GTK 4.4) gives the cursor rectangle for a
character index, in the `GtkText`'s own coordinates -- and the rectangle for
index 0 begins exactly where the layout begins. A standalone probe against
plain GTK, with no wx involved:

    short text, cursor at home : origin.x = 0
    200 chars, cursor at home  : origin.x = 0
    200 chars, cursor at end   : origin.x = -1650

Zero while the text fits, and negative by the scrolled amount once it does
not: that is precisely what the removed getter returned. Subtracting it after
translating the point into the `GtkText`'s coordinate space -- which already
handled the border, padding and icons -- completes the conversion. It is a
no-op for an unscrolled entry, so the cases that already passed are untouched.

Only the horizontal offset is taken from it. A single-line layout has one
line, so where it sits vertically makes no difference to which character a
point lands on, and leaving `y` alone keeps the change to the axis that was
actually wrong.

Guarded at compile time with `GTK_CHECK_VERSION(4,4,0)` and at run time with
`gtk_check_version(4,4,0)`, so a build against an older GTK4 keeps the
previous approximation rather than failing to build.

### The suite

    All tests passed (49444 assertions in 503 test cases)

**Every GUI test passes under GTK4**, in the configuration CI builds -- shared,
OpenGL, wxSTC, wxMedia, all samples. That is the first time in this port.

### Worth remembering

Two of the three hardest things fixed today were declared impossible or
recorded as unfixable earlier in this same document, and both turned out to
have an answer in an API that does not carry the name of the thing it
replaces. `gtk_entry_get_layout_offsets()` was not replaced by another
"layout offsets" call; it was replaced by a cursor-extents call that happens
to expose the same number. Searching the GTK4 headers for the *removed*
function's concept finds nothing. Searching for what a caller actually needs
-- "where on screen is character N" -- finds it immediately.

## Progress update 48: one test disabled, and why

Progress update 47 ended with the whole GUI suite passing locally. CI then
failed one case that had never been reached before, because the run used to
crash in `TextCtrlTestCase` long before it:

    ./controls/textctrltest.cpp:892: FAILED:
      REQUIRE( pos0 == m_text->PositionToCoords(0) )
    with expansion:
      (0, 0) == (0, -6)

After filling a multi-line control with 51 lines and calling
`SetInsertionPoint(0)`, the view is left scrolled down by a few pixels
instead of sitting at the top.

### What was established

* **It is font-size dependent.** The default font on this machine passes; CI's
  default fails. Setting `gtk-font-name` explicitly and re-running the case
  gives: `Sans 9`, `Sans 10`, `Sans 11` and `Cantarell 11` pass, `Sans 12`
  fails. That is how it was reproduced locally at all.
* **It only fails in sequence.** Running just that section
  (`test_gui TextCtrlTestCase -c PositionToCoords`) passes at `Sans 12`; it
  fails only after the earlier sections of the same fixture have run.
* **It does not reproduce outside the suite.** A standalone program following
  the same call sequence -- `SetValue("Hello")`, `AppendText("\n")`,
  `PositionToCoords()`, then the 51-line `SetValue()` and
  `SetInsertionPoint(0)` -- reports `(0, 0)` after every one of six yields,
  after a second `SetInsertionPoint(0)`, and after `ShowPosition(0)`.
* **None of the code involved is the port's.** `SetInsertionPoint()`,
  `DoPositionToCoords()` and the `m_afterLayoutId` / `m_showPositionDefer`
  deferral they use are untouched by this branch; `git diff origin/master`
  over them is empty.

The one substantive suspicion, not confirmed: that deferral arms an idle at
`GTK_TEXT_VIEW_PRIORITY_VALIDATE + 1`, which is `GDK_PRIORITY_REDRAW + 6`.
Under GTK+ 3 that reliably ran after the text view's incremental validation.
GTK4 drives redraw from the frame clock rather than from an idle at that
priority, so the ordering that made the deferral correct no longer follows
from the priority. Confirming it would mean instrumenting GtkTextView's
validation, which is where this stopped.

### What was done

The single assertion is disabled under `__WXGTK4__` with a `WARN()`, so it
shows up in the output rather than disappearing, and the rest of
`PositionToCoords` -- including the check immediately after it, that the last
position is beyond the window boundary -- still runs. Disabling one line
rather than the case keeps the coverage that does work. Tracked as #116,
with the lead above written down there too.

### An honest note on font sensitivity

While reproducing this, `Sans 12` also made `ListCtrl::Visible`
(`listbasetest.cpp:506`) and another `TextCtrl` hit-test assertion fail. Those
are in generic controls shared with wxGTK3 and were not investigated: CI runs
the default font, and the suite is green there. It is worth knowing that parts
of this suite assume something about font metrics, so "all tests pass" means
"at the font the runner happens to use".

### The suite

At the default font, in the CI configuration:

    All tests passed (47715 assertions in 503 test cases)

## Progress update 49: accessibility (#81)

`wxUSE_ACCESSIBILITY` was wxMSW-only. `include/wx/chkconf.h` turned it off
again everywhere else, `wx/access.h` declared only the abstract
`wxAccessibleBase`, and building with it on anywhere else failed: with
`--enable-accessibility --with-gtk=4`, `configure` succeeded and the build then
produced 96 errors in `src/generic/grid.cpp`, `src/generic/gridsel.cpp` and
`src/generic/datavgen.cpp`, all of them `wxAccessible` being an incomplete type.

The issue quoted an opinion that a GTK4 implementation "would be relatively
easy". Half of that is right: the pieces exist, and there is now a working
implementation. The other half is not, and it is worth being precise about why,
because the reasons are all things GTK4 changed rather than things wx got wrong.

`wxAccessibleBase` is shaped like MSAA and answers questions. GTK4 dropped ATK
and does not ask them: it caches what the application last pushed, and the only
things it pulls are bounds, platform states, and the tree walk. So the port has
to push, and the only moments it knows something changed are the
`NotifyEvent()` calls the generic controls already make.

Three things had to be true for any of it to work, since most of what a
`wxAccessible` describes has no window of its own.
`docs/gtk/probes/gtk4-a11y-virtual-child.c` settles them: a plain `GObject` can
implement `GtkAccessible` and carry a role, properties and states pushed to it
read back, and a widget can name a non-widget as its first accessible child.

Two things turned out not to be true:

* `gtk_accessible_get_next_accessible_sibling()` does not call the interface
  vfunc it declares. It returns whatever `gtk_accessible_set_accessible_parent()`
  stored, while `gtk_accessible_get_first_accessible_child()` right beside it
  does call its vfunc. So children cannot be made lazily along the walk -- the
  chain has to exist first -- which is a real cost for a wxGrid, which reports
  `rows * cols` of them.
* A widget's `accessible-role` cannot be changed once it has an AT context,
  which a wx window already has by the time an application attaches a
  `wxAccessible`. The role's ARIA name goes into the role *description* instead.

And one is a GTK bug: `gtk_accessible_update_next_accessible_sibling()` drops a
reference on the accessible parent, which with stock widgets and no wx involved
is enough to finalize it. The probe reproduces it.

What works now, verified end to end by
`docs/gtk/probes/gtk4-a11y-wx-bridge.cpp` against a real wxGrid: name,
description, keyboard shortcut, states, bounds, the child tree, and which child
has the focus, all reaching GTK's accessibility tree. A three-by-two wxGrid
exposes its twelve cells and headers, each with its label -- "Column A: hello",
"Row 2 Header", "Grid Corner".

`--enable-accessibility` is accepted for wxGTK4 against GTK 4.10 or later and
refused elsewhere. The default is unchanged: off everywhere but wxMSW.

The details, the full mapping, and the list of `wxAccessibleBase` members that
GTK4 has no counterpart for are in `docs/gtk/gtk4-accessibility.md`.

CI builds this. The wxGTK4 job passes `--enable-accessibility`, so
`src/gtk/accessgtk.cpp` is compiled there, and two steps run the probes: one
before the library is built, checking the GTK 4.10 capabilities and the GTK bug
the implementation rests on together with the clipboard release ordering, and
one after it, running the wxGrid bridge check. None of what they establish is
visible to a compiler, so without them a GTK upgrade could leave `wxAccessible`
describing nothing with the suite still green.

## Progress update 50: the clipboard was losing everything put on it

The upstream resync brought in `RichTextCtrlTestCase::CutCopyPaste`, and the
wxGTK4 CI job failed on it -- 508 of 509 -- while wxGTK3 passed. The failure
looked like a rich text problem and was not.

A wxWidgets program doing nothing but this:

```cpp
wxClipboardLocker lock;
wxTheClipboard->Clear();
wxTheClipboard->SetData(new wxTextDataObject("hello"));
```

ends up, one turn of the main loop later, with an empty clipboard.
`SetData()` returns true, the content is there immediately afterwards, and
then it is gone: `gdk_clipboard_is_local()` goes back to false and
`gdk_clipboard_get_formats()` reports nothing.

`docs/gtk/probes/gtk4-clipboard-reclaim.c` takes wx out of it entirely and
tries the four orderings against GDK directly, ten times each:

| sequence | clipboard kept |
| --- | --- |
| `set_content(provider)` | 10/10 |
| `set_content(NULL)` then `set_content(provider)` | **0/10** |
| `set_content(NULL)`, iterate the main loop, `set_content(provider)` | 10/10 |
| `set_content(providerA)` then `set_content(providerB)` | 10/10 |

So it is release-then-claim without returning to the main loop in between:
GDK's X11 backend acts on the `SelectionClear` caused by the release *after*
the claim has already happened, and takes the newly set content with it.
Claiming over an existing claim is fine. Releasing is fine if the loop runs
first. Measured on GTK 4.14.5 under X11; not checked against newer GTK or
under Wayland.

That is exactly the sequence `wxClipboard` produced, because `Clear()` gave up
ownership immediately and applications clear before setting -- the test does it
in as many words.

### The fix

`Clear()` still empties everything wx reports at once, but the release of the
X selection waits for an idle callback, and `AddData()` cancels that callback
if a new claim comes first. Only the moment the selection is given up moves;
nothing observable through the wxWidgets API changes. The destructor releases
directly, since there is no main loop left to defer to.

Deferred per clipboard kind rather than globally: clearing the primary
selection must not cancel a pending release of the clipboard, or the other way
round.

### What it was hiding

This was not one failing test. Nothing a wxGTK4 application copied could be
pasted anywhere, including into itself, whenever the application cleared the
clipboard first -- which is the documented way to replace its contents. The
test suite only noticed because the new test reads back what it wrote.

## Progress update 51: a GTK+ 2 regression the port had introduced

The upstream resync's other new CI failure was the **wxGTK 2** job, crashing in
`XRC::UnknownControlSizeHints` -- a test that came in with upstream's
[4958d2d](https://github.com/wxWidgets/wxWidgets/commit/4958d2d), which makes
`wxXmlResource::AttachUnknownControl()` refresh the top level's size hints
after reparenting a control into its placeholder.

The comfortable reading was that this is upstream's bug and upstream CI does
not see it because it no longer builds wxGTK 2 on this image. That reading was
wrong, and checking it took one measurement: the same test, built from
`upstream/master` with the same `--with-gtk=2` configure line on this machine,
passes. On the port branch it segfaults. So the port introduced it.

The backtrace says where:

```
#0  gtk_widget_is_toplevel ()
#7  gtk_widget_unparent ()
...
#23 g_object_run_dispose ()
#24 wxWindow::~wxWindow ()
```

A top-level window is being disposed, GTK walks its list of children, and one
of them is freed memory.

### One line

`wxWindowGTK::Reparent()` detaches the widget from its old parent before
attaching it to the new one. Upstream:

```cpp
if ( GtkWidget *parentGTK = gtk_widget_get_parent(m_widget) )
    gtk_container_remove(GTK_CONTAINER(parentGTK), m_widget);
```

The port, from the batch that replaced `gtk_widget_destroy()` and the
`GtkContainer` API across the GTK sources:

```cpp
if ( gtk_widget_get_parent(m_widget) )
{
#ifdef __WXGTK4__
    GTKDetachFromParent();
#else
    gtk_widget_unparent(m_widget);
#endif
}
```

Those are not the same call. `gtk_container_remove()` goes through the
container's `remove` vfunc, which takes the child off the container's *own*
list and then unparents it. `gtk_widget_unparent()` only does the second half,
so the container is left holding a pointer to a widget that is about to be
freed, and unparents it again when it is disposed.

This is exactly the mistake `GTKDetachFromParent()` was written to avoid on the
GTK4 side, where the same substitution produced stale `wxPizza::m_children`
entries and eventually an abort inside `gtk_css_node_validate()`. The comment
above that function has said so since it was written. The GTK+ 2 and 3 branch
of `Reparent()` simply never got the same treatment.

### What it does and does not affect

| | |
| --- | --- |
| wxGTK 2 | crashes, reproducibly |
| wxGTK 3 | survives, silently -- the stale entry does not get walked |
| wxGTK 4 | unaffected, `GTKDetachFromParent()` was already right |

`docs/gtk/probes` is not the right home for this one: it needs wx, and the
test that finds it is already upstream's. The standalone reproduction is fifty
lines and lives in the issue.

The fix restores `gtk_container_remove()` for GTK+ 2 and 3, with a comment
saying why the shorter call is not a substitute -- since the whole point of a
mechanical substitution is that it looks equivalent.

## Progress update 52: what the CI job was not looking at

The cross-review (#111) turned up something larger than the commits it was
reviewing. The wxGTK 4 CI job passes `--disable-uiactionsim`, which compiles
out every test that needs synthesised input. Same configure line otherwise,
same machine:

| | test cases | result |
| --- | --- | --- |
| `--disable-uiactionsim`, what CI runs | 509 | all passed |
| without it | 533 | 521 passed, 12 failed |

So the suite has been reporting green with 24 cases absent, and half of those
fail. The reason the flag was added -- progress update 7, `uiactionx11.cpp`
"not attempted" -- has been stale for a while: that file has a working GTK4
branch. Verified rather than assumed, in a build with the simulator enabled:

    mouse    : MouseMove=1 MouseClick=1  left-down events seen=1
    keyboard : focus is on the text ctrl: 1
               Text() returned 1; key-downs=3 chars=3 value="abc"

Tracked as #126.

### Triage

Running each failure on its own splits them three ways, which is more useful
than the count:

| | |
| --- | --- |
| fail on their own -- real, reproducible bugs | `EnterLeaveEvents`, `MenuTestCase`, `RadioButton::Click`, `SliderTestCase`, `wxTreeCtrl::CollapseExpandEvents` |
| pass alone, fail only in the suite -- ordering, like update 48 | `EventPropagationTestCase`, `HtmlWindowTestCase`, `KeyboardEventTestCase`, `ValNum::Interactive` |
| the two wxStyledTextCtrl cases | `AutoComp`, `Calltip` |

Two of the first group are fixed as of #123 and #124: `SliderTestCase` passes
all 28 assertions and `RadioButton::Click` passes. Three remain.

### The GTK4 differences behind the slider and radio button ones

Recorded here because they are toolkit behaviour rather than anything about
wx, and the measurements are worth keeping. On a 0..100 scale starting at 50,
step 1, page increment 20, driven by real key events against plain GTK:

|  | Up | Down | Page_Up | Page_Down |
| --- | --- | --- | --- | --- |
| GTK+ 3, horizontal | 49 | 51 | 30 | 70 |
| **GTK 4, horizontal** | **51** | **49** | **70** | **30** |
| GTK+ 3, horizontal inverted | 51 | 49 | 70 | 30 |
| GTK 4, horizontal inverted | 51 | 49 | 70 | 30 |
| GTK+ 3, vertical | 49 | 51 | 30 | 70 |
| GTK 4, vertical | 49 | 51 | 30 | 70 |

Exactly one row differs, and Left and Right agree everywhere -- so a fix that
reverses the keys unconditionally would have broken the other rows, while one
that reverses them and then compensates for `inverted`, as #124 does, comes out
right on all six.

For the thumb release, the relevant fact is when a `GtkGestureClick` gives up
on a `GtkScale` that has claimed the sequence for its own drag:

    press at x=60:  legacy: button press / begin / pressed / end
    drag to x=260:
    release:        legacy: button release

All of `begin`, `pressed` and `end` arrive at press time, before the drag
starts -- not at the release, and not when the widget is destroyed. Only the
legacy controller hears the release, which is what #124 hooks.

Worth one caution, since the two look interchangeable: this is `GtkScale`
specifically. The same probe against a `GtkScrollbar` gets `released` as
expected, so the scrollbar tracking in `window.cpp` -- which clears the global
`g_blockEventsOnScroll` from that handler -- is not affected and needs no
change. That was checked because it looked like the same bug and would have
been much worse; it is not.

## Progress update 53: the delayed scroll behind #116

The assertion disabled in progress update 48 was not a font-metric problem.
GTK4 animates the adjustments changed by
`gtk_text_view_scroll_mark_onscreen()`. A scroll requested for an earlier
insertion point could therefore still be running when
`SetInsertionPoint(0)` was called. If position zero happened to be visible at
that instant, GTK considered the new scroll request a no-op and let the old
animation continue, leaving `PositionToCoords(0)` a few pixels above the
viewport after the call.

This was confirmed by tracing the text view's frame-by-frame adjustment value:
the insertion mark stayed at buffer offset zero while the previous vertical
animation continued towards its stale target. On GTK 4.22.4 the failure is no
longer limited to the full fixture or to `Sans 12`; controlled runs from
`Sans 9` through `Sans 13` reproduce offsets between three and five pixels.

Before asking GTK to show the new insertion mark, the GTK4 implementation now
sets each scrolled-window adjustment to its current value. This ends any
animation left by an earlier request without moving the viewport, after which
the new mark scroll supersedes it normally. The GTK+ 2 and 3 paths are
unchanged.

The `PositionToCoords(0)` assertion is enabled again for GTK4. It passes for
the base, rich and rich2 text controls on X11 and Wayland, including 50
repeated X11 runs across the five controlled font sizes. The complete GTK+ 3
and GTK+ 2 `TextCtrlTestCase` fixtures also pass.

## Progress update 54: the CI job now measures what the other jobs measure

Update 52 established that the wxGTK 4 job's `--disable-uiactionsim` was
hiding failures and that the reason the flag was added had gone stale. This
update removes it, and records what changed in what the job actually
measures, since "the job is green" means nothing without that.

### What the flag was removing

Counted from `--list-tests` rather than from a run, so that a suite aborting
part way cannot distort it:

| build | `wxUSE_UIACTIONSIMULATOR` | test cases |
| --- | --- | --- |
| `../configure --with-gtk=4` | 1 | 534 |
| `../configure --with-gtk=4 --disable-uiactionsim` | 0 | 510 |

Twenty-four cases, and the set difference is exactly the input-driven ones:

```
Button::Click                     wxDVC::KeyEvents
Button::Disabled                  wxTextCtrl::KeyEventsWhenFocused
EnterLeaveEvents                  wxTextValidator::IsValid
KeyboardEventTestCase             wxTextValidator::TransferFromWindow
ListCtrl::ColumnClick             wxTextValidator::TransferToWindow
ListCtrl::ColumnDrag              wxTreeCtrl::CollapseExpandEvents
SpinCtrlDouble::Arrows            wxTreeCtrl::ItemClick
SpinCtrlDouble::Increment         wxTreeCtrl::KeyDown
SpinCtrlDouble::Wrap              wxTreeCtrl::KeyNavigation
ValNum::Interactive               wxTreeCtrl::LabelEdit
wxUIActionSimulator is            wxTreeCtrl::Menu
  unavailable under Wayland       wxTreeCtrl::SelectItemMultiInteractive
                                  wxTreeCtrl::SelectionChange
```

The case count understates the loss, which is worth knowing before quoting
it. `#if wxUSE_UIACTIONSIMULATOR` also guards *sections inside* cases that
stay in the list either way -- `MenuTestCase`, `SliderTestCase`,
`HtmlWindowTestCase`, `EventPropagationTestCase`, `RadioButton::Click` and
`wxStyledTextCtrl::AutoComp` all appear in both builds' `--list-tests`
output. That is why several of the failures update 52 found are in cases the
24-case difference does not contain: the assertion counts below are the
measure that captures both.

### Before and after, measured

Full suite, `~[.]`, `wxUSE_XVFB=1`, out-of-tree builds with the test data
copied in (an in-tree build, which is what CI does, has it already).

Isolating the flag first -- same configure line on both sides, only
`--disable-uiactionsim` differing:

| `../configure --with-gtk=4` plus | test cases | assertions | result |
| --- | --- | --- | --- |
| `--disable-uiactionsim` | 510 | 47033 | all passed |
| nothing | 534 | 47731 | 528 passed, 6 failed (25 assertions) |

**698 assertions** were being compiled out, not the ~24 cases' worth the case
count suggests. The six that fail:

```
EnterLeaveEvents            KeyboardEventTestCase
EventPropagationTestCase    MenuTestCase
HtmlWindowTestCase          ValNum::Interactive
```

Three of those -- `EventPropagationTestCase`, `HtmlWindowTestCase` and
`MenuTestCase` -- are *not* in the 24-case list above. They ran in the old
configuration too, and passed, because the parts of them that synthesise
input were compiled out of the case rather than the case out of the suite.
That is the section-level guarding made concrete, and it is why the old job
could report "all tests passed" about cases that do not pass.

All six are tracked in #82.

### That the change builds is verified, not assumed

The exact configure line the job now runs, built out-of-tree here:

```
../configure --with-gtk=4 --enable-accessibility
   -> #define wxUSE_UIACTIONSIMULATOR 1
      #define wxUSE_ACCESSIBILITY 1
   make: 0 errors, make -C tests test_gui: 0 errors
```

and run: 534 test cases, 47930 assertions, the same 6 failures. Accessibility
therefore contributes 199 assertions and no test cases, and does not change
the failing set -- which is why the flag could be isolated in a pair of builds
without it. `wxUSE_ACCESSIBILITY` and `wxUSE_UIACTIONSIMULATOR` had never been
on together before, since the flag was added at the same time accessibility
was; they build together. `src/unix/uiactionx11.cpp`, the file whose deferral
was the original reason for the flag, is compiled again.

Only the one matrix entry changed: parsing `ci.yml` before and after and
comparing the entries shows a single differing key on the wxGTK 4 job,
the other ten entries identical, and `continue-on-error` untouched --
which is what keeps the six red cases from blocking anyone.

### Why now rather than once #82 is green

The job cannot block CI (`continue-on-error: ${{ matrix.gtk_version == 4 }}`),
so the only thing the flag bought was a green tick. A job that measures less
than it appears to is worse than a job with known red tests in it: the wxGTK
2 and 3 jobs run these cases, and a GTK4 number that is not comparable with
theirs is not a number anyone can use.
