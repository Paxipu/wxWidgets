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

## Editing this file

Both instances add entries here, so this file is marked `merge=union` in
the `.gitattributes` beside it: a merge keeps the lines from both sides
instead of stopping on a conflict, and appending a table row or a section
of your own therefore merges by itself.

Union merges never conflict, including when they should. Two branches that
edit the *same* line leave both versions of it in the file, one after the
other, and the merge reports success. So append freely, and change existing
prose in one branch at a time.

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
| `gtk4-destroyed-surface-pointer.c` | What happens when the pointer position of a surface whose X window is already gone is asked for? (The process dies with `BadWindow`; `gdk_surface_is_destroyed()` does not know yet, so only an X error trap makes the query safe. That is issue #113.) |
| `gtk4-print-portal-route.c` | Does `gtk_print_operation_run()` show a dialog in our own process or hand the job to `xdg-desktop-portal`, and does it come back? (GTK 4.22 takes the portal route outside a sandbox, where a present-but-silent portal blocks with no timeout; a *missing* portal is handled fine. That is issue #161. See `../gtk4-printing.md`.) |
| `gtk4-popup-dismiss-poisons-keys.cpp` | Why do six test cases stop receiving key events once a `wxPopupTransientWindow` has been dismissed earlier in the suite? Reproduces it in twenty seconds outside the suite and separates Popup/Dismiss/Destroy, which names `Dismiss()` as the step responsible -- and reports the X input focus alongside, which is on the top level in the poisoned run just as in the clean one, ruling out the focus everyone suspects. That is issue #82. Links against wx, unlike the others. |
| `gtk4-slider-negative-css-margin.c` | Can `GtkGizmo (slider) reported min width -2` be produced with no application code at all? (Yes -- one `GtkScrollbar` and a negative CSS `margin` on the slider node reproduce the issue's exact value. The warning belongs to the active theme's stylesheet, not to what wx allocated, which is issue #24. GTK's built-in Adwaita is not affected, so it does not appear under `xvfb` with no theme installed.) |
| `gtk4-renderer-snapshot.c` | Does snapshotting a widget and drawing the node through `gsk_render_node_draw()` produce the same picture as the deprecated `gtk_render_background()`/`gtk_render_frame()` that `renderer.cpp` uses? (Byte for byte yes, in four states -- but only with the widget allocated to the target rect, a `gtk_widget_queue_draw()` after the state change to drop GTK's cached render node, and no translation of the node's own negative bounds. `PROBE_NO_INVALIDATE=1` shows the middle one failing. That is issue #181.) |
| `gtk4-listview-vs-listbox.c` | Can `GtkListView` + a `GListModel` do everything `wxListBox` needs -- deselection, multi-selection, hit testing, item geometry, scroll-to? (Yes, all of it, which makes #180 the only one of the four deprecation migrations that is not blocked. Two traps: `gtk_widget_pick()` needs `GTK_PICK_NON_TARGETABLE` or it stops at the list view itself, and the rows are never allocated by non-blocking main loop iteration, so every hit test answers "nothing here".) |
| `gtk4-columnview-cell-mechanics.c` | The three things the wxDataViewCtrl migration cannot guess at: can a drop-down cell be deselected (#183), can wx's editing protocol hang off a cell widget, and can `SetRowHeight()` still impose a height? (Deselection: yes. Editing: yes -- `GtkEditableLabel` reports `notify::editing` on both edges, started=1 done=1. Row height: **no, not downwards**. A cell asking for 40px makes the row 56; asking the same cell back down to 12 leaves it at 40 and the row at 56, because a size request is a *minimum*. `GtkColumnView` has no uniform-height setting at all, so a uniform height can only come from every renderer requesting it. Two traps this is written around: rows live at `columnview > listview > row > cell`, so a flat walk over the column view's own children finds none of them and reads as "the factory never ran"; and a *blocking* main-loop iteration with nothing pending never returns, which hung every earlier version.) |
| `gtk4-dropdown-deselection.c` | Can `GtkDropDown` be returned to "nothing selected" once its model is non-empty? wxChoice cannot leave the deprecated `GtkComboBox` unless it can, because `itemcontainertest.cpp` asserts `SetSelection(wxNOT_FOUND)` after `Append()`. (**Yes on GTK 4.14, no from GTK 4.22.** The original run of this probe said yes in all seven states tried and the whole migration was planned on that, but it had linked against the distribution's GTK 4.14 while wx was built against 4.22 -- `ldd` on the probe and on the library disagreed, and nothing in the output said which GTK had answered. Against 4.22 `gtk_drop_down_set_selected(GTK_INVALID_LIST_POSITION)` is silently refused with any items present. wxChoice therefore carries a `GtkSingleSelection` of its own, with `autoselect` off and `can-unselect` on, which is clearable on both. `build/tools/gtk4-invariants.c` reports the difference rather than asserting either answer, and prints the GTK version it ran against. **Build a probe against the same GTK the library uses, or it is measuring a different program.**) |
| `gtk4-dialog-controller-async.c` | GTK4 replaced the dialog *widgets* with dialog *controllers* -- `GtkColorDialog`, `GtkFontDialog`, `GtkAlertDialog` and `GtkFileDialog` are not widgets and only choose things asynchronously, while `wxDialog::ShowModal()` has to block and return a code. Can the two be bridged? (Yes: a callback started *before* the nested main loop is still delivered to it. The trap is the error: dismissing a dialog is reported in GTK4's own `GTK_DIALOG_ERROR` domain, so the obvious check for `G_IO_ERROR_CANCELLED` matches nothing and every ordinary Cancel looks like a failure. `GTK_DIALOG_ERROR_DISMISSED` is the user closing it, `_CANCELLED` is the program cancelling the call, and only `_FAILED` is worth reporting. Part of #173.) |
| `gtk4-dialogbutton-vs-button.c` | Are `GtkColorDialogButton`/`GtkFontDialogButton` drop-in replacements for the deprecated `GtkColorButton`/`GtkFontButton` that `clrpicker.cpp` and `fontpicker.cpp` use? (Value round trips are exact, and the new buttons report the same font string as the old one. One trap, and it is the whole reason the migration needs care: the replacements have no `"color-set"`/`"font-set"` signal, only a property notify -- and that notify fires for *our own* writes too, which the old signals never did. Without blocking the handler around the write, `SetColour()` raises a `wxColourPickerEvent` that no user asked for. Part of #180.) |
| `gtk4-assertdlg-backtrace.cpp` | Does the assert dialog's backtrace list still round-trip after moving from `GtkTreeView` to `GtkColumnView`? (Yes. It exists because that dialog is unreachable from the test suite -- it appears only on an assertion failure and its entry points are not exported -- so the conversion would otherwise have gone in with no functional check, which is how #181's substitution shipped drawing nothing. Compiles `assertdlg_gtk.cpp` into itself.) |
| `gtk4-columnview-vs-dataview.c` | Can `GtkColumnView` carry what `wxDataViewCtrl` needs -- a tree, per-column renderers, cells that draw themselves, in-place editing, per-column sorting? (All of it, so the 143 warnings in `dataview.cpp` are work rather than a wall -- except drag and drop, which this does not cover. Two traps: `GtkTreeListModel` wraps items in a `GtkTreeListRow` that has to be unwrapped, and iterating the main loop from inside a callback hangs.) |
| `gtk4-entry-completion-parts.c` | `GtkEntryCompletion` is deprecated in GTK 4.10 with no replacement, so `wxTextEntry::AutoComplete()` has to be rebuilt from parts. Three things decide whether the obvious construction -- a `GtkPopover` under the entry over a filtered `GtkListView` -- behaves like a completion popup rather than a menu. (All three yes: `autohide=FALSE` leaves the focus where it was, `GtkStringFilter` in prefix mode filters as `GtkEntryCompletion` did, and a capture-phase key controller can take Up/Down/Return while everything else reaches the entry. **The trap is the focus check**: a `GtkEntry` delegates editing to an inner `GtkText`, so `gtk_widget_has_focus(entry)` is FALSE even when the entry is where typing goes -- the first version of this probe reported "the popup stole the focus" on that basis. Ask `gtk_root_get_focus()` instead.) |
| `gtk3-reference-values.c` + `gtk4-comparison-values.c` | Differential check: does the GTK4 real-widget approach return the same values as the GTK3 synthetic-path approach for the same logical query? |
| `gtk4-snapshot-mapped-vs-allocated.c` | Does snapshotting a widget need a *mapped* toplevel, or was the missing piece only the allocation? The earlier probes conflated the two, and the answer decides whether #181 is a small change or a large one. (**Both are required**, and so is genuine visibility: `gtk_widget_set_visible(FALSE)`, `gtk_widget_set_child_visible(FALSE)` and even `opacity 0` each yield a NULL node. wx's own scratch container is never shown, so the widget has to live somewhere that is.) |
| `gtk4-snapshot-allocated-child.c` | Reopening #181 properly: `wxRendererNative::Draw*()` is handed the window it draws into, and during a paint that window *is* mapped. Put the themed widget in there, allocate it, snapshot it, take it out again -- does that match the deprecated `gtk_render_background()`/`gtk_render_frame()`? (Byte for byte yes, `hash=4efd8b1a` both ways. The trap is the node's own origin: it is negative -- -3,-2 for a button, because the shadow overflows the allocation -- and cancelling it shifts the button by exactly that much. Translate to the target position only. Counting non-transparent pixels cannot tell a colour change from no change at all, so this hashes them instead; the first version reported "the states do not differ" on that basis.) |
| `gtk4-renderer-scratch-in-paint.c` | The remaining risk from the probe above: wx draws from inside a paint, which under GTK4 is inside the container's `snapshot` vfunc. Is the allocate-and-snapshot dance refused there? (No. This is the design wx actually uses: the themed widget is parented once and kept, parked far outside the visible area, and the container simply never paints it -- it only asks for a render node when a draw needs one.) |
| `gtk4-style-query-replacements.c` | I had been writing off about 50 warnings across `settings.cpp`, `window.cpp`, `control.cpp`, `statbox.cpp`, `spinbutt.cpp` and `win_gtk.cpp` with "`gtk_style_context_get_padding()`/`get_border()` have no replacement at all" -- the same sentence I had just had to withdraw for #181. So what does a real widget answer? (`gtk_widget_get_color()` returns values *identical* to the deprecated `gtk_style_context_get_color()`, and `gtk_widget_measure()` of an empty widget equals padding+border wherever the widget has no intrinsic content. Neither query is a wall.) |
| `gtk4-style-metrics-replacements.c` | The follow-up question: `gtk_widget_measure()` gives padding+border *together*, and three call sites need them apart -- `win_gtk.cpp` wants the border alone, `spinbutt.cpp` the padding alone, `statbox.cpp` each side separately. Can they be separated with supported API? (**Yes, exactly**, per side and per property, including a deliberately asymmetric case: a CSS class that zeroes one side of one property, and the change in the widget's minimum size is that side's width. Two traps, both of which made an earlier version report that the whole approach failed. The widget has to be told to `gtk_widget_queue_resize()`, or it answers out of its cached size request and every side measures 0 -- while `gtk_widget_get_color()` needs no such thing, which is what made the difference look like a property of CSS rather than of caching. And the zeroing rules have to sit at a *higher* provider priority than whatever sets the property: with both in one stylesheet the later rule wins at equal specificity and, again, everything measures 0.) |
| `gtk4-widget-css-background.c` | `window.cpp` paints a widget's own CSS background for `wxBG_STYLE_SYSTEM` with the deprecated `gtk_render_background()`. Is that call also *redundant* under GTK4? (**Yes.** `gtk_widget_snapshot()` renders every widget node's CSS boxes before the widget's own snapshot vfunc runs, so the background is already there and nothing in this program ever asks for it. The GTK4 branch of that code is therefore empty rather than rewritten.) |

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

After its own moves the probe keeps reporting once a second, and the `WATCH`
lines carry the running count of `wxEVT_MOVE`. The compositor is told to move
the window during that window of time, so the other half of #166 -- a move
that really happens sending no event -- lands in the log as a count that does
not change, rather than as output that is not there. An absence proves
nothing about a probe that might simply have exited.

## `wayland-screen-coords.cpp` — are screen coordinates screen coordinates?

```
./wayland-screen-coords.sh /path/to/wx-build
```

The probe prints what `ClientToScreen()` and `ScreenToClient()` make of a
frame and of a child inside it. The driver supplies the witness both times:
the X server under a private Xvfb with openbox, the compositor under headless
sway. A wrong answer on both backends would be a broken probe; a wrong answer
only where the position is unknowable is the defect.

```
X server says the frame is at: (441,392)
WX frame-client-origin (440,370)
WX roundtrip (17,23) -> (457,393) -> (17,23)

compositor says the frame is at: (438,362) 404x302
WX frame-client-origin (0,0)
WX roundtrip (17,23) -> (17,23) -> (17,23)
```

Under Wayland the mapping is the identity: client coordinates are handed
back labelled as screen coordinates, short by the whole position of the
window. The round trip still agrees with itself, which is why this is easy
to miss -- only an absolute answer, or one compared against something
outside the process, is wrong.

It also prints where the child sits inside the frame, because a `wxFrame`
resizes a lone child to fill its client area: the child's origin equalling
the frame's is then arithmetic rather than a second defect.

It also drives the pointer to a known place and compares
`ScreenToClient(wxGetMousePosition())` against the motion event, which is
ground truth because GTK hands wx that position with no mapping involved:

```
WX motion event (80,108) | ScreenToClient(GetMousePosition) (80,108) | agree
```

They agree, on both backends. `wxGetMousePosition()` answers in the surface's
coordinates and wx's "screen" space is that same space, so the two defects
cancel -- which is why the identity mapping is mostly invisible, and what
bounds its impact. `docs/gtk/wayland-testing.md` has the rule and the three
ways out of it.

When the pointer never reaches the window the probe says so rather than
printing nothing: an empty comparison and a comparison that agreed look
identical otherwise. Driving the pointer needs `wldrag` on the Wayland side
(headless sway has no pointer device of its own); note that `wldrag.c` is C++
despite its name.

This is issue #214, and it is *not* the same code path as #166. Nothing here
reads `m_x`/`m_y`, so the fix for that one changes no number above.

## `sni-roundtrip.sh` — does a GTK4 tray icon reach a panel?

```
./sni-roundtrip.sh /path/to/wx-build
```

GTK4 removed `GtkStatusIcon`, so `wxTaskBarIcon` talks the
StatusNotifierItem D-Bus interfaces itself (issue #216). Testing that
normally means a desktop with a tray, which no CI has. It does not have to:
`sni-watcher.c` is a stand-in `org.kde.StatusNotifierWatcher`, and
`dbus-run-session` gives it a private bus, so the whole thing runs headless.

The watcher does what a panel does *after* adopting an item -- reads its
properties back, fetches the menu layout, and clicks an item -- rather than
counting the registration call. That is the difference between a test and a
formality: the first run of it registered successfully and then timed out on
all eight property reads, which is an icon a panel would show as nothing.
The cause was the item registering synchronously and so being unable to
answer while it waited.

Two controls. The watcher runs alone first and must report
`WATCHER-TIMEOUT` and fail, so a run in which nothing registers cannot pass.
And the check on the output asks what the layout *contains* -- a separator
typed as one, a check item carrying its state, a submenu still nested, a
disabled item still disabled -- plus that the click arrived back in the
application with the id it gave the item. An earlier version matched
property *names*, which an unreadable property prints too, and passed the
deadlocked run above.

The probe is written against the public `wxTaskBarIcon` only. Reaching past
it into `wxStatusNotifierItem` would answer a question about the probe
rather than about wx.

## `crash-capture.sh` — what to collect when a sample crashes elsewhere

Some crashes only happen on a real desktop: a running session bus, a desktop
portal answering, a GTK newer than CI's. This collects enough to act on one
without an interactive debugger, which on a crashing GTK app is worse than
useless -- the app dies holding an X server grab, gdb stops at the signal and
never releases it, and the whole desktop stops accepting input, gdb's own
window included. It looks exactly like gdb hanging. `gdb -batch` runs to the
crash, prints, and exits, so the grab never outlives the process.

It also runs the app once with `GTK_THEME` set. That is not cosmetic:
`wxSystemSettingsModule::OnInit()` only talks to the desktop portal when
`GTK_THEME` is unset, so setting it skips the colour-scheme code that
otherwise runs at startup in every GUI app. Crash in one and not the other
narrows it to that path in a single run. On the first crash it was used
for, it crashed both ways, which ruled that path out -- a discriminator
earns its place by excluding as readily as by confirming.

Where a crash dumps core, `coredumpctl info` prints a stack trace on its
own and the script prefers it: nothing has to be run again, and no debugger
has to resolve symbols before you can read anything.

Before any of that it runs `gtk4-widget-factory`, which contains no wx at
all. That is the control, and it is first because it can end the
investigation outright. The crash it was written for looked like ours --
several samples dying at startup on X11, on a machine the port had never
run on -- and the backtrace turned out to be a recursion between GTK and
the ibus input method module, with not one wx frame in the cycle.
`gtk4-widget-factory` segfaulted identically on that machine, which is what
settled it.

The bottom of that stack is worth keeping, because it shows how little the
application had to do to trigger it:

```
main -> wxEntry -> MyApp::OnInit -> MyFrame::MyFrame
     -> wxTextCtrl::Create
     -> gtk_text_view_new_with_buffer()
     -> g_object_new -> [ nine frame GTK/ibus cycle, ~7900 times ]
```

Constructing a `GtkTextView` was enough. The reported cause of this shape is
an ibus GTK4 module built against a different GTK4 than the one installed --
it announces itself with "class size for type 'IBusIMContext' is smaller than
the parent type's 'GtkIMContext' class size" before dying -- so reinstalling
the module against the current GTK fixes it. Setting `GTK_IM_MODULE` to
`gtk-im-context-simple` sidesteps it meanwhile -- confirmed on the affected
machine, where it stopped the crash outright.

Three readings agree, which is what makes this reportable rather than a
guess: the cycle contains no wx frame, a GTK application containing no wx
crashes identically, and replacing the input method module fixes it without
changing a line of wx.

The general lesson is worth more than the instance: a crash reported
against a port is not evidence about the port until something without the
port in it has been shown to survive. Any X11 crash from a machine whose
GTK is broken this way says nothing about wx, however many wx samples
reproduce it.

Two later additions came from the crash it was written for, which turned
out to be a runaway recursion between GTK and the ibus input method module
with no wx frame anywhere in the cycle. An input method module loads into
every GTK application on the machine, runs before any of wx's code does,
and no CI covers it, so the script runs the application once with
`GTK_IM_MODULE=gtk-im-context-simple`. And it runs a stock GTK4
application, because without that control the whole exercise establishes
only that something crashed on a machine wx happened to be running on --
if `gtk4-widget-factory` dies the same way, the fault is in the
installation and a day spent reading wx is a day wasted.

## Checking whether a build contains a given fix

`nm | grep` for a function name only works if the function survives to have a
name. A `static` helper does not: at `-O2` it is inlined into its callers and
no symbol is emitted, so `nm` finds nothing whether the fix is present or not.
Measured on `wxAuiGetDragClientPosition()` -- symbol present at `-O0`, absent
at `-O2`, same source both times.

Check the source and the build's freshness instead, which does not care how
the compiler chose to emit anything:

```sh
git log --oneline -1
grep -c wxAuiGetDragClientPosition ../src/aui/framemanager.cpp
find . -name '*framemanager.o' -newer ../src/aui/framemanager.cpp
```

## `sway-up.sh` — a compositor that is actually there

Prints the socket of a live headless sway, starting one if none answers.
Sway exits on its own often enough here that a run which "failed" has to be
checked against whether the compositor was still up, and a stale socket file
left behind by a dead one answers nothing while looking exactly like a live
one. Every Wayland probe should get its `SWAYSOCK` from this rather than from
`ls -t`.

```sh
export SWAYSOCK=$(docs/gtk/probes/sway-up.sh)
```

## `aui-redock-floating.sh` — the reproduction for #167

```
$ aui-redock-floating.sh x11
  RESULT PASS the floating pane docked again
  dock logic ran 137 times

$ aui-redock-floating.sh wayland
  pane was dragged 60,85 -> 262,597
  RESULT FAIL ended up floating, not docked
  dock logic ran 0 times
```

Same code, same drag, opposite outcomes, and both controls hold: the Wayland
line shows the window really did move, so the drag happened, and the X11 leg
passes, so the test can succeed.

Two things about the drag are load-bearing, and every scripted attempt at this
bug before finding the first of them was measuring nothing at all.

**Three pixels at a time.** `wxAuiFloatingFrame::OnMoveEvent()` discards any
move larger than that outright:

```cpp
if ((abs(winRect.x - m_lastRect.x) > 3) ||
    (abs(winRect.y - m_lastRect.y) > 3))
    return;
```

A drag that jumps in tens of pixels has *every* event thrown away, reaches no
dock logic, and reports a failure that belongs to the harness. A person's hand
produces a continuous stream of small moves and never notices this.

**Finish about fifteen pixels from the edge**, because that is where the dock
zone is. Dropping sixty pixels in leaves the pane floating on X11 too, which
looks exactly like the bug and is not.

The pane is floated through the manager rather than by dragging
(`AUIDOCK_START_FLOATING`), so a failure here cannot be a failed undock
wearing a re-dock's clothes.
