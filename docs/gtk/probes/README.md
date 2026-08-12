# GTK4 port probe programs

Small standalone programs used to answer design questions about GTK4 with
measurements rather than assumptions, while porting wxGTK. They are kept
here so the conclusions recorded in `../gtk4-stylecontext-design.md` and
`../gtk4-status.md` can be re-checked against a different GTK4 version
instead of being taken on faith.

These are exploratory: they print what they find and are meant to be read by
a human. The invariants they established that the port actually *depends*
on have been turned into an automated regression check,
`build/tools/gtk4-invariants.c`, which asserts rather than prints and runs
in CI on the GTK4 job. If a GTK upgrade changes something fundamental, that
check is what should catch it; these programs are then useful for
investigating what changed.

## Building and running

Each is a single file with no dependencies beyond GTK itself. A display is
required (widgets need a GdkDisplay even though they are never shown), so
run them under `xvfb-run` on a headless machine:

```
gcc -o probe gtk4-css-node-probe.c $(pkg-config --cflags --libs gtk4)
xvfb-run -a ./probe
```

`gtk3-reference-values.c` is the odd one out and builds against GTK3:

```
gcc -o ref gtk3-reference-values.c $(pkg-config --cflags --libs gtk+-3.0) \
    -Wno-deprecated-declarations
```

## What each one establishes

| Program | Question it answers |
|---|---|
| `gtk4-css-node-probe.c` | Are interior CSS nodes (`header`/`tabs`/`tab`, `trough`/`slider`, `check`) reachable as real child widgets, and do metrics resolve on unrealized widgets? |
| `gtk4-style-resolution-probe.c` | Does ancestry affect style resolution (i.e. must scratch hierarchies really be parented)? Do state flags and CSS classes still apply? |
| `gtk4-widget-lifecycle-probe.c` | Widget ownership/floating-reference behaviour, which nodes exist on an empty vs populated widget, and whether `gtk_widget_measure()` replaces the removed `min-width` query. |
| `gtk4-stylecontext-lifecycle.c` | Does the rewritten class's create/destroy cycle leak or emit GTK criticals? (Runs 500 cycles; children attached with `gtk_widget_set_parent()` are *not* freed with the parent, so this is easy to get wrong.) |
| `gtk3-reference-values.c` + `gtk4-comparison-values.c` | Differential check: does the GTK4 real-widget approach return the same values as the GTK3 synthetic-path approach for the same logical query? |

## Reading the differential check

Run both and compare. Exact equality is *not* the standard: GTK3 and GTK4
ship different versions of Adwaita, so small genuine theme differences are
expected. What matters is the absence of gross discrepancies -- zeros
where a real value is expected, or values off by more than a pixel or two.

At the time of writing (GTK 3.24.41 vs GTK 4.14.5) they report:

```
                       GTK3 (synthetic path)        GTK4 (real widgets)
statbox frame>border   border=1,1,1,1 pad=0,0,0,0   border=1,1,1,1 pad=0,0,0,0
notebook tab           pad=12,3,12,3 margin=0,...   pad=12,3,12,4 margin=4,0,4,0
```

The statbox line matching exactly is the significant one: GTK4's GtkFrame
has no `border` child node, so the descent finds nothing and deliberately
stays on `frame` -- and that turns out to be precisely right. The notebook
tab differences (1px bottom padding, and horizontal margins) are real
changes in GTK4's Adwaita, not artifacts of the approach.
