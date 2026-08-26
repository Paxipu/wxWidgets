// Does wxWindow::Move() actually move a toplevel window?
//
// wxAuiManager::OnMotion() drags a floating pane by calling Move() on its
// frame once per motion event.  Under Wayland a client cannot position its
// own toplevel -- xdg_toplevel has no request for it -- so the question is
// whether that call does anything at all, and whether wx notices that it
// did not.
//
// The window prints what wx believes its position to be.  The compositor is
// the control: ask swaymsg for the real rectangle and compare.  See
// wayland-toplevel-move.sh, which also checks that the compositor can move
// this very window, so that "nothing moved" cannot be explained by a window
// that was never movable in the first place.

#include "wx/wx.h"

namespace
{

const int MOVE_TARGETS[][2] = { { 400, 300 }, { 700, 120 }, { 150, 600 } };
const size_t MOVE_COUNT = WXSIZEOF(MOVE_TARGETS);

class MoveProbeFrame : public wxFrame
{
public:
    MoveProbeFrame()
        : wxFrame(nullptr, wxID_ANY, "movetest",
                  wxPoint(50, 50), wxSize(320, 200))
    {
        m_step = 0;
        m_timer.SetOwner(this);
        Bind(wxEVT_TIMER, &MoveProbeFrame::OnTimer, this);
        Bind(wxEVT_MOVE, &MoveProbeFrame::OnMove, this);
    }

    void Start() { m_timer.Start(700); }

private:
    void OnMove(wxMoveEvent& event)
    {
        wxPrintf("EVENT  wxEVT_MOVE says (%d,%d)\n",
                 event.GetPosition().x, event.GetPosition().y);
        event.Skip();
    }

    void OnTimer(wxTimerEvent&)
    {
        if ( m_step >= MOVE_COUNT )
        {
            // Stay up rather than closing: the driver script wants to ask the
            // compositor about this window after the moves are over.
            m_timer.Stop();
            wxPrintf("DONE\n");
            fflush(stdout);
            return;
        }

        const int x = MOVE_TARGETS[m_step][0];
        const int y = MOVE_TARGETS[m_step][1];

        // Print the position before the move as well: if wx is simply
        // echoing back whatever it was last told, that shows up here.
        const wxPoint before = GetPosition();
        Move(x, y);
        const wxPoint after = GetPosition();

        wxPrintf("MOVE   asked for (%d,%d) -- wx said (%d,%d) before, "
                 "(%d,%d) after\n",
                 x, y, before.x, before.y, after.x, after.y);
        fflush(stdout);

        m_step++;
    }

    wxTimer m_timer;
    size_t m_step;
};

class MoveProbeApp : public wxApp
{
public:
    bool OnInit() override
    {
        MoveProbeFrame* const frame = new MoveProbeFrame();
        frame->Show();
        frame->Start();
        return true;
    }
};

} // anonymous namespace

wxIMPLEMENT_APP(MoveProbeApp);
