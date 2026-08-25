// wxAuiManager drags a pane by capturing the mouse on the main frame and
// moving the floating frame from the motion events it then receives. Both
// halves are checked here: does the capture still deliver motion, and does
// moving a toplevel do anything?
#include <wx/wx.h>
#include <stdio.h>

class Frame : public wxFrame
{
public:
    Frame() : wxFrame(nullptr, wxID_ANY, "capture-probe",
                      wxPoint(0, 0), wxSize(900, 700))
    {
        Bind(wxEVT_LEFT_DOWN, &Frame::OnDown, this);
        Bind(wxEVT_MOTION,    &Frame::OnMotion, this);
        Bind(wxEVT_LEFT_UP,   &Frame::OnUp, this);

        // Stand-in for the floating pane: a second toplevel wx tries to move.
        m_pane = new wxFrame(this, wxID_ANY, "pane",
                             wxPoint(400, 300), wxSize(200, 150));
        m_pane->Show();
    }

    void OnDown(wxMouseEvent& e)
    {
        CaptureMouse();
        m_captured = true;
        m_motions = 0;
        fprintf(stderr, "LEFT_DOWN, mouse captured\n"); fflush(stderr);
        e.Skip();
    }

    void OnMotion(wxMouseEvent& e)
    {
        if ( m_captured && e.Dragging() )
        {
            ++m_motions;
            // What wxAuiManager does on every one of these.
            const wxPoint want(300 + m_motions * 7, 200 + m_motions * 5);
            m_pane->Move(want);
            const wxPoint got = m_pane->GetPosition();
            if ( m_motions <= 4 )
            {
                fprintf(stderr, "  motion %d: asked pane for (%d,%d), it reports (%d,%d)%s\n",
                        m_motions, want.x, want.y, got.x, got.y,
                        got == want ? "" : "   <-- did not move");
                fflush(stderr);
            }
        }
        e.Skip();
    }

    void OnUp(wxMouseEvent& e)
    {
        if ( m_captured ) { ReleaseMouse(); m_captured = false; }
        fprintf(stderr, "LEFT_UP after %d dragging motions\n", m_motions);
        fflush(stderr);
        e.Skip();
    }

    int Motions() const { return m_motions; }
private:
    wxFrame* m_pane = nullptr;
    bool m_captured = false;
    int m_motions = 0;
};

class App : public wxApp
{
public:
    bool OnInit() override
    {
        if ( !wxApp::OnInit() ) return false;
        m_f = new Frame(); m_f->Show();
        fprintf(stderr, "ready\n"); fflush(stderr);
        m_t.SetOwner(this); m_t.Start(500);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&){
            if ( ++m_n >= 30 )
            {
                fprintf(stderr, "TOTAL dragging motions delivered = %d\n",
                        m_f->Motions());
                fflush(stderr);
                ExitMainLoop();
            }
        });
        return true;
    }
private:
    Frame* m_f = nullptr; wxTimer m_t; int m_n = 0;
};
wxIMPLEMENT_APP(App);
