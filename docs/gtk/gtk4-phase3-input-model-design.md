# Phase 3 design: event-controller input model

Status: **design only, not implemented.** Traced the real code (not
written from memory of the GTK4 docs) to ground this, the same way
`gtk4-phase2-window-model-design.md` was grounded. Unlike that document,
this one stops at design rather than also executing step 1, for reasons
explained in §5.

## 1. What's being replaced

`wxWindowGTK::ConnectWidget()` (`src/gtk/window.cpp:4315`) wires up eight
raw signals per widget, all of which take a concrete `GdkEventFoo*`
struct pointer and none of which exist under GTK4:

| Signal | Struct | Callback | GTK4 replacement |
|---|---|---|---|
| `key_press_event`/`key_release_event` | `GdkEventKey` | `gtk_window_key_press/release_callback` | `GtkEventControllerKey`, signals `key-pressed`/`key-released(keyval, keycode, state)` |
| `button_press_event`/`button_release_event` | `GdkEventButton` | `wxGTKImpl::WindowButtonPress/ReleaseCallback` | `GtkGestureClick`, signals `pressed`/`released(n_press, x, y)` |
| `motion_notify_event` | `GdkEventMotion` | `wxGTKImpl::WindowMotionCallback` | `GtkEventControllerMotion`, signal `motion(x, y)` |
| `scroll_event` | `GdkEventScroll` | `scroll_event` | `GtkEventControllerScroll`, signals `scroll(dx, dy)`/`discrete-scroll` |
| `enter_notify_event`/`leave_notify_event` | `GdkEventCrossing` | `gtk_window_enter/leave_callback` | Also `GtkEventControllerMotion`, signals `enter`/`leave` — same controller as motion |
| `popup_menu` | n/a (no struct) | `wxgtk_window_popup_menu_callback` | Unchanged — this signal still exists in GTK4 |
| `notify::scale-factor` | n/a | `gtk_window_scale_factor_notify` | Already `#ifdef __WXGTK3__`-only; GTK4 needs its own scale-factor change notification (`GtkWidget` has no direct signal for this — likely needs `notify::scale-factor` verified separately, low priority) |

All of the struct-based ones funnel their `GdkEventFoo*` into a shared
template, `wxGTKImpl::InitMouseEvent<T>` (`include/wx/gtk/private/event.h:40`),
which reads `->state`, `->x`, `->y`, `->window`, `->time` directly —
also incompatible with GTK4's opaque `GdkEvent`, and used from 4 files
(`window.cpp`, `dataview.cpp`, `srchctrl.cpp`, and transitively wherever
`WindowButtonPressCallback` etc. are called with `synthesized=true`).

## 2. The wrinkle found while tracing this: `EventAlreadyProcessed<T>`

`window.cpp:1371` defines a generic template used by (at least)
`WindowButtonReleaseCallback` and `WindowMotionCallback`:

```cpp
template <typename EventType>
bool EventAlreadyProcessed(const EventType* event)
{
    auto* const loop = static_cast<wxGUIEventLoop*>(wxEventLoop::GetActive());
    auto* const ev = reinterpret_cast<const GdkEvent*>(event);
    if ( loop->GTKIsSameAsLastEvent(ev, sizeof(EventType)) && !gs_isNewEvent )
        return true;
    ...
}
```

This reinterprets a `GdkEventButton*`/`GdkEventMotion*` as a raw
`GdkEvent*` and byte-compares it (`sizeof(EventType)` bytes) against the
event loop's cached "last event" to detect and skip an event that's
already been processed once (GTK propagates unhandled events up the
widget hierarchy, and wx doesn't want to reprocess the same native event
for multiple wx windows). **This has no equivalent under GTK4**: opaque
`GdkEvent` has no `sizeof()` a caller can rely on, and more importantly,
event controllers don't have the same propagate-up-the-hierarchy
duplication problem this was built to solve in the first place — each
controller is attached directly to the widget it cares about, so this
whole mechanism may simply become unnecessary rather than needing a
translated replacement. That's a judgment call requiring its own
verification once controllers are wired up, not a mechanical port.

## 3. Recommended per-signal implementation order

Ranked by self-containment (lowest cross-cutting risk first):

1. **Keyboard** (`GtkEventControllerKey`). Single target widget
   (`focusWidget`), no cross-widget redirection, no capture semantics, no
   `EventAlreadyProcessed` involvement found. `wxFillOtherKeyEventFields`
   and `wxTranslateGTKKeyEventToWx` (`window.cpp:1141`, `1216`) need their
   `GdkEventKey*` parameter replaced with the primitive values the new
   signal hands over directly — `keyval`, `keycode` (`= hardware_keycode`),
   `state`, plus `gtk_event_controller_get_current_event_time()` for the
   timestamp and the signal name itself (`key-pressed` vs. `key-released`)
   in place of `gdk_event->type`. The keysym-to-wx-keycode translation
   logic below that (hundreds of `case GDK_KEY_*` lines) is already
   value-based, not struct-based, and needs no changes at all.

2. **Scroll** (`GtkEventControllerScroll`). Single callback, but has real
   behavioral nuance to preserve: horizontal/vertical axis swap when the
   event lands on an embedded scrollbar (`is_range_h`/`is_range_v` in
   `scroll_event`, `window.cpp:2250`), and smooth-scroll delta handling
   (`GDK_SCROLL_SMOOTH` case). GTK4 splits this across `scroll` (smooth,
   `dx`/`dy` doubles) and needs `GTK_EVENT_CONTROLLER_SCROLL_DISCRETE`
   flag setup at controller-creation time to also get discrete
   up/down/left/right notifications — needs verifying which mode(s) wx
   actually needs based on the existing dual-path logic.

3. **Motion + Enter/Leave** (`GtkEventControllerMotion`, one controller,
   three signals: `motion`, `enter`, `leave`). Significantly more
   involved: `WindowMotionCallback` (`window.cpp:2097`) has a whole
   separate code path for `g_captureWindow` (mouse-capture) that
   identifies the widget currently under the pointer via
   `wx_gdk_device_get_window_at_position()` and `gdk_window_get_user_data()`
   — both `GdkWindow`-based hit-testing that doesn't exist under GTK4.
   The GTK4-native replacement for "which widget is under this point" is
   `gtk_widget_pick(toplevel, x, y, GTK_PICK_DEFAULT)`, which returns a
   `GtkWidget*` directly — actually a simplification once ported, since
   it skips the window→widget indirection entirely, but the scrollbar-
   overlay special case (`GTK_IS_SCROLLBAR(widgetUnderMouse)` check,
   `window.cpp:2143`) needs re-verifying against widget-based picking.
   Also handles the hint-based motion throttling
   (`gdk_event_request_motions`/`is_hint`, `window.cpp:2206`) which has
   no equivalent concept in GTK4 (event compression is handled
   differently, likely needs no explicit code at all — GTK4 already
   compresses motion events internally).

4. **Buttons** (`GtkGestureClick`). Saved for last because `GtkGesture`
   introduces claim/deny sequence semantics that don't exist for plain
   signal-based event delivery: multiple gestures on the same or nested
   widgets can compete for a single pointer sequence, and the widget
   needs to explicitly claim it (`gtk_gesture_set_state(gesture,
   GTK_EVENT_SEQUENCE_CLAIMED)`) for it to behave like the old
   unconditional `button_press_event` delivery. Getting this wrong
   produces either lost clicks or double-handling, which is exactly the
   kind of bug that's invisible by code review and only shows up when
   actually clicking things.

## 4. What this means for `docs/gtk/gtk4-status.md`'s error categories

This work would address most of the `gtk_widget_get_toplevel` (Phase 3
row) and a meaningful chunk of `gtk_widget_get_window` occurrences that
are part of the `GTKGetWindow`/`GTKIsOwnWindow` event-routing machinery
described in the Phase 2 design doc §2 — but not all of it; some
`gtk_widget_get_window` hits are the paint/clip ones that belong to the
former Phase 4 (`draw`→`snapshot`) work instead.

## 5. Why this document stops at design instead of also executing step 1

The Phase 2 window-model document went on to implement and commit its
first step (the display swap) in the same session because that work was
mechanical and independently compile-verifiable — I could be confident
it was correct from the code alone. This one is different in kind, for
two reasons:

- **§2's `EventAlreadyProcessed` wrinkle is a real architectural
  question**, not a translation exercise — whether GTK4's controller
  model makes it unnecessary needs to be verified by tracing what
  problem it actually prevents, then confirming a controller-based
  design doesn't reintroduce it, not assumed.
- **None of this is runtime-testable yet.** `Xvfb`/`xvfb-run` are
  available in this environment, which means once enough of the tree
  compiles to link a sample, mouse/keyboard behavior *can* actually be
  exercised here — but the library doesn't compile yet (1760 errors,
  `docs/gtk/gtk4-status.md`), so that's not available now. Mouse capture
  redirection, gesture claim/deny, and scroll-axis-swap behavior are
  exactly the kind of logic that reads correctly in isolation but is
  wrong in a way only a running app would reveal. Shipping that
  unverified, in a part of the code every single widget depends on,
  isn't a good trade against the cost of waiting until it can be
  clicked on.

Recommendation: come back to this once enough of the `GtkContainer`/
`GtkBin` mechanical cleanup (Phase 5, see below) has landed to get a
sample binary linking under `xvfb-run`, so each signal in §3's order can
be implemented and then actually clicked/typed at before being called
done.
