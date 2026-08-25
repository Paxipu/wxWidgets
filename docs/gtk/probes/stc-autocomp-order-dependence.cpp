// Why does tests/controls/styledtextctrltest.cpp "wxStyledTextCtrl::AutoComp"
// pass on its own and fail inside test_gui?
//
// The test shows an autocompletion popup and double-clicks the first entry,
// which must be inserted into the control. Under GTK4 it does that when run
// alone and does not when the suite has run anything before it, and the
// failure then aborts the whole run -- so 255 of the suite's 533 cases are
// never measured at all. See issue #82.
//
// This narrows that down to a single preceding action. Build against the
// port and run under a private X display:
//
//   g++ -o probe stc-autocomp-order-dependence.cpp \
//       $(path/to/wx-config --cxxflags --libs stc,core,base)
//   for m in none button move click; do PROBE_PRE=$m xvfb-run -a ./probe; done
//
// Measured with GTK 4.22.4, wx 3.3.4, X11 backend:
//
//   pre=none    text="ability"    <-- as the test expects
//   pre=button  text="ability"    <-- creating and destroying a control is fine
//   pre=move    text=""           <-- one simulated pointer move is enough
//   pre=click   text=""
//
// So it is not the control the previous test created, and not the click it
// performed: a single earlier wxUIActionSimulator::MouseMove() suffices, and
// nothing recovers from it -- in the suite 176 further test cases run in
// between and the failure still happens, so it is not a timing window either.
//
// Ruled out by measurement alongside this, so nobody re-derives them:
//
//   * where the pointer is parked. Placing it inside the popup's future
//     rectangle with xdotool before the program starts does NOT reproduce it.
//   * where the popup lands. Its X rectangle is 124x116+201+204 either way
//     (+-1 px), and the highlighted row occupies the same screen pixels, with
//     the click landing in the middle of it in both runs.
//   * whether a popup can receive a double click at all after a simulated
//     move. A plain wxPopupWindow, with and without wxPU_CONTAINS_CONTROLS,
//     reports down=1 up=2 dclick=1 in both cases.
//
// The answer, found after this probe was written and recorded here so the file
// does not go on pointing at a dead end: the click never reaches the popup at
// all. Tracing wxVListBox::OnLeftDown() shows the list receives nothing, while
// a handler on the wxStyledTextCtrl receives the press instead. Under GTK4 a
// wxPopupWindow is a GtkPopover, and a popover which does not autohide stops
// being given the pointer once GTK has processed a motion event over the
// parent window -- which is what the "move" row above does. See issue #138,
// probes/gtk4-popover-input.c, which measures that in plain GTK, and the fix.

#include <wx/wx.h>
#include <wx/stc/stc.h>
#include <wx/uiaction.h>

#include <stdio.h>
#include <string.h>

namespace
{

enum Pre
{
    PRE_NONE,    // show the popup and double-click it, nothing before
    PRE_BUTTON,  // create and destroy a control first, but simulate nothing
    PRE_MOVE,    // ... and move the pointer once with the simulator
    PRE_CLICK    // ... and click as the button tests in the suite do
};

Pre PreFromEnvironment()
{
    const char* const p = getenv("PROBE_PRE");

    if ( !p )                    return PRE_NONE;
    if ( !strcmp(p, "button") )  return PRE_BUTTON;
    if ( !strcmp(p, "move") )    return PRE_MOVE;
    if ( !strcmp(p, "click") )   return PRE_CLICK;

    return PRE_NONE;
}

const char* PreName(Pre pre)
{
    switch ( pre )
    {
        case PRE_BUTTON: return "button";
        case PRE_MOVE:   return "move";
        case PRE_CLICK:  return "click";
        case PRE_NONE:   break;
    }

    return "none";
}

// The simulator needs the event loop to run for anything it injects to be
// delivered, and one wxYield() is not always enough.
void Settle()
{
    for ( int i = 0; i < 10; ++i )
    {
        wxMilliSleep(50);
        wxYield();
    }
}

class Probe : public wxApp
{
public:
    bool OnInit() override
    {
        if ( !wxApp::OnInit() )
            return false;

        m_pre = PreFromEnvironment();

        // Same geometry as the suite's test frame, so the coordinates below
        // are the ones the failing test computes.
        m_frame = new wxFrame(nullptr, wxID_ANY, "Probe Frame",
                              wxPoint(200, 200), wxSize(400, 250));
        m_frame->Show();

        CallAfter(&Probe::RunProbe);

        return true;
    }

private:
    // Whatever the preceding test case did, reduced to its parts.
    void DoPrelude(wxUIActionSimulator& sim)
    {
        if ( m_pre == PRE_NONE )
            return;

        wxButton* const button = new wxButton(m_frame, wxID_ANY, "b");
        button->Show();
        wxYield();

        if ( m_pre != PRE_BUTTON )
        {
            sim.MouseMove(button->GetScreenPosition() + wxPoint(10, 10));
            wxYield();
        }

        if ( m_pre == PRE_CLICK )
        {
            sim.MouseClick();
            wxYield();
        }

        delete button;
        wxYield();
    }

    void RunProbe()
    {
        wxUIActionSimulator sim;

        DoPrelude(sim);

        wxStyledTextCtrl* const stc = new wxStyledTextCtrl(m_frame, wxID_ANY);
        stc->Show();
        wxYield();

        stc->SetFocus();
        stc->AutoCompShow(0, "ability able about above abroad absence absent");
        wxYield();

        // Straight out of the failing test: the middle of the first entry,
        // one and a half lines below the top of line 0.
        const wxPoint zero = stc->PointFromPosition(0);
        const int textHeight = stc->TextHeight(0);
        const int textWidth = stc->TextWidth(0, "ability");
        const wxPoint target = stc->ClientToScreen(
            wxPoint(zero.x + textWidth/2,
                    zero.y + textHeight + textHeight/2));

        sim.MouseMove(target);
        wxYield();
        sim.MouseDblClick();
        Settle();

        printf("pre=%-6s target=(%d,%d) text=\"%s\" autocomp_active=%d\n",
               PreName(m_pre), target.x, target.y,
               static_cast<const char*>(stc->GetText().mb_str()),
               stc->AutoCompActive() ? 1 : 0);
        fflush(stdout);

        if ( stc->AutoCompActive() )
            stc->AutoCompCancel();

        m_frame->Destroy();
        ExitMainLoop();
    }

    wxFrame* m_frame = nullptr;
    Pre m_pre = PRE_NONE;
};

} // anonymous namespace

wxIMPLEMENT_APP(Probe);
