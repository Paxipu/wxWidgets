#!/bin/sh
# Capture enough about a sample crash to act on it, on a machine where the
# crash actually happens. Nothing here needs an interactive debugger.
#
#   crash-capture.sh ./samples/combo/combo
#
# gdb is used only in batch mode. An interactive gdb on a crashing GTK app
# tends to look like a hang: the app dies holding an X server grab, gdb
# stops at the signal, the grab is never released, and the whole desktop
# stops responding to input -- including gdb's own window. Batch mode runs
# to the crash, prints, and exits, so the grab never outlives the process.

APP=${1:?usage: $0 /path/to/sample}

echo "== environment =="
echo "GDK_BACKEND     = ${GDK_BACKEND:-(unset)}"
echo "WAYLAND_DISPLAY = ${WAYLAND_DISPLAY:-(unset)}"
echo "DISPLAY         = ${DISPLAY:-(unset)}"
echo "GTK build       = $(pkg-config --modversion gtk4 2>/dev/null || echo '?')"
echo "GTK runtime     = $(pkg-config --variable=libdir gtk4 2>/dev/null)"
echo "session bus     = ${DBUS_SESSION_BUS_ADDRESS:-(none)}"
scheme=$(gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null)
echo "colour scheme   = ${scheme:-?}"

echo
echo "== does it crash at all? =="
"$APP" >/tmp/crash-plain.log 2>&1 &
pid=$!
sleep 5
if kill -0 $pid 2>/dev/null; then
    echo "survived 5s -- not a startup crash"
    kill $pid 2>/dev/null
else
    wait $pid; rc=$?
    if [ $rc -gt 128 ]; then
        echo "died on $(kill -l $((rc-128)))"
    else
        echo "exited $rc"
    fi
fi

echo
echo "== with GTK_THEME set =="
# Setting GTK_THEME makes wxSystemSettingsModule::OnInit() skip the desktop
# portal entirely, and with it the colour-scheme code that runs at startup
# in every GUI app. If the crash goes away here and comes back above, that
# code is where to look; if it crashes both ways, it is not.
GTK_THEME=Adwaita "$APP" >/tmp/crash-theme.log 2>&1 &
pid=$!
sleep 5
if kill -0 $pid 2>/dev/null; then
    echo "survived 5s with GTK_THEME set"
    kill $pid 2>/dev/null
else
    wait $pid; rc=$?
    if [ $rc -gt 128 ]; then
        echo "died on $(kill -l $((rc-128)))"
    else
        echo "exited $rc"
    fi
fi

echo
echo "== backtrace =="
if command -v gdb >/dev/null; then
    gdb -batch -ex run -ex 'bt full' -ex 'info sharedlibrary' \
        --args "$APP" 2>&1 | tail -60
else
    echo "no gdb; try: ulimit -c unlimited && $APP, then coredumpctl gdb"
fi
