///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/backend.h
// Author:      Paul Cornett
// Copyright:   (c) 2022 Paul Cornett
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifdef __WXGTK3__
namespace wxGTKImpl
{
    WXDLLIMPEXP_CORE bool IsWayland(void* instance);
    WXDLLIMPEXP_CORE bool IsX11(void* instance);

#ifdef __WXGTK4__
    // Naming a window the X server does not know is not a recoverable error:
    // the server answers BadWindow and GDK's error handler ends the process.
    // That is not hypothetical -- it killed test_gui halfway through the suite
    // and it killed wxMaxima on shutdown -- and the two calls below are what
    // every request naming a surface has to go through.

    // Can the X server still be asked about this surface? False for a surface
    // of another backend and for one GDK has seen destroyed.
    //
    // This is never enough on its own: GDK only learns of the destruction when
    // it processes the DestroyNotify, so between this returning true and the
    // request reaching the server the window may be gone anyway. It only keeps
    // the common case out of the trap below.
    WXDLLIMPEXP_CORE bool CanAskServerAbout(void* surface);

    // Traps X errors on a display for its lifetime, so that a request naming a
    // window that has since gone away returns an error instead of ending the
    // process. Wrap every request that names a surface in one of these.
    class WXDLLIMPEXP_CORE X11ErrorTrap
    {
    public:
        // Starts trapping at once. Does nothing at all when not on X11.
        explicit X11ErrorTrap(void* display);

        // Stops trapping without waiting for the server. Errors for the
        // requests made inside the trap stay ignored even when they only
        // arrive later, which is what a request expecting no reply --
        // XMoveWindow(), XSetWMHints() -- needs, and it costs no round trip.
        ~X11ErrorTrap();

        // Stop trapping and return the X error code, or 0 if there was none.
        //
        // Use this when the answer matters, i.e. for a request that has a
        // reply. It blocks until the server has answered, which is also what
        // makes the error land inside the trap rather than after it. Calling
        // it twice returns 0 the second time.
        int Pop();

    private:
        void* m_display;

        wxDECLARE_NO_COPY_CLASS(X11ErrorTrap);
    };
#endif // __WXGTK4__
}
#endif
