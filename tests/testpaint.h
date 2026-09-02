///////////////////////////////////////////////////////////////////////////////
// Name:        tests/testpaint.h
// Purpose:     Unit test helper for checking that a window paints something.
// Copyright:   (c) 2026 wxWidgets development team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_TESTS_TESTPAINT_H_
#define _WX_TESTS_TESTPAINT_H_

#include "wx/app.h"
#include "wx/bitmap.h"
#include "wx/dcmemory.h"
#include "wx/dcscreen.h"
#include "wx/image.h"
#include "wx/stopwatch.h"
#include "wx/utils.h"
#include "wx/window.h"

// Does this window actually put anything on the screen?
//
// This exists because "it compiled and the suite is green" has three times
// failed to notice that a control had stopped drawing. Every one of them was
// found by taking a screenshot and looking at it, which is not something CI
// does:
//
//  - the first conversion of wxRendererGTK to GTK4's widget snapshots left
//    DrawPushButton() drawing zero pixels;
//  - scoping wx's CSS to a class dropped every custom colour and font;
//  - the scratch widget the GTK4 renderer photographs was left sitting over
//    the control it had drawn, which stopped the *other* controls in the same
//    window drawing at all.
//
// Geometry, event and value assertions pass happily on a blank control, and
// all three of those passed the entire suite.
//
// The check is deliberately weak: a window that draws nothing shows its
// parent's flat background, so its capture is a single colour, while anything
// with a frame, a label or a focus ring is not. It cannot say the drawing is
// *right* -- no portable test can -- but it can say there was some.
//
// Not suitable for a window that legitimately paints one flat colour.

// The window's pixels, read back from the screen. Empty if it has no size or
// the screen cannot be read.
inline wxBitmap wxTestCaptureWindow(wxWindow* win)
{
    if ( !win || !win->IsShownOnScreen() )
        return wxBitmap();

    const wxSize size = win->GetSize();
    if ( size.x <= 0 || size.y <= 0 )
        return wxBitmap();

    const wxPoint pos = win->GetScreenPosition();

    wxBitmap bmp(size);
    {
        wxScreenDC screen;
        wxMemoryDC mem(bmp);
        if ( !mem.Blit(0, 0, size.x, size.y, &screen, pos.x, pos.y) )
            return wxBitmap();
    }

    return bmp;
}

// How many distinct colours a capture shows. 0 if there is nothing to look at,
// 1 if it is blank.
inline int wxTestCountBitmapColours(const wxBitmap& bmp)
{
    if ( !bmp.IsOk() )
        return 0;

    const wxImage img = bmp.ConvertToImage();
    if ( !img.IsOk() || img.GetWidth() <= 0 || img.GetHeight() <= 0 )
        return 0;

    const unsigned char r0 = img.GetRed(0, 0);
    const unsigned char g0 = img.GetGreen(0, 0);
    const unsigned char b0 = img.GetBlue(0, 0);

    for ( int y = 0; y < img.GetHeight(); ++y )
    {
        for ( int x = 0; x < img.GetWidth(); ++x )
        {
            if ( img.GetRed(x, y) != r0 ||
                 img.GetGreen(x, y) != g0 ||
                 img.GetBlue(x, y) != b0 )
            {
                return 2;       // more than one, which is all we ask
            }
        }
    }

    return 1;
}

// Reading a window back from the screen is a race with the toolkit.
//
// A window is not on the screen when Show() returns: it gets there when the
// toolkit next paints, which under this environment can be the better part of
// a second later, and nothing available here forces it. wxWindow::Update()
// used to, through gdk_window_process_updates(), but that call has had no
// effect since GTK+ 3.22.
//
// Polling the screen is a poor way to wait for it. Every read is a round trip
// to the display server for the whole screen, so a loop of them leaves the
// frame clock so little room that the poll can outlast what it is waiting for
// -- which then reads exactly like a window that never draws.
//
// The window knows when it was painted, though, so ask it: force a repaint and
// wait for the event. Returns false if none arrived in time.
namespace wxTestPaintDetail
{

struct PaintWatcher
{
    void OnPaint(wxPaintEvent& event) { painted = true; event.Skip(); }

    bool painted = false;
};

} // namespace wxTestPaintDetail

inline bool wxTestWaitForPaint(wxWindow* win, int timeoutMs = 5000)
{
    if ( !win )
        return false;

    wxTestPaintDetail::PaintWatcher watcher;
    win->Bind(wxEVT_PAINT, &wxTestPaintDetail::PaintWatcher::OnPaint, &watcher);

    // Asked for rather than waited for: the paint that showing the window will
    // eventually bring may already have happened, and then nothing else is
    // coming.
    win->Refresh();

    wxStopWatch sw;
    while ( !watcher.painted && sw.Time() < timeoutMs )
    {
        wxYield();
        wxMilliSleep(10);
    }

    win->Unbind(wxEVT_PAINT, &wxTestPaintDetail::PaintWatcher::OnPaint, &watcher);

    return watcher.painted;
}

// How many distinct colours the window shows on the screen, once it has been
// painted. 0 if it could not be captured, 1 if it is blank.
inline int wxTestCountWindowColours(wxWindow* win, int timeoutMs = 5000)
{
    wxTestWaitForPaint(win, timeoutMs);

    // The paint event says the toolkit has drawn the window, not that the
    // display server has the pixels yet, so still read more than once -- but
    // only a few times, and with room in between.
    wxStopWatch sw;
    for ( ;; )
    {
        for ( int n = 0; n < 10; ++n )
        {
            wxYield();
            wxMilliSleep(25);
        }

        const int colours = wxTestCountBitmapColours(wxTestCaptureWindow(win));
        if ( colours > 1 || sw.Time() >= timeoutMs )
            return colours;
    }
}

#define CHECK_WINDOW_PAINTS(win) \
    CHECK( wxTestCountWindowColours(win) > 1 )

#endif // _WX_TESTS_TESTPAINT_H_
