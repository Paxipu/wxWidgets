# Printing under GTK4 may not be a dialog at all

Issue #161 started from an observation that looks like a hang in wx: under
GTK4, `wxPrinter::Print(parent, printout, true)` never returns, no print
dialog ever appears, and the application is wedged in the print path forever.
The reported reading was that GTK takes the portal route even though no
`xdg-desktop-portal` is running, and that the missing service is why no
answer ever comes.

Measured, it is almost the opposite of that, and none of it is a wx defect.
This file records what the measurements were, because "printing hangs" will
be reported again and the useful question is a narrow one: *which of the two
routes did GTK take, and who is not answering?*

## Two routes, chosen by GTK, not by wx

`wxGtkPrintDialog::ShowModal()` calls `gtk_print_operation_run()`. GTK serves
that call in one of two completely different ways:

| route | who shows the dialog | what a stall looks like |
|---|---|---|
| in-process `GtkPrintUnixDialog` | our own process, on our own display | a real window; it "hangs" only in the sense that a modal dialog waits for the user |
| `xdg-desktop-portal` | another process, over D-Bus, on whatever display *it* is attached to | no window anywhere we can see, and a nested main loop that waits with no timeout |

wx does not choose between them and has no API to. `gtk_print_operation_run()`
is `GDK_AVAILABLE_IN_ALL` in 4.22.4, i.e. wx is not using a deprecated or
discouraged entry point that has a better replacement.

The route depends on the GTK version in play, and
`probes/gtk4-print-portal-route.c` reports which one was taken from inside
the process: while `gtk_print_operation_run()` spins its nested main loop, a
timeout fires *inside that loop* and asks GTK for its toplevel windows. An
in-process dialog is one of them; a portal dialog belongs to another process
and never appears there. Four runs, same machine, same Xvfb display:

| run | GTK | portal on the bus | `PROBE_ROUTE` |
|---|---|---|---|
| A | 4.14.5 | yes | `in-process`, toplevel `[Print]` |
| B | 4.22.4 | yes | `portal-or-stalled`, no toplevel |
| C | 4.22.4, `GDK_DEBUG=no-portals` | yes | `in-process`, toplevel `[Print]` |
| D | 4.22.4, fresh bus with no portal | no | `in-process`, toplevel `[Print]` |

All four report `PROBE_RESULT=blocked`, and that is the point: *blocked* alone
says nothing. A modal dialog waiting for a user it does not have looks exactly
like a nested loop waiting for a reply that will never come. Only the route
tells them apart, which is why the probe reports it.

Note what run A means for anyone reproducing a print bug: **the GTK the
program was compiled against is not necessarily the GTK it runs against.**
The build here compiles with `-I.../Cellar/gtk4/4.22.4/include/gtk-4.0` but
resolves `libgtk-4.so.1` to the distribution's 4.14.5 unless
`LD_LIBRARY_PATH` puts the Homebrew tree first. A reproduction attempt that
forgets that takes the in-process route and sees nothing wrong. This is the
same class of mistake as swapping a `.so` while the loader follows the
`.so.4` SONAME.

## The portal switch that everyone documents is not the one in play here

The widely-documented gate is `gtk_should_use_portal()`: portals are used
inside a Flatpak sandbox, or when `GTK_USE_PORTAL=1` is set. Neither applies
here -- `GTK_USE_PORTAL` is unset, and there is no `/.flatpak-info` nor one
under `XDG_RUNTIME_DIR`. GTK 4.22 nevertheless takes the portal route. The decision has moved into
`gdk_display_should_use_portal()` -- a local symbol in the library, alongside
`check_portal_interface`, and `GTK_USE_PORTAL` no longer appears in the binary
at all. What the runs above actually establish is the behaviour rather than
the source: with a portal on the bus 4.22 uses it (run B), without one it does
not (run D), and neither run is inside a sandbox. Newer GTK simply prefers
portals outside sandboxes too.

So the answer to "does this hit users on lean desktops without a portal?" is
**no**, and that is worth stating positively, because it is the reassuring
half of the finding. Run D above starts a session bus with no portal service
at all and no way to activate one:

```sh
XDG_DATA_DIRS=/nonexistent dbus-run-session -- ./print-route
```

GTK 4.22 then serves the call itself and the dialog appears. GTK's own
fallback works. A *missing* portal is handled; what is not handled is a portal
that is present and silent.

## Where it actually stalls

Monitoring the whole conversation rather than just the first call is what
identifies the silent party:

```sh
dbus-monitor --session "path_namespace='/org/freedesktop/portal/desktop'"
```

```
method call  :1.3808 -> :1.4       interface=org.freedesktop.portal.Print       member=PreparePrint
method call  :1.4    -> :1.1163    interface=org.freedesktop.impl.portal.Print  member=PreparePrint
...
Response signals in 40 s: 0
```

`PreparePrint` is not the thing that blocks -- it returns a request object
path immediately, and calling it by hand with `gdbus call` answers at once.
The result of a portal request arrives later, as a
`org.freedesktop.portal.Request.Response` signal on that object path, and
GTK's nested main loop waits for that signal. Here it never arrives: the
front end (`:1.4`) forwards the request to the backend
(`:1.1163`, `xdg-desktop-portal-gtk`) and the backend goes quiet, because it
cannot put its dialog on the display the application is using. Under WSL2 the
portal lives in the WSLg system distribution -- its PIDs are not even in our
PID namespace -- so it is attached to the WSLg session, not to the Xvfb
display the test runs on.

The wait has no timeout. Measured at 240 s with a single `PreparePrint` and
no reply, so "never returns" is accurate and is not some D-Bus timeout that
would eventually fire.

## What this means for wx

Nothing to fix in the print path, and the checks worth recording as *not*
being the problem:

- wx passes a valid transient parent. The portal receives
  `parent_window = "x11:200005"`, not an empty string, so the dialog would be
  correctly parented if it were shown.
- `gtk_print_operation_run()` is not deprecated in 4.22.4.
- The test suite never enters the print path -- `tests/allheaders.h` includes
  `wx/print.h` to check that it compiles, and nothing calls into it -- so this
  cannot stall CI. The `printing` sample does call into it, and will stall in
  an environment like the one above.

Setting `GDK_DEBUG=no-portals` restores the in-process dialog and makes
printing work again, which is the quickest way to confirm that a reported
print hang really is this and not something in wx:

```
GDK_DEBUG=no-portals ./yourapp     -> window "Print" appears next to the app window
```

That is a diagnostic, not a fix to ship. wx should not disable portals for
every application it hosts: inside a sandbox the portal is the *only* way to
reach a printer, so a toolkit-wide opt-out would break the case portals exist
for. GTK 4.22 exposes `gtk_disable_portals()` for an application that wants
to make that choice for itself.
