// Does dragging a wxMiniFrame's caption produce wxMoveEvent, and what does
// wxGetMousePosition() report while it happens? Run under X11 and Wayland.
#include <wx/wx.h>
#include <wx/minifram.h>
#include <stdio.h>

static int gMoves = 0;

class Mini : public wxMiniFrame
{
public:
    Mini(wxWindow* p)
        : wxMiniFrame(p, wxID_ANY, "drag me", wxPoint(120, 120),
                      wxSize(240, 160),
                      wxCAPTION | wxRESIZE_BORDER | wxFRAME_TOOL_WINDOW)
    {
        Bind(wxEVT_MOVE, &Mini::OnMove, this);
        Bind(wxEVT_MOVING, &Mini::OnMove, this);
    }
    void OnMove(wxMoveEvent& e)
    {
        ++gMoves;
        if ( gMoves <= 3 )
            fprintf(stderr, "  move #%d to (%d,%d)\n",
                    gMoves, e.GetPosition().x, e.GetPosition().y);
        e.Skip();
    }
};

class App : public wxApp
{
public:
    bool OnInit() override
    {
        if ( !wxApp::OnInit() )
            return false;
        wxFrame* f = new wxFrame(nullptr, wxID_ANY, "wlmove-main",
                                 wxPoint(0, 0), wxSize(900, 700));
        f->Show();
        (new Mini(f))->Show();

        fprintf(stderr, "ready\n"); fflush(stderr);

        // Report periodically so an external driver can see progress, and
        // quit on its own so nothing hangs if injection does nothing.
        m_timer.SetOwner(this);
        m_timer.Start(500);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&){
            if ( ++m_ticks >= 24 ) { ExitMainLoop(); return; }
            const wxPoint p = ::wxGetMousePosition();
            fprintf(stderr, "t%02d moves=%d mouse=(%d,%d) down=%d\n",
                    m_ticks, gMoves, p.x, p.y,
                    wxGetMouseState().LeftIsDown());
            fflush(stderr);
        });
        return true;
    }
    int OnExit() override
    {
        fprintf(stderr, "TOTAL wxMoveEvent = %d\n", gMoves);
        fflush(stderr);
        return 0;
    }
private:
    wxTimer m_timer;
    int m_ticks = 0;
};
wxIMPLEMENT_APP(App);
