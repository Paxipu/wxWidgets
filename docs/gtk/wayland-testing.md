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

## Limits

One compositor, one version, headless, software rendering. `xdg_surface`
semantics are not sway's to change, so the conclusion above is not expected to
be compositor-specific, but it has only been measured on sway 1.9.
