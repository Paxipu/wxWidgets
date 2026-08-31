// Does wxStatusNotifierItem actually appear on the bus as a tray icon?
//
// Driven by sni-roundtrip.sh, which supplies the control: a stand-in watcher
// on a private session bus, which reads the item's properties back rather
// than only counting the registration call.
//
// Prints ITEM-SHOWN or ITEM-FAILED, then stays up long enough to be read.

#include "wx/wx.h"
#include "wx/gtk/private/statusnotifier.h"

#include <stdio.h>

namespace
{

class Handler : public wxStatusNotifierItem::Handler
{
public:
    void OnActivate() override
    {
        printf("ITEM-ACTIVATE\n");
        fflush(stdout);
    }

    void OnSecondaryActivate() override
    {
        printf("ITEM-SECONDARY\n");
        fflush(stdout);
    }

    void OnContextMenu() override
    {
        printf("ITEM-CONTEXTMENU\n");
        fflush(stdout);
    }
};

class App : public wxApp
{
public:
    bool OnInit() override
    {
        if ( !wxApp::OnInit() )
            return false;

        m_item.reset(new wxStatusNotifierItem("wxprobe", &m_handler));
        m_item->SetIcon("/usr/share/icons/hicolor", "probe-icon");
        m_item->SetToolTip("probe tooltip");

        printf(m_item->Show() ? "ITEM-SHOWN\n" : "ITEM-FAILED\n");
        fflush(stdout);

        // Nothing here draws, so quit on a timer rather than on a window
        // being closed.
        m_timer.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { ExitMainLoop(); });
        m_timer.StartOnce(8000);

        return true;
    }

private:
    Handler m_handler;
    std::unique_ptr<wxStatusNotifierItem> m_item;
    wxTimer m_timer;
};

} // anonymous namespace

wxIMPLEMENT_APP_CONSOLE(App);
