#!/bin/sh
# Control for wayland-screen-coords.cpp: run it on both backends and ask
# somebody outside the process where the window really is.
#
# The probe alone proves nothing -- wx may report whatever it likes. What
# makes the numbers mean something is that the same probe runs under X11,
# where the mapping is known to work and the X server can be asked, and
# under headless sway, where the compositor can be asked the same question.
# A wrong answer on both would be a broken probe; a wrong answer only where
# the position is unknowable is the defect.
#
# Usage: wayland-screen-coords.sh <wx-build-dir>

set -e

BUILD=${1:?usage: $0 <wx-build-dir>}
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${TMPDIR:-/tmp}/wl-screen-coords.$$
PROBE=$WORK/wayland-screen-coords
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

echo "== building the probe =="
g++ -o "$PROBE" "$HERE/wayland-screen-coords.cpp" \
    $("$BUILD/wx-config" --cxxflags) $("$BUILD/wx-config" --libs core,base)

run_under_x11()
{
    echo
    echo "== X11: the mapping that is known to work =="
    Xvfb :78 -screen 0 1280x1024x24 >/dev/null 2>&1 &
    xvfb=$!
    sleep 1
    DISPLAY=:78 openbox >/dev/null 2>&1 &
    wm=$!
    sleep 2
    env -u WAYLAND_DISPLAY DISPLAY=:78 GDK_BACKEND=x11 \
        LD_LIBRARY_PATH="$BUILD/lib" "$PROBE" >"$WORK/x11.out" 2>&1 &
    probe=$!
    sleep 3

    win=$(DISPLAY=:78 xdotool search --name '^coordstest$' 2>/dev/null |
          head -1)
    if [ -n "$win" ]; then
        echo "X server says the frame is at: $(DISPLAY=:78 xdotool \
            getwindowgeometry "$win" 2>/dev/null |
            sed -n 's/.*Position: *\([0-9-]*,[0-9-]*\).*/(\1)/p')"
    else
        echo "X server says: (no such window)"
    fi

    wait $probe 2>/dev/null || true
    grep '^WX' "$WORK/x11.out" || cat "$WORK/x11.out"
    kill $wm $xvfb 2>/dev/null || true
}

run_under_wayland()
{
    echo
    echo "== Wayland: the same probe, the same questions =="
    XDG_RUNTIME_DIR=$WORK/xdgrt
    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 700 "$XDG_RUNTIME_DIR"
    export XDG_RUNTIME_DIR

    {
        echo 'output HEADLESS-1 mode 1280x1024'
        echo 'for_window [title="coordstest"] floating enable'
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

    echo "compositor says the frame is at: $(swaymsg -t get_tree 2>/dev/null |
        jq -r '.. | objects | select(.name == "coordstest") |
               "(\(.rect.x),\(.rect.y)) \(.rect.width)x\(.rect.height)"')"

    wait $probe 2>/dev/null || true
    grep '^WX' "$WORK/wl.out" || cat "$WORK/wl.out"
    kill $sway_pid 2>/dev/null || true
}

run_under_x11
run_under_wayland
