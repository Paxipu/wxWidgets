// Does dragging a docked wxAUI pane by its caption move it to another dock?
//
// The assertion is the pane's own state, not pixels: a pane that starts docked
// left must end up somewhere else after being dragged there. That is what
// "docking works" means, and it is checkable without looking at the screen.
//
// Prints AIM so a driver knows where the caption is, then reports the outcome.
#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <stdio.h>

static const char* DirName(int d)
{
    switch ( d )
    {
        case wxAUI_DOCK_LEFT:   return "LEFT";
        case wxAUI_DOCK_RIGHT:  return "RIGHT";
        case wxAUI_DOCK_TOP:    return "TOP";
        case wxAUI_DOCK_BOTTOM: return "BOTTOM";
        case wxAUI_DOCK_CENTER: return "CENTER";
        default:                return "NONE";
    }
}

class Frame : public wxFrame
{
public:
    Frame()
        : wxFrame(nullptr, wxID_ANY, "auidock",
                  wxPoint(0, 0), wxSize(900, 700))
    {
        m_mgr.SetManagedWindow(this);

        wxPanel* centre = new wxPanel(this);
        centre->SetBackgroundColour(*wxWHITE);
        m_mgr.AddPane(centre, wxAuiPaneInfo().CenterPane().Name("centre"));
        WatchPointer(centre);

        m_left = new wxPanel(this, wxID_ANY, wxDefaultPosition,
                             wxSize(200, 300));
        m_left->SetBackgroundColour(wxColour(200, 220, 255));
        m_mgr.AddPane(m_left, wxAuiPaneInfo().Left().Caption("Tree Pane")
                          .Name("tree").Floatable(true).BestSize(200, 300));
        m_mgr.Update();
    }
    ~Frame() { m_mgr.UnInit(); }

    void ReportGeometry()
    {
        const wxRect r = m_left->GetRect();
        const wxPoint cap = ClientToScreen(wxPoint(r.x, r.y - 18));
        fprintf(stderr, "AIM %d %d\n", cap.x + r.width / 2, cap.y + 9);

        // Where the driver may drop: the frame's client area on screen, so a
        // drop can be aimed at a dock zone rather than off the edge.
        const wxSize cs = GetClientSize();
        const wxPoint origin = ClientToScreen(wxPoint(0, 0));
        fprintf(stderr, "CLIENT %d %d %d %d\n",
                origin.x, origin.y, cs.x, cs.y);

        // The same two things in client coordinates, which is all this
        // window reliably knows. Everything above went through
        // ClientToScreen(), and under Wayland that adds a position the
        // compositor never granted -- the very defect being tested -- so a
        // driver there must map these itself. See CALIBRATE below.
        fprintf(stderr, "AIMREL %d %d\n", r.x + r.width / 2, r.y - 9);
        fprintf(stderr, "CLIENTREL %d %d\n", cs.x, cs.y);
        fflush(stderr);
    }

    // A driver that cannot trust ClientToScreen() can still find the mapping
    // by moving the pointer somewhere and asking where that landed: every
    // motion is echoed in client coordinates, and the difference between the
    // two is the offset. Bound on the panel rather than the frame, whose
    // client area is a child window that takes the pointer first.
    void WatchPointer(wxWindow* on)
    {
        on->Bind(wxEVT_MOTION, [on, this](wxMouseEvent& e) {
            // Report in the frame's client coordinates, which is what
            // AIMREL is expressed in. The event arrives relative to the
            // window it was bound on, and that window does not start at the
            // frame's client origin -- it starts after the docked pane, so
            // using it directly puts the whole calibration out by the width
            // of that dock.
            wxPoint p = e.GetPosition();
            for ( wxWindow* w = on; w && w != this; w = w->GetParent() )
                p += w->GetPosition();

            fprintf(stderr, "MOTION %d %d\n", p.x, p.y);
            fflush(stderr);
            e.Skip();
        });
    }

    void ReportState(const char* when)
    {
        wxAuiPaneInfo& p = m_mgr.GetPane("tree");
        fprintf(stderr, "STATE %-8s floating=%d docked=%d direction=%s\n",
                when, p.IsFloating() ? 1 : 0, p.IsDocked() ? 1 : 0,
                DirName(p.dock_direction));
        fflush(stderr);
    }

    // Start with the pane floating, without dragging it there. Undocking and
    // re-docking are separate failures with separate causes, and a test for
    // the second that has to perform the first cannot say which it measured
    // when it fails.
    void FloatThePane()
    {
        m_mgr.GetPane("tree").Float();
        m_mgr.Update();

        if ( wxWindow* const frame = m_mgr.GetPane("tree").frame )
        {
            fprintf(stderr, "FLOATING title=%s\n",
                    static_cast<const char*>(frame->GetLabel().utf8_str()));
        }
        else
        {
            fprintf(stderr, "FLOATING none -- the pane has no frame\n");
        }
        fflush(stderr);
    }

    int Direction() { return m_mgr.GetPane("tree").dock_direction; }
    bool Floating() { return m_mgr.GetPane("tree").IsFloating(); }
private:
    wxAuiManager m_mgr;
    wxPanel* m_left = nullptr;
};

class App : public wxApp
{
public:
    bool OnInit() override
    {
        if ( !wxApp::OnInit() ) return false;
        m_f = new Frame();
        m_f->Show();
        if ( wxGetEnv("AUIDOCK_START_FLOATING", nullptr) )
            m_f->FloatThePane();

        m_f->ReportGeometry();
        m_f->ReportState("start");
        m_startDir = m_f->Direction();

        m_t.SetOwner(this); m_t.Start(500);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&){
            if ( m_f->Floating() )
                m_everFloated = true;

            if ( ++m_n < 40 )
                return;

            m_f->ReportState("end");

            // What docking means is that a pane dragged to another dock ends
            // up in it. Asserting that directly, rather than that the pane
            // floated on the way, keeps the test honest about the outcome and
            // silent about the route: a drag that moves the pane between docks
            // without ever floating it has still docked it.
            const bool docked = !m_f->Floating();
            const bool moved = m_f->Direction() != m_startDir;

            fprintf(stderr, "CHECK moved-dock=%d docked=%d floated=%d\n",
                    moved ? 1 : 0, docked ? 1 : 0, m_everFloated ? 1 : 0);
            fprintf(stderr, "RESULT %s\n",
                    (moved && docked)
                        ? "PASS the pane docked where it was dropped"
                        : docked
                            ? "FAIL never left its original dock"
                            : "FAIL ended up floating, not docked");
            fflush(stderr);
            ExitMainLoop();
        });
        return true;
    }
private:
    Frame* m_f = nullptr; wxTimer m_t; int m_n = 0; int m_startDir = 0;
    bool m_everFloated = false;
};
wxIMPLEMENT_APP(App);
