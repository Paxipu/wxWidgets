# Phase 4 design: the `draw` → `snapshot` paint model

The last unstarted phase, and now the single largest blocker in the port:
`window.cpp` cannot compile without it (about 30 of its remaining errors are
here), and `dc.cpp`, `generic/graphicc.cpp`, `overlay.cpp` and `image_gtk.cpp`
are queued behind the same change. Until it lands, `test_gui` cannot link and
**nothing in this port can be runtime-verified**, which is why it matters more
than its error count suggests.

As with the style-context work, the findings below were measured against real
GTK4 4.14.5 rather than read off headers. The probe is committed as
`docs/gtk/probes/gtk4-snapshot-cairo.c`.

## 1. What changes

GTK3 widgets implement a `draw` vfunc taking a `cairo_t*`. GTK4 replaced it
with `snapshot`, taking a `GtkSnapshot*` that accumulates *render nodes* —
a retained scene graph the renderer diffs and replays, rather than immediate
drawing commands.

wx's painting is entirely cairo-based: `GTKSendPaintEvents(cairo_t*)` sets up
the clip and update region, then hands the `cairo_t` to `wxPaintDC` via
`m_paintContext`. Rewriting that onto render nodes would mean rewriting
`wxGraphicsContext` and every `wxDC` operation — a far larger job than this
port.

## 2. The finding that makes this tractable

`gtk_snapshot_append_cairo()` returns a real `cairo_t` that draws into the
snapshot. Measured:

```
snapshot #1: widget 200x100
  cairo clip extents : 0.0,0.0 .. 200.0,100.0
  user (0,0) maps to device 0.0,0.0  -> widget-relative
render node produced: yes
  node bounds: 200x100 at 0,0
```

Two things matter here:

- **The coordinate space is widget-relative**, identical to what the GTK3
  `draw` vfunc gave a windowless widget. So `GTKSendPaintEvents(cairo_t*)`,
  and everything downstream of it, needs no coordinate changes at all.
- **It rasterises**: a genuine render node comes out, and this worked with the
  widget merely allocated inside a window — no realization dance required.

So Phase 4 is **not** a rewrite of wx's drawing. It is: give `wxPizza` a
`snapshot` vfunc, obtain a `cairo_t` from it, and feed the existing paint path.

## 3. The real cost: update regions disappear

This is the part that cannot be preserved, and it should be decided
deliberately rather than discovered later.

GTK3 handed the `draw` handler a `cairo_t` already clipped to the **damage
region**. `GTKSendPaintEvents()` reads that back out:

```cpp
double x1, y1, x2, y2;
cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
if (x1 >= x2 || y1 >= y2)
    return;                       // nothing damaged, skip painting entirely
m_updateRegion = wxRegion(int(x1), int(y1), int(x2 - x1), int(y2 - y1));
```

and that becomes `wxWindow::GetUpdateRegion()`, which applications use to
repaint only what changed.

GTK4 gives a widget no damage information whatsoever. There is no partial
invalidation API — `gtk_widget_queue_draw_area()` was removed, leaving only
whole-widget `gtk_widget_queue_draw()` (already encountered in `win_gtk.cpp`,
status update 8) — and the snapshot vfunc is called to rebuild the widget's
whole scene, with the *renderer* deciding what actually needs repainting by
diffing render nodes.

Consequences, all of which should be stated in the port's user-facing notes:

- `wxWindow::GetUpdateRegion()` will report the full client area under GTK4.
- `wxPaintDC` clipping to the update region becomes a no-op.
- The early-out above never triggers, so `wxEVT_PAINT` handlers run in full
  every time.

**Correctness is preserved** — repainting more than necessary is always safe.

The practical severity of this is **lower than it first appears**, and the
first draft of this document overstated it. Two things temper it:

- **wxGTK3 already discarded most of the precision.** The code above does not
  read the damage region and hand it to wx. It takes
  `gdk_window_get_clip_region()` — the *window's* region, not the damage —
  reduces it to its `extents`, intersects the cairo clip with that, then takes
  `cairo_clip_extents()` and builds `m_updateRegion` from a **single
  rectangle**. Any multi-rectangle damage was therefore already collapsed to
  one bounding box before an application saw it, so a scattered repaint
  already reported an area far larger than what actually changed.
- **In practice GTK3 tended to damage the whole window anyway**, so for most
  applications the reported update region was already the full client area
  much of the time. (Reported from experience with wxGTK3 by the maintainer of
  a wxWidgets application; consistent with the coarseness the code shows.)

So for *scattered* damage the real change is from "bounding box of the damage,
often the whole window" to "always the whole window" — a narrowing of an
already-coarse guarantee, not the loss of a precise one.

**For an explicit `RefreshRect()` it is the loss of a precise one, and this
document originally said otherwise.** Measured afterwards, on a 300x200
window, with `Window::RefreshRectUpdateRegion` in
`tests/controls/windowtest.cpp`:

| `RefreshRect(20,30 100x40)` | reported update region |
|---|---|
| GTK+ 3 3.24.52, X11 | `20,30 100x40` |
| GTK+ 3 3.24.52, Wayland | `20,30 100x40` |
| GTK4 4.22.4, X11 | `0,0 300x200` |
| GTK4 4.22.4, Wayland | `0,0 300x200` |

A single `RefreshRect()` is not scattered damage, so the bounding-box
coarseness above does not apply to it, and that is precisely the case
incremental drawing uses: a canvas invalidating one cell per mouse motion —
what `demos/life` does — did a one-cell repaint under GTK+ 3 and does a
full-window repaint under GTK4.

The decision stands: wx cannot report less than it redraws, and under GTK4 it
must redraw everything. But the cost is real for paint-heavy applications and
belongs in the port's user-facing notes as such, not as "costs nothing in
practice".

Efficiency is not lost overall either: GTK4's renderer culls by diffing render
nodes, so the work still gets skipped, just below wx rather than inside it.

## 4. Work items

1. **`wxPizza`**: implement `GtkWidgetClass::snapshot`, replacing the `draw`
   signal connection in `window.cpp` (~line 3975). Chain up so child widgets
   are still snapshotted (`gtk_widget_snapshot_child()`).
2. **`GTKSendPaintEvents(cairo_t*)`**: drop the `gdk_window_get_clip_region()`
   preamble (there is no window and no damage region); set `m_updateRegion`
   from `gtk_widget_get_width()/get_height()`. The RTL mirroring below it uses
   `gdk_window_get_width()` and becomes `gtk_widget_get_width()`.
3. **`draw_border`** (`window.cpp` ~652): drawn on the *parent's* draw signal
   with `gtk_cairo_should_draw_window()` deciding which window is being
   painted. With one surface per toplevel that test is meaningless; the border
   should be drawn by wxPizza's own snapshot instead. This is also what would
   restore the `BORDER_STYLES` rendering gap left open in status update 8.
4. **`draw_freeze`** (`window.cpp` ~8096): freezes painting by connecting to
   `draw` and returning TRUE. Needs a snapshot-vfunc equivalent, or a flag
   checked in wxPizza's snapshot.
5. **`dc.cpp` / `graphicc.cpp` / `overlay.cpp` / `image_gtk.cpp`**: expected to
   be mostly mechanical once the `cairo_t` is flowing, since they consume a
   `cairo_t` rather than producing one — but this is an assumption, not yet
   verified, and should be checked before being relied on.

## 5. Recommended order

Items 1 and 2 together are the smallest change that makes `wxPizza` paint at
all, and they are independently compile-verifiable. Do those first and
re-measure before touching the rest: if the assumption in item 5 holds, the
remaining files may fall out cheaply, and if it doesn't, that is much better
discovered against a working paint path than in the middle of one.

Item 3 is where the risk sits. Border drawing was already broken by wxPizza
going windowless, and it is the one piece here that cannot be confirmed by
compilation — it needs a rendered comparison against GTK3, which needs
`test_gui`, which needs this phase. Expect to have to leave it visibly
imperfect and documented rather than silently wrong.
