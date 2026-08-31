// Does wxStatusNotifierItem actually appear on the bus as a tray icon?
//
// Driven by sni-roundtrip.sh, which supplies the control: a stand-in watcher
// on a private session bus, which reads the item's properties back rather
// than only counting the registration call.
//
// Prints ITEM-SHOWN or ITEM-FAILED, then stays up long enough to be read.

#include "wx/wx.h"
#include "wx/gtk/private/statusnotifier.h"
#include "wx/gtk/private/dbusmenu.h"

#include "wx/menu.h"

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

class MenuHandler : public wxDBusMenu::Handler
{
public:
    void OnMenuItem(wxMenuItem* item) override
    {
        printf("ITEM-MENU-CLICKED %s\n",
               static_cast<const char*>(item->GetItemLabel().utf8_str()));
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

        // The menu has to be built on the item's own connection, so get on
        // the bus before either of them goes up.
        if ( m_item->Connect() )
        {
            m_menu.reset(new wxMenu);
            m_menu->Append(wxID_OPEN, "&Open window");
            m_menu->AppendCheckItem(wxID_ANY, "Stay on &top")->Check(true);
            m_menu->AppendSeparator();

            wxMenu* const sub = new wxMenu;
            sub->Append(wxID_ANY, "Su&bitem");
            m_menu->AppendSubMenu(sub, "&More");

            m_menu->Append(wxID_EXIT, "E&xit")->Enable(false);

            m_dbusMenu.reset(new wxDBusMenu(m_item->GetConnection(),
                                            "/MenuBar", &m_menuHandler));
            if ( m_dbusMenu->IsOk() )
            {
                m_dbusMenu->SetMenu(m_menu.get());
                m_item->SetMenuPath(m_dbusMenu->GetPath());
            }
        }

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
    MenuHandler m_menuHandler;
    std::unique_ptr<wxStatusNotifierItem> m_item;
    std::unique_ptr<wxMenu> m_menu;
    std::unique_ptr<wxDBusMenu> m_dbusMenu;
    wxTimer m_timer;
};

} // anonymous namespace

wxIMPLEMENT_APP_CONSOLE(App);
