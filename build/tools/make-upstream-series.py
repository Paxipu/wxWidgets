#!/usr/bin/env python3
"""Cut the GTK4 port into a series of review-size branches.

Upstream asked for this rather than one patch:

    I don't have any experience with this, but could you please ask your LLM
    to create a series of PRs, with each one being of reasonable size? [...]
    Personally I'm not ready to merge a 50000 line patch without review but
    neither am I capable of actually reviewing it. Reviewing 10 5000 line PRs
    might be doable, however.

                                                                 -- #175

So this splits by subsystem rather than by commit. Each branch takes the
*final* state of one group of files from the port branch and stacks on the
one before it, which means the series has no intermediate states that were
never tested: every step is the port's own code, and the last step is the
port.

Two things decide the order.

  * The changes outside the GTK backend come first. They are the ones that
    could affect Windows, macOS or Qt, they are the smallest group, and a
    maintainer should be able to see all of them before anything else.

  * The build system comes last. It is what lists the new source files and
    turns the GTK4 switch on, so until it lands the existing GTK+ 2 and
    GTK+ 3 builds compile exactly as before -- the new files are simply not
    built. Putting it first would break the build at step one, which is how
    the first version of this script was found to be wrong.

docs/ and CLAUDE.md are left out: they are this fork's working notes.

Usage:
    build/tools/make-upstream-series.py --dry   # what the groups would be
    build/tools/make-upstream-series.py         # create the branches

Check afterwards that nothing was dropped:
    git diff upstream-series/17-build <port branch> -- . ':!docs' ':!CLAUDE.md'
"""

import subprocess, collections, sys, os

MB  = "1e8311d98f80a045f9e22fac7415c7e89356d0e7"
TIP = "gtk4-project/claude/gtk4-wxwidgets-port-plan-pwo52u"

def gtk(p, *names):
    return any(p.startswith("src/gtk/"+n) or p.startswith("include/wx/gtk/"+n) for n in names)

# order matters: first match wins
RULES = [
 ("01-shared",     "GTK4: the changes outside the GTK backend",
   lambda p: not (p.startswith("src/gtk/") or p.startswith("include/wx/gtk/")
                  or p.startswith("tests/") or p.startswith("samples/")
                  or p.startswith("demos/") or p.startswith("build/")
                  or p.startswith(".github/") or p.startswith("misc/")
                  or p in ("configure","configure.ac","Makefile.in","CLAUDE.md"))),
 ("02-private",    "GTK4: private headers and the GTK+ 3 compatibility shim",
   lambda p: p.startswith("include/wx/gtk/private/")),
 ("03-core",       "GTK4: wxWindow, the event loop and the wxPizza container",
   lambda p: p in ("src/gtk/window.cpp","src/gtk/win_gtk.cpp","src/gtk/evtloop.cpp","src/gtk/app.cpp","src/gtk/utilsgtk.cpp","src/gtk/private.cpp","include/wx/gtk/window.h","include/wx/gtk/app.h","include/wx/gtk/evtloop.h")),
 ("04-toplevel",   "GTK4: top level windows, frames, dialogs and popups",
   lambda p: gtk(p,"toplevel","popupwin","minifram","nonownedwnd","frame","dialog","mdi")),
 ("05-drawing",    "GTK4: device contexts, the renderer and overlays",
   lambda p: gtk(p,"dc","renderer","overlay","bitmap","brush","pen","colour","cursor","icon","image","region","graphics")),
 ("06-menus",      "GTK4: menus on GMenuModel and GAction",
   lambda p: gtk(p,"menu","accel","assertdlg")),
 ("07-text",       "GTK4: text entry and text control",
   lambda p: gtk(p,"textctrl","textentry","spinctrl","srchctrl","combobox")),
 ("08-items",      "GTK4: item containers",
   lambda p: gtk(p,"listbox","choice","dataview","radiobox","checklst","listctrl","treectrl","treeentry","bmpcbox")),
 ("09-controls",   "GTK4: the remaining controls",
   lambda p: gtk(p,"anybutton","button","radiobut","slider","toolbar","statusbar","notebook","gauge","scrolbar","scrolwin","spinbutt","statbmp","statbox","stattext","tglbtn","filepicker","clrpicker","fontpicker","calctrl","collpane","infobar","headerctrl","checkbox","control","animate","hyperlink","artgtk","settings","font","sockgtk","timer","tooltip","aboutdlg","display","glcanvas","activityindicator")),
 ("10-dialogs",    "GTK4: the standard dialogs",
   lambda p: gtk(p,"dirdlg","filedlg","msgdlg","print","colordlg","fontdlg","filectrl","progdlg")),
 ("11-clipboard",  "GTK4: clipboard and drag and drop",
   lambda p: gtk(p,"clipbrd","dnd")),
 ("12-a11y",       "GTK4: accessibility",
   lambda p: gtk(p,"access")),
 ("13-taskbar",    "GTK4: the taskbar icon and the status notifier",
   lambda p: gtk(p,"taskbar","statusnotifier","dbusmenu","appindicator")),
 ("14-webview",    "GTK4: wxWebView on WebKitGTK 6",
   lambda p: gtk(p,"webview")),
 ("15-rest",       "GTK4: the last of the backend",
   lambda p: p.startswith("src/gtk/") or p.startswith("include/wx/gtk/")),
 ("16-tests",      "GTK4: tests and samples",
   lambda p: p.startswith("tests/") or p.startswith("samples/") or p.startswith("demos/")),
 ("17-build",      "GTK4: the build system, the configure switch and CI",
   lambda p: p in ("configure","configure.ac","Makefile.in") or p.startswith("build/")
             or p.startswith(".github/") or p.startswith("misc/")),
]

out = subprocess.run(["git","diff","--numstat",MB,TIP],capture_output=True,text=True).stdout
files = collections.defaultdict(list)
for line in out.splitlines():
    a,d,p = line.split("\t")
    if p.startswith("docs/") or p == "CLAUDE.md": continue
    for name,_desc,fn in RULES:
        if fn(p):
            files[name].append(p); break
    else:
        files["15-rest"].append(p)

if "--dry" in sys.argv:
    for name,desc,_ in RULES:
        print("%-13s %3d Dateien  %s" % (name, len(files[name]), desc))
    sys.exit()

def run(*args, **kw):
    r = subprocess.run(args, capture_output=True, text=True, **kw)
    if r.returncode:
        print("FEHLER:", " ".join(args), r.stderr[:400]); sys.exit(1)
    return r.stdout

prev = MB
for name, desc, _ in RULES:
    paths = files[name]
    if not paths:
        print("übersprungen (leer):", name); continue
    branch = "upstream-series/" + name
    run("git","branch","-D",branch) if branch in run("git","branch","--list",branch) else None
    run("git","checkout","-q","-b",branch,prev)
    # take this group's final state from the port branch
    for i in range(0, len(paths), 60):
        run("git","checkout",TIP,"--",*paths[i:i+60])
    msg = ("%s\n\n"
           "One step of the GTK4 port, split for review as upstream asked (#175).\n"
           "The series is cumulative: this applies on top of %s and the whole\n"
           "port is the last step of it.\n\n"
           "Co-authored-by: Claude Opus 5 <noreply@anthropic.com>\n") % (desc, prev if prev==MB else "the previous step")
    run("git","commit","-q","-a","-m",msg)
    head = run("git","rev-parse","--short","HEAD").strip()
    print("%-30s %s  %3d Dateien" % (branch, head, len(paths)))
    prev = branch
print("TIP:", prev)
