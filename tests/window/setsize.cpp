///////////////////////////////////////////////////////////////////////////////
// Name:        tests/window/setsize.cpp
// Purpose:     Tests for SetSize() and related wxWindow methods
// Author:      Vadim Zeitlin
// Created:     2008-05-25
// Copyright:   (c) 2008 Vadim Zeitlin <vadim@wxwidgets.org>
///////////////////////////////////////////////////////////////////////////////

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "testprec.h"


#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/frame.h"
    #include "wx/window.h"
#endif // WX_PRECOMP

#include <memory>

#include "asserthelper.h"
#include "waitfor.h"

#ifdef __WXGTK4__
    #include "wx/gtk/private/wrapgtk.h"
    #include "wx/gtk/private/win_gtk.h"
#endif // __WXGTK4__

// ----------------------------------------------------------------------------
// tests helpers
// ----------------------------------------------------------------------------

namespace
{

// Helper class overriding DoGetBestSize() for testing purposes.
class MyWindow : public wxWindow
{
public:
    MyWindow(wxWindow* parent)
        : wxWindow(parent, wxID_ANY)
    {
    }

protected:
    virtual wxSize DoGetBestSize() const override { return wxSize(50, 250); }
};

} // anonymous namespace

// ----------------------------------------------------------------------------
// tests themselves
// ----------------------------------------------------------------------------

TEST_CASE("wxWindow::SetSize", "[window][size]")
{
    std::unique_ptr<wxWindow> w(new MyWindow(wxTheApp->GetTopWindow()));

    SECTION("Simple")
    {
        const wxSize size(127, 35);
        w->SetSize(size);
        CHECK( size == w->GetSize() );
    }

    SECTION("With min size")
    {
        w->SetMinSize(wxSize(100, 100));

        const wxSize size(200, 50);
        w->SetSize(size);
        CHECK( size == w->GetSize() );
    }
}

TEST_CASE("wxWindow::GetBestSize", "[window][size][best-size]")
{
    std::unique_ptr<wxWindow> w(new MyWindow(wxTheApp->GetTopWindow()));

    CHECK( wxSize(50, 250) == w->GetBestSize() );

    w->SetMinSize(wxSize(100, 100));
    CHECK( wxSize(100, 250) == w->GetBestSize() );

    w->SetMaxSize(wxSize(200, 200));
    CHECK( wxSize(100, 200) == w->GetBestSize() );
}

TEST_CASE("wxWindow::MovePreservesSize", "[window][size][move]")
{
    std::unique_ptr<wxWindow>
        w(new wxFrame(wxTheApp->GetTopWindow(), wxID_ANY, "Test child frame"));

    // Unfortunately showing the window is asynchronous, at least when using
    // X11, so we have to wait for some time before retrieving its true
    // geometry. And it's not clear how long should we wait, so we do it until
    // we get the first paint event -- by then the window really should have
    // its final size.
    WaitForPaint waitForPaint(w.get());

    w->Show();

    waitForPaint.YieldUntilPainted();

    const wxRect rectOrig = w->GetRect();

    // Check that moving the window doesn't change its size.
    w->Move(rectOrig.GetPosition() + wxPoint(100, 100));
    CHECK( w->GetSize() == rectOrig.GetSize() );
}

#ifdef __WXGTK4__

TEST_CASE("wxWindow::SetShape", "[window][shape]")
{
    std::unique_ptr<wxFrame> w(new wxFrame(nullptr, wxID_ANY, wxEmptyString,
                                           wxDefaultPosition, wxSize(80, 80),
                                           wxFRAME_SHAPED | wxBORDER_NONE));

    WaitForPaint waitForPaint(w.get());
    w->Show();
    waitForPaint.YieldUntilPainted();

    CHECK( w->SetShape(wxRegion(0, 0, 40, 80)) );
    CHECK( w->SetShape(wxRegion()) );
}

// GTK4 removed GtkWidget's "size-allocate" signal, so wxPizza registers a
// replacement of its own and both src/gtk/toplevel.cpp and src/gtk/window.cpp
// connect to it using the name held in wxPIZZA_SIGNAL_SIZE_ALLOCATED. Nothing
// else ties that constant to the name pizza_class_init() actually registers:
// should the two ever diverge, g_signal_connect() fails at run time with a
// GLib warning and no wxEVT_SIZE is generated at all, for any window.
TEST_CASE("wxPizza::SizeAllocatedSignal", "[window][size]")
{
    REQUIRE( wxPIZZA_SIGNAL_SIZE_ALLOCATED != nullptr );

    const GType type = wxPizza::type();
    REQUIRE( type != 0 );

    // The signal is created in class_init, which GType only runs on demand.
    gpointer klass = g_type_class_ref(type);
    REQUIRE( klass != nullptr );

    INFO("wxPIZZA_SIGNAL_SIZE_ALLOCATED = " << wxPIZZA_SIGNAL_SIZE_ALLOCATED);
    const guint id = g_signal_lookup(wxPIZZA_SIGNAL_SIZE_ALLOCATED, type);

    g_type_class_unref(klass);

    CHECK( id != 0 );
}

#endif // __WXGTK4__
