///////////////////////////////////////////////////////////////////////////////
// Name:        tests/graphics/renderer.cpp
// Purpose:     wxRendererNative tests
// Copyright:   (c) 2026 wxWidgets development team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "testprec.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/bitmap.h"
    #include "wx/brush.h"
    #include "wx/dcmemory.h"
    #include "wx/frame.h"
    #include "wx/image.h"
#endif // WX_PRECOMP

#include "wx/renderer.h"
#include "wx/dcclient.h"
#include "wx/dcbuffer.h"
#include "wx/panel.h"
#include "wx/uiaction.h"

#include "testableframe.h"

// ----------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------

namespace
{

// An unlikely colour, so that "differs from the background" means "was drawn"
// rather than "happens to match the theme".
const wxColour BackgroundColour(255, 0, 255);

// How many pixels of the bitmap are not the background colour.
int CountDrawnPixels(const wxBitmap& bmp)
{
    const wxImage img = bmp.ConvertToImage();

    int count = 0;
    for ( int y = 0; y < img.GetHeight(); ++y )
    {
        for ( int x = 0; x < img.GetWidth(); ++x )
        {
            if ( img.GetRed(x, y)   != BackgroundColour.Red()   ||
                 img.GetGreen(x, y) != BackgroundColour.Green() ||
                 img.GetBlue(x, y)  != BackgroundColour.Blue() )
            {
                ++count;
            }
        }
    }

    return count;
}

} // anonymous namespace

// ----------------------------------------------------------------------------
// tests
// ----------------------------------------------------------------------------

// Every renderer function must put something on the DC.
//
// This is a deliberately weak assertion -- it does not check *what* was drawn,
// which no portable test could -- but it is the one that catches a whole class
// of failure that is otherwise invisible: a drawing call that is replaced by a
// newer API which silently does nothing. That is not hypothetical. Converting
// wxRendererGTK to GTK4's widget-snapshot drawing left DrawPushButton() drawing
// zero pixels, and it compiled, passed the entire suite and passed CI, because
// nothing anywhere looked at the output of wxRendererNative.
TEST_CASE("wxRendererNative::DrawsSomething", "[renderer]")
{
    wxWindow* const win = wxTheApp->GetTopWindow();
    REQUIRE( win );

    wxRendererNative& renderer = wxRendererNative::Get();

    const wxSize size(80, 30);
    const wxRect rect(5, 5, size.x - 10, size.y - 10);

    // Drawn into a fresh bitmap each time, so one function cannot pass on
    // another's output.
    struct DrawFunc
    {
        const char* name;
        void (*draw)(wxRendererNative&, wxWindow*, wxDC&, const wxRect&);
    };

    static const DrawFunc functions[] =
    {
        { "DrawPushButton",
          [](wxRendererNative& r, wxWindow* w, wxDC& dc, const wxRect& rc)
            { r.DrawPushButton(w, dc, rc, 0); } },
        { "DrawComboBox",
          [](wxRendererNative& r, wxWindow* w, wxDC& dc, const wxRect& rc)
            { r.DrawComboBox(w, dc, rc, 0); } },
        { "DrawCheckBox",
          [](wxRendererNative& r, wxWindow* w, wxDC& dc, const wxRect& rc)
            { r.DrawCheckBox(w, dc, rc, 0); } },
        { "DrawTextCtrl",
          [](wxRendererNative& r, wxWindow* w, wxDC& dc, const wxRect& rc)
            { r.DrawTextCtrl(w, dc, rc, 0); } },
        { "DrawItemSelectionRect",
          [](wxRendererNative& r, wxWindow* w, wxDC& dc, const wxRect& rc)
            { r.DrawItemSelectionRect(w, dc, rc, wxCONTROL_SELECTED); } },
        { "DrawHeaderButton",
          [](wxRendererNative& r, wxWindow* w, wxDC& dc, const wxRect& rc)
            { r.DrawHeaderButton(w, dc, rc, 0); } },
    };

    for ( const DrawFunc& f : functions )
    {
        INFO("drawing with " << f.name);

        wxBitmap bmp(size, 32);
        {
            wxMemoryDC dc(bmp);
            dc.SetBackground(wxBrush(BackgroundColour));
            dc.Clear();

            f.draw(renderer, win, dc, rect);
        }

        CHECK( CountDrawnPixels(bmp) > 0 );
    }
}

// Drawing must not leave anything behind in the window it drew into.
//
// Under GTK4 wxRendererGTK draws by parenting a themed widget into the window,
// allocating it to the rectangle being drawn and photographing it. Both halves
// of that leave a trap, and neither is visible in what was drawn:
//
//  - the widget is a real widget inside a real window, so GTK will happily
//    pick it for input, and after the allocation it sits exactly over the
//    rectangle that was just drawn, swallowing that area's clicks;
//  - it also keeps that allocation, and a container which draws all of its
//    children draws this one too, over the window's own contents, until the
//    next layout pass moves it back.
//
// The first of those is what this checks, because it is the one a test can see
// at all. It was found only when every bordered window started drawing its
// border through the renderer and seven unrelated test cases stopped receiving
// their clicks; before that, the fault was present but nothing exercised it.
#if wxUSE_UIACTIONSIMULATOR

namespace
{

// A window that draws itself the way a custom control does, from its paint
// handler, so that the click below arrives after a real paint rather than
// after a wxClientDC. That distinction is the whole point: a layout pass
// undoes the misplacement, and yielding for a wxClientDC draw gives it one.
class DrawingPanel : public wxPanel
{
public:
    DrawingPanel(wxWindow* parent)
        : wxPanel(parent, wxID_ANY, wxPoint(0, 0), wxSize(120, 40),
                  wxBORDER_THEME)
    {
        Bind(wxEVT_PAINT, &DrawingPanel::OnPaint, this);
    }

private:
    void OnPaint(wxPaintEvent&)
    {
        wxPaintDC dc(this);
        wxRendererNative::Get().DrawPushButton(this, dc,
                                               wxRect(GetClientSize()), 0);
    }
};

} // anonymous namespace

TEST_CASE("wxRendererNative::LeavesNoWidgetBehind", "[renderer]")
{
    wxWindow* const parent = wxTheApp->GetTopWindow();
    REQUIRE( parent );

    std::unique_ptr<DrawingPanel> panel(new DrawingPanel(parent));
    panel->Show();
    wxYield();

    EventCounter clicked(panel.get(), wxEVT_LEFT_DOWN);

    const wxSize size = panel->GetClientSize();
    wxUIActionSimulator sim;
    sim.MouseMove(panel->ClientToScreen(wxPoint(size.x / 2, size.y / 2)));
    wxYield();

    // Paint immediately before the click, with nothing in between that would
    // ask for a layout: a layout pass puts the scratch widget back where it
    // belongs and hides the fault.
    panel->Refresh();
    panel->Update();

    sim.MouseClick();
    wxYield();

    CHECK( clicked.GetCount() == 1 );
}

#endif // wxUSE_UIACTIONSIMULATOR
