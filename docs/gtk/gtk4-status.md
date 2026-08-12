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
