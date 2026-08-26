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
echo "== control: does a GTK application with no wx in it survive? =="
# Ask this before anything else. If GTK's own demo dies the same way, the
# fault is in the GTK installation and every reading below it is about that,
# not about wx. This is not hypothetical: the first crash this script was
# written for turned out to be a recursion between GTK and the ibus input
# method module, and gtk4-widget-factory crashed identically.
control=
for c in gtk4-widget-factory gtk4-demo; do
    command -v $c >/dev/null && { control=$c; break; }
done
if [ -z "$control" ]; then
    echo "neither gtk4-widget-factory nor gtk4-demo installed -- no control"
else
    $control >/tmp/crash-control.log 2>&1 &
    pid=$!
    sleep 5
    if kill -0 $pid 2>/dev/null; then
        echo "$control survived 5s -- a plain GTK app is fine here"
        kill $pid 2>/dev/null
    else
        wait $pid; rc=$?
        if [ $rc -gt 128 ]; then
            echo "$control ALSO died on $(kill -l $((rc-128)))"
            echo "=> this is the GTK installation, not wx. Stop here."
        else
            echo "$control exited $rc"
        fi
    fi
fi

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
echo "== with a plain input method =="
# An input method module is loaded into every GTK application on the
# machine, runs before anything of wx's does, and is not covered by any
# CI. Swapping it for the built-in one says whether the crash needs it.
GTK_IM_MODULE=gtk-im-context-simple "$APP" >/tmp/crash-im.log 2>&1 &
pid=$!
sleep 5
if kill -0 $pid 2>/dev/null; then
    echo "survived 5s with gtk-im-context-simple"
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
echo "== the control: a GTK4 application that is not wx =="
# Without this the whole run only establishes that something crashed on a
# machine wx was also running on. If a stock GTK4 application dies the same
# way, the fault is in the installation and looking through wx wastes the
# next day.
found=
for app in gtk4-widget-factory gtk4-demo; do
    command -v $app >/dev/null || continue
    found=$app
    $app >/tmp/crash-control.log 2>&1 &
    pid=$!
    sleep 5
    if kill -0 $pid 2>/dev/null; then
        echo "$app survived 5s"
        kill $pid 2>/dev/null
    else
        wait $pid; rc=$?
        if [ $rc -gt 128 ]; then
            echo "$app died on $(kill -l $((rc-128)))"
        else
            echo "$app exited $rc"
        fi
    fi
    break
done
if [ -z "$found" ]; then
    # Say so rather than printing nothing: an empty section here reads
    # like a control that passed.
    echo "no stock GTK4 application installed -- install"
    echo "gtk-4-examples for gtk4-widget-factory and run this again,"
    echo "because without it nothing here rules out the installation"
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
    # Bounded, because "run" only returns when the program stops. If it
    # does not crash there is nothing to stop it and gdb waits forever --
    # which is how this step behaves on a machine where the bug does not
    # reproduce, and is not the same thing as gdb being stuck.
    DEBUGINFOD_URLS= DEBUGINFOD_TIMEOUT=1 \
    timeout 120 gdb -batch \
        -iex 'set debuginfod enabled off' \
        -ex run -ex 'bt full' -ex 'info sharedlibrary' \
        --args "$APP" >/tmp/crash-gdb.log 2>&1
    # Check timeout's own status, which a pipeline here would hide behind
    # tail's, so redirect first and read the file afterwards.
    if [ $? -eq 124 ]; then
        echo "gdb timed out: it did not crash under gdb"
    fi
    tail -60 /tmp/crash-gdb.log
else
    echo "no gdb; try: ulimit -c unlimited && $APP, then coredumpctl gdb"
fi
