#!/bin/bash
# Drives one wxAUI dock round trip with real pointer input and reports what
# the pane's own state says afterwards. Used by issues #134 and #150.
#
#   aui-dock-roundtrip.sh x11        under a private Xvfb + openbox
#   aui-dock-roundtrip.sh wayland    under an already-running compositor
#
# Build the two binaries it drives first, and point the variables below at
# them if they are not where this expects. A stale auidock is worse than no
# auidock: after a header change it reads the manager's fields at the old
# offsets and dies somewhere unrelated (see #150), so rebuild it whenever
# include/wx/aui/ has moved.
#
#   WXBUILD=/path/to/build   the wx build to run against
#   AUIDOCK, XDRAG, WLDRAG   the probe and the input injectors
#
#   g++ -o $AUIDOCK aui-dock-roundtrip.cpp $($WXBUILD/wx-config --cxxflags \
#       --libs core,base,aui)
#   gcc -o $XDRAG xdrag.c -lX11 -lXtst        # X11: XTEST
#   gcc -o $WLDRAG wldrag.c ...               # Wayland: see wldrag.c
WXBUILD=${WXBUILD:-/home/user/wxbuild-gtk4-ci}
AUIDOCK=${AUIDOCK:-/tmp/auidock}
XDRAG=${XDRAG:-/tmp/xdrag}
WLDRAG=${WLDRAG:-/tmp/wldrag}

if [ ! -x "$AUIDOCK" ]; then
  echo "  no $AUIDOCK -- build it first, see the header of this script"
  exit 1
fi

if [ "$1" = "x11" ]; then
  D=":$((110 + RANDOM % 40))"
  Xvfb $D -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
  sleep 2
  DISPLAY=$D openbox >/dev/null 2>&1 &
  export DISPLAY=$D GDK_BACKEND=x11; unset WAYLAND_DISPLAY
  # Wait for the window manager to actually own the display rather than
  # sleeping and hoping. Without one the frame is undecorated and lands
  # somewhere else, the caption is not where the drag aims, and the run
  # reports "never floated it" -- a failure of the harness wearing the
  # clothes of a result.
  for _ in $(seq 40); do
    xdotool get_num_desktops >/dev/null 2>&1 && break
    sleep 0.25
  done
  if ! xdotool get_num_desktops >/dev/null 2>&1; then
    echo "  no window manager on $D -- openbox did not come up"
    exit 1
  fi
  DRAG="env DISPLAY=$D $XDRAG"
else
  export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/tmp/xdgrt}
  export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1}
  export GDK_BACKEND=wayland
  unset DISPLAY
  DRAG="$WLDRAG"

  # Pick the socket that answers rather than the newest name: a compositor
  # that died leaves its socket behind, and swaymsg against one of those
  # fails in a way that looks like the window not existing.
  if [ -z "$SWAYSOCK" ] || ! swaymsg -t get_version >/dev/null 2>&1; then
    for sock in $(ls -t "$XDG_RUNTIME_DIR"/sway-ipc.*.sock 2>/dev/null); do
      if SWAYSOCK=$sock swaymsg -t get_version >/dev/null 2>&1; then
        export SWAYSOCK=$sock
        break
      fi
    done
  fi
  if ! swaymsg -t get_version >/dev/null 2>&1; then
    echo "  HARNESS no compositor answering in $XDG_RUNTIME_DIR"
    exit 1
  fi
fi
export LD_LIBRARY_PATH=$WXBUILD/lib
LOG=/tmp/dockrun-$1.log
( timeout 32 $AUIDOCK > $LOG 2>&1 ) &
APP=$!
# Poll for the geometry rather than sleeping a fixed time: under gdb or a
# loaded machine the app needs longer, and a fixed sleep then reads an empty
# log and calls it a failed start.
for _ in $(seq 60); do
  grep -q '^CLIENT ' $LOG 2>/dev/null && break
  sleep 0.25
done
read AX AY <<< "$(grep -m1 '^AIM ' $LOG | awk '{print $2,$3}')"
read CX CY CW CH <<< "$(grep -m1 '^CLIENT ' $LOG | awk '{print $2,$3,$4,$5}')"
if [ -z "$AX" ]; then
  echo "  no geometry reported -- app did not start"
  kill $APP 2>/dev/null
  exit 1
fi

if [ "$1" = "wayland" ]; then
  # Everything the app reported above went through ClientToScreen(), which
  # under Wayland adds a position the compositor never granted. Aiming at it
  # misses the caption entirely, and the run then reports "never floated it"
  # -- a broken harness wearing the clothes of a result. So derive the
  # mapping from the pointer instead: put it somewhere known, read back where
  # the app says it landed, and subtract.
  # Poll: the app prints its geometry as soon as it has any, which is before
  # the compositor has finished mapping and floating the window. Asking once
  # reads an empty tree and looks like the window never existing.
  for _ in $(seq 40); do
    read SX SY SW SH <<< "$(swaymsg -t get_tree |
        jq -r '.. | objects | select(.name == "auidock") |
               "\(.rect.x) \(.rect.y) \(.rect.width) \(.rect.height)"')"
    [ -n "$SX" ] && break
    sleep 0.25
  done
  if [ -z "$SX" ]; then
    echo "  HARNESS the compositor does not know a window called auidock"
    kill $APP 2>/dev/null; exit 1
  fi
  read RCW RCH <<< "$(grep -m1 '^CLIENTREL ' $LOG | awk '{print $2,$3}')"
  echo "  compositor puts it at $SX,$SY ${SW}x${SH}"

  # Refuse to run if the compositor did not honour the floating rule and
  # tiled the window instead. The application reports its client size once,
  # at startup, before any such resize; every coordinate below is derived
  # from that size, so a window the compositor has since made a different
  # size sends the drop somewhere nobody aimed at. It then lands outside
  # every dock, the pane stays floating, and the run reports a docking
  # failure that is entirely this script's doing.
  if [ "$SW" -gt $((RCW + 40)) ] || [ "$SH" -gt $((RCH + 60)) ]; then
    echo "  HARNESS the compositor resized the window to ${SW}x${SH}, but the"
    echo "          application still reports ${RCW}x${RCH} -- not measurable"
    kill $APP 2>/dev/null; exit 1
  fi

  probe_client()   # $1,$2 absolute -> echoes the client coords reported
  {
    # Approach in steps rather than jumping straight there. Two absolute
    # moves in quick succession do not reliably produce a motion event on the
    # surface -- the pointer has to be seen crossing into it -- and a single
    # jump to a point the pointer already occupies produces nothing at all.
    # Either way the calibration then reads nothing and reports that the
    # pointer never arrived, which is a harness failure dressed as a result.
    $DRAG move $(($1 - 60)) $(($2 - 60)) sleep 150 \
          move $(($1 - 30)) $(($2 - 30)) sleep 150 \
          move "$1" "$2" sleep 300 >/dev/null 2>&1
    sleep 0.3
    grep '^MOTION ' $LOG | tail -1 | awk '{print $2,$3}'
  }

  P1X=$((SX + SW*3/4)); P1Y=$((SY + SH/2))
  read M1X M1Y <<< "$(probe_client $P1X $P1Y)"
  if [ -z "$M1X" ]; then
    echo "  HARNESS the pointer never reached the window -- no motion seen"
    kill $APP 2>/dev/null; exit 1
  fi
  OFFX=$((P1X - M1X)); OFFY=$((P1Y - M1Y))

  # Control: predict a second point from that offset and check the app agrees.
  # Without this the offset is an assumption, and a wrong one produces a run
  # that looks like a measurement.
  P2X=$((SX + SW*3/5)); P2Y=$((SY + SH*2/5))
  read M2X M2Y <<< "$(probe_client $P2X $P2Y)"
  EX=$((P2X - OFFX)); EY=$((P2Y - OFFY))
  if [ -z "$M2X" ] || [ $((M2X - EX)) -gt 2 ] || [ $((EX - M2X)) -gt 2 ] ||
     [ $((M2Y - EY)) -gt 2 ] || [ $((EY - M2Y)) -gt 2 ]; then
    echo "  HARNESS calibration failed: predicted $EX,$EY"
    echo "            but the app saw ${M2X:-none},${M2Y:-none}"
    kill $APP 2>/dev/null; exit 1
  fi
  echo "  calibrated: client+($OFFX,$OFFY) = absolute, checked on a 2nd point"

  read RAX RAY <<< "$(grep -m1 '^AIMREL ' $LOG | awk '{print $2,$3}')"
  AX=$((RAX + OFFX)); AY=$((RAY + OFFY))
  CX=$OFFX; CY=$OFFY; CW=$RCW; CH=$RCH
fi
# Aim at the dock on the far side from where the pane starts. Dropping it
# back on its own edge cannot distinguish a drag that worked from one that
# did nothing at all.
TX=$((CX + CW - 15)); TY=$((CY + CH/2))
echo "  caption $AX,$AY | client $CX,$CY ${CW}x${CH} | drop $TX,$TY"
# Dwell on the drop point with small movements: wxAUI decides where a pane
# would land from the motion events, and shows a hint, before the release
# acts on it. A single jump to the target and an immediate release often
# arrives before that has happened.
$DRAG move $AX $AY sleep 400 down sleep 600 \
  move $((CX+CW/2)) $((CY+CH/3)) sleep 350 \
  move $((CX+CW/3)) $TY sleep 350 \
  move $((TX+80)) $TY sleep 350 \
  move $((TX+8)) $TY sleep 300 \
  move $TX $((TY+3)) sleep 300 \
  move $((TX+2)) $((TY-2)) sleep 300 \
  move $TX $TY sleep 900 up sleep 500 >/dev/null 2>&1
wait $APP
grep -E "STATE|CHECK|RESULT" $LOG
[ "$1" = "x11" ] && pkill -f "Xvfb $D" 2>/dev/null
true
