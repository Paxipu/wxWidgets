#!/bin/sh
# Capture enough about a sample crash to act on it, on a machine where the
# crash actually happens. Nothing here needs an interactive debugger.
#
#   crash-capture.sh ./samples/combo/combo
#
# gdb is run with debuginfod turned off, and in batch mode.
#
# debuginfod is the usual reason gdb looks like it has hung on a GTK
# application: it fetches debug info over the network for each shared
# library as symbols load, a GTK app loads a great many of them, and an
# unreachable or slow server costs a timeout on every single one. Turning
# it off loses nothing here, since distribution debug info would not
# describe your own build of wx anyway.
#
# It needs -iex rather than -ex, because the setting has to be in place
# before the program file is loaded, which is when the fetching starts.
# Clearing DEBUGINFOD_URLS covers gdb builds that read it directly.

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
# If the crash dumped core, the core has the whole story and nothing has to
# be reproduced under a debugger to get at it. coredumpctl prints a stack
# trace by itself, so try that before running anything again.
if command -v coredumpctl >/dev/null &&
   coredumpctl info >/dev/null 2>&1; then
    coredumpctl info | tail -40
elif command -v gdb >/dev/null; then
    DEBUGINFOD_URLS= DEBUGINFOD_TIMEOUT=1 \
    gdb -batch \
        -iex 'set debuginfod enabled off' \
        -ex run -ex 'bt full' -ex 'info sharedlibrary' \
        --args "$APP" 2>&1 | tail -60
else
    echo "no gdb; try: ulimit -c unlimited && $APP, then coredumpctl gdb"
fi
