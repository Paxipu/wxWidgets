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
#include <cstdlib>

#include "asserthelper.h"
#include "waitfor.h"

#ifdef __WXGTK4__
    #include "wx/utils.h"
    #include "wx/gtk/private/wrapgtk.h"
    #include "wx/gtk/private/win_gtk.h"

    #ifdef GDK_WINDOWING_X11
        #include <gdk/x11/gdkx.h>
        #include <X11/Xlib.h>
    #endif // GDK_WINDOWING_X11
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

#if defined(__WXGTK4__) && defined(GDK_WINDOWING_X11)

int ExitOnXError(Display*, XErrorEvent*)
{
    // This runs in the subprocess below. An untrapped request reaching this
    // handler is the failure the parent process is checking for.
    std::_Exit(99);
}

#endif // __WXGTK4__ && GDK_WINDOWING_X11

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

#ifdef GDK_WINDOWING_X11

TEST_CASE("wxTopLevelWindow::DestroyedSurfaceMovePoll",
          "[window][position][x11]")
{
    GdkDisplay* const display = gdk_display_get_default();
    if ( !GDK_IS_X11_DISPLAY(display) )
    {
        WARN("Skipping destroyed-surface move poll test outside X11");
        return;
    }

    wxString isChild;
    if ( !wxGetEnv("WX_TEST_DESTROYED_SURFACE_MOVE_POLL", &isChild) )
    {
        wxExecuteEnv env;
        wxGetEnvMap(&env.env);
        env.env["WX_TEST_DESTROYED_SURFACE_MOVE_POLL"] = "1";

        // The wxString has to outlive the buffer: utf8_str() can hand back a
        // view into the string rather than a copy of it, and a temporary here
        // leaves argv[0] pointing at freed memory -- which execvp() then
        // reports as a missing file.
        const wxString exePath = wxTheApp->argv[0];
        const wxScopedCharBuffer executable = exePath.utf8_str();
        const char* const argv[] =
        {
            executable.data(),
            "wxTopLevelWindow::DestroyedSurfaceMovePoll",
            nullptr
        };

        // The child deliberately destroys an X window behind GDK's back.
        // Isolating it keeps that artificial state out of the main suite.
        CHECK( wxExecute(argv, wxEXEC_SYNC | wxEXEC_NOEVENTS,
                         nullptr, &env) == 0 );
        return;
    }

    std::unique_ptr<wxFrame> w(new wxFrame(nullptr, wxID_ANY,
                                           "destroyed surface poll"));
    WaitForPaint waitForPaint(w.get());
    w->Show();
    waitForPaint.YieldUntilPainted();

    GtkWidget* const widget = static_cast<GtkWidget*>(w->GetHandle());
    GdkSurface* const surface =
        gtk_native_get_surface(GTK_NATIVE(widget));
    REQUIRE( surface );
    REQUIRE( GDK_IS_X11_SURFACE(surface) );

    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    Display* const xdisplay = GDK_SURFACE_XDISPLAY(surface);
    XDestroyWindow(xdisplay, GDK_SURFACE_XID(surface));
    XSync(xdisplay, False);
    wxGCC_WARNING_RESTORE(deprecated-declarations)

    // GDK has not processed DestroyNotify yet, so its liveness flag cannot
    // protect the query. Any X error escaping the poll exits with 99.
    REQUIRE_FALSE( gdk_surface_is_destroyed(surface) );
    XSetErrorHandler(ExitOnXError);

    w->GTKPollCompositorMove();

    // Don't let GTK clean up the intentionally invalid surface in this child.
    std::_Exit(EXIT_SUCCESS);
}

#endif // GDK_WINDOWING_X11

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
