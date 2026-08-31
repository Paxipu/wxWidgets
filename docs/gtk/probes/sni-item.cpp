// Does a wxTaskBarIcon reach a panel under GTK4, and can the panel use it?
//
// Deliberately through the public API and nothing else: what matters is that
// an application which already has a tray icon gets one here without
// changing, so the probe is written the way such an application is.
//
// Driven by sni-roundtrip.sh, which supplies the control: a stand-in watcher
// on a private session bus, which reads the item's properties back and walks
// its menu rather than only counting the registration call.

#include "wx/wx.h"
#include "wx/taskbar.h"

#include <stdio.h>

namespace
{

class ProbeIcon : public wxTaskBarIcon
{
public:
    // The panel draws this menu itself, in its own process, so it has to be
    // handed over rather than shown on demand.
    wxMenu* CreatePopupMenu() override
    {
        wxMenu* const menu = new wxMenu;
        menu->Append(wxID_OPEN, "&Open window");
        menu->AppendCheckItem(wxID_ANY, "Stay on &top")->Check(true);
        menu->AppendSeparator();

        wxMenu* const sub = new wxMenu;
        sub->Append(wxID_ANY, "Su&bitem");
        menu->AppendSubMenu(sub, "&More");

        menu->Append(wxID_EXIT, "E&xit")->Enable(false);

        return menu;
    }
};

class App : public wxApp
{
public:
    bool OnInit() override
    {
        if ( !wxApp::OnInit() )
            return false;

        printf("AVAILABLE %d\n", wxTaskBarIcon::IsAvailable() ? 1 : 0);

        m_icon.reset(new ProbeIcon);

        // Any icon will do; the panel is asked whether it can read one, not
        // what it looks like.
        wxBitmap bmp(22, 22);
        {
            wxMemoryDC dc(bmp);
            dc.SetBackground(*wxBLUE_BRUSH);
            dc.Clear();
        }

        const bool ok = m_icon->SetIcon(wxBitmapBundle(bmp), "probe tooltip");
        printf(ok ? "ITEM-SHOWN\n" : "ITEM-FAILED\n");
        printf("INSTALLED %d\n", m_icon->IsIconInstalled() ? 1 : 0);
        fflush(stdout);

        m_icon->Bind(wxEVT_MENU, [](wxCommandEvent& e) {
            printf("ITEM-MENU-CLICKED id=%d\n", e.GetId());
            fflush(stdout);
        });

        // Nothing here draws, so quit on a timer rather than on a window
        // being closed.
        m_timer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { ExitMainLoop(); });
        m_timer.StartOnce(8000);

        return true;
    }

    int OnExit() override
    {
        m_icon.reset();
        return wxApp::OnExit();
    }

private:
    std::unique_ptr<ProbeIcon> m_icon;
    wxTimer m_timer;
};

} // anonymous namespace

wxIMPLEMENT_APP(App);
