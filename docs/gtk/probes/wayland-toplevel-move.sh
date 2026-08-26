#!/bin/sh
# Control for wayland-toplevel-move.cpp: run it under a headless sway and ask
# the compositor where the window really is.
#
# The probe alone can only report what wx believes.  Two extra measurements
# make that reading mean something:
#
#   1. after the probe has done its moves, sway is told to move the window
#      itself.  If that does not move it either, the harness is broken and no
#      other number in this run is worth reading.
#   2. the same probe runs under X11 first, where Move() is expected to work.
#      That is what shows the probe can detect a move at all.
#
# Usage: wayland-toplevel-move.sh <wx-build-dir>

set -e

BUILD=${1:?usage: $0 <wx-build-dir>}
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${TMPDIR:-/tmp}/wl-toplevel-move.$$
PROBE=$WORK/wayland-toplevel-move
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

echo "== building the probe =="
g++ -o "$PROBE" "$HERE/wayland-toplevel-move.cpp" \
    $("$BUILD/wx-config" --cxxflags) $("$BUILD/wx-config" --libs core,base)

rect_of()
{
    swaymsg -t get_tree 2>/dev/null | jq -r --arg t "$1" \
        '.. | objects | select(.name == $t) |
         "(\(.rect.x),\(.rect.y)) \(.rect.width)x\(.rect.height)"'
}

x11_rect()
{
    win=$(DISPLAY=$1 xdotool search --name '^movetest$' 2>/dev/null | head -1)
    [ -n "$win" ] || { echo "(no such window)"; return; }
    DISPLAY=$1 xdotool getwindowgeometry "$win" 2>/dev/null |
        sed -n 's/.*Position: *\([0-9-]*,[0-9-]*\).*/(\1)/p'
}

run_under_x11()
{
    echo
    echo "== X11: can the probe detect a move at all? =="
    Xvfb :77 -screen 0 1280x1024x24 >/dev/null 2>&1 &
    xvfb=$!
    sleep 1
    env -u WAYLAND_DISPLAY DISPLAY=:77 GDK_BACKEND=x11 \
        LD_LIBRARY_PATH="$BUILD/lib" "$PROBE" >"$WORK/x11.out" 2>&1 &
    probe=$!
    sleep 4
    echo "X server says the window is at: $(x11_rect :77)"
    echo "-- what wx believed --"
    grep '^MOVE' "$WORK/x11.out" || cat "$WORK/x11.out"
    kill $probe $xvfb 2>/dev/null || true
    wait $probe 2>/dev/null || true
}

run_under_wayland()
{
    echo
    echo "== Wayland: what does the compositor say? =="
    XDG_RUNTIME_DIR=$WORK/xdgrt
    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 700 "$XDG_RUNTIME_DIR"
    export XDG_RUNTIME_DIR

    {
        echo 'output HEADLESS-1 mode 1280x1024'
        echo 'for_window [title="movetest"] floating enable'
    } > "$WORK/sway.cfg"
    WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 \
        sway -c "$WORK/sway.cfg" >"$WORK/sway.log" 2>&1 &
    sway_pid=$!
    sleep 2

    WAYLAND_DISPLAY=$(basename "$(ls "$XDG_RUNTIME_DIR"/wayland-* 2>/dev/null |
                                  grep -v '\.lock$' | head -1)")
    SWAYSOCK=$(ls "$XDG_RUNTIME_DIR"/sway-ipc.*.sock 2>/dev/null | head -1)
    export WAYLAND_DISPLAY SWAYSOCK

    env -u DISPLAY GDK_BACKEND=wayland LD_LIBRARY_PATH="$BUILD/lib" \
        "$PROBE" >"$WORK/wl.out" 2>&1 &
    probe=$!

    sleep 1
    echo "on screen, before the moves:   $(rect_of movetest)"
    sleep 3
    echo "on screen, after the moves:    $(rect_of movetest)"

    # The harness control.  If this one does not move either, stop reading.
    swaymsg '[title="movetest"] move position 900 700' >/dev/null 2>&1 || true
    sleep 1
    echo "after the compositor moved it: $(rect_of movetest)"

    echo "-- what wx believed --"
    cat "$WORK/wl.out"

    kill $probe $sway_pid 2>/dev/null || true
    wait $probe 2>/dev/null || true
}

run_under_x11
run_under_wayland
