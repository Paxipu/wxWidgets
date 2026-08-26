# Testing wxGTK under Wayland

Nothing in CI runs under Wayland, and neither does any developer machine this
port has been worked on. That matters more for GTK4 than it did for GTK+ 3,
because several things the port had to redesign -- window positioning, the
pointer, the compositor-driven move -- behave differently there, and some of
them cannot work at all. This describes how to get a Wayland session with
working input simulation on a headless machine, so those claims can be measured
instead of argued about.

## What is needed

| package | why |
|---|---|
| `sway` | a wlroots compositor with a headless backend: no GPU, no DRM device and no seat required |
| `libwayland-dev`, `wayland-scanner` | to build the input driver below |

`ydotool` is the usual answer for injecting input under Wayland and is **not**
usable here: it works through `/dev/uinput`, which containers generally do not
have. `wlrctl` does work, going through the compositor rather than the kernel,
but it can only `move`, `click` and `scroll` -- it cannot hold a button down,
so it cannot perform a drag, which is precisely what the interesting cases need.

## Starting a session

```sh
export XDG_RUNTIME_DIR=/tmp/xdgrt && mkdir -p $XDG_RUNTIME_DIR && chmod 700 $XDG_RUNTIME_DIR
printf 'output HEADLESS-1 mode 1280x1024\n' > /tmp/sway.cfg
WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 sway -c /tmp/sway.cfg &
export WAYLAND_DISPLAY=wayland-1
export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock | head -1)
```

`sway` logs `drmGetDevices2 failed`, which is expected and harmless: it falls
back to software rendering. Run applications with `GDK_BACKEND=wayland` and
with `DISPLAY` unset, or GTK will quietly pick X11 and the test will measure
nothing.

`swaymsg` is what makes any of this targetable. The application cannot be asked
where its own windows are -- that is the whole point of several of these tests
-- but the compositor can:

```sh
swaymsg -t get_tree                              # window geometry
swaymsg '[title="drag me"] floating enable'      # sway tiles by default
swaymsg '[title="drag me"] move position 300 300'
```

## Injecting a drag

`probes/wldrag.c` is a minimal Wayland client that drives
`zwlr_virtual_pointer_v1` and, unlike `wlrctl`, keeps the button down across
motion:

```sh
cd docs/gtk/probes
wayland-scanner client-header wlr-virtual-pointer-unstable-v1.xml vp.h
wayland-scanner private-code  wlr-virtual-pointer-unstable-v1.xml vp.c
gcc -c -o vp.o vp.c -I. $(pkg-config --cflags wayland-client)
g++ -o wldrag wldrag.c vp.o -I. $(pkg-config --cflags --libs wayland-client)

./wldrag move 300 300 sleep 400 down sleep 300 \
         move 340 330 sleep 200 move 400 380 sleep 300 up
```

Coordinates are absolute screen pixels. Under the sway configuration above the
offset from screen to client coordinates is a constant (-2, -25) -- the border
and the title bar -- so a target can be worked out from `swaymsg -t get_tree`
and checked against what the application reports.

The `.xml` is a minimal hand-written definition of the parts of the protocol
this driver uses, because Debian and Ubuntu package no `wlr-protocols`. It is
not the canonical file; that lives in the `wlr-protocols` repository, and if it
is available it should be preferred. The request ordering matters -- it is the
wire format -- but a mistake there is loud rather than silent: the compositor
rejects the connection with a protocol error.

## What has been measured with this

`probes/wayland-move-events.cpp` counts `wxMoveEvent` for a `wxMiniFrame` while
the **compositor** moves it, which is what dragging a floating wxAUI pane by its
caption ends up doing. Four moves driven through `swaymsg`:

```
GTK4  + Wayland : 0 wxMoveEvent
GTK+3 + Wayland : 0 wxMoveEvent
```

GTK+ 3 reports one event in total, but already has it at the first sample,
before any move is driven -- that is the initial placement.

The same probe also prints what the window believes its position to be, which
needs no input injection at all and is the clearest form of the result. With the
compositor moving it to (200,200), then (500,350), then (800,500):

```
GTK4  : reported_pos=(120,120) at every sample -- what wx asked for, frozen
GTK+3 : reported_pos=(0,0)     at every sample -- the origin, simply wrong
```

Neither toolkit ever reports where the window actually is, and GTK+ 3 is the
worse of the two: GTK4 at least echoes the position that was requested.

This is what makes the docking failure structural. `wxAuiManager` decides where
a pane lands with `m_frame->ScreenToClient(::wxGetMousePosition())`, and
`ScreenToClient()` is relative to the frame's screen position -- so on Wayland
that hit test is computed in a fictional coordinate system under **both**
toolkits, not just under GTK4.

The reason is below both toolkits and cannot be worked around in either:
`xdg_surface.configure` carries a **size** and never a position. Wayland does
not tell a client where it is, deliberately. So anything built on knowing a
toplevel's screen position -- which is what `wxAuiFloatingFrame` docking needed
under GTK+ 3 on X11 -- has no Wayland implementation in any toolkit, and never
had one.

This is worth being precise about, because it changes what kind of problem it
is: floating-pane docking not working under Wayland is a **long-standing
wxWidgets limitation that GTK4 makes total**, not a regression introduced by
this port.

A fix, if one is wanted, cannot be position-based. It would have to be
pointer-based: `wxAuiManager::OnFloatingPaneMoving()` and `OnFloatingPaneMoved()`
actually want `m_frame->ScreenToClient(::wxGetMousePosition())`, and the pointer
relative to our own surface *is* something Wayland delivers -- just not while
`gdk_toplevel_begin_move()` has handed the pointer to the compositor for the
duration of the drag.

## What a captured drag can and cannot do

`probes/wayland-capture-motion.cpp` checks the two halves of what
`wxAuiManager` does when dragging a pane: it captures the mouse on the main
frame, and moves the floating frame from the motion events it then receives.
Driven with `wldrag` under sway, and with the compositor asked where the window
really is rather than asking wx:

```
dragging motions delivered to the captured window : 4
compositor: pane at 538,437 204x152   (before the drag)
compositor: pane at 538,437 204x152   (after wx called Move() on every motion)
wx's own GetPosition()                : exactly the coordinates it asked for
```

Two things follow, and they point in opposite directions.

`Move()` on a toplevel does nothing on Wayland **and reports success**: the
position wx hands back is the one it was asked for, so an application cannot
discover that the move did not happen. That is the whole of the visible
symptom -- a floating pane that will not follow the pointer.

But the pointer itself arrives perfectly well. A window holding a capture is
delivered motion throughout the drag, which means the information
`wxAuiManager::OnFloatingPaneMoving()` needs is present. What breaks the chain
is that wxAUI asks for it indirectly: it calls `Move()` and then acts on the
`wxMoveEvent` that would follow, and on Wayland that event never comes.

So docking is **not** unreachable here, which an earlier reading of this
suggested. Driving the dock decision from the motion event wxAUI already
receives would work; only the pane visually following the cursor is impossible,
because positioning a toplevel is not something a Wayland client may do.

## wxGetMousePosition() answers in the wrong surface's coordinates

`probes/wayland-mouse-position.cpp` puts the pointer at known places with two
toplevels of one application on screen, and asks. With the compositor's own
geometry alongside:

```
compositor: main at 100,125 404x302
compositor: pane at 700,475

pointer 250,220 (over main) : wxGetMousePosition = (148, 95)   -> main-relative
pointer 820,530 (over pane) : wxGetMousePosition = (118, 55)   -> pane-relative
pointer 250,220 (over main) : wxGetMousePosition = (148, 95)   -> main-relative
```

820 - 702 = 118 and 530 - 475 = 55, so the second reading is in the **pane's**
coordinates. The call answers relative to whichever surface the pointer happens
to be over, which is what `gdk_device_get_surface_at_position()` does and what
the comment on it in `window.cpp` already says. It is not a screen coordinate.

`ScreenToClient()` cannot repair it: a GTK4 window does not know its own
position on Wayland, so it subtracts nothing and hands the value straight back.

This is what breaks wxAUI docking there. The dock decision is

```cpp
wxPoint pt = ::wxGetMousePosition();
wxPoint client_pt = m_frame->ScreenToClient(pt);
```

and during a drag the pointer is over the **floating pane**, not over
`m_frame`. So a pane-relative coordinate is interpreted as a coordinate in the
main frame, and the hit test decides against a position nobody is pointing at.

Note what is *not* wrong, since two earlier readings of this said otherwise:
the `wxMoveEvent` does arrive (`wxTopLevelWindowGTK` sends it whenever `m_x`
or `m_y` change, whether or not the compositor honoured the move), so
`OnFloatingPaneMoving()` does run. It simply runs on a bad coordinate.

## Move() does nothing to a toplevel, and wx does not notice

`probes/wayland-toplevel-move.sh` asks a wxFrame to move to three positions in
turn and asks the compositor where it actually is:

```
on screen, before the moves:   (478,412) 324x202
on screen, after the moves:    (478,412) 324x202
after the compositor moved it: (900,725) 324x202
-- what wx believed --
EVENT  wxEVT_MOVE says (400,300)
MOVE   asked for (400,300) -- wx said (50,50) before, (400,300) after
EVENT  wxEVT_MOVE says (700,120)
MOVE   asked for (700,120) -- wx said (400,300) before, (700,120) after
EVENT  wxEVT_MOVE says (150,600)
MOVE   asked for (150,600) -- wx said (700,120) before, (150,600) after
```

Three moves, and the window never left the spot the compositor put it in. The
last line is the control: sway moved that same window on request, so it was
movable throughout and "nothing moved" is not a dead harness. The X11 leg of
the same script is the other control -- there the X server confirms the window
really is at (150,600), so the probe can see a move when one happens.

`wx_gtk_window_move()` says as much in its own body: it goes through
`XMoveWindow()` on X11 and does nothing anywhere else, because there is no
Wayland request to position a toplevel. What the probe adds is the second half,
which is not obvious from reading the code: `GetPosition()` afterwards returns
the position that was *asked for*, and a `wxMoveEvent` is sent carrying it. wx
reports a move that did not happen.

This is the whole of the first half of the docking bug. wxAUI drags a floating
pane by calling `Move()` on its frame once per motion event
(`wxAuiManager::OnMotion`, the `actionDragFloatingPane` branch), so under
Wayland the pane stays wherever the compositor first placed it -- which, for a
newly mapped toplevel, is usually the middle of the screen. It is also why the
cached `m_x`/`m_y` that `ScreenToClient()` subtracts are meaningless, so the
two halves of the bug are one cause seen twice.

## Confirmed away from this machine

Everything above is headless sway. The two user-visible consequences have since
been reproduced on a real Wayland desktop with a real mouse, on **wxGTK 3**,
with none of the port's changes present: a floating pane loses the pointer when
undocked, and cannot be docked again.

So the measurements here predicted behaviour on a compositor they were not
taken on, and on a toolkit this port does not touch. That is worth recording
for two reasons: it is evidence the readings generalise, and it settles that
wxAUI docking under Wayland is a wxWidgets defect rather than anything GTK4
introduced.

## Limits

One compositor, one version, headless, software rendering. `xdg_surface`
semantics are not sway's to change, so the conclusion above is not expected to
be compositor-specific, but it has only been measured on sway 1.9.
