///////////////////////////////////////////////////////////////////////////////
// Name:        tests/graphics/screendc.cpp
// Purpose:     wxScreenDC tests
// Copyright:   (c) 2026 wxWidgets development team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "testprec.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/bitmap.h"
    #include "wx/dcmemory.h"
    #include "wx/image.h"
#endif // WX_PRECOMP

#include "wx/dcscreen.h"

#ifdef __WXGTK3__
    #include "wx/gtk/private/backend.h"
#endif

#include "testpaint.h"

namespace
{

// The colour of the middle of a window, read back from the screen. The middle,
// to stay clear of any border the theme may draw. Invalid if it could not be
// read.
wxColour ReadWindowCentreFromScreen(wxWindow* win)
{
    const wxSize size = win->GetSize();
    const wxPoint pos = win->GetScreenPosition();

    wxBitmap bmp(size);
    {
        wxScreenDC screen;
        wxMemoryDC mem(bmp);
        if ( !mem.Blit(0, 0, size.x, size.y, &screen, pos.x, pos.y) )
            return wxColour();
    }

    const wxImage img = bmp.ConvertToImage();
    if ( !img.IsOk() )
        return wxColour();

    const int x = size.x / 2;
    const int y = size.y / 2;

    return wxColour(img.GetRed(x, y), img.GetGreen(x, y), img.GetBlue(x, y));
}

} // anonymous namespace

// Can wxScreenDC read the screen back?
//
// This is the one thing most of its users want it for, and it is worth an
// explicit test because the way it fails is silent: a wxScreenDC that cannot
// reach the screen blits black, and an application taking a screenshot gets a
// black image with no error at all. That is what the GTK4 build did before it
// grew an X11 fallback -- GTK4 has no root window, so there was nothing for
// cairo to be pointed at.
//
// A window is given an unmistakable colour, shown, and read back from the
// screen at its own position.
TEST_CASE("wxScreenDC::ReadBack", "[screendc]")
{
    // Nothing to read on a display that does not let a client see the screen.
    // Wayland is the case that matters: refusing this is part of its design
    // rather than a gap in the port.
    //
    // Asked of the display rather than of the environment. WAYLAND_DISPLAY is
    // set under WSL even when the application is talking to an X server, and
    // the first version of this check believed it and skipped the whole test
    // -- which then passed, with no assertions at all, which is worse than
    // failing.
#ifdef __WXGTK3__
    if ( wxGTKImpl::IsWayland(nullptr) )
        return;
#endif

    // The application's own window is what gets read: it is on the screen for
    // the whole run, which a window put up for this test is not necessarily --
    // with no window manager there is nothing to keep one in front of whatever
    // the tests before it left behind, and a test that reads somebody else's
    // window measures the window manager rather than wxScreenDC.
    wxWindow* const win = wxTheApp->GetTopWindow();
    REQUIRE( win );
    REQUIRE( win->IsShownOnScreen() );

    // It has to have been painted before it can be read: that happens when the
    // toolkit next paints, not when the window is shown. See tests/testpaint.h.
    wxTestWaitForPaint(win);

    const wxColour read = ReadWindowCentreFromScreen(win);
    REQUIRE( read.IsOk() );

    INFO("read back " << read.GetAsString(wxC2S_HTML_SYNTAX));

    // Black is the answer to look for, and it means one of exactly two things,
    // both of them faults: the screen could not be reached, which is what the
    // GTK4 build did before it grew the X11 path -- every pixel came back
    // (0,0,0), with no error -- or the read went somewhere other than the
    // window, since under X11 the root window behind it is black and the
    // window is not.
    CHECK( read != wxColour(0, 0, 0) );
}
