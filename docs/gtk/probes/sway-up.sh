#!/bin/sh
# Bring up a headless sway and print its socket, reusing a live one.
export XDG_RUNTIME_DIR=/tmp/xdgrt
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
for s in $(ls -t "$XDG_RUNTIME_DIR"/sway-ipc.*.sock 2>/dev/null); do
    if SWAYSOCK=$s swaymsg -t get_version >/dev/null 2>&1; then echo "$s"; exit 0; fi
done
rm -f "$XDG_RUNTIME_DIR"/sway-ipc.*.sock
cat > /tmp/sway.cfg <<'CFG'
output HEADLESS-1 mode 1600x1200
for_window [title="auidock"] floating enable
for_window [title="Tree Pane"] floating enable
CFG
nohup env WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 \
    XDG_RUNTIME_DIR=/tmp/xdgrt sway -c /tmp/sway.cfg >/tmp/sway.log 2>&1 &
for _ in $(seq 40); do
    for s in $(ls -t "$XDG_RUNTIME_DIR"/sway-ipc.*.sock 2>/dev/null); do
        if SWAYSOCK=$s swaymsg -t get_version >/dev/null 2>&1; then echo "$s"; exit 0; fi
    done
    sleep 0.25
done
echo "FAILED" >&2; exit 1
