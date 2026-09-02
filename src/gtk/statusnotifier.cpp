///////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/statusnotifier.cpp
// Purpose:     StatusNotifierItem, the tray icon protocol, spoken directly
// Author:      wxWidgets team
// Copyright:   (c) 2026 wxWidgets team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_TASKBARICON && defined(__WXGTK4__)

#include "wx/gtk/private/statusnotifier.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/log.h"
    #include "wx/utils.h"
#endif

#include "wx/gtk/private/error.h"
#include "wx/gtk/private/variant.h"

namespace
{

const char* const TRACE_SNI = "statusnotifier";

const char* const WATCHER_NAME = "org.kde.StatusNotifierWatcher";
const char* const WATCHER_PATH = "/StatusNotifierWatcher";
const char* const ITEM_PATH = "/StatusNotifierItem";
const char* const ITEM_IFACE = "org.kde.StatusNotifierItem";

// Only the members a taskbar icon actually needs are declared.  A panel asks
// for properties by name and tolerates ones that are not there, so declaring
// the whole specification would be extra surface without extra behaviour.
const char* const ITEM_INTROSPECTION =
"<node>"
"  <interface name='org.kde.StatusNotifierItem'>"
"    <property name='Category' type='s' access='read'/>"
"    <property name='Id' type='s' access='read'/>"
"    <property name='Title' type='s' access='read'/>"
"    <property name='Status' type='s' access='read'/>"
"    <property name='IconName' type='s' access='read'/>"
"    <property name='IconThemePath' type='s' access='read'/>"
"    <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
"    <property name='Menu' type='o' access='read'/>"
"    <property name='ItemIsMenu' type='b' access='read'/>"
"    <method name='Activate'>"
"      <arg name='x' type='i' direction='in'/>"
"      <arg name='y' type='i' direction='in'/>"
"    </method>"
"    <method name='SecondaryActivate'>"
"      <arg name='x' type='i' direction='in'/>"
"      <arg name='y' type='i' direction='in'/>"
"    </method>"
"    <method name='ContextMenu'>"
"      <arg name='x' type='i' direction='in'/>"
"      <arg name='y' type='i' direction='in'/>"
"    </method>"
"    <method name='Scroll'>"
"      <arg name='delta' type='i' direction='in'/>"
"      <arg name='orientation' type='s' direction='in'/>"
"    </method>"
"    <signal name='NewIcon'/>"
"    <signal name='NewToolTip'/>"
"    <signal name='NewStatus'>"
"      <arg name='status' type='s'/>"
"    </signal>"
"  </interface>"
"</node>";

GDBusNodeInfo* GetItemNodeInfo()
{
    static GDBusNodeInfo* s_info = nullptr;
    if ( !s_info )
    {
        wxGtkError error;
        s_info = g_dbus_node_info_new_for_xml(ITEM_INTROSPECTION, error.Out());
        if ( !s_info )
        {
            wxLogDebug("StatusNotifierItem introspection is malformed: %s",
                       error.GetMessage());
        }
    }

    return s_info;
}

} // anonymous namespace

// The vtable cannot take member functions, so it is a friend that forwards.
struct wxStatusNotifierItemVTable
{
    static void MethodCall(GDBusConnection*,
                           const gchar*,
                           const gchar*,
                           const gchar*,
                           const gchar* method,
                           GVariant*,
                           GDBusMethodInvocation* invocation,
                           gpointer userData)
    {
        wxStatusNotifierItem* const self =
            static_cast<wxStatusNotifierItem*>(userData);

        wxLogTrace(TRACE_SNI, "item method %s", method);

        if ( self->m_handler )
        {
            if ( strcmp(method, "Activate") == 0 )
                self->m_handler->OnActivate();
            else if ( strcmp(method, "SecondaryActivate") == 0 )
                self->m_handler->OnSecondaryActivate();
            else if ( strcmp(method, "ContextMenu") == 0 )
                self->m_handler->OnContextMenu();
        }

        // Scroll is accepted and ignored: wxTaskBarIcon has no event for it,
        // and returning an error for a method we advertise would show up in
        // the panel's log on every wheel turn over the icon.
        g_dbus_method_invocation_return_value(invocation, nullptr);
    }

    static GVariant* GetProperty(GDBusConnection*,
                                 const gchar*,
                                 const gchar*,
                                 const gchar*,
                                 const gchar* name,
                                 GError** error,
                                 gpointer userData)
    {
        wxStatusNotifierItem* const self =
            static_cast<wxStatusNotifierItem*>(userData);

        if ( strcmp(name, "Category") == 0 )
            return g_variant_new_string("ApplicationStatus");

        if ( strcmp(name, "Id") == 0 )
            return g_variant_new_string(self->m_id.utf8_str());

        if ( strcmp(name, "Title") == 0 )
            return g_variant_new_string(self->m_toolTip.utf8_str());

        if ( strcmp(name, "Status") == 0 )
            return g_variant_new_string("Active");

        if ( strcmp(name, "IconName") == 0 )
            return g_variant_new_string(self->m_iconName.utf8_str());

        if ( strcmp(name, "IconThemePath") == 0 )
            return g_variant_new_string(self->m_iconThemePath.utf8_str());

        if ( strcmp(name, "ToolTip") == 0 )
        {
            // (icon name, icon pixmaps, title, body): the icon is left empty
            // and the text goes in the title, which is what panels show.
            GVariantBuilder pixmaps;
            g_variant_builder_init(&pixmaps, G_VARIANT_TYPE("a(iiay)"));

            return g_variant_new("(sa(iiay)ss)",
                                 "", &pixmaps,
                                 self->m_toolTip.utf8_str().data(), "");
        }

        if ( strcmp(name, "Menu") == 0 )
        {
            // An object path is not allowed to be empty even when there is no
            // menu, so name one that carries nothing.
            return g_variant_new_object_path(
                self->m_menuPath.empty() ? "/NO_DBUSMENU"
                                         : self->m_menuPath.utf8_str().data());
        }

        if ( strcmp(name, "ItemIsMenu") == 0 )
        {
            // False: a primary click is ours to handle, not an instruction to
            // the panel to open the menu instead.
            return g_variant_new_boolean(FALSE);
        }

        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                    "No such property '%s'", name);
        return nullptr;
    }
};

namespace
{

const GDBusInterfaceVTable ITEM_VTABLE =
{
    wxStatusNotifierItemVTable::MethodCall,
    wxStatusNotifierItemVTable::GetProperty,
    nullptr,
    { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }
};

} // anonymous namespace

wxStatusNotifierItem::wxStatusNotifierItem(const wxString& id,
                                           Handler* handler)
    : m_id(id),
      m_handler(handler)
{
}

wxStatusNotifierItem::~wxStatusNotifierItem()
{
    Hide();
}

void wxStatusNotifierItem::SetIcon(const wxString& themePath,
                                   const wxString& iconName)
{
    m_iconThemePath = themePath;
    m_iconName = iconName;

    EmitSignal("NewIcon");
}

void wxStatusNotifierItem::SetToolTip(const wxString& tip)
{
    m_toolTip = tip;

    EmitSignal("NewToolTip");
}

void wxStatusNotifierItem::SetMenuPath(const wxString& path)
{
    m_menuPath = path;
}

void wxStatusNotifierItem::EmitSignal(const char* name)
{
    if ( !m_connection || !m_objectId )
        return;

    // NewStatus carries an argument and the others do not, and this is only
    // ever called for the others.
    g_dbus_connection_emit_signal(m_connection, nullptr, ITEM_PATH,
                                  ITEM_IFACE, name, nullptr, nullptr);
}

bool wxStatusNotifierItem::RegisterWithWatcher()
{
    // Asynchronously, and not only to keep startup quick. A panel adopts an
    // item and then reads its properties back; if we were blocked in a
    // synchronous call waiting for the reply to this one, we could not answer
    // that, and the panel would adopt an item it cannot read. A panel that
    // hung would also take the application's startup with it.
    g_dbus_connection_call(
        m_connection,
        WATCHER_NAME, WATCHER_PATH, WATCHER_NAME,
        "RegisterStatusNotifierItem",
        g_variant_new("(s)", g_dbus_connection_get_unique_name(m_connection)),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        [](GObject* source, GAsyncResult* result, gpointer data)
        {
            wxGtkError error;
            GVariant* const res = g_dbus_connection_call_finish(
                G_DBUS_CONNECTION(source), result, error.Out());

            wxStatusNotifierItem* const self =
                static_cast<wxStatusNotifierItem*>(data);

            if ( !res )
            {
                wxLogTrace(TRACE_SNI, "RegisterStatusNotifierItem failed: %s",
                           error.GetMessage());
                return;
            }

            g_variant_unref(res);
            wxLogTrace(TRACE_SNI, "registered with the watcher");
            self->m_registered = true;
        },
        this);

    return true;
}

bool wxStatusNotifierItem::Connect()
{
    if ( m_connection )
        return true;

    wxGtkError error;
    m_connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, error.Out());
    if ( !m_connection )
    {
        wxLogTrace(TRACE_SNI, "no session bus: %s", error.GetMessage());
        return false;
    }

    return true;
}

bool wxStatusNotifierItem::Show()
{
    if ( m_registered )
        return true;

    if ( !Connect() )
        return false;

    GDBusNodeInfo* const node = GetItemNodeInfo();
    if ( !node )
        return false;

    if ( !m_objectId )
    {
        wxGtkError regError;
        m_objectId = g_dbus_connection_register_object(
            m_connection, ITEM_PATH, node->interfaces[0],
            &ITEM_VTABLE, this, nullptr, regError.Out());

        if ( !m_objectId )
        {
            wxLogTrace(TRACE_SNI, "cannot export the item: %s",
                       regError.GetMessage());
            return false;
        }
    }

    // The item is registered by unique name, so no well-known name has to be
    // owned for the watcher to find it.  What the watch is for is the watcher
    // itself coming and going: a panel restart drops every item it knew, and
    // an icon that does not register again simply disappears until the
    // application is restarted.
    if ( !m_watcherWatchId )
    {
        m_watcherWatchId = g_bus_watch_name_on_connection(
            m_connection, WATCHER_NAME, G_BUS_NAME_WATCHER_FLAGS_NONE,
            [](GDBusConnection*, const gchar*, const gchar*, gpointer data)
            {
                static_cast<wxStatusNotifierItem*>(data)->RegisterWithWatcher();
            },
            [](GDBusConnection*, const gchar*, gpointer data)
            {
                static_cast<wxStatusNotifierItem*>(data)->m_registered = false;
            },
            this, nullptr);
    }

    // Ask now as well as on the watch: the watch fires on the main loop, and
    // an application that shows its icon before entering that loop would
    // otherwise not register until it did.
    //
    // Note that this returning true means the item is exported and has asked
    // to be adopted, not that a panel has taken it -- that answer arrives
    // later and is what IsShown() reports.
    return RegisterWithWatcher();
}

void wxStatusNotifierItem::Hide()
{
    if ( m_watcherWatchId )
    {
        g_bus_unwatch_name(m_watcherWatchId);
        m_watcherWatchId = 0;
    }

    if ( m_objectId )
    {
        g_dbus_connection_unregister_object(m_connection, m_objectId);
        m_objectId = 0;
    }

    if ( m_nameOwnerId )
    {
        g_bus_unown_name(m_nameOwnerId);
        m_nameOwnerId = 0;
    }

    if ( m_connection )
    {
        g_object_unref(m_connection);
        m_connection = nullptr;
    }

    m_registered = false;
}

/* static */
bool wxStatusNotifierItem::IsWatcherPresent()
{
    wxGtkError error;
    GDBusConnection* const conn =
        g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, error.Out());
    if ( !conn )
    {
        wxLogTrace(TRACE_SNI, "no session bus: %s", error.GetMessage());
        return false;
    }

    wxGtkError callError;
    const wxGtkVariant res{g_dbus_connection_call_sync(
        conn,
        "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "NameHasOwner",
        g_variant_new("(s)", WATCHER_NAME),
        G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        callError.Out()
    )};

    g_object_unref(conn);

    if ( !res )
    {
        wxLogTrace(TRACE_SNI, "NameHasOwner failed: %s",
                   callError.GetMessage());
        return false;
    }

    gboolean hasOwner = FALSE;
    res.Get("(b)", &hasOwner);

    return hasOwner != FALSE;
}

#endif // wxUSE_TASKBARICON && __WXGTK4__
