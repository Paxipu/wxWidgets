# Moving `wxDataViewCtrl` from `GtkTreeView` to `GtkColumnView`

Design document for the largest single remaining piece of #180.

`src/gtk/dataview.cpp` is 6009 lines, of which **5238 are compiled under
GTK4**, and it holds **142 of the port's 405 remaining deprecation warnings**
-- more than a third. Every other family under #173 is either done, blocked on
a GTK limitation, or small.

Unlike the other migrations in this port, this one cannot be staged: there is
no intermediate state in which half the control is on `GtkTreeView` and half
on `GtkColumnView`. This document exists because a change of that size should
be agreed before it is written, not after.

## 1. Why it cannot be done in pieces

The 142 warnings are spread across **84 functions**. The largest single
concentration is seven, in `wxDataViewCtrl::Create()`. There is no hot spot to
attack first:

```
 58  gtk_tree_view_*             the view itself
 36  gtk_tree_view_column_*      columns
 17  gtk_tree_selection_*        selection
 13  gtk_tree_path_*             addressing a row
 11  gtk_cell_renderer_*         drawing a cell
  5  gtk_tree_model_*            the model interface
  2  gtk_list_store_*
  2  gtk_cell_editable_*
  1  gtk_tree_sortable_*
  0  gtk_tree_drag_*             -- see below
```

Each of these families is reachable only through a `GtkTreeView`. Replacing
the view replaces all of them at once, and keeping the view keeps all of them.

## 2. What is already settled

**Feasibility.** `docs/gtk/probes/gtk4-columnview-vs-dataview.c` builds each
piece `wxDataViewCtrl` needs on `GtkColumnView` and reports:

```
tree rows, autoexpanded      n=3 (parent+child+leaf, ok)
column view sorter           present
cell drew itself             yes
editable cell widget         hosted
VERDICT all needed pieces present
```

So this is work rather than a wall, which is not what #181, #182 and #183
turned out to be.

**Drag and drop is not a problem, and this was the biggest open question.**
The probe deliberately did not answer it, because `dataview.cpp` names
`GtkTreeDragSource` and `GtkTreeDragDest` in 23 places and those have no
`GListModel` equivalent. Measured since: the GTK4 build produces **zero**
`gtk_tree_drag_*` warnings, because every one of those uses is already inside
`#ifndef __WXGTK4__`. Drag and drop is not in the GTK4 build at all, so the
migration does not have to invent a story for it.

**There is a safety net.** Before this document, the test suite for a
6009-line control was 12 cases and 130 assertions, and **every one of them
asked the control about its model**. Not one asked whether anything had been
laid out. That is the shape of bug that reached a release in #187: a renderer
that drew zero pixels while the whole suite and all of CI stayed green.

Five tests were added first (#204), each demonstrated to fail when the
behaviour it pins is broken on purpose:

| test | broken on purpose | what failed |
|---|---|---|
| `HitTestFindsItems` | `HitTest()` stubbed | 2 cases, 8 assertions -- and **none of the 12 existing tests noticed** |
| `ItemRectsDoNotOverlap` | `GetItemRect()` stubbed | 5 cases, 27 assertions |
| `HitTestFindsColumns` | | |
| `VisibleRange` | | |
| `SortingChangesRowOrder` | descending sort silently dropped | 3 assertions |

## 3. The shape of the change

### 3.1 The model

Today `wxDataViewCtrlInternal` owns `GtkWxTreeModel`, a GObject implementing
`GtkTreeModel`, `GtkTreeSortable`, and (outside GTK4) the two drag
interfaces. Alongside it, `wxGtkTreeModelNode` maintains a **mirror of the wx
model's tree**, because `GtkTreeModel` addresses rows by `GtkTreePath` and
somebody has to be able to turn a path into a `wxDataViewItem`.

`GtkColumnView` does not use `GtkTreeModel` at all. It takes a
`GtkSelectionModel` over a `GListModel`, and a tree is expressed by wrapping
that in a `GtkTreeListModel`, which asks a callback for the child model of a
row when it needs one:

```
wx model -> GListModel of item objects
         -> GtkTreeListModel   (children fetched lazily per row)
         -> GtkSortListModel   (when a sort column is set)
         -> GtkSingleSelection / GtkMultiSelection
         -> GtkColumnView
```

The mirror tree can go: `GtkTreeListModel` keeps the expansion state and hands
back a `GtkTreeListRow` per visible row, from which the depth, the parent and
the expanded flag are all readable. That removes `wxGtkTreeModelNode`
entirely, roughly 200 lines, and with it the class of bug where the mirror and
the real model disagree.

**The trap, already paid for once in #195:** `GtkTreeListModel` wraps every
item in a `GtkTreeListRow`. A cell factory that calls
`gtk_list_item_get_item()` receives the *row*, not the item, and casting it to
the item type reads rubbish out of it. The only symptom is a GObject cast
critical followed by Pango complaining about invalid UTF-8.

### 3.2 The columns and the renderers

`wxDataViewColumn` wraps `GtkTreeViewColumn`; each `wxDataViewRenderer`
subclass wraps a `GtkCellRenderer`:

| wx class | today | on `GtkColumnView` |
|---|---|---|
| `wxDataViewTextRenderer` | `gtk_cell_renderer_text_new()` | `GtkLabel`, or `GtkEditableLabel` when editable |
| `wxDataViewBitmapRenderer` | `gtk_cell_renderer_pixbuf_new()` | `GtkImage` |
| `wxDataViewToggleRenderer` | `gtk_cell_renderer_toggle_new()` | `GtkCheckButton` |
| `wxDataViewProgressRenderer` | `gtk_cell_renderer_progress_new()` | `GtkProgressBar` |
| `wxDataViewIconTextRenderer` | pixbuf + text renderer | `GtkBox` of `GtkImage` + `GtkLabel` |
| `wxDataViewChoiceRenderer` | `gtk_cell_renderer_combo_new()` | see below |
| `wxDataViewCustomRenderer` | a `GtkCellRenderer` subclass drawing through `wxDC` | `GtkDrawingArea` with a draw function |

Each becomes a `GtkSignalListItemFactory` with two handlers: `setup`, which
creates the widget once per recycled row, and `bind`, which fills it from the
item. The probe confirms both the self-drawing cell and the editable cell
work.

**`wxDataViewChoiceRenderer` is the one to watch.** Its natural replacement is
`GtkDropDown`, and #183 established that `GtkDropDown` cannot be returned to
"nothing selected" once its model is non-empty -- which is why `wxChoice`
itself is *not* being migrated. Inside a cell the constraint may not bite,
because a cell always has a value, but this needs its own measurement before
the migration relies on it.

### 3.3 The control

44 `wxDataViewCtrl` methods are implemented in this file. The ones that need
real thought rather than mechanical translation:

- `HitTest()` and `GetItemRect()` -- pinned by the tests added in #204, and
  the reason those were written first. `gtk_widget_pick()` replaces
  `gtk_tree_view_get_path_at_pos()`, and **needs `GTK_PICK_NON_TARGETABLE`**
  or it stops at the column view itself and answers "nothing here" everywhere
  (measured in `gtk4-listview-vs-listbox.c`).
- `GetCountPerPage()` and `GetTopItem()` -- also pinned by #204.
- `Expand()` / `Collapse()` / `IsExpanded()` -- become
  `gtk_tree_list_row_set_expanded()`, which is simpler than today.
- `SetSelections()` / `GetSelections()` -- `GtkSelectionModel`; note the trap
  from #195, that the last argument of
  `gtk_selection_model_select_item()` means "unselect everything else".
- Sorting -- `GtkColumnViewColumn` carries a `GtkSorter`, and the wx model's
  `Compare()` goes into a `GtkCustomSorter`.

## 4. What has to be measured before this is written

Three things, each of which would produce a plausible wrong answer rather than
an error if guessed:

1. **`wxDataViewChoiceRenderer` in a cell** -- does the #183 deselection
   problem apply inside a `GtkColumnView` cell, where there is always a value?
2. **In-place editing end to end** -- the probe hosted an editable widget in a
   cell, but did not run wx's editing protocol
   (`wxEVT_DATAVIEW_ITEM_EDITING_STARTED` / `_DONE`, validation, cancellation)
   through it.
3. **Row height with a custom renderer** -- `wxDataViewCtrl::SetRowHeight()`
   sets a uniform height today. A `GtkDrawingArea` in a cell reports its own
   minimum, and a cell that asks for more than the uniform height will silently
   win.

## 5. Staging, and what "done" means at each step

The change lands as one commit series on one branch, but it is written and
verified in this order, and each step has a gate that must be green before the
next begins:

| step | gate |
|---|---|
| 1. probes for the three questions in §4 | committed under `docs/gtk/probes/` with their measured output |
| 2. model layer: `GListModel` + `GtkTreeListModel` over the wx model | the control shows rows; #204's `HitTestFindsItems` passes |
| 3. columns and the non-custom renderers | `HitTestFindsColumns`, `AppendTextColumn` |
| 4. sorting and selection | `SortingChangesRowOrder`, the five selection tests |
| 5. custom renderer and editing | the `dataview` sample renders and edits |
| 6. everything else | the full `[wxDataViewCtrl]` tag, then the full GUI suite |
| 7. GTK+ 3 and GTK+ 2 | rebuilt with **0 warnings** and their full suites green |

Step 7 is not a formality. The GTK+ 2 build has caught two defects in this
port that neither GTK4 nor GTK+ 3 saw, most recently a shared callback written
against `GdkRGBA`, which GTK+ 2 does not have.

## 6. The honest risk

`wxDataViewCtrl` is the most complex control in the GTK backend, this replaces
essentially all of it, and a partial result is worth nothing. Against that:

- the port currently **works** -- `dataview.cpp` on `GtkTreeView` compiles and
  passes its tests under GTK4, deprecations and all;
- the only gains are the 142 warnings and GTK5 readiness.

That second point is the whole argument, and it is @gunterkoenigsmann's:

> I don't want to hand in a port that is already known to have a limited
> lifetime.

`GtkTreeView` and everything under it is removed in GTK5. A port that ships on
it has to be rewritten anyway, by someone with less context than we have now.
