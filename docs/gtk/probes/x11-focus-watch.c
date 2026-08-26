// Where is the X input focus, where is the pointer, and does anyone hold a
// grab? Sampled from outside a running application, which is the only way to
// answer those questions about a process one is not debugging.
//
// It exists because "the test gets no input" has three quite different causes
// which look identical from inside the test -- the pointer being over nothing,
// the input focus being nowhere, and another client holding a grab -- and
// wxWidgets issue #82 turned out to be the second of them. See
// docs/gtk/x11-input-debugging.md.
//
//   gcc -o focus-watch x11-focus-watch.c -lX11
//   DISPLAY=:$disp ./focus-watch            # one sample
//   while ./focus-watch; do :; done          # a trace, alongside the run
//
// Nothing here is GTK-specific: it asks the X server, not the toolkit, which
// is the point. On a display with no window manager the focus stays at
// PointerRoot unless a client sets it, and then every key goes to whatever
// window the pointer happens to be over -- "None" below.

#include <X11/Xlib.h>

#include <stdio.h>

int main(void)
{
    Display* const dpy = XOpenDisplay(NULL);
    if ( !dpy )
    {
        fprintf(stderr, "cannot open display\n");
        return 1;
    }

    const Window root = DefaultRootWindow(dpy);

    Window focus;
    int revert;
    XGetInputFocus(dpy, &focus, &revert);

    Window rootReturn, child;
    int rootX, rootY, winX, winY;
    unsigned mask;
    XQueryPointer(dpy, root, &rootReturn, &child,
                  &rootX, &rootY, &winX, &winY, &mask);

    // Taking a grab and dropping it again is how one asks whether somebody
    // else already holds one: there is no query for it.
    const int p = XGrabPointer(dpy, root, True, 0, GrabModeAsync, GrabModeAsync,
                               None, None, CurrentTime);
    if ( p == GrabSuccess )
        XUngrabPointer(dpy, CurrentTime);

    const int k = XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync,
                                CurrentTime);
    if ( k == GrabSuccess )
        XUngrabKeyboard(dpy, CurrentTime);

    printf("focus=%s(0x%lx) revert=%s  pointer=%d,%d over=%s  "
           "pointer_grab=%s keyboard_grab=%s\n",
           focus == None ? "None"
                         : focus == PointerRoot ? "PointerRoot" : "window",
           (unsigned long)focus,
           revert == RevertToNone ? "None"
               : revert == RevertToPointerRoot ? "PointerRoot"
               : revert == RevertToParent ? "Parent" : "?",
           rootX, rootY,
           child == None ? "None (the root window)" : "a window",
           p == GrabSuccess ? "free" : "held by someone",
           k == GrabSuccess ? "free" : "held by someone");

    XCloseDisplay(dpy);

    return 0;
}
