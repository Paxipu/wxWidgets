# The port as a series of review-size patches

Upstream asked for this rather than one patch (#175):

> I don't have any experience with this, but could you please ask your LLM to
> create a series of PRs, with each one being of reasonable size? Maybe also ask
> it to rewrite Git history in case there were any later corrections that it
> would make sense to fold in earlier commits.
>
> Personally I'm not ready to merge a 50000 line patch without review but
> neither am I capable of actually reviewing it. Reviewing 10 5000 line PRs
> might be doable, however.

`build/tools/make-upstream-series.py` produces it, and can produce it again
after more work lands. What follows is what it produces today.

## What is not in it

`docs/` and `CLAUDE.md` -- 18,690 lines across 85 files -- are this fork's
working notes: design documents, probe programs, progress reports. They are
what the port was built with rather than part of it, and they are left out.
That alone takes the submission from 49k lines to 31k.

## The split

| step | branch | files | + | − | what it is |
|---|---|---:|---:|---:|---|
| 1 | `01-shared` | 46 | 967 | 85 | the changes outside the GTK backend |
| 2 | `02-private` | 20 | 1716 | 11 | private headers and the GTK+ 3 compatibility shim |
| 3 | `03-core` | 8 | 4216 | 301 | wxWindow, the event loop and the wxPizza container |
| 4 | `04-toplevel` | 9 | 1575 | 105 | top level windows, frames, dialogs and popups |
| 5 | `05-drawing` | 9 | 1415 | 37 | device contexts, the renderer and overlays |
| 6 | `06-menus` | 5 | 1782 | 42 | menus on GMenuModel and GAction |
| 7 | `07-text` | 10 | 2004 | 52 | text entry and text control |
| 8 | `08-items` | 12 | 2206 | 160 | item containers |
| 9 | `09-controls` | 42 | 3346 | 209 | the remaining controls |
| 10 | `10-dialogs` | 10 | 774 | 32 | the standard dialogs |
| 11 | `11-clipboard` | 4 | 1244 | 4 | clipboard and drag and drop |
| 12 | `12-a11y` | 2 | 1011 | 0 | accessibility |
| 13 | `13-taskbar` | 3 | 1227 | 43 | the taskbar icon and the status notifier |
| 14 | `14-webview` | 4 | 580 | 77 | wxWebView on WebKitGTK 6 |
| 15 | `15-rest` | 6 | 246 | 8 | the last of the backend |
| 16 | `16-tests` | 37 | 2572 | 146 | tests and samples |
| 17 | `17-build` | 32 | 3805 | 53 | the build system, the configure switch and CI |

30,686 insertions across 260 files. The largest step is 4,216 lines and the
median is about 1,600, which is the size the request asked for.

## Why this order

**The changes outside the GTK backend come first.** They are the only ones that
can affect Windows, macOS or Qt, they are the smallest group in the series, and
a reviewer should be able to see all of them before anything else. If step 1 is
acceptable, nothing after it can break another port.

**The build system comes last.** It is what lists the new source files and adds
the `--with-gtk=4` switch, so until it lands the existing GTK+ 2 and GTK+ 3
builds compile exactly as before: the new files are simply not built, and the
changes to existing files are all behind `#ifdef __WXGTK4__` or are shared
fixes. Putting it first breaks the build at step one, which is how the first
version of this script was found to be wrong -- `Makefile.in` referred to
`src/gtk/accessgtk.cpp` fourteen steps before that file existed.

Between those two, the order is dependency order: the private headers and the
compatibility shim, then the window and event plumbing everything else calls
into, then the subsystems.

## What each step is, and is not

Each step takes the **final** state of its files from the port branch. So the
series has no intermediate states that were never tested -- every step is the
port's own code, and the last step *is* the port. What it is not is a history:
a reviewer following the series sees each subsystem once, in its finished form,
rather than the path that got there.

That is deliberate, and it is the half of the request this answers. The other
half -- "rewrite Git history in case there were any later corrections that it
would make sense to fold in earlier commits" -- is answered by construction:
there are no later corrections to fold in, because each file appears once.

## What was verified

| | |
|---|---|
| the split loses nothing | `git diff upstream-series/17-build <port> -- . ':!docs' ':!CLAUDE.md'` is **empty** |
| step 1 keeps the existing build green | GTK+ 3, configured and built from a clean directory at that step: **rc=0** |
| step 16 keeps it green | the whole port except the build system, GTK+ 3, clean configure: **rc=0** |
| the series produces the port | step 17 under GTK4: builds, and the GUI suite passes **554 cases, 43,000 assertions** |

Steps 2 to 15 were not each configured and built separately: each is a subset of
the file changes that step 16 contains, and step 16 is green. That is an
argument rather than a measurement, and it is worth saying which of the two it
is.
