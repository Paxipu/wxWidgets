# GTK4 port: the menu subsystem

Status: design, grounded in `docs/gtk/probes/gtk4-menu-actions.c`.

`src/gtk/menu.cpp` is not a file that needs translating. Every GTK type it is
built on was deleted:

| GTK3 | GTK4 |
| --- | --- |
| `GtkMenu` | removed |
| `GtkMenuItem` | removed |
| `GtkMenuBar` | removed |
| `GtkCheckMenuItem` | removed |
| `GtkRadioMenuItem` | removed |
| `GtkSeparatorMenuItem` | removed |
| `GtkImageMenuItem` | removed (already removed in GTK3.10) |
| `GtkTearoffMenuItem` | removed |
| `GtkAccelGroup` | removed |
| `gtk_menu_popup_at_*()` | removed |

The replacement is not a widget set at all. GTK4 menus are **declarative**: a
`GMenuModel` describes the structure, `GAction` objects carry the behaviour,
and `GtkPopoverMenu` / `GtkPopoverMenuBar` are dumb views that render a model.
Nothing in that stack hands you a per-item widget, so the GTK3 backend's
central data structure -- `wxMenuItem::m_menuItem`, a `GtkWidget*` -- has no
counterpart.

## 1. Why this is a rewrite we are allowed to do

The reason this is tractable is that the GTK-specific menu state is, by
accident of history, completely encapsulated:

* `wxMenuItem::GetMenuItem()` (the one returning `GtkWidget*`) is referenced
  **only inside `src/gtk/menu.cpp`**. The other hits in the tree --
  `src/common/framecmn.cpp:438`, `src/generic/accel.cpp:187`,
  `include/wx/event.h:2708`, `include/wx/accel.h:76` -- are a different,
  unrelated `GetMenuItem()` that returns `wxMenuItem*`.
* `wxMenu::m_menu`, `m_owner` and `m_accel` are used outside `menu.cpp` in
  exactly one place, `wxWindowGTK::DoPopupMenu()` in `src/gtk/window.cpp`,
  and that function is already `#ifdef`ed out for GTK4.

So the entire backend can be replaced without touching a single caller. That
was worth establishing before writing any code: had per-item `GtkWidget*`s
leaked into generic or application-facing code, this port would have needed a
compatibility shim rather than a rewrite.

## 2. What the probe established

`docs/gtk/probes/gtk4-menu-actions.c`, run against GTK 4.14.5 under Xvfb:

```
(1) named action via shortcut controller resolved: yes
(2) accel attribute survives into the model: yes (<Control>q)
(4) private attribute survives into the model: yes (5101)
(3) radio activation reported target: yes ("b")
    check activation toggled state: yes (1)
(6) disabled action reports disabled: yes
(5) popover menu shown/closed signals: yes (1/1)
    model item count after late append: 6
(7) popover menu bar built and realized: yes
    menubar survives remove+insert of its submenu: yes
```

The load-bearing result is (1). Menu accelerators under GTK4 have to come from
a `GtkShortcutController` holding `GtkShortcut`s whose action is a
`GtkNamedAction`, and named actions are resolved by walking *up* the widget
hierarchy looking for a matching action group. It was not obvious that a group
installed with `gtk_widget_insert_action_group()` on the frame would be found
by a controller also attached to the frame; it is. That single fact is what
makes it possible to keep menu accelerators working at all.

A separate one-line probe established that **`GdkTexture` implements `GIcon`**.
That matters because `GMenuItem` accepts only a `GIcon` for its icon
attribute, which at first reading looked like it ruled out arbitrary
`wxBitmap` menu icons. It does not: `wxBitmap -> GdkPixbuf -> GdkTexture` is a
`GIcon`, so `wxMenuItem::SetBitmap()` remains implementable.

## 3. The mapping

| wx | GTK4 |
| --- | --- |
| `wxMenu` | a `GMenu` (the model) + a `GSimpleActionGroup` (the behaviour) |
| `wxMenuItem`, normal | `GMenuItem` + `GSimpleAction`, stateless |
| `wxMenuItem`, check | `GMenuItem` + `GSimpleAction` with boolean state |
| `wxMenuItem`, radio | `GMenuItem` with a target + one `GSimpleAction` with string state shared by the whole radio run |
| `wxMenuItem`, separator | a section boundary (`g_menu_append_section`) |
| `wxMenuItem`, submenu | `g_menu_append_submenu()` referencing the sub-`GMenu` |
| `wxMenuBar` | a `GMenu` of submenus, rendered by `GtkPopoverMenuBar` |
| popup menu | `GtkPopoverMenu` parented on the invoking window |
| `Enable()` | `g_simple_action_set_enabled()` |
| `Check()` / `IsChecked()` | the action's state |
| accelerator display | the item's `"accel"` attribute |
| accelerator activation | `GtkShortcutController` + `GtkNamedAction` on the frame |
| bitmap | `g_menu_item_set_icon()` with a `GdkTexture` |

### 3.1 Rebuild rather than patch

`GMenu` copies a `GMenuItem`'s attributes when the item is inserted. There is
no "get me item 3 and change its label" -- mutation means
`g_menu_remove(pos)` followed by `g_menu_insert_item(pos, newItem)`. Combined
with separators being modelled as *sections* (so wx position N is generally
not GMenu position N), incremental editing would need a wx-position-to-GMenu-
path mapping that has to stay correct across every insert, remove and
separator change.

So the backend does not patch the model. Any structural change --
append, insert, remove, label change, bitmap change, accel change -- calls
`wxMenu::GTKRebuildModel()`, which clears the `GMenu` and the action group and
regenerates both from the wx item list. Menus have tens of items, this is
O(n) per edit, and it removes an entire category of index-mapping bugs. The
probe confirmed a live `GtkPopoverMenuBar` survives its model being emptied
and refilled underneath it.

State-only changes (`Enable()`, `Check()`) do *not* rebuild; they go straight
to the `GAction`, which is what keeps an open menu responsive.

### 3.2 Action naming

Each `wxMenu` owns a `GSimpleActionGroup` and a process-unique prefix,
`wxmN`. Item actions are named `iN` (unique serial) and radio-group actions
`rN`, so a fully qualified name looks like `wxm7.i42`.

Actions live in the group of the menu that owns the item -- they are never
moved when a menu becomes a submenu of another. Instead, installing a menu on
a widget walks the whole menu tree and inserts *every* group in it; detaching
removes them all. This keeps attach/detach the only recursive operation and
means a submenu can be built in isolation and attached later, which wx code
does routinely.

The unique-prefix-per-menu scheme also means two menus on the same frame can
hold items with the same wx ID without colliding, which the GTK3 backend got
for free by using widgets.

### 3.3 Radio groups

GTK4 renders a menu item as a radio (rather than a check) when the item
carries a *target* and its action carries *state*; the item is drawn selected
when state equals target. So a radio run needs one shared stateful action.

wx determines radio groups by adjacency -- a maximal run of consecutive
`wxITEM_RADIO` items -- which is also what the GTK3 backend reconstructed by
hand in `wxMenu::GtkAppend()`. Because the model is rebuilt wholesale anyway,
the runs are simply recomputed on each rebuild and one string-state action is
created per run, with each member's target being its index within the run.
The hand-rolled "look at the previous item, then the next item" group-joining
logic in the GTK3 path disappears.

### 3.4 Menubar `EnableTop()`

A `GMenuModel` submenu item has no action, so there is nothing to disable.
`EnableTop(pos, false)` is therefore implemented by rebuilding the menubar
model with that entry emitted as a plain item bound to a permanently disabled
action instead of as a submenu: the label still shows, greyed, and clicking it
does nothing. `IsEnabledTop()` reads back wx's own flag rather than GTK's.

## 4. Capability losses, and why

These are the places where GTK4 removed something that wx exposes. Each is
`#ifdef`ed and documented in the headers, per the project rule that a feature
that cannot be ported may be dropped behind a feature test.

* **`wxEVT_MENU_HIGHLIGHT`.** GTK3 emitted `select`/`deselect` per menu item.
  A `GMenuModel` has no notion of a highlighted item and `GtkPopoverMenu`
  does not expose the internal `GtkModelButton`s it builds, so there is no
  supported hook. Not emitted under GTK4.
* **`wxEVT_MENU_OPEN` / `wxEVT_MENU_CLOSE` for menubar menus.** These still
  work for popup menus, where wx owns the `GtkPopoverMenu` and can watch its
  `show`/`closed` signals (probe result 5). `GtkPopoverMenuBar` creates its
  per-menu popovers internally and does not publish them, so menubar
  drop-downs cannot be observed. `UpdateUI()` is consequently driven from
  popover show for popup menus only.
* **`wxMENU_TEAROFF`.** `GtkTearoffMenuItem` was removed in GTK3 and the
  concept does not exist in GTK4. The style flag is accepted and ignored.
* **`wxMB_DOCKABLE`.** `GtkHandleBox` is gone. Already ignored under GTK3
  since 3.19.7; now ignored unconditionally.
* **Stock accelerators.** `gtk_stock_lookup()` is gone, so an item like
  `wxID_COPY` no longer picks up GTK's default `Ctrl+C` automatically. An
  explicit accelerator in the item label still works.
* **Per-item layout direction.** There are no per-item widgets to call
  `gtk_widget_set_direction()` on. The direction is set on the menubar widget
  and inherited.

## 5. Testing

The invariants this port relies on are asserted in
`build/tools/gtk4-invariants.c`, which CI runs before the build. The menu
additions there check GTK's own behaviour rather than wx's:

* a named action resolves through `gtk_widget_insert_action_group()`;
* the `accel` attribute round-trips through a `GMenu`;
* a stateful string action reports the activated target;
* a `GtkPopoverMenuBar` survives its model being emptied and refilled.

If a future GTK release changes any of these, CI reports it as a platform
change with a pointed message rather than as an unexplained wx failure.
