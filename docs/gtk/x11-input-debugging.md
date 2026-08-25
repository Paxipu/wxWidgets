# When a GUI test gets no input

Several of the failures tracked in issue #82 look identical from inside the
test: an `EventCounter` stays at zero, and the assertion says the control never
saw the click or the key. From inside the process there is nothing else to
look at. From outside there are three quite different causes, and telling them
apart is the whole job.

`probes/x11-focus-watch.c` asks the X server rather than the toolkit:

```
gcc -o focus-watch probes/x11-focus-watch.c -lX11
DISPLAY=:$disp ./focus-watch
```

```
focus=window(0x400005) revert=Parent  pointer=640,512 over=None (the root window)
pointer_grab=free keyboard_grab=free
```

| what it shows | what it means |
|---|---|
| `focus=PointerRoot` | nobody set the input focus. Every key goes to whatever window the pointer is over -- and if that is `over=None`, into nothing at all |
| `over=None (the root window)` | the pointer is not over any application window, so injected clicks land nowhere |
| `pointer_grab=held by someone` | another client, or another window of the same client, has taken a grab and is receiving everything |

Sampling it in a loop next to a running test gives a trace rather than a
snapshot, which is what makes the difference visible:

```sh
./test_gui -f spec & tp=$!
while kill -0 $tp 2>/dev/null; do ./focus-watch; done
```

## What this established for #82

The order-dependent failures in the GTK4 suite -- `KeyboardEventTestCase`,
`EventPropagationTestCase`, `HtmlWindowTestCase`, `ValNum::Interactive`,
`wxTextCtrl::EmptyUndoBuffer` and `EnterLeaveEvents` -- all pass on their own
and all fail once `Window::TransientPopupClientSize` has run before them.
Two test cases are enough to reproduce it:

```
Window::TransientPopupClientSize, KeyboardEventTestCase
    -> 8 assertions, 2 passed, 6 failed   (GetKeyDownCount() == 0 throughout)

KeyboardEventTestCase alone
    -> All tests passed (96 assertions)
```

The traces differ in exactly one line:

```
alone            : focus=PointerRoot ... then focus=window(0x400005) revert=Parent
after the popup  : focus=PointerRoot ... and it never changes
```

So the toplevel never gets the X input focus, and with the pointer over the
root window every injected key is discarded. `wxWindowGTK::SetFocus()` is what
would establish it:

```cpp
if (tlw && gtk_widget_get_visible(tlw) && !gtk_window_is_active(GTK_WINDOW(tlw)))
    gtk_window_present(GTK_WINDOW(tlw));
```

and tracing that call shows the difference plainly: run alone, the first
`SetFocus()` sees `active=0`, presents, and the focus is set. After the popup
test the *first* call already sees `active=1`, so nothing is presented, ever.
GTK's idea of the toplevel being active and the toplevel actually holding the
input focus have come apart, and wx uses the first as a proxy for the second.

### Ruled out by measurement

So that nobody spends the time again -- none of these changes anything:

* the popup window leaking: it is destroyed, confirmed in the X window tree
* a leaked grab: `pointer_grab` is held for nine samples while the popup is up
  and free afterwards
* the missing window manager: it fails under `openbox` exactly as it does on a
  bare `Xvfb`
* `gtk_popover_popdown()` instead of hiding the widget
* leaving `autohide` on while the popover closes
* `gtk_window_present()` on the toplevel, from `Dismiss()` and from an idle
  after it, with and without `gtk_window_set_focus(nullptr)` first
* `gtk_widget_grab_focus()` on the parent or on the toplevel
* presenting unconditionally in `SetFocus()`, ignoring `gtk_window_is_active()`
* `XSetInputFocus()` on the toplevel, both from the dismiss path and from
  `SetFocus()` itself

A plain GTK4 program doing the same popup dance does **not** reproduce it: its
toplevel keeps the focus throughout. Whatever diverges is therefore in how wx
shows and focuses its windows, not in `GtkPopover` alone -- which is where the
next attempt should start.
