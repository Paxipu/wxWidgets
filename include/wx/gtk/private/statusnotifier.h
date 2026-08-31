///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/statusnotifier.h
// Purpose:     StatusNotifierItem, the tray icon protocol, spoken directly
// Author:      wxWidgets team
// Copyright:   (c) 2026 wxWidgets team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_PRIVATE_STATUSNOTIFIER_H_
#define _WX_GTK_PRIVATE_STATUSNOTIFIER_H_

#include "wx/defs.h"

#if wxUSE_TASKBARICON && defined(__WXGTK4__)

#include "wx/string.h"

#include <gio/gio.h>

class wxMenu;

// A tray icon, as the desktop actually asks for one: the application owns a
// bus name, exports org.kde.StatusNotifierItem on it, and asks
// org.kde.StatusNotifierWatcher to adopt it.  The panel then reads properties
// back and calls the methods below when the user acts on the icon.
//
// This talks the protocol itself rather than through libayatana-appindicator,
// which exists only as a GTK+ 3 build and would put libgtk-3 in the same
// process as libgtk-4 -- see #198, where that broke a real application.
class wxStatusNotifierItem
{
public:
    // What the icon does when the user acts on it.  Activate is the primary
    // click, SecondaryActivate the middle one; the context menu is served
    // separately, by the menu object handed to SetMenu().
    class Handler
    {
    public:
        virtual ~Handler() = default;
        virtual void OnActivate() = 0;
        virtual void OnSecondaryActivate() = 0;
        virtual void OnContextMenu() = 0;
    };

    // Nothing reaches the bus until Show() is called.
    wxStatusNotifierItem(const wxString& id, Handler* handler);
    ~wxStatusNotifierItem();

    // The icon is named rather than sent as pixels: a theme directory is
    // handed over once and the name changes when the image does, which is
    // what the panels implement most reliably.
    void SetIcon(const wxString& themePath, const wxString& iconName);

    void SetToolTip(const wxString& tip);

    // The object path of a com.canonical.dbusmenu server on this same
    // connection, or empty for an icon with no menu.  Has to be set before
    // Show(): the Menu property is read once when the item is adopted.
    void SetMenuPath(const wxString& path);

    // Get on the session bus without exporting anything yet. Show() does
    // this itself, but a caller that wants to serve a menu has to build it
    // on this same connection -- the panel reaches the menu through the bus
    // name the item is registered under -- and so needs the connection
    // before the item goes up.
    bool Connect();

    GDBusConnection* GetConnection() const { return m_connection; }

    // Exports the object and asks the watcher to adopt it. Returns false if
    // that could not even be attempted, having logged why; a true return
    // means the item is on the bus, not that a panel has taken it, which is
    // what IsShown() reports once the answer arrives.
    bool Show();

    void Hide();

    bool IsShown() const { return m_registered; }

    // Whether anything on this session would display an item at all.
    static bool IsWatcherPresent();

private:
    // Called back from the GDBus vtable, which cannot take a member function.
    friend struct wxStatusNotifierItemVTable;

    void EmitSignal(const char* name);
    bool RegisterWithWatcher();

    const wxString m_id;
    Handler* const m_handler;

    wxString m_iconThemePath;
    wxString m_iconName;
    wxString m_toolTip;
    wxString m_menuPath;

    GDBusConnection* m_connection = nullptr;
    guint m_objectId = 0;
    guint m_nameOwnerId = 0;
    guint m_watcherWatchId = 0;
    bool m_registered = false;

    wxDECLARE_NO_COPY_CLASS(wxStatusNotifierItem);
};

#endif // wxUSE_TASKBARICON && __WXGTK4__

#endif // _WX_GTK_PRIVATE_STATUSNOTIFIER_H_
