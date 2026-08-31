#!/bin/sh
# Does a wxStatusNotifierItem reach a panel, and can the panel read it?
#
# Runs on a private session bus, so it needs no desktop and no tray: the
# stand-in watcher in sni-watcher.c plays the panel's part, and reads the
# item's properties back rather than only counting the registration call.
# "It registered" says a method arrived; an item whose IconName cannot be
# read still shows nothing.
#
# The control is built in and runs first: with no item started, the watcher
# has to report WATCHER-TIMEOUT and fail. A test that cannot fail when
# nothing registers proves nothing when something does.
#
# Usage: sni-roundtrip.sh <wx-build-dir> [<wx-source-dir>]

set -e

BUILD=${1:?usage: $0 <wx-build-dir> [<wx-source-dir>]}
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${2:-$(cd "$HERE/../../.." && pwd)}
WORK=${TMPDIR:-/tmp}/sni-roundtrip.$$
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

echo "== building =="
gcc -o "$WORK/watcher" "$HERE/sni-watcher.c" \
    $(pkg-config --cflags --libs gio-2.0)
g++ -o "$WORK/item" "$HERE/sni-item.cpp" "$SRC/src/gtk/statusnotifier.cpp" \
    "$SRC/src/gtk/dbusmenu.cpp" \
    -I"$SRC/include" \
    $("$BUILD/wx-config" --cxxflags) $(pkg-config --cflags gio-2.0) \
    $("$BUILD/wx-config" --libs core,base) $(pkg-config --libs gio-2.0)

run_watcher_alone()
{
    echo
    echo "== control: a watcher with nothing to adopt =="
    if dbus-run-session -- "$WORK/watcher" 4 > "$WORK/ctl.out" 2>&1; then
        echo "  CONTROL FAILED: the watcher reported success with no item"
        cat "$WORK/ctl.out"
        return 1
    fi
    grep -E 'WATCHER-(READY|TIMEOUT)' "$WORK/ctl.out" | sed 's/^/  /'
    echo "  control ok: nothing registered, and the watcher said so"
}

run_together()
{
    echo
    echo "== the item, with the watcher listening =="
    # The item is a GUI application, so it needs a display even though
    # nothing it does draws: wx initialises GTK before OnInit() runs. A
    # private Xvfb keeps this off whatever the machine is already showing.
    Xvfb :79 -screen 0 800x600x24 >/dev/null 2>&1 &
    xvfb=$!
    sleep 2

    cat > "$WORK/both.sh" <<'INNER'
"$WORK/watcher" 15 > "$WORK/watcher.out" 2>&1 &
w=$!
# Wait for the name to be owned rather than sleeping: a bus that is slow to
# hand it over would otherwise look exactly like an item that never called.
for _ in $(seq 40); do
    grep -q WATCHER-READY "$WORK/watcher.out" 2>/dev/null && break
    sleep 0.25
done
DISPLAY=:79 GDK_BACKEND=x11 LD_LIBRARY_PATH="$BUILD/lib" \
    "$WORK/item" > "$WORK/item.out" 2>&1
wait $w
INNER
    WORK="$WORK" BUILD="$BUILD" dbus-run-session -- sh "$WORK/both.sh" || true
    kill $xvfb 2>/dev/null || true

    sed 's/^/  /' "$WORK/item.out"
    sed 's/^/  /' "$WORK/watcher.out"

    # Every property has to carry a value. Matching the property *name* is
    # not enough: an unreadable one prints its name too, and an earlier
    # version of this check passed a run in which the panel adopted the item
    # and then timed out on all eight reads -- exactly the failure the read
    # back exists to catch.
    unreadable=$(grep -c '<unreadable' "$WORK/watcher.out" || true)

    # The menu has to be checked for what it contains, not for having
    # answered. Each of these is a different way for the walk to be wrong
    # while still producing output: a separator that came out as an ordinary
    # item, a check item with no state, a submenu that was flattened, a
    # disabled item reported enabled.
    ok=1
    for want in \
        'MENU-REVISION' \
        'label=_Open window' \
        'label=Stay on _top type=standard enabled=1 toggle=checkmark state=1' \
        'type=separator' \
        'label=_More .* children=submenu' \
        'label=Su_bitem' \
        'label=E_xit type=standard enabled=0'
    do
        grep -qE "$want" "$WORK/watcher.out" || {
            echo "  missing from the layout: $want"
            ok=0
        }
    done

    # And the click has to arrive on the other side. Without this the menu
    # could be readable and inert.
    grep -q 'ITEM-MENU-CLICKED' "$WORK/item.out" || {
        echo "  the click never reached the application"
        ok=0
    }

    if grep -q REGISTERED "$WORK/watcher.out" &&
       grep -q "PROP IconName *'" "$WORK/watcher.out" &&
       grep -q "PROP Id *'" "$WORK/watcher.out" &&
       [ "$unreadable" -eq 0 ] &&
       [ "$ok" -eq 1 ]
    then
        echo "  RESULT PASS the panel read the item, walked the menu," \
             "and the click came back"
    else
        echo "  RESULT FAIL ($unreadable unreadable properties)"
        return 1
    fi
}

run_watcher_alone
run_together
