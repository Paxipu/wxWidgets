///////////////////////////////////////////////////////////////////////////////
// Name:        tests/controls/windowtest.cpp
// Purpose:     wxWindow unit test
// Author:      Steven Lamerton
// Created:     2010-07-10
// Copyright:   (c) 2010 Steven Lamerton
//              (c) 2026 wxWidgets development team
///////////////////////////////////////////////////////////////////////////////

#include "testprec.h"


#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/window.h"
    #include "wx/button.h"
    #include "wx/sizer.h"
#endif // WX_PRECOMP

#include "asserthelper.h"
#include "testableframe.h"
#include "testwindow.h"
#include "waitfor.h"

#include "wx/uiaction.h"
#include "wx/caret.h"
#include "wx/cshelp.h"
#include "wx/dcclient.h"
#include "wx/overlay.h"
#include "wx/timer.h"
#include "wx/tooltip.h"
#include "wx/wupdlock.h"

#include "wx/private/make_unique.h"

#ifdef __WXGTK4__
    #include "wx/button.h"
    #include "wx/popupwin.h"
    #include "wx/scrolwin.h"
    #include "wx/gtk/private/wrapgtk.h"
    #include "wx/gtk/private/win_gtk.h"
#endif // __WXGTK4__

class WindowTestCase
{
public:
    WindowTestCase()
        : m_window(new wxWindow(wxTheApp->GetTopWindow(), wxID_ANY))
    {
    #ifdef __WXGTK3__
        // Without this, when running this test suite solo it succeeds,
        // but not when running it together with the other tests !!
        // Not needed when run under Xvfb display.
        YieldForAWhile();
    #endif
    }

    ~WindowTestCase()
    {
        wxTheApp->GetTopWindow()->DestroyChildren();
    }

protected:
    wxWindow* const m_window;

    wxDECLARE_NO_COPY_CLASS(WindowTestCase);
};

#if wxUSE_HELP
class ContextHelpCaptureLostTester : public wxWindow
{
public:
    ContextHelpCaptureLostTester(wxWindow* parent)
        : wxWindow(parent, wxID_ANY)
    {
    }

    void SimulateCaptureLost()
    {
        DoReleaseMouse();
        NotifyCaptureLost();
    }
};

class ContextHelpCaptureLostState : public wxEvtHandler
{
public:
    explicit ContextHelpCaptureLostState(ContextHelpCaptureLostTester* win)
        : m_win(win),
          m_captureLostTimer(this),
          m_fallbackTimer(this)
    {
        Bind(wxEVT_TIMER, &ContextHelpCaptureLostState::OnTimer, this);
    }

    void Start()
    {
        m_captureLostTimer.StartOnce(1);
    }

    void Done()
    {
        m_done = true;
        m_captureLostTimer.Stop();
        m_fallbackTimer.Stop();
    }

    bool WasCaptureLostSent() const { return m_captureLostSent; }
    bool WasFallbackUsed() const { return m_fallbackUsed; }

private:
    void OnTimer(wxTimerEvent& event)
    {
        if ( m_done )
            return;

        if ( &event.GetTimer() == &m_captureLostTimer )
        {
            m_captureLostSent = true;
            m_win->SimulateCaptureLost();
            m_fallbackTimer.StartOnce(100);
            return;
        }

        m_fallbackUsed = true;

        wxKeyEvent eventKey(wxEVT_KEY_DOWN);
        eventKey.SetEventObject(m_win);
        m_win->GetEventHandler()->ProcessEvent(eventKey);
    }

    ContextHelpCaptureLostTester* const m_win;
    wxTimer m_captureLostTimer;
    wxTimer m_fallbackTimer;
    bool m_done = false;
    bool m_captureLostSent = false;
    bool m_fallbackUsed = false;
};
#endif // wxUSE_HELP

static void DoTestShowHideEvent(wxWindow* window)
{
    EventCounter show(window, wxEVT_SHOW);

    CHECK(window->IsShown());

    window->Show(false);

    CHECK(!window->IsShown());

    window->Show();

    CHECK(window->IsShown());

    CHECK( show.GetCount() == 2 );
}

TEST_CASE_METHOD(WindowTestCase, "Window::ShowHideEvent", "[window]")
{
    SECTION("Normal window")
    {
        DoTestShowHideEvent(m_window);
    }

    SECTION("Frozen window")
    {
        wxWindowUpdateLocker freeze(m_window->GetParent() );
        REQUIRE( m_window->IsFrozen() );

        DoTestShowHideEvent(m_window);
    }
}

TEST_CASE_METHOD(WindowTestCase, "Window::KeyEvent", "[window]")
{
#if wxUSE_UIACTIONSIMULATOR
    if ( !EnableUITests() )
        return;

    EventCounter keydown(m_window, wxEVT_KEY_DOWN);
    EventCounter keyup(m_window, wxEVT_KEY_UP);
    EventCounter keychar(m_window, wxEVT_CHAR);

    wxUIActionSimulator sim;

    m_window->SetFocus();
    wxYield();

    sim.Text("text");
    sim.Char(WXK_SHIFT);
    wxYield();

    CHECK( keydown.GetCount() == 5 );
    CHECK( keyup.GetCount() == 5 );
    CHECK( keychar.GetCount() == 4 );
#endif
}

TEST_CASE_METHOD(WindowTestCase, "Window::FocusEvent", "[window]")
{
#ifndef __WXOSX__
    if ( IsAutomaticTest() )
    {
        // Skip this test when running under buildbot, it fails there for
        // unknown reason and this failure can't be reproduced locally.
        return;
    }

    EventCounter setfocus(m_window, wxEVT_SET_FOCUS);
    EventCounter killfocus(m_window, wxEVT_KILL_FOCUS);

    m_window->SetFocus();

    CHECK(setfocus.WaitEvent(500));
    CHECK_FOCUS_IS( m_window );

    wxButton* button = new wxButton(wxTheApp->GetTopWindow(), wxID_ANY);

    button->SetFocus();
    wxYield();

    CHECK( killfocus.GetCount() == 1 );
    CHECK(!m_window->HasFocus());
#endif
}

TEST_CASE_METHOD(WindowTestCase, "Window::Mouse", "[window]")
{
    wxCursor cursor(wxCURSOR_HAND);
    m_window->SetCursor(cursor);

    CHECK(m_window->GetCursor().IsOk());

#if wxUSE_CARET
    CHECK(!m_window->GetCaret());

    wxCaret* caret = nullptr;

    // Try creating the caret in two different, but normally equivalent, ways.
    SECTION("Caret 1-step")
    {
        caret = new wxCaret(m_window, 16, 16);
    }

    SECTION("Caret 2-step")
    {
        caret = new wxCaret();
        caret->Create(m_window, 16, 16);
    }

    m_window->SetCaret(caret);

    CHECK(m_window->GetCaret()->IsOk());
#endif

    m_window->CaptureMouse();

    CHECK(m_window->HasCapture());

    m_window->ReleaseMouse();

    CHECK(!m_window->HasCapture());
}

#ifdef __WXGTK4__
TEST_CASE_METHOD(WindowTestCase, "Window::DestroyOverlayRemovesNativeChild",
                 "[window][overlay]")
{
    m_window->SetSize(200, 150);
    m_window->Show();
    wxYield();

    wxPizza* const pizza = WX_PIZZA(m_window->GetConnectWidget());
    const unsigned int initialChildCount = g_list_length(pizza->m_children);

    unsigned int state = 0x28;
    for (int i = 0; i < 32; ++i)
    {
        state = state * 1664525u + 1013904223u;
        m_window->SetSize(100 + state % 200, 100 + (state >> 16) % 150);

        {
            wxOverlay overlay;
            wxClientDC dc(m_window);
            {
                wxDCOverlay overlayDC(overlay, &dc);
            }

            REQUIRE( g_list_length(pizza->m_children) == initialChildCount + 1 );
        }

        CHECK( g_list_length(pizza->m_children) == initialChildCount );
    }
}

TEST_CASE_METHOD(WindowTestCase, "Window::MoveVisibleCaret",
                 "[window][caret]")
{
    class BlinkTimeRestorer
    {
    public:
        BlinkTimeRestorer()
            : m_blinkTime(wxCaret::GetBlinkTime())
        {
            wxCaret::SetBlinkTime(0);
        }

        ~BlinkTimeRestorer()
        {
            wxCaret::SetBlinkTime(m_blinkTime);
        }

    private:
        const int m_blinkTime;
    } restoreBlinkTime;

    constexpr int windowWidth = 200;
    constexpr int windowHeight = 150;
    constexpr int caretWidth = 3;
    constexpr int caretHeight = 12;

    m_window->SetSize(windowWidth, windowHeight);
    m_window->Show();
    wxYield();

    wxCaret* const caret = new wxCaret(m_window, caretWidth, caretHeight);
    m_window->SetCaret(caret);

    const wxPoint initialPosition(7, 9);
    caret->Move(initialPosition);
    caret->Show();
    wxYield();

    GtkWidget* overlay = nullptr;
    GtkWidget* const connectWidget = m_window->GetConnectWidget();
    for (GtkWidget* child = gtk_widget_get_first_child(connectWidget);
         child;
         child = gtk_widget_get_next_sibling(child))
    {
        if (GTK_IS_DRAWING_AREA(child) && !gtk_widget_get_can_target(child))
        {
            overlay = child;
            break;
        }
    }
    REQUIRE( overlay );

    const auto getOverlayPosition = [overlay]
    {
        graphene_rect_t bounds;
        REQUIRE( gtk_widget_compute_bounds(overlay,
                                            gtk_widget_get_parent(overlay),
                                            &bounds) );
        return wxPoint(wxRound(bounds.origin.x), wxRound(bounds.origin.y));
    };

    CHECK( getOverlayPosition() == initialPosition );

    const wxPoint positions[] =
    {
        wxPoint(31, 17),
        wxPoint(0, 0),
        wxPoint(177, 133),
        wxPoint(83, 61)
    };
    for (const wxPoint& position : positions)
    {
        caret->Move(position);
        wxYield();
        CHECK( getOverlayPosition() == position );
    }

    // Exercise the same position invariant over a wider deterministic sample
    // without allowing the caret to extend beyond the window.
    unsigned state = 0x71;
    for (int i = 0; i < 32; ++i)
    {
        state = state * 1664525u + 1013904223u;
        const wxPoint position(
            state % (windowWidth - caretWidth + 1),
            (state >> 16) % (windowHeight - caretHeight + 1));
        caret->Move(position);
        wxYield();
        CHECK( getOverlayPosition() == position );
    }

    caret->OnTimer();
    const wxPoint blinkedOutMovePosition(47, 29);
    caret->Move(blinkedOutMovePosition);
    wxYield();
    caret->OnTimer();
    wxYield();
    CHECK( getOverlayPosition() == blinkedOutMovePosition );

    caret->Hide();
    const wxPoint hiddenMovePosition(19, 23);
    caret->Move(hiddenMovePosition);
    wxYield();
    caret->Show();
    wxYield();
    CHECK( getOverlayPosition() == hiddenMovePosition );
}
#endif // __WXGTK4__

#if wxUSE_HELP
TEST_CASE_METHOD(WindowTestCase, "Window::ContextHelpCaptureLost",
                 "[window][help]")
{
#ifdef __WXOSX__
    if ( IsAutomaticTest() )
    {
        // For some not well-understood reason this test results in failures in
        // another test run later in the CI: somehow executing it makes the
        // child outside of the refreshed rectangle still be repainted there.
        WARN("Skipping the test result in Window::Refresh test failures later.");
        return;
    }
#endif // __WXOSX__

    auto const winPtr =
        std::make_unique<ContextHelpCaptureLostTester>(wxTheApp->GetTopWindow());
    auto* const win = winPtr.get();

    ContextHelpCaptureLostState state(win);
    state.Start();

    wxContextHelp contextHelp(win, false);

    CHECK(contextHelp.BeginContextHelp(win));

    state.Done();
    CHECK(state.WasCaptureLostSent());
    CHECK(!state.WasFallbackUsed());
    CHECK(!win->HasCapture());
}
#endif // wxUSE_HELP

TEST_CASE_METHOD(WindowTestCase, "Window::Properties", "[window]")
{
    m_window->SetLabel("label");

    CHECK( m_window->GetLabel() == "label" );

    m_window->SetName("name");

    CHECK( m_window->GetName() == "name" );

    //As we used wxID_ANY we should have a negative id
    CHECK(m_window->GetId() < 0);

    m_window->SetId(wxID_HIGHEST + 10);

    CHECK( m_window->GetId() == wxID_HIGHEST + 10 );
}

#ifdef __WXGTK4__
TEST_CASE_METHOD(WindowTestCase, "Window::TransparentBackgroundSupport",
                 "[window][transparent]")
{
    wxString reason;
    CHECK( m_window->IsTransparentBackgroundSupported(&reason) );
}

TEST_CASE_METHOD(WindowTestCase, "Window::TransientPopupClientSize",
                 "[window][popup][scroll]")
{
    wxWindow* const parent = wxTheApp->GetTopWindow();
    wxPopupTransientWindow popup(parent);
    new wxScrolledWindow(&popup, wxID_ANY, wxDefaultPosition,
                         wxSize(300, 300));
    popup.SetClientSize(300, 300);
    popup.Position(parent->ClientToScreen(wxPoint(20, 20)), wxSize(1, 1));

    popup.Popup();
    wxYield();

    GtkWidget* const content =
        gtk_popover_get_child(GTK_POPOVER(popup.GetHandle()));
    CHECK( gtk_widget_get_width(content) == 300 );
    CHECK( gtk_widget_get_height(content) == 300 );

    popup.Dismiss();
}
#endif // __WXGTK4__

#if wxUSE_TOOLTIPS
TEST_CASE_METHOD(WindowTestCase, "Window::ToolTip", "[window]")
{
    CHECK(!m_window->GetToolTip());
    CHECK( m_window->GetToolTipText() == "" );

    m_window->SetToolTip("text tip");

    CHECK( m_window->GetToolTipText() == "text tip" );

    m_window->UnsetToolTip();

    CHECK(!m_window->GetToolTip());
    CHECK( m_window->GetToolTipText() == "" );

    wxToolTip* tip = new wxToolTip("other tip");

    m_window->SetToolTip(tip);

    CHECK( m_window->GetToolTip() == tip );
    CHECK( m_window->GetToolTipText() == "other tip" );
}
#endif // wxUSE_TOOLTIPS

TEST_CASE_METHOD(WindowTestCase, "Window::Help", "[window]")
{
#if wxUSE_HELP
    wxHelpProvider::Set(new wxSimpleHelpProvider());

    CHECK( m_window->GetHelpText() == "" );

    m_window->SetHelpText("helptext");

    CHECK( m_window->GetHelpText() == "helptext" );
#endif
}

TEST_CASE_METHOD(WindowTestCase, "Window::Parent", "[window]")
{
    CHECK( m_window->GetGrandParent() == static_cast<wxWindow*>(nullptr) );
    CHECK( m_window->GetParent() == wxTheApp->GetTopWindow() );
}

TEST_CASE_METHOD(WindowTestCase, "Window::Siblings", "[window]")
{
    CHECK( m_window->GetNextSibling() == static_cast<wxWindow*>(nullptr) );
    CHECK( m_window->GetPrevSibling() == static_cast<wxWindow*>(nullptr) );

    wxWindow* newwin = new wxWindow(wxTheApp->GetTopWindow(), wxID_ANY);

    CHECK( m_window->GetNextSibling() == newwin );
    CHECK( m_window->GetPrevSibling() == static_cast<wxWindow*>(nullptr) );

    CHECK( newwin->GetNextSibling() == static_cast<wxWindow*>(nullptr) );
    CHECK( newwin->GetPrevSibling() == m_window );

    wxDELETE(newwin);
}

TEST_CASE_METHOD(WindowTestCase, "Window::Children", "[window]")
{
    CHECK( m_window->GetChildren().GetCount() == 0 );

    wxWindow* child1 = new wxWindow(m_window, wxID_ANY);

    CHECK( m_window->GetChildren().GetCount() == 1 );

    m_window->RemoveChild(child1);

    CHECK( m_window->GetChildren().GetCount() == 0 );

    child1->SetId(wxID_HIGHEST + 1);
    child1->SetName("child1");

    m_window->AddChild(child1);

    CHECK( m_window->GetChildren().GetCount() == 1 );
    CHECK( m_window->FindWindow(wxID_HIGHEST + 1) == child1 );
    CHECK( m_window->FindWindow("child1") == child1 );

    m_window->DestroyChildren();

    CHECK( m_window->GetChildren().GetCount() == 0 );
}

TEST_CASE_METHOD(WindowTestCase, "Window::Focus", "[window]")
{
#ifndef __WXOSX__
    CHECK(!m_window->HasFocus());

    if ( m_window->AcceptsFocus() )
    {
        m_window->SetFocus();
        CHECK_FOCUS_IS(m_window);
    }

    //Set the focus back to the main window
    wxTheApp->GetTopWindow()->SetFocus();

    if ( m_window->AcceptsFocusFromKeyboard() )
    {
        m_window->SetFocusFromKbd();
        CHECK_FOCUS_IS(m_window);
    }
#endif
}

TEST_CASE_METHOD(WindowTestCase, "Window::Positioning", "[window]")
{
    //Some basic tests for consistency
    int x, y;
    m_window->GetPosition(&x, &y);

    CHECK( m_window->GetPosition().x == x );
    CHECK( m_window->GetPosition().y == y );
    CHECK( m_window->GetRect().GetTopLeft() == m_window->GetPosition() );

    m_window->GetScreenPosition(&x, &y);
    CHECK( m_window->GetScreenPosition().x == x );
    CHECK( m_window->GetScreenPosition().y == y );
    CHECK( m_window->GetScreenRect().GetTopLeft() == m_window->GetScreenPosition() );
}

TEST_CASE_METHOD(WindowTestCase, "Window::PositioningBeyondShortLimit", "[window]")
{
#ifdef __WXMSW__
    //Positioning under MSW is limited to short relative coordinates

    //
    //Test window creation beyond SHRT_MAX
    int commonDim = 10;
    wxWindow* w = new wxWindow(m_window, wxID_ANY,
                               wxPoint(0, SHRT_MAX + commonDim),
                               wxSize(commonDim, commonDim));
    CHECK( w->GetPosition().y == SHRT_MAX + commonDim );

    w->Move(0, 0);

    //
    //Test window moving beyond SHRT_MAX
    w->Move(0, SHRT_MAX + commonDim);
    CHECK( w->GetPosition().y == SHRT_MAX + commonDim );

    //
    //Test window moving below SHRT_MIN
    w->Move(0, SHRT_MIN - commonDim);
    CHECK( w->GetPosition().y == SHRT_MIN - commonDim );

    //
    //Test deferred move beyond SHRT_MAX
    m_window->SetVirtualSize(-1, SHRT_MAX + 2 * commonDim);
    wxWindow* bigWin = new wxWindow(m_window, wxID_ANY, wxDefaultPosition,
                                    //size is also limited by SHRT_MAX
                                    wxSize(commonDim, SHRT_MAX));
    wxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(bigWin);
    sizer->AddSpacer(commonDim); //add some space to go beyond SHRT_MAX
    sizer->Add(w);
    m_window->SetSizer(sizer);
    m_window->Layout();
    CHECK( w->GetPosition().y == SHRT_MAX + commonDim );
#endif
}

TEST_CASE_METHOD(WindowTestCase, "Window::Show", "[window]")
{
    CHECK(m_window->IsShown());

    m_window->Hide();

    CHECK(!m_window->IsShown());

    m_window->Show();

    CHECK(m_window->IsShown());

    m_window->Show(false);

    CHECK(!m_window->IsShown());

    m_window->ShowWithEffect(wxSHOW_EFFECT_BLEND);

    CHECK(m_window->IsShown());

    m_window->HideWithEffect(wxSHOW_EFFECT_BLEND);

    CHECK(!m_window->IsShown());
}

TEST_CASE_METHOD(WindowTestCase, "Window::Enable", "[window]")
{
    CHECK(m_window->IsEnabled());

    m_window->Disable();

    CHECK(!m_window->IsEnabled());

    m_window->Enable();

    CHECK(m_window->IsEnabled());

    m_window->Enable(false);

    CHECK(!m_window->IsEnabled());
    m_window->Enable();


    wxWindow* const child = new wxWindow(m_window, wxID_ANY);
    CHECK(child->IsEnabled());
    CHECK(child->IsThisEnabled());

    m_window->Disable();
    CHECK(!child->IsEnabled());
    CHECK(child->IsThisEnabled());

    child->Disable();
    CHECK(!child->IsEnabled());
    CHECK(!child->IsThisEnabled());

    m_window->Enable();
    CHECK(!child->IsEnabled());
    CHECK(!child->IsThisEnabled());

    child->Enable();
    CHECK(child->IsEnabled());
    CHECK(child->IsThisEnabled());
}

TEST_CASE_METHOD(WindowTestCase, "Window::FindWindowBy", "[window]")
{
    m_window->SetId(wxID_HIGHEST + 1);
    m_window->SetName("name");
    m_window->SetLabel("label");

    CHECK( wxWindow::FindWindowById(wxID_HIGHEST + 1) == m_window );
    CHECK( wxWindow::FindWindowByName("name") == m_window );
    CHECK( wxWindow::FindWindowByLabel("label") == m_window );

    CHECK( wxWindow::FindWindowById(wxID_HIGHEST + 3) == nullptr );
    CHECK( wxWindow::FindWindowByName("noname") == nullptr );
    CHECK( wxWindow::FindWindowByLabel("nolabel") == nullptr );
}

TEST_CASE_METHOD(WindowTestCase, "Window::SizerErrors", "[window][sizer][error]")
{
    wxWindow* const child = new wxWindow(m_window, wxID_ANY);
    std::unique_ptr<wxSizer> const sizer1(new wxBoxSizer(wxHORIZONTAL));
    std::unique_ptr<wxSizer> const sizer2(new wxBoxSizer(wxHORIZONTAL));

    REQUIRE_NOTHROW( sizer1->Add(child) );
#ifdef __WXDEBUG__
    CHECK_THROWS_AS( sizer1->Add(child), TestAssertFailure );
    CHECK_THROWS_AS( sizer2->Add(child), TestAssertFailure );
#else
    CHECK_NOTHROW( sizer1->Add(child) );
    CHECK_NOTHROW( sizer2->Add(child) );
#endif

    CHECK_NOTHROW( sizer1->Detach(child) );
    CHECK_NOTHROW( sizer2->Add(child) );

    REQUIRE_NOTHROW( delete child );
}

TEST_CASE_METHOD(WindowTestCase, "Window::Refresh", "[window]")
{
    wxWindow* const parent = m_window;

    // Ensure that the window doesn't need a redraw before starting this test:
    // it could need one if some other window overlapping it created by a
    // previously running test was destroyed but this window was not repainted
    // after that yet. Without this, child1 could still get repainted even if
    // we don't refresh it and this is exactly what happened under Mac.
    parent->Refresh();
    WaitForPaint waitForPaint(parent);

    wxWindow* const child1 = new wxWindow(parent, wxID_ANY, wxPoint(10, 20), wxSize(80, 50));
    wxWindow* const child2 = new wxWindow(parent, wxID_ANY, wxPoint(110, 20), wxSize(80, 50));
    wxWindow* const child3 = new wxWindow(parent, wxID_ANY, wxPoint(210, 20), wxSize(80, 50));

    m_window->SetSize(300, 100);

    // to help see the windows when debugging
    parent->SetBackgroundColour(*wxBLACK);
    child1->SetBackgroundColour(*wxBLUE);
    child2->SetBackgroundColour(*wxRED);
    child3->SetBackgroundColour(*wxGREEN);

    // Notice that using EventCounter here will give incorrect results,
    // so we have to bind each window to a distinct event handler instead.

    bool isParentPainted;
    bool isChild1Painted;
    bool isChild2Painted;
    bool isChild3Painted;

    const auto setFlagOnPaint = [](wxWindow* win, bool* flag)
    {
        win->Bind(wxEVT_PAINT, [=](wxPaintEvent&)
        {
            wxPaintDC dc(win);
            *flag = true;
        });
    };

    setFlagOnPaint(parent, &isParentPainted);
    setFlagOnPaint(child1, &isChild1Painted);
    setFlagOnPaint(child2, &isChild2Painted);
    setFlagOnPaint(child3, &isChild3Painted);

    // Prepare for the RefreshRect() call below
    wxYield();

    // Now initialize/reset the flags before calling RefreshRect()
    isParentPainted =
    isChild1Painted =
    isChild2Painted =
    isChild3Painted = false;

    parent->RefreshRect(wxRect(150, 10, 300, 80));

    WaitFor("parent repaint", [&]() { return isParentPainted; }, 100);

    // child1 should be the only window not to receive the wxEVT_PAINT event
    // because it does not intersect with the refreshed rectangle.
    CHECK(isParentPainted == true);
    CHECK(isChild1Painted == false);
    CHECK(isChild2Painted == true);
    CHECK(isChild3Painted == true);
}

// Window::Refresh above only asks whether a paint event arrived. Code that
// repaints just the damaged part -- the Life demo redraws exactly the cells
// GetUpdateRegion() reports, and has to, since wxClientDC cannot draw outside
// a paint handler on several platforms -- also needs the update region to
// actually cover what was refreshed. Too small a region draws too little, and
// nothing above would notice.
TEST_CASE_METHOD(WindowTestCase, "Window::RefreshRectUpdateRegion", "[window]")
{
    wxWindow* const win = m_window;

    win->SetSize(300, 200);
    win->Refresh();

    wxRect updated;
    bool painted = false;
    win->Bind(wxEVT_PAINT, [&](wxPaintEvent&)
    {
        wxPaintDC dc(win);
        updated = win->GetUpdateRegion().GetBox();
        painted = true;
    });

    // Settle any repaint still owed from the resize above.
    YieldForAWhile();

    const wxRect refreshed(20, 30, 100, 40);

    painted = false;
    updated = wxRect();
    win->RefreshRect(refreshed);

    REQUIRE( WaitFor("repaint", [&]() { return painted; }, 500) );

    INFO("refreshed "
         << refreshed.x << "," << refreshed.y << " "
         << refreshed.width << "x" << refreshed.height
         << " -- update region "
         << updated.x << "," << updated.y << " "
         << updated.width << "x" << updated.height);
    CHECK( updated.Contains(refreshed) );

    // Measured while adding this: GTK+ 3 reports exactly the refreshed
    // rectangle here, GTK4 reports the whole window. Both satisfy the check
    // above -- repainting more than asked is correct, only wasteful -- so this
    // is deliberately not asserted, but it is why the check is a Contains()
    // and not an equality.
}

#ifdef __WXGTK4__

// A window given a size has to report that size back, whatever GTK would
// rather draw. This pins #125: a clamp in wxPizza::size_allocate_child(),
// added for #95, quietly replaced any size below the child's minimum with that
// minimum -- and since the size_allocate handler reads wx's own m_width and
// m_height back out of the allocation, GetSize() then reported the
// replacement. An application placing siblings by hand got overlapping
// controls under GTK4 and not under GTK+ 2 or GTK+ 3, which both report the
// size that was asked for.
TEST_CASE_METHOD(WindowTestCase, "wxWindow::SetSizeIsHonoured", "[window][size]")
{
    // A button with a real label has a native minimum size well above what is
    // asked for below. The fixture destroys it with the rest of the children.
    wxButton* const button =
        new wxButton(wxTheApp->GetTopWindow(), wxID_ANY,
                     "A button with a fairly long label");

    button->SetSize(0, 0, 10, 4);
    YieldForAWhile();

    CHECK( button->GetSize() == wxSize(10, 4) );
}

#endif // __WXGTK4__
