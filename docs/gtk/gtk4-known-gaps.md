# wxGTK4: what does not work, and why

This answers one question -- *what should an application not expect to
work if it is built against GTK 4?* -- in one table, because that is the
question a reviewer or a porting user asks first, and neither
`gtk4-status.md` (a narrative of how the port got here) nor
`gtk4-upstream-summary.md` (an offer of the work to upstream) is shaped
to answer it.

Every row was checked against the tree it describes rather than copied
from the issue that reported it; where a row rests on a measurement, the
probe that made it is named. Filed for #106, gate 8.

The measurements below were taken against **GTK 4.14.5**. That matters
for two rows in particular -- what GTK does with `GtkAlertDialog` and
which Wayland protocols it binds -- so a later GTK may move them, and
each says which versions it was checked against.

## First, the thing that is easy to get wrong

**A GTK4 build is not a reduced build.** Configuring the same tree twice
with the same options, changing only `--with-gtk`, the two generated
`setup.h` files differ in exactly two of 280 `wxUSE_` settings:

| setting | GTK+ 3 | GTK 4 | why |
|---|---|---|---|
| `wxUSE_APPINDICATOR` | 1 | 0 | deliberate; see wxTaskBarIcon below |
| `wxUSE_SPELLCHECK` | 0 | 1 | not a port difference: `wxTextCtrl` spell checking wants `gspell-1` under GTK+ 3 and `libspelling-1` under GTK 4, and only the latter was installed on the machine that ran this |

So exactly one of the 280 settings differs for a reason to do with the
port, and it is a deliberate one. The gaps below are all *behavioural*,
and none of them can be found by looking at which features got compiled
in.

The same goes for the removed GTK+ 3 APIs that dominate `gtk4-status.md`
-- `GtkContainer`/`GtkBin`, `gtk_widget_get_window`, `gtk_box_pack_start`,
the drag-and-drop and clipboard rewrite, `GTK_WINDOW_POPUP`, the
GFile-based choosers. None of those symbols exist in the GTK 4 headers
(checked: zero occurrences under `/usr/include/gtk-4.0`), and the GTK4
library builds, so every one of them has been dealt with. Each of the
files that carries one has a GTK4 branch: `dnd.cpp`, `clipbrd.cpp`,
`popupwin.cpp`, `filedlg.cpp`, `dirdlg.cpp`.

## The table

"Fixable" means fixable *inside wxWidgets*. Several rows are not, and
saying so is the point of the column: they are consequences of what
Wayland and GTK 4 offer, and an application that needs them should run
under X11, which `GDK_BACKEND=x11` still selects.

| What | State under GTK 4 | Why | Fixable in wx? |
|---|---|---|---|
| `wxWindow::Move()` on a top level window | No effect under Wayland. No `wxEVT_MOVE` is sent any more either; `GetPosition()` still answers what was asked for | Wayland has no request to position a toplevel | No (#166) |
| `ClientToScreen()`, `ScreenToClient()`, `GetScreenPosition()`, `GetScreenRect()` | Under Wayland these are relative to the top level window, not the screen | A client is not told where the compositor put it | No (#214) |
| `wxGetMousePosition()` | Under Wayland, in the coordinates of whichever surface the pointer is over | Same | No (#134) |
| `wxUIActionSimulator` | Reports failure under Wayland rather than synthesising input | Needs XTest, which is an X11 extension | No (#69) |
| `wxNativeContainerWindow` | Compiled out; `wxHAS_NATIVE_CONTAINER_WINDOW` is undefined, as it already is on Cocoa | `GtkSocket`/`GtkPlug` were removed from GTK in 3.14, and Wayland has no window IDs to embed by | No |
| `wxClientDC` drawing outside a paint handler | `wxClientDC::CanBeUsedForDrawing()` returns false; use `wxOverlay` | GTK 4 draws through snapshots, not to a live surface. wxOSX and wxQt already return false here, and so does wxGTK 3 on Wayland | No |
| `wxPopupWindow` with no parent | Fails to create | It is a `GtkPopover`, anchored to a rectangle in a parent widget; GTK 4 removed `GTK_WINDOW_POPUP` | No |
| `wxMessageDialog` caption, icon styles, selectable message text | Ignored | It is a `GtkAlertDialog`, which has no title, no icon and no selectable text. GTK 4 removed `GtkMessageDialog` | No |
| Dragging a floating wxAUI pane back into its dock | Does not dock under Wayland | Docking is driven from the pane's motion, and a compositor-driven move reports none. `xdg_toplevel_drag_v1` exists for this case, but neither GTK 4.14.5 nor 4.22.4 implements it -- read from the library's own Wayland interface strings, with `xdg_wm_base` as the control that shows the reading works | **Yes, once GTK does** (#167) |
| `wxTaskBarIcon` | Works, through wx's own StatusNotifierItem and dbusmenu implementation. Needs a `StatusNotifierWatcher` on the session bus, so a desktop whose panel offers none still shows nothing | Wayland has no tray protocol, and Ayatana's appindicator ships GTK+ 2 and GTK+ 3 builds only | Done (#198, #216) |
| Printing | Goes through `xdg-desktop-portal`. If a portal service is registered on the bus but does not answer, the dialog never returns; with no portal at all, GTK falls back to its own dialog and all is well | Not a wx defect; measured in `gtk4-printing.md` | No (#161) |

## Where the evidence is

* Wayland rows: `docs/gtk/wayland-testing.md` for the numbers, and
  `docs/gtk/probes/` for the programs that produced them -- each with the
  control run that shows the probe could have failed.
* `wxClientDC`: `src/gtk/dc.cpp`, the two `CanBeUsedForDrawing()` bodies.
* `wxPopupWindow`: `src/gtk/popupwin.cpp`, and the note now on
  `wxPopupWindow` in `interface/wx/popupwin.h`.
* `wxMessageDialog`: the note on `wxMessageDialog` in
  `interface/wx/msgdlg.h`.
* `wxTaskBarIcon`: `src/gtk/statusnotifier.cpp`, `src/gtk/dbusmenu.cpp`,
  and `docs/gtk/probes/sni-roundtrip.sh`, whose control run starts no
  item and must time out.

## What this table is not

It is not the list of open bugs -- that is the issue tracker -- and it is
not a list of things nobody has looked at. Every row here is a decision
that has been made and can be defended, or a limitation of the platform
underneath. A row leaves this table by being fixed or by being shown to
have been wrong, not by being reworded.
