// Do ClientToScreen() and ScreenToClient() answer in screen coordinates?
//
// Under Wayland a client is not told where the compositor put its surface, so
// wxGTKGetOriginInRoot() -- which every mapping goes through -- can only add
// the surface's position on screen inside its GDK_WINDOWING_X11 branch. This
// prints what wx makes of a frame and of a child inside it; the driver asks
// the compositor for the same frame's rectangle, which is the control.
//
// Note that this is a different code path from the one issue #166 is about:
// nothing here reads m_x/m_y, so suppressing the phantom wxMoveEvent does not
// change any number below.

#include "wx/wx.h"

namespace
{

class CoordsFrame : public wxFrame
{
public:
    CoordsFrame()
        : wxFrame(nullptr, wxID_ANY, "coordstest",
                  wxDefaultPosition, wxSize(400, 300))
    {
        m_panel = new wxPanel(this, wxID_ANY, wxPoint(30, 40), wxSize(80, 20));
        m_timer.SetOwner(this);
        Bind(wxEVT_TIMER, &CoordsFrame::OnTimer, this);
    }

    void Start() { m_timer.Start(1500); }

private:
    void OnTimer(wxTimerEvent&)
    {
        if ( m_reported )
        {
            // Stay up until now so the driver, which asks the X server or
            // the compositor where this window is, has a window to ask
            // about. Exiting as soon as the numbers are printed leaves it
            // querying a window that is already gone.
            m_timer.Stop();
            wxTheApp->ExitMainLoop();
            return;
        }

        m_reported = true;

        // The frame's client origin, as wx would hand it to an application
        // asking where to put a popup.
        const wxPoint frameOrigin = ClientToScreen(wxPoint(0, 0));

        // The same for a child, which reaches the toplevel by recursion and
        // so carries the same error plus its own offset.
        const wxPoint panelOrigin = m_panel->ClientToScreen(wxPoint(0, 0));

        // And the round trip, which stays self-consistent even when both
        // halves are wrong: it is the absolute answer that is not usable.
        const wxPoint there = ClientToScreen(wxPoint(17, 23));
        const wxPoint back = ScreenToClient(there);

        wxPrintf("WX frame-client-origin (%d,%d)\n",
                 frameOrigin.x, frameOrigin.y);
        // Print where the child actually sits as well: a wxFrame resizes a
        // lone child to fill its client area, so a panel asked for (30,40)
        // is at (0,0) and its origin equalling the frame's is arithmetic
        // rather than a second bug.
        const wxPoint panelPos = m_panel->GetPosition();
        wxPrintf("WX panel-in-frame      (%d,%d)\n",
                 panelPos.x, panelPos.y);
        wxPrintf("WX panel-client-origin (%d,%d)\n",
                 panelOrigin.x, panelOrigin.y);
        wxPrintf("WX getposition          (%d,%d)\n",
                 GetPosition().x, GetPosition().y);
        wxPrintf("WX roundtrip (17,23) -> (%d,%d) -> (%d,%d)\n",
                 there.x, there.y, back.x, back.y);
        fflush(stdout);

        m_timer.Start(5000);
    }

    wxPanel* m_panel;
    wxTimer m_timer;
    bool m_reported = false;
};

class CoordsApp : public wxApp
{
public:
    bool OnInit() override
    {
        CoordsFrame* const frame = new CoordsFrame();
        frame->Show();
        frame->Start();
        return true;
    }
};

} // anonymous namespace

wxIMPLEMENT_APP(CoordsApp);
