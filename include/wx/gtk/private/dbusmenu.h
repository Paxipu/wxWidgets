///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/dbusmenu.h
// Purpose:     Serve a wxMenu over com.canonical.dbusmenu
// Author:      wxWidgets team
// Copyright:   (c) 2026 wxWidgets team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_PRIVATE_DBUSMENU_H_
#define _WX_GTK_PRIVATE_DBUSMENU_H_

#include "wx/defs.h"

#if wxUSE_TASKBARICON && defined(__WXGTK4__)

#include "wx/string.h"
#include "wx/vector.h"

#include <gio/gio.h>

class wxMenu;
class wxMenuItem;

// A menu as the panel wants it: not a widget handed over, but a tree it can
// ask about over the bus and act on from its own process.
//
// The menu itself stays an ordinary wxMenu owned by the caller. This walks it
// on demand and answers for it, so nothing about how menus are built has to
// change to put one in the tray.
class wxDBusMenu
{
public:
    // What to do when the user picks something. The item is the one that was
    // chosen; the caller decides how to turn that into a wx event.
    class Handler
    {
    public:
        virtual ~Handler() = default;
        virtual void OnMenuItem(wxMenuItem* item) = 0;
    };

    // Exports itself immediately; check IsOk() before using the path.
    wxDBusMenu(GDBusConnection* connection,
               const wxString& path,
               Handler* handler);
    ~wxDBusMenu();

    bool IsOk() const { return m_objectId != 0; }

    const wxString& GetPath() const { return m_path; }

    // The menu to serve, which the caller goes on owning. Passing a different
    // one -- or the same one after its contents changed -- tells the panel to
    // read the layout again.
    void SetMenu(wxMenu* menu);

private:
    friend struct wxDBusMenuVTable;

    // Ids are positions in a flattened walk of the menu, with 0 for the root,
    // rebuilt whenever the layout changes. A panel may ask about an id from
    // before a rebuild, so a lookup that fails is ordinary rather than an
    // error.
    void Rebuild();
    wxMenuItem* FindItem(gint32 id) const;
    void AppendItems(wxMenu* menu);

    GVariant* BuildProperties(wxMenuItem* item) const;
    GVariant* BuildLayout(gint32 id, gint32 depth) const;

    const wxString m_path;
    Handler* const m_handler;

    GDBusConnection* m_connection = nullptr;
    guint m_objectId = 0;

    wxMenu* m_menu = nullptr;

    // Index is the id; entry 0 is unused, standing for the root.
    wxVector<wxMenuItem*> m_items;

    guint32 m_revision = 1;

    wxDECLARE_NO_COPY_CLASS(wxDBusMenu);
};

#endif // wxUSE_TASKBARICON && __WXGTK4__

#endif // _WX_GTK_PRIVATE_DBUSMENU_H_
