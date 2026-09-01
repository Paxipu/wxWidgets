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
    #include "wx/frame.h"
    #include "wx/image.h"
    #include "wx/panel.h"
#endif // WX_PRECOMP

#include "wx/dcscreen.h"

#ifdef __WXGTK3__
    #include "wx/gtk/private/backend.h"
#endif

#include "waitfor.h"

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

    wxWindow* const parent = wxTheApp->GetTopWindow();
    REQUIRE( parent );

    const wxColour marker(255, 0, 255);

    std::unique_ptr<wxPanel> panel(new wxPanel(parent, wxID_ANY,
                                               wxPoint(0, 0), wxSize(40, 20)));
    panel->SetBackgroundColour(marker);
    panel->Show();
    parent->Update();

    // The window has to have reached the screen before it can be read from it.
    YieldForAWhile();

    if ( !panel->IsShownOnScreen() )
        return;

    const wxSize size = panel->GetSize();
    const wxPoint pos = panel->GetScreenPosition();

    wxBitmap bmp(size);
    {
        wxScreenDC screen;
        wxMemoryDC mem(bmp);
        REQUIRE( mem.Blit(0, 0, size.x, size.y, &screen, pos.x, pos.y) );
    }

    const wxImage img = bmp.ConvertToImage();
    REQUIRE( img.IsOk() );

    // The middle, to stay clear of any border the theme may draw.
    const int x = size.x / 2;
    const int y = size.y / 2;

    INFO("read back (" << (int)img.GetRed(x, y) << ","
                       << (int)img.GetGreen(x, y) << ","
                       << (int)img.GetBlue(x, y) << ")");

    CHECK( img.GetRed(x, y) == marker.Red() );
    CHECK( img.GetGreen(x, y) == marker.Green() );
    CHECK( img.GetBlue(x, y) == marker.Blue() );
}
