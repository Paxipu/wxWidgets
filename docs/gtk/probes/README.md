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
| `x11-focus-watch.c` | Where is the X input focus, where is the pointer, and does anyone hold a grab? Asked from outside a running test, which is the only way to tell "no input arrives" apart from "input arrives somewhere else". Builds with `-lX11`, no GTK. See `../x11-input-debugging.md`. |
| `gtk4-popover-input.c` | Is a `GtkPopover` given the pointer when the pointer was already inside its parent window before the popover appeared? (No, unless it autohides -- and a `wxPopupWindow` is a popover under GTK4. Needs `xdotool`. See issue #138.) |
| `gtk3-dnd-file-source.c` | Does repeated X11 file DnD from a legacy GTK3 source leave GTK4's drop state valid? (It caught the re-entrant `GtkDropTargetAsync::drop` handling in issue #144.) |
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

## `gtk4-gesture-semantics.c`

Answers the question the Phase 3 design document flagged as the riskiest in
the whole input port: when a `GtkGestureClick` competes with a widget's own
gesture, what does wx actually receive, and what does claiming the sequence
change?

Unlike the other probes this one injects **real clicks** via XTest
(`-lXtst -lX11`) rather than inspecting state, because the behaviour only
appears when a genuine pointer sequence is delivered. Build with:

```
gcc -o g gtk4-gesture-semantics.c $(pkg-config --cflags --libs gtk4) -lXtst -lX11
xvfb-run -a ./g
```

Measured against GTK 4.14.5:

```
target          phase    claim   wx press  wx release  native control acts
GtkButton       BUBBLE   no      yes       NO          yes
GtkButton       BUBBLE   yes     yes       yes         no
GtkButton       CAPTURE  no      yes       yes         yes
GtkButton       CAPTURE  yes     yes       yes         yes
GtkButton       TARGET   no      NO        NO          yes
plain widget    BUBBLE   no      yes       yes         n/a
plain widget    BUBBLE   yes     yes       yes         n/a
```

The important row is the first: on a widget with its own gesture, **not**
claiming means the press arrives but the release never does, because the
native gesture claims the sequence and cancels ours. GTK3 delivered both
unconditionally. This is why `window.cpp` claims exactly when wx handles the
press — that reproduces GTK3's TRUE/FALSE semantics — and why CAPTURE is not
used instead, since it would restore the release at the cost of wx no longer
being able to prevent a native control from acting at all.

## `forty-gui-tests.py` — an application-level regression check

The odd one out: not a GTK probe but a test that drives the built
`demos/forty` binary under a private Xvfb with real XTEST input and decides
whether it worked by looking at the pixels. It exists because the demo's
drawing *is* the thing that broke in issue #84, and because "does clicking
do anything" cannot be answered from a unit test.

```
LD_LIBRARY_PATH=... python3 forty-gui-tests.py path/to/build/demos/forty/forty
```

Needs `Xvfb`, `xdotool`, `scrot`, and Python with Pillow and NumPy. It runs
each test in its own process, so one cannot leave state for the next.

| Test | What it asserts |
|---|---|
| `deal` | Clicking the pack deals a card *and draws it*, and a forced full repaint agrees with what the click drew — i.e. the screen matches the game state. |
| `drag` | A card picked up with the mouse follows the pointer, and returns cleanly when dropped somewhere illegal. This is the path that saves the pixels under the card and reads them back. |
| `drag_restore` | Every pixel the card is *not* covering is unchanged by a drag. `drag`'s threshold of 200 px answers "did the card appear at all" and cannot see a handful of stray pixels left behind, which is what issue #136 was. Deliberately drags across the dealt rows: restoring green onto green looks perfect whatever the rectangles do, so a drag over empty baize cannot see this class of defect at all. |
| `resize` | The board is unchanged after the window grows and shrinks again. |
| `undo` | A right-click undoes the deal, visibly. |
| `quiet` | The demo prints no assertions and no GTK criticals. |

The useful trick in it, if you need to script a modal dialog on a bare Xvfb:
there is no window manager, so the input focus stays at `PointerRoot` and
keystrokes go to whatever window the pointer is over. Parking the pointer on
the dialog's text field before typing is what makes it work; without that the
keys go to the root window and vanish, which reads exactly like "wx will not
dismiss this dialog".

## `stc-autocomp-order-dependence.cpp` — why the GUI suite stops at case 278

Also not a GTK probe but a wx one, and the reason it is worth keeping: under
GTK4 the suite's `wxStyledTextCtrl::AutoComp` passes when run alone, fails
when anything ran before it, and the failure aborts the process — so 255 of
`test_gui`'s 533 cases are never measured. Every one of the nine other
failures known on this branch sits *after* that abort, which is why finding
what poisons this one test matters more than its own two assertions.

```
g++ -o probe stc-autocomp-order-dependence.cpp \
    $(path/to/wx-config --cxxflags --libs stc,core,base)
for m in none button move click; do PROBE_PRE=$m xvfb-run -a ./probe; done
```

| `PROBE_PRE` | What runs before the popup is shown | Result |
|---|---|---|
| `none` | nothing | `text="ability"` — as the test expects |
| `button` | a control is created and destroyed | `text="ability"` |
| `move` | ... and `wxUIActionSimulator::MouseMove()` once | `text=""` |
| `click` | ... and a simulated click, as the button tests do | `text=""` |

So one simulated pointer move is the whole trigger, and nothing recovers
from it: in the full suite 176 further cases run in between and the failure
still happens, so it is not a timing window. The file's header comment lists
the hypotheses this ruled out by measurement — pointer parking, popup
placement, and double-click delivery to a plain `wxPopupWindow` — so they do
not have to be re-derived.

It also records the answer, which is issue #138: the click never reaches the
popup, because a `GtkPopover` which does not autohide is not given the pointer
once GTK has processed a motion event over the parent window.

## `wayland-toplevel-move.cpp` — can a toplevel be moved at all?

Run it through its driver, which supplies the controls:

```
./wayland-toplevel-move.sh /path/to/wx-build
```

The probe asks a `wxFrame` to move to three positions in turn and prints what
`GetPosition()` says and what `wxEVT_MOVE` carries. On its own that measures
nothing, because wx is free to believe whatever it likes; the script therefore
runs it twice and adds an outside witness each time. Under X11 the X server is
asked where the window is, which shows a move is detectable at all. Under
headless sway the compositor is asked, and is then told to move that same
window itself -- if it could not, the run says nothing about `Move()` and the
numbers should be thrown away.

The answer is in `docs/gtk/wayland-testing.md`: three moves, no movement, and
wx reporting all three as having happened. It is the first half of issue #134.
