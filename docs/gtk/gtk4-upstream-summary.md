# wxGTK4 port: findings so far (draft summary for upstream)

This is a draft write-up intended to be shareable with the wxWidgets
maintainers (e.g. as a wiki page, mailing list post, or GitHub issue),
summarizing an independent investigation into what a real GTK4 port of
wxGTK would take. It's a snapshot of work done in a personal fork, not a
formal proposal — edit freely before sending it anywhere, add your own
contact details, and trim whatever doesn't seem useful to include.

Everything referenced below lives in `docs/gtk/` in this fork:
`gtk4-port-plan.md` (phased roadmap), `gtk4-status.md` (compiler-verified
error inventory), `gtk4-phase2-window-model-design.md` (the window/child
model design), and this file.

## Why this exists

wxWidgets already has partial GTK4 scaffolding — `configure --with-gtk=4`
works, and roughly 95 `__WXGTK4__` conditionals exist across `src/gtk`
and `include/wx/gtk` — but no CI job builds against GTK4, so nobody
would notice if that scaffolding stopped working. It's worth knowing,
concretely, how far it currently gets and what's actually left.

## What was done

1. **Built against real GTK4 headers.** Installed `libgtk-4-dev` 4.14.5
   and ran `configure --with-gtk=4` + `make -k` (keep-going) to surface
   every independent compile failure in one pass, rather than guessing
   from source inspection alone.
2. **Catalogued the failures by root cause**, not just by file. The
   result (1304 compiler errors across ~80 files) sounds alarming but
   isn't ~80 unrelated bugs — it's a small number of removed GTK3 APIs
   (`GtkContainer`, `GtkBin`, `gtk_widget_get_window`/`GdkWindow`,
   `gtk_box_pack_start/end`, the old style-properties API,
   `GtkSelectionData`) whose absence cascades through shared code. Full
   breakdown with occurrence counts in `gtk4-status.md`.
3. **Traced the single largest category** (`gtk_widget_get_window`/
   `GdkWindow`, ~43 combined occurrences plus the ~18 per-widget
   `GTKGetWindow()` overrides that exist to support it) to its actual
   call sites rather than assuming it meant the child-positioning model
   needed a redesign. It doesn't: `wxPizza`'s child layout is already
   windowless and carries over from GTK3 as-is. The real usages split
   into six narrower purposes — display lookup, event-source identity
   matching, cursor setting, paint/clip, Z-order, coordinate translation
   — most of which are simpler under GTK4's event-controller/snapshot
   model, not harder. Full design in
   `gtk4-phase2-window-model-design.md`. This changes the effort
   estimate for what looked like the hardest part of the port from
   "months of redesign" to "weeks of coordinated mechanical work,"
   assuming that design holds up once implemented.
4. **Added CI coverage.** `.github/workflows/ci.yml` gets a
   `gtk_version: 4` matrix entry marked `continue-on-error` (not
   expected to pass for a long while, but now tracked rather than
   invisible), and `build/tools/before_install.sh` learned how to
   install `libgtk-4-dev` (it previously only handled GTK 2/3).
5. **Landed two small, self-contained, GTK3-safe fixes** (both verified
   to compile clean against GTK3 and GTK4 headers in isolation):
   - `include/wx/gtk/evtloop.h`: `GdkEvent` was forward-declared as a
     `union` unconditionally; GTK4 declares it a `struct`. Fixed with a
     `__WXGTK4__` conditional.
   - `wxNativeContainerWindow` (`include/wx/nativewin.h`,
     `src/gtk/nativewin.cpp`) is gated off for GTK4 via the *existing*
     `wxHAS_NATIVE_CONTAINER_WINDOW` feature-test macro (the same
     mechanism already used to disable it on Cocoa), rather than left
     to fail with a confusing compiler error. Reasoning below.

## Decision: `wxNativeContainerWindow` is not implementable on GTK4

`src/gtk/nativewin.cpp` is the *only* file in the whole `src/gtk` tree
that reparents a raw `GdkWindow` (used to embed a foreign top-level
window, identified by XID, as a wx container). GTK4 has no equivalent
mechanism: `GdkWindow` doesn't exist, `GtkSocket`/`GtkPlug`-style
embedding was removed from GTK entirely (starting 3.14), and Wayland's
security model precludes foreign-window embedding by ID in the way X11
allowed it. There doesn't appear to be a portal-based or
protocol-specific replacement that provides the same capability.

Given that, `wxHAS_NATIVE_CONTAINER_WINDOW` is now `#undef`'d for
`__WXGTK4__` (mirroring how it's already `#undef`'d for Cocoa, which
also lacks native-TLW support), the implementation is compiled out
entirely, and the Doxygen docs for the class
(`interface/wx/nativewin.h`) now note the GTK4 limitation and point at
the macro. `wxNativeWindow` (the *other* class in the same header,
which embeds a native child *control* rather than a foreign toplevel) is
unaffected — it doesn't touch `GdkWindow` at all and should port without
much difficulty.

## Limitations that follow from Wayland's security model

The `wxNativeContainerWindow` decision above is one instance of something
wider, and it is worth stating as a group rather than meeting six times.
A Wayland client is not allowed to position its own window, is not told
where the compositor put it, and is not told about surfaces belonging to
anyone else. That is deliberate: a client that could do those things
could place a window over another application's and follow it around,
and where a window sits on screen is itself a small fact about the
session. None of it is a gap waiting for an API, and none of it is
something wx can work around.

What wx *can* do is not claim otherwise, and that is what the port aims
at in each case below. Every one of these is measured; the probes and
their controls are in `docs/gtk/probes/` and the numbers in
`docs/gtk/wayland-testing.md`.

* **`Move()` on a top level window does nothing.** There is no request to
  position a toplevel. wx used to send a `wxMoveEvent` carrying the
  requested position regardless, so a move that had not happened still
  closed an open `wxComboCtrl` popup; it now sends the event only when
  the request was actually issued. `GetPosition()` still answers with
  what was asked for, because the true answer is unknowable and any
  other lie would break more callers than it fixed. (#166)

* **`ClientToScreen()` and `GetScreenPosition()` do not return screen
  coordinates.** They can only map as far as the top level window, so
  they are relative to it. The round trip stays consistent, and so does
  comparison with `wxGetMousePosition()`, which answers in the same
  space -- the two defects cancel while everything involved belongs to
  one toplevel. Mixing in a second toplevel, `wxDisplay`, or a
  compositor rectangle is what breaks. Documented as an `@note` on
  `wxWindow::ClientToScreen()`. (#214)

* **`wxGetMousePosition()` answers in the coordinates of whichever
  surface the pointer is over**, not the desktop's. (#134)

* **A floating wxAUI pane cannot be dragged back into a dock.** Docking
  is driven from the pane's motion, and a compositor-driven move sends
  none. The protocol built for exactly this case,
  `xdg_toplevel_drag_v1`, is not implemented in GTK 4.22.4 -- measured
  from the library's own interface strings, and it is a GTK gap rather
  than a Wayland one. (#167)

* **`wxUIActionSimulator` cannot synthesise input.** It needs XTest.
  `wxUIActionSimulatorWaylandImpl` reports the limitation rather than
  sending events to a separate Xwayland connection and calling them
  delivered. (#69)

* **`wxNativeContainerWindow` cannot embed a foreign top level window**,
  as set out in the section above.

An application that needs any of these keeps working under X11, and
`GDK_BACKEND=x11` remains available to it. That is worth saying plainly
to anyone porting: the answer to several of these is not a workaround
inside wx.

## What's still open (not attempted)

Everything else in `gtk4-status.md`'s root-cause table: the
`GtkContainer`/`GtkBin` removal (~103 occurrences, mechanical but wide),
the DnD/clipboard rewrite (`GdkContentProvider`/`GtkDropTarget`/
`GdkClipboard`), the old style-properties API, `gtk_box_pack_start/end`
→ `append`/`prepend`, GFile-based file choosers, and `wxPopupWindow`
(`GTK_WINDOW_POPUP` is gone, needs a `GtkPopover`/`GdkPopup`-anchored
rewrite — self-contained to one file). None of these have been started;
they're scoped in `gtk4-port-plan.md`'s phase breakdown.

## If any of this is useful upstream

The two landed fixes are minimal and don't change GTK3 behavior at all
(verified by compiling `nativewin.cpp` against both GTK3 and GTK4
headers). The CI job and `before_install.sh` change are similarly
low-risk and could be adapted directly. The design document for the
window/child model might be useful as a starting point for anyone else
looking at this, even if the actual implementation here hasn't caught up
to it yet.

## How this work is attributed

Most of this port was written by Claude Opus 5 driving a build, a test
suite and a set of probe programs, with @gunterkoenigsmann directing the
work, reporting the bugs and deciding what gets submitted. That should be
readable in the history rather than inferred from it, so every commit says
so in the same way:

```
Co-authored-by: Claude Opus 5 <noreply@anthropic.com>
```

One form, on every commit, whether or not the change is large. The history
before this convention was settled is inconsistent -- 108 of 285 commits
carried that trailer, 80 also carried a `Claude-Session:` URL, and 177
carried nothing -- which is the whole reason for writing it down (#177).

Two rules go with it:

**The git author is the person submitting the change.** Whoever submits is
warranting the code under the wxWindows licence, and that is a human
decision (see #106, gate 3); a commit whose author field names the model
has nobody standing behind it. `Co-authored-by:` says the model wrote it
without making a claim about who is answerable for it.

**No `Claude-Session:` trailers on anything published.** Those URLs point at
private sessions and resolve for nobody else, so in a permanent public
history they are dead links. They stay useful in local work; they are
stripped when a series is cut.

Disclosure does not live only in the trailers. Any series offered upstream
says in its cover text how it was produced, because a trailer is easy to
miss and this is not something to let a reviewer discover late.
