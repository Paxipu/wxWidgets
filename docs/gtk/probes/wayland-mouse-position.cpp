// wxAuiManager decides where a pane docks with
//     m_frame->ScreenToClient(::wxGetMousePosition())
// and during a drag the pointer is over the FLOATING PANE, not over m_frame.
// So: what does wxGetMousePosition() answer when the pointer is over a
// different toplevel of the same application?
//
// The control is built in: each frame reports its own motion events, so which
// window the pointer is really over is evidenced rather than assumed.
#include <wx/wx.h>
#include <stdio.h>

static const char* g_over = "(nothing yet)";
static wxPoint g_overPos(-1, -1);

class Watcher : public wxFrame
{
public:
    Watcher(wxWindow* parent, const char* name, wxPoint pos, wxSize size)
        : wxFrame(parent, wxID_ANY, name, pos, size), m_name(name)
    {
        // A wxFrame does not get motion itself on GTK: its client area is a
        // child window, so the panel is what has to be watched.
        wxPanel* const p = new wxPanel(this);
        p->Bind(wxEVT_MOTION, [this](wxMouseEvent& e){
            g_over = m_name;
            g_overPos = e.GetPosition();
            e.Skip();
        });
    }
private:
    const char* m_name;
};

class App : public wxApp
{
public:
    bool OnInit() override
    {
        if ( !wxApp::OnInit() ) return false;
        m_main = new Watcher(nullptr, "main",
                             wxPoint(100, 100), wxSize(400, 300));
        m_main->Show();
        m_pane = new Watcher(m_main, "pane",
                             wxPoint(700, 450), wxSize(300, 200));
        m_pane->Show();

        fprintf(stderr, "ready\n"); fflush(stderr);
        m_t.SetOwner(this); m_t.Start(1000);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&){
            const wxPoint g = ::wxGetMousePosition();
            const wxPoint viaMain = m_main->ScreenToClient(g);
            fprintf(stderr,
                "t%02d over %-6s client (%4d,%4d) | "
                "wxGetMousePosition (%4d,%4d) | ScreenToClient (%4d,%4d)\n",
                ++m_n, g_over, g_overPos.x, g_overPos.y, g.x, g.y,
                viaMain.x, viaMain.y);
            fflush(stderr);
            if ( m_n >= 14 ) ExitMainLoop();
        });
        return true;
    }
private:
    Watcher* m_main = nullptr;
    Watcher* m_pane = nullptr;
    wxTimer m_t; int m_n = 0;
};
wxIMPLEMENT_APP(App);
