# Phase 2 design: replacing GdkWindow (the window/child-model blocker)

Status: **design proposal, no code written yet.** This is the write-up the
port plan (`gtk4-port-plan.md`) said should exist before touching
`window.cpp` for real. It supersedes that document's framing of Phase 2 —
tracing actual call sites showed the problem is smaller and more
tractable than the initial grep-based estimate suggested, though two
genuinely open decisions remain (see §6 and §7).

## 1. Correcting the initial estimate

`gtk4-status.md` flagged 33 `gtk_widget_get_window` call sites and "10+"
direct `GdkWindow` uses as the critical-path blocker, and the original
plan assumed this meant wx's child-positioning model (how child controls
are placed inside a `wxWindow`) needed a redesign. Tracing the actual
call sites shows that's not the shape of the problem:

- **Child positioning is already windowless.** `wxPizza`
  (`src/gtk/win_gtk.cpp`), the `GtkFixed`-derived container behind
  `m_wxwindow`, positions child `GtkWidget`s directly via
  `size_allocate`/`measure` — there is no per-child `GdkWindow` and no
  reparenting involved. `grep`ing for `gdk_window_new`/
  `gdk_window_reparent` across all of `src/gtk` turns up exactly **one**
  file: `src/gtk/nativewin.cpp` (see §7). The window/child-widget *layout*
  model wx already uses for GTK3 carries over to GTK4 essentially as-is.
- **What actually breaks is narrower**: `m_wxwindow` itself (and other
  "real" native GTK controls like `GtkComboBox`, `GtkSpinButton`) has
  `gtk_widget_set_has_window(widget, true)` set (`win_gtk.cpp:364`), so
  it owns one real `GdkWindow` of its own — and *that* window is queried
  for a handful of distinct purposes, each with its own GTK4 replacement.
  None of them are "how do I position my children."

The rest of this document is that breakdown.

## 2. What `gtk_widget_get_window()`/`GdkWindow` are actually used for today

Traced every call site in `src/gtk/window.cpp` (the highest-density
file) plus the ~18 per-widget `GTKGetWindow()` overrides
(`anybutton.cpp`, `checkbox.cpp`, `choice.cpp`, `combobox.cpp`,
`dataview.cpp`, `listbox.cpp`, `notebook.cpp`, `radiobox.cpp`,
`slider.cpp`, `spinbutt.cpp`, `spinctrl.cpp`, `srchctrl.cpp`,
`textctrl.cpp`, `toolbar.cpp`, etc.). They fall into six buckets:

| Purpose | Example call site | GTK4 replacement | Harder or easier than GTK3? |
|---|---|---|---|
| **"Get me a display/seat/backend"** — not really about the window at all | `wxGetTopLevelGDK()` (`window.cpp:478`), `ShowPopupMenu`'s Wayland device lookup (`window.cpp:6514-6522`) | `gdk_display_get_default()` directly — no window needed | **Easier.** One-line fix at ~10 call sites. |
| **Event-source identity matching** — "which wx window did this native event target" | `GTKIsOwnWindow()`/`GTKGetWindow()` (`window.cpp:6621-6643`), raw `gdk_event->window` comparisons (`window.cpp:2389`, `2434`) | Not needed — `GtkEventController` callbacks already know their owning widget; no matching required | **Easier.** This whole ~20-file mechanism (the per-widget `GTKGetWindow()` overrides exist *only* to feed this) gets deleted, not reimplemented, once Phase 3's controller-based input lands. |
| **Cursor setting** | (via `GTKGetMainWindow()` in cursor-related code) | `gtk_widget_set_cursor()` directly on the `GtkWidget` — no window needed | **Easier.** |
| **Paint/clip/size queries** | `GTKSendPaintEvents()` clip region and width (`window.cpp:5776`, `5786`) | `gtk_widget_get_width()`/`get_height()`; clipping is automatic in GTK4's snapshot scene graph | **Easier**, and overlaps Phase 4 — same call sites already need to change for the `draw`→`snapshot` migration, so this isn't extra work, it's the same work. |
| **Z-order** (`Raise()`/`Lower()`, `window.cpp:5431-5452`) | `gdk_window_raise`/`lower` | `gtk_widget_insert_after`/`insert_before` for reordering among siblings (affects paint/focus order, not a real "window stack"); **toplevels** have `gtk_window_present()` for raise but nothing symmetric for lower | **Harder, small gap.** `wxWindow::Lower()` on a real toplevel has no direct GTK4 equivalent — needs to be documented as reduced-fidelity or worked around via the window manager where possible. Low-impact: `Lower()` is rarely used in practice. |
| **Coordinate translation** (screen↔widget) | uses of `gdk_window_get_origin` | `gtk_widget_translate_coordinates()` / `gtk_widget_compute_point()` | **Roughly even** — different API, same capability. |

## 3. What still genuinely needs a `GdkSurface`

A small, well-defined set of call sites need an actual native surface,
not just "a display" — these map to **toplevels only**, which is exactly
what GTK4 still supports via the `GtkNative` interface:

- Backend-specific interop: `nativewin.cpp`, `taskbar.cpp` (tray icon
  positioning), `renderer.cpp`, `display.cpp` (monitor geometry) already
  guard their `gdk/gdkx.h`/`gdk/gdkwayland.h` `#include`s behind
  `#ifdef GDK_WINDOWING_X11`/`WAYLAND` — the include path just needs
  updating to `gdk/x11/gdkx.h`/`gdk/wayland/gdkwayland.h` (§ noted in
  `gtk4-status.md`), and the window handle itself comes from
  `gtk_native_get_surface(GTK_NATIVE(toplevel_widget))` instead of
  `gtk_widget_get_window()`.
- `wxGetTopLevel()` (`window.cpp:449`) already only walks
  `wxTopLevelWindows` — i.e. it already only ever looks at real
  toplevels. Swapping its `GdkWindow*` output parameter for a
  `GdkSurface*` (via `gtk_native_get_surface`) is a contained,
  mechanical change confined to this one function and its ~10 direct
  callers.

## 4. Proposed target design

No new abstraction layer is needed — the existing structure holds, with
these concrete changes to `wxWindowGTK`:

1. **Delete** `GTKIsOwnWindow()`, `GTKGetWindow()`, `GTKGetMainWindow()`,
   `GTKGetConnectWindow()` and all ~18 per-widget overrides, once Phase 3
   (event-controller input) lands — they exist only to support
   window-identity event routing, which GTK4 doesn't need.
2. **Replace** `wxGetTopLevelGDK()` with a version returning
   `GdkDisplay*` directly (`gdk_display_get_default()` under
   `__WXGTK4__`) for the ~10 call sites that only wanted a display; audit
   each caller individually since a couple (e.g. `bitmap.cpp`'s
   `gdk_bitmap_create_from_data`/`gdk_pixmap_new` calls) are already
   GTK2-only dead code under `__WXGTK3__`/`__WXGTK4__` and can simply be
   left alone or removed as unreachable.
3. **Replace** `wxGetTopLevel()`'s `GdkWindow**` output with
   `GdkSurface*` via `gtk_native_get_surface()`, for the narrow set of
   real toplevel/backend-interop callers in §3.
4. **Fold** the clip/size fix into Phase 4's `draw`→`snapshot` migration
   rather than treating it as separate Phase 2 work — same call sites,
   same commit makes sense.
5. **Accept and document** the `Lower()` fidelity gap for toplevels
   rather than engineering around it.

This means Phase 2, 3, and 4 as originally split are more entangled than
the plan assumed — recommend doing them as one coordinated pass through
`window.cpp` rather than three sequential phases, since fixing a given
call site usually resolves an item from more than one "phase" at once.

## 5. Revised effort estimate

Given the above, the core `window.cpp`/`win_gtk.cpp` rework (former
Phases 2+3+4 combined) looks like **weeks of focused work on one file
family**, not the 4-8 months the three phases summed to individually —
provided the two open items below are scoped out or explicitly deferred.
The remaining ~18 per-widget `GTKGetWindow()` overrides are then pure
mechanical deletions (Phase 5), not redesign work.

## 6. Open decision: `wxPopupWindow` (self-contained, not blocking)

`src/gtk/popupwin.cpp` implements `wxPopupWindow` as its own
`gtk_window_new(GTK_WINDOW_POPUP)` — a real (if special-typed) toplevel,
not a subwindow of anything (`popupwin.cpp:105`). GTK4 removed the
`GTK_WINDOW_POPUP` type hint entirely; the replacement primitive is
`GtkPopover`/`GdkPopup`, which is anchored to a parent widget rather than
positioned in screen coordinates the way `wxPopupWindow` currently is
(`popupwin.cpp:183` computes absolute screen position). This affects
`wxComboBox`/`wxChoice` dropdown lists, tooltips, and context-menu-style
popups built on `wxPopupWindow`.

This is entirely self-contained to `popupwin.cpp` and its ~3-4 direct
consumers — it doesn't touch the core window model above and can be
designed and built independently, in parallel with the `window.cpp`
rework. Flagging it here because it's a real API-shape change (anchor +
relative position vs. absolute screen position), not just a rename, so
it deserves its own short design note when it's picked up — not
attempting that design in this document.

## 7. Open decision needed: is `wxNativeContainerWindow` in scope?

`src/gtk/nativewin.cpp:114` (`gdk_window_reparent`) is the *only* place
in the tree that reparents a raw `GdkWindow` — used by
`wxNativeContainerWindow`/`wxNativeWindow` to embed a foreign
(non-wx-created) native window by XID inside a wx window. The file
already has a standing `// TODO: we probably need equivalent code for
other GDK platforms` for non-X11 (`nativewin.cpp:79`), meaning it's
X11-only today even under GTK3.

GTK4 does not expose a general "reparent an arbitrary foreign X11/Wayland
window into my widget tree" mechanism — the closest things (XEmbed-style
socket embedding, `GtkSocket`/`GtkPlug`) were removed from GTK entirely
starting in GTK3.14 and have no GTK4 equivalent; Wayland's security model
makes foreign-window embedding by XID largely impossible in the way X11
allowed it.

**This needs an explicit decision, not a technical answer** — the
options are: (a) drop `wxNativeContainerWindow` support on the GTK4
build and document it as a known platform limitation, or (b) if there's
a concrete use case, investigate whether a portal-based or
protocol-specific mechanism (e.g. `xdg-foreign` for Wayland) could serve
the same purpose, which would be a materially different implementation
than what exists today. Recommend (a) unless something in your actual
usage depends on it — it's a narrow, rarely-used feature, and chasing a
technical workaround for it would be exactly the kind of scope creep the
rest of this plan is trying to avoid.

## 8. Suggested next step

Start the `window.cpp` rework described in §4 as one coordinated
workstream (former Phases 2-4). Concretely, in order:
1. `wxGetTopLevelGDK()` → `GdkDisplay*` swap (§4.2) — smallest, most
   mechanical, good first PR to validate the approach.
2. `wxGetTopLevel()` → `GdkSurface*` swap for the toplevel/backend-interop
   call sites (§4.3).
3. Event-controller input model (former Phase 3) — this is what makes
   §4.1's deletions possible, so it has to land before the `GTKGetWindow`
   family can be removed.
4. `draw`→`snapshot` + clip/size migration (former Phase 4), folded in
   as described in §4.4.
5. Delete the now-dead `GTKGetWindow`/`GTKIsOwnWindow` machinery and its
   ~18 per-widget overrides (mechanical cleanup, former Phase 5 item).

`wxPopupWindow` (§6) and `wxNativeContainerWindow` (§7) are separate,
parallelizable tracks once §7's scope decision is made.
