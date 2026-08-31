#!/bin/bash
# Can a pane that is already floating be docked again by dragging its caption?
#
#   aui-redock-floating.sh x11
#   aui-redock-floating.sh wayland
#
# Separate from aui-dock-roundtrip.sh because this is a different failure with
# a different cause: that one drags a *docked* caption, which never goes near
# the code this exercises. The pane is floated through the manager rather than
# by dragging, so a failure here cannot be a failed undock wearing a re-dock's
# clothes.
#
# Two things about the drag are load-bearing and were each found the hard way.
#
# It moves three pixels at a time. wxAuiFloatingFrame::OnMoveEvent() discards
# any move larger than that outright -- "skip if moving too fast to avoid
# massive redraws" -- so a drag that jumps in tens of pixels has every one of
# its events thrown away, reaches no dock logic at all, and reports a docking
# failure that is entirely the harness's doing. Every scripted drag written
# for this bug before finding that was measuring nothing.
#
# And it finishes within about fifteen pixels of the frame's edge, because
# that is where the dock zone is. Dropping sixty pixels in leaves the pane
# floating on X11 too, which looks exactly like the bug.
WXBUILD=${WXBUILD:-/home/user/wxbuild-gtk4-ci}
AUIDOCK=${AUIDOCK:-/tmp/auidock}
XDRAG=${XDRAG:-/tmp/xdrag}
WLDRAG=${WLDRAG:-/tmp/wldrag}
HERE=$(cd "$(dirname "$0")" && pwd)

steps() {  # x0 y0 x1 y1 -> a wldrag/xdrag command line in 3 px increments
  python3 -c '
import sys
x0,y0,x1,y1 = (int(a) for a in sys.argv[1:5])
out = ["move", str(x0), str(y0), "sleep", "400", "down", "sleep", "500"]
x, y = x0, y0
while (x, y) != (x1, y1):
    x += max(-3, min(3, x1 - x)); y += max(-3, min(3, y1 - y))
    out += ["move", str(x), str(y), "sleep", "18"]
print(" ".join(out + ["sleep", "900", "up", "sleep", "900"]))' "$@"
}

verdict() {
  for _ in $(seq 40); do grep -q '^RESULT' "$1" && break; sleep 1; done
  grep -E '^CHECK|^RESULT' "$1"
  echo "  dock logic ran $(grep -c AUIDRAG "$1") times"
}

if [ "$1" = "x11" ]; then
  D=":$((150 + RANDOM % 40))"
  Xvfb $D -screen 0 1600x1200x24 -nolisten tcp >/dev/null 2>&1 &
  sleep 2
  DISPLAY=$D openbox >/dev/null 2>&1 &
  for _ in $(seq 40); do
    DISPLAY=$D xdotool get_num_desktops >/dev/null 2>&1 && break
    sleep 0.25
  done
  export DISPLAY=$D GDK_BACKEND=x11 LD_LIBRARY_PATH=$WXBUILD/lib
  unset WAYLAND_DISPLAY
  LOG=/tmp/redock-x11.log
  ( AUIDOCK_START_FLOATING=1 WXAUI_DRAGLOG=1 timeout 60 $AUIDOCK > $LOG 2>&1 ) &
  sleep 6
  W=$(xdotool search --name '^Tree Pane$' | head -1)
  M=$(xdotool search --name '^auidock$' | head -1)
  if [ -z "$W" ] || [ -z "$M" ]; then
    echo "  HARNESS windows not found"
    exit 1
  fi
  xdotool windowmove $W 60 60; sleep 1
  eval $(xdotool getwindowgeometry --shell $M)
  $XDRAG $(steps 160 63 $((X+14)) $((Y+350))) >/dev/null 2>&1
  sleep 2
  verdict $LOG
  pkill -f "Xvfb $D" 2>/dev/null
else
  export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/tmp/xdgrt}
  export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1} GDK_BACKEND=wayland
  export LD_LIBRARY_PATH=$WXBUILD/lib
  unset DISPLAY
  SWAYSOCK=$("$HERE/sway-up.sh") || { echo "  HARNESS no compositor"; exit 1; }
  export SWAYSOCK
  LOG=/tmp/redock-wayland.log
  ( AUIDOCK_START_FLOATING=1 WXAUI_DRAGLOG=1 timeout 60 $AUIDOCK > $LOG 2>&1 ) &
  sleep 6
  swaymsg '[title="Tree Pane"] move position 60 60' >/dev/null 2>&1; sleep 1
  origin() {
    swaymsg -t get_tree |
      jq -r --arg t "$1" '.. | objects | select(.name == $t) |
                          "\(.rect.x) \(.rect.y)"'
  }
  read MX MY <<< "$(origin auidock)"
  read PX PY <<< "$(origin "Tree Pane")"
  if [ -z "$MX" ] || [ -z "$PX" ]; then
    echo "  HARNESS windows not found"
    exit 1
  fi
  before="$PX,$PY"
  $WLDRAG $(steps $((PX+100)) $((PY+3)) $((MX+14)) $((MY+350))) >/dev/null 2>&1
  sleep 2
  after=$(origin "Tree Pane" | tr ' ' ',')
  # The control: if the window did not move, the drag never happened and
  # nothing below it is a reading about docking.
  if [ -n "$after" ] && [ "$before" = "$after" ]; then
    echo "  HARNESS the pane never moved -- the drag did not take"
    exit 1
  fi
  echo "  pane was dragged $before -> ${after:-(gone)}"
  verdict $LOG
fi
