# Rebuilding `wxGtkStyleContext` for GTK4

Design document for the highest-leverage deferred item in
`docs/gtk/gtk4-status.md` (scoped in progress update 10): the
`wxGtkStyleContext` helper is unbuildable under GTK4, and it blocks
`statbox.cpp`, `notebook.cpp`, `renderer.cpp`, `srchctrl.cpp`,
`toplevel.cpp`, `generic/infobar.cpp`, and a large part of `settings.cpp`
itself.

Unlike the Phase 2/3 design docs, the findings here are **empirical**, not
read off headers: GTK4 4.14.5 is installed in the development environment
and `xvfb` is available, so the open questions were answered by compiling
and running probe programs against real GTK4 under a real (virtual) X
display. Those probes are preserved in `docs/gtk/probes/` so the findings
can be re-checked against another GTK4 version rather than taken on faith.

## 1. What the class is for

`wxGtkStyleContext` answers "what does the current GTK theme say about
widget type X" — colours, borders, padding, minimum sizes — for widgets
that are **never instantiated**. wxWidgets needs this to report system
metrics and colours (`wxSystemSettings::GetColour()`), to size its own
generic controls to match native ones, and to draw theme-consistent
decorations in `wxRendererNative`.

Under GTK3 it does this by building a *synthetic* CSS node path:

```cpp
wxGtkStyleContext sc;
sc.Add(GTK_TYPE_NOTEBOOK, "notebook", "notebook", "frame", nullptr);
sc.Add(G_TYPE_NONE, "header", "top", nullptr);
sc.Add(G_TYPE_NONE, "tabs", nullptr);
sc.Add(G_TYPE_NONE, "tab", nullptr);
// -> ask the theme for this node's border/padding/margin
```

built out of `gtk_widget_path_new()` / `gtk_widget_path_append_type()` /
`gtk_widget_path_iter_set_object_name()`, fed to a free-standing context
via `gtk_style_context_new()` + `gtk_style_context_set_path()`, and
chained to its ancestors with `gtk_style_context_set_parent()`.

## 2. Why it cannot survive GTK4 unchanged

Every mechanism in that paragraph is gone:

| GTK3 API | GTK4 status |
|---|---|
| `GtkWidgetPath` (the whole type) | removed, no replacement |
| `gtk_widget_path_new/append_type/iter_set_object_name/iter_add_class` | removed |
| `gtk_style_context_new()` | removed — a context can only come from a widget |
| `gtk_style_context_set_path()` | removed |
| `gtk_style_context_set_parent()` / `get_parent()` | removed |
| `gtk_style_context_get()` (varargs property query) | **removed** |
| `gtk_style_context_get_color()` | survives (deprecated in 4.10) |
| `gtk_style_context_get_border/padding/margin()` | survive, minus the `GtkStateFlags` parameter |
| `gtk_style_context_lookup_color()` | survives, deprecated in 4.10 with no replacement — still used, see §5 |
| `gtk_style_context_set_state()` | survives |

Two consequences, and they are of very different severity:

1. **Construction** must change: the only way to obtain a
   `GtkStyleContext` in GTK4 is `gtk_widget_get_style_context()` on a real
   widget. This is solvable — see §3.
2. **`gtk_style_context_get()`'s removal** takes with it the only way to
   query `"background-color"`, `"border-color"`, `"min-width"` and
   `"min-height"`. This is *not* fully solvable — see §5.

It is worth being precise about which of the class's operations each
consequence hits, because the answer is much better than "the whole class
is lost":

| Operation | Used by | GTK4 outcome |
|---|---|---|
| `get_border/padding/margin` (metrics) | statbox, notebook, renderer, settings | **works fully** |
| `Fg()` (`get_color`) | renderer, settings, infobar | **works fully** |
| `Bg()` (`"background-color"`) | settings, infobar, toplevel | approximation only |
| `Border()` (`"border-color"`) | settings | approximation only |
| `"min-width"`/`"min-height"` | notebook, renderer, settings | replaceable, arguably *better* |

The dominant use across the blocked files is metrics, which survive
intact. That reframes this from "a subsystem is lost" to "the construction
mechanism must be rebuilt, and two colour queries degrade".

## 3. The replacement: real, parented scratch widgets

The design is to back each `wxGtkStyleContext` with an actual widget
hierarchy instead of a synthetic path, and to reach interior CSS nodes by
walking real children.

This rests on four empirical findings (probe sources in
`docs/gtk/probes/`, run under `xvfb-run` against GTK4 4.14.5):

**Finding 1 — interior CSS nodes are real, walkable child widgets.**
`gtk_widget_get_first_child()`/`gtk_widget_get_next_sibling()` descend
straight into the structure the GTK3 code used to name synthetically:

```
<GtkNotebook>   css_name="notebook" classes=[frame]  border=1,1
  <GtkBox>      css_name="header"   classes=[top,horizontal]  padding=1,1
    <GtkGizmo>  css_name="tabs"     padding=4,0
      <GtkGizmo> css_name="tab"     padding=12,3
<GtkScrollbar>  css_name="scrollbar"
  <GtkRange>    css_name="range"
    <GtkGizmo>  css_name="trough"
      <GtkGizmo> css_name="slider"  border=4,4
<GtkCheckButton> css_name="checkbutton"
  <GtkBuiltinIcon> css_name="check" border=1,1
```

So `Add(G_TYPE_NONE, "header")` becomes "descend to the child whose
`gtk_widget_get_css_name()` is `header`" — a direct, faithful translation
of what the synthetic path meant.

**Finding 2 — style resolves on unrealized, unmapped, unparented
widgets.** Every metric above was read without realizing a widget or
attaching it to a displayed window. This matters enormously: it means the
approach works headlessly, in the same "no display" contexts where
wxWidgets currently queries system settings, and does not require mapping
scratch windows on screen.

**Finding 3 — ancestry genuinely affects resolution, so the chain must be
really parented.** This is the finding that rules out the tempting
shortcut of creating each node standalone:

```
standalone label               -> 1.000 1.000 1.000
label parented into headerbar  -> 1.000 1.000 1.000
label parented into button     -> 0.180 0.204 0.212   <-- differs
```

A label's resolved colour depends on its ancestors, exactly as CSS
descendant selectors imply. `AddButton().AddLabel()` must therefore
create a real `GtkLabel` and really put it inside the real `GtkButton`.

**Finding 4 — lifecycle is clean and cheap.** Fresh widgets are floating
(`g_object_is_floating() == 1`), so the root needs `g_object_ref_sink()`;
children are owned by their parent, and destroying a `GtkWindow` root
disposes the whole tree (verified with a weak pointer). One owned root
pointer per `wxGtkStyleContext` is a sufficient ownership story.

### Resulting shape

```cpp
class wxGtkStyleContext
{
    GtkWidget* m_root;     // owned (ref_sink'd); destroyed in dtor
    GtkWidget* m_current;  // borrowed; the node we are "at"
    GtkStyleContext* m_context;  // borrowed from m_current
    int m_scale;
};
```

* `Add(type, objectName, classes...)` with a real `GType` creates a widget
  of that type and attaches it under `m_current`.
* `Add(G_TYPE_NONE, objectName, ...)` (and the one-argument
  `Add(objectName)`) descends to the child named `objectName`.
* CSS classes are applied with `gtk_widget_add_css_class()`, which
  replaces `gtk_widget_path_iter_add_class()` one-for-one.

Generic construction works: `g_object_new(GTK_TYPE_NOTEBOOK, nullptr)`
yields a proper `GtkNotebook` with `css_name == "notebook"`, so the
existing `GType`-driven API does not have to become a per-widget switch.
Attachment does need a small per-type helper, because GTK4 has no generic
`gtk_container_add()` — `GtkWindow`, `GtkButton`, `GtkFrame`, `GtkBox` and
`GtkHeaderBar` each have their own setter.

## 4. Where the translation is not one-for-one

Three call sites in the existing code describe node structure that GTK4
does not have. These are **not** bugs introduced by this design; they are
places where GTK4's widget tree genuinely differs, and each needs a
decision rather than a mechanical port:

* **`statbox.cpp` asks for a `"border"` child of `frame`.** GTK4's
  `GtkFrame` has no such node — its only child is `label`, and the border
  lives on the `frame` node itself (`border=1,1` above). Descending should
  therefore *fail soft*: if no child matches, stay on the current node,
  which yields exactly the right answer here.
* **`settings.cpp` asks for a `"contents"` child of `scrollbar`.** GTK4
  names that node `range`. Same fail-soft rule applies, though the more
  faithful fix is to teach the descent about the rename.
* **`notebook.cpp`'s `"tab"` node only exists once a page is added.** An
  empty `GtkNotebook` has `header` → `tabs` but no `tab` child. Any
  scratch notebook built for tab metrics must have a page appended first,
  or the descent silently lands on `tabs` and returns the wrong padding.

The fail-soft rule (descend if the node exists, otherwise stay put) is
what makes the first two safe, but it is also a hazard: it converts a
structural mismatch into a *silently wrong number* rather than a
compile-time or runtime error. The implementation should therefore assert
in debug builds when a descent misses, so these stay visible.

## 5. What genuinely degrades, and what improves

**`Bg()` and `Border()` degrade.** There is no GTK4 API that answers "what
colour does the theme paint behind this node". GTK4 backgrounds are
painted through `render_background()` and may legitimately be a gradient
or an image, not a flat colour, which is *why* the flat-colour query was
removed rather than merely renamed. The available approximation is the
theme's named colours, with the names Adwaita-derived themes conventionally
define -- probes confirm `theme_bg_color`, `theme_base_color` and `borders`
all resolve -- but this is a convention, not a guarantee, and a theme that
omits them will fall back.

Reading those names is itself a problem, because
`gtk_style_context_lookup_color()` is deprecated with nothing to replace
it. CSS does still resolve them, and a probe can read one back: install a
provider setting `color` to the name, then read the result with
`gtk_widget_get_color()`. Measured against the deprecated call in
`docs/gtk/probes/gtk4-theme-colour-probe.c`, that reproduces every name the
port asks for exactly, and reports a name no theme defines as missing.

**Asking on the application's own display is not usable here.** `Bg()` and
`Border()` are called while GTK is measuring, laying out or painting some
other widget, and the probe has to install a provider on the display to ask
its question. Doing that per query segfaulted the GUI suite inside
`gtk_widget_snapshot_child()` after about a hundred cases, with the crash
point moving between runs. Bisected to the commit that introduced it; every
commit before it ran all 551 cases. A cache would reduce the number of
those queries but not remove the first one, which still falls inside
someone else's layout.

**So the question is asked on a display of our own.** `gdk_display_open()`
gives a second connection, the provider goes on that, and the probe widgets
live there: the application has nothing on that display, so nothing of the
application's is invalidated, whenever the question is asked. The answers
are the theme's rather than any widget's, so they are the same for every
caller and are kept until the theme changes -- `notify::gtk-theme-name`
drops them. That is what `wxGTKLookupThemeColour()` does now, and the
deprecated call is gone.

Verified against the deprecated implementation: all 35
`wxSystemSettings::GetColour()` values identical, and the GUI suite green
three runs in a row where the per-query version died in one. If the display
server will not give a second connection, the application's own display is
used instead -- a handful of names, each installing and removing a provider
once, which is the risk this design exists to avoid but is better than no
theme colours at all.

Two details of GTK the probe had to establish are worth keeping whatever is
done here, and are pinned in `build/tools/gtk4-invariants.c`:

* An off-screen widget's style is computed once, on demand, and adding a
  provider afterwards does not invalidate it. This is also issue #245 seen
  from the other side: a `wxStaticText` measured by `SetFont()` while hidden
  keeps the style that measurement computed, and a colour set after it never
  arrives.
* An undefined name is substituted silently -- no `parsing-error` signal,
  and an ordinary colour comes back -- so one answer cannot say whether the
  name exists. Asking through `@name` and through two different `mix()`es
  can: the substitute does not depend on the expression, a colour that
  resolves does. Two mixes because a single one collapses for the theme
  colour equal to what it is mixed with, and Adwaita's `theme_base_color` is
  pure white, which is exactly what an undefined name resolves to. This is the
same wall already hit and documented in `control.cpp`
(`GetClassDefaultAttributes()`, status update 10); the two should use one
shared helper rather than two independent approximations.

The honest summary is that `wxSystemSettings::GetColour()` will be
approximate under GTK4 for background-ish entries, exact for
foreground/text entries. That should be stated in the port's user-facing
notes, not buried.

**`"min-width"`/`"min-height"` improve.** The removed property query has a
better replacement in `gtk_widget_measure()`, which reports the widget's
actual minimum size including everything CSS contributes. The probe reads
a real scrollbar width of `min=14` this way. Because the node is now a
real widget rather than a synthetic path, this is available for free and
is *more* trustworthy than the old property read.

## 6. Items this design does not resolve

* **`AddMenu()` / `AddMenuItem()`** reference `GTK_TYPE_MENU` and
  `GTK_TYPE_MENU_ITEM`, which do not exist under GTK4 in any form. These
  belong to the already-deferred `menu.cpp` `GMenuModel`/`GtkPopoverMenu`
  rewrite and cannot be ported ahead of it. The affected
  `wxSystemSettings` colours (`wxSYS_COLOUR_MENU*`) will need to fall back
  to window/button colours in the interim.
* **`AddTreeviewHeaderButton(pos)`** used
  `gtk_widget_path_append_with_siblings()` to describe "the *n*th of three
  sibling header buttons", so that themes styling `:first-child` /
  `:last-child` resolve correctly. Real treeview header buttons *are*
  reachable (the probe finds a `button` child of `treeview`), so this is
  portable in principle by appending three columns and walking to the
  *n*th — but whether the resulting `:first-child`/`:last-child`
  resolution matches GTK3's synthetic sibling list is exactly the kind of
  claim that needs a rendered comparison to confirm, and no GUI test can
  run until `test_gui` links.
* **Runtime verification generally.** Everything above is verified to
  *compile and resolve values*; none of it is verified to produce
  *visually correct* results, because that requires `test_gui`, which
  still cannot link. The probes reduce this risk substantially — they show
  real theme numbers coming back, not zeros or defaults — but they are not
  a substitute for rendering comparison against GTK3.

## 7. Recommended sequencing

1. Reimplement the class on real widgets (§3), keeping the existing public
   API so no call site changes. Fail-soft descent plus a debug assertion.
2. Route `Bg()`/`Border()` through one shared, clearly-named approximation
   helper also used by `control.cpp`, so the fidelity gap has exactly one
   implementation and one place to improve later.
3. Replace `GetNodeWidth()`'s property query with `gtk_widget_measure()`.
4. Fix the three structural mismatches in §4 at their call sites.
5. Leave menu-related entries stubbed with a documented fallback until
   `menu.cpp` is ported.

Steps 1-3 unblock `statbox.cpp`, `notebook.cpp`, `renderer.cpp`,
`srchctrl.cpp`, `toplevel.cpp` and `generic/infobar.cpp` together, which
is the point of doing this before the remaining per-file work.
