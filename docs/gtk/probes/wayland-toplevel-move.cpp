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

// How many one-second reports to make after the moves are over. The driver
// asks the compositor to move the window during this window of time.
const int WATCH_TICKS = 6;

class MoveProbeFrame : public wxFrame
{
public:
    MoveProbeFrame()
        : wxFrame(nullptr, wxID_ANY, "movetest",
                  wxPoint(50, 50), wxSize(320, 200))
    {
        m_step = 0;
        m_events = 0;
        m_watch = 0;
        m_timer.SetOwner(this);
        Bind(wxEVT_TIMER, &MoveProbeFrame::OnTimer, this);
        Bind(wxEVT_MOVE, &MoveProbeFrame::OnMove, this);
    }

    void Start() { m_timer.Start(700); }

private:
    void OnMove(wxMoveEvent& event)
    {
        m_events++;
        wxPrintf("EVENT  wxEVT_MOVE says (%d,%d)\n",
                 event.GetPosition().x, event.GetPosition().y);
        fflush(stdout);
        event.Skip();
    }

    void OnTimer(wxTimerEvent&)
    {
        if ( m_step >= MOVE_COUNT )
        {
            // The moves are over, but the run is not: the driver now asks the
            // compositor to move this window itself. Keep reporting, once a
            // second, so that a move the compositor really performs can be
            // seen arriving -- or not arriving -- rather than inferred from
            // an absence of output. WATCH lines carry the running count of
            // wxEVT_MOVE, so a compositor move that sends none says so in a
            // number.
            if ( m_watch == 0 )
            {
                wxPrintf("DONE   wxEVT_MOVE seen so far: %d\n", m_events);
                fflush(stdout);
                m_timer.Start(1000);
            }

            if ( ++m_watch > WATCH_TICKS )
            {
                m_timer.Stop();
                return;
            }

            const wxPoint pos = GetPosition();
            wxPrintf("WATCH  t+%ds wx says (%d,%d), wxEVT_MOVE total %d\n",
                     m_watch, pos.x, pos.y, m_events);
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
    int m_events;
    int m_watch;
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
