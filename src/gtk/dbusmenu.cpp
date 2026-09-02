///////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/dbusmenu.cpp
// Purpose:     Serve a wxMenu over com.canonical.dbusmenu
// Author:      wxWidgets team
// Copyright:   (c) 2026 wxWidgets team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_TASKBARICON && defined(__WXGTK4__)

#include "wx/gtk/private/dbusmenu.h"

#ifndef WX_PRECOMP
    #include "wx/log.h"
    #include "wx/menu.h"
#endif

#include "wx/gtk/private/error.h"

namespace
{

const char* const TRACE_DBUSMENU = "dbusmenu";

const char* const MENU_IFACE = "com.canonical.dbusmenu";

const char* const MENU_INTROSPECTION =
"<node>"
"  <interface name='com.canonical.dbusmenu'>"
"    <property name='Version' type='u' access='read'/>"
"    <property name='Status' type='s' access='read'/>"
"    <property name='TextDirection' type='s' access='read'/>"
"    <property name='IconThemePath' type='as' access='read'/>"
"    <method name='GetLayout'>"
"      <arg name='parentId' type='i' direction='in'/>"
"      <arg name='recursionDepth' type='i' direction='in'/>"
"      <arg name='propertyNames' type='as' direction='in'/>"
"      <arg name='revision' type='u' direction='out'/>"
"      <arg name='layout' type='(ia{sv}av)' direction='out'/>"
"    </method>"
"    <method name='GetGroupProperties'>"
"      <arg name='ids' type='ai' direction='in'/>"
"      <arg name='propertyNames' type='as' direction='in'/>"
"      <arg name='properties' type='a(ia{sv})' direction='out'/>"
"    </method>"
"    <method name='GetProperty'>"
"      <arg name='id' type='i' direction='in'/>"
"      <arg name='name' type='s' direction='in'/>"
"      <arg name='value' type='v' direction='out'/>"
"    </method>"
"    <method name='Event'>"
"      <arg name='id' type='i' direction='in'/>"
"      <arg name='eventId' type='s' direction='in'/>"
"      <arg name='data' type='v' direction='in'/>"
"      <arg name='timestamp' type='u' direction='in'/>"
"    </method>"
"    <method name='EventGroup'>"
"      <arg name='events' type='a(isvu)' direction='in'/>"
"      <arg name='idErrors' type='ai' direction='out'/>"
"    </method>"
"    <method name='AboutToShow'>"
"      <arg name='id' type='i' direction='in'/>"
"      <arg name='needUpdate' type='b' direction='out'/>"
"    </method>"
"    <method name='AboutToShowGroup'>"
"      <arg name='ids' type='ai' direction='in'/>"
"      <arg name='updatesNeeded' type='ai' direction='out'/>"
"      <arg name='idErrors' type='ai' direction='out'/>"
"    </method>"
"    <signal name='ItemsPropertiesUpdated'>"
"      <arg name='updatedProps' type='a(ia{sv})'/>"
"      <arg name='removedProps' type='a(ias)'/>"
"    </signal>"
"    <signal name='LayoutUpdated'>"
"      <arg name='revision' type='u'/>"
"      <arg name='parent' type='i'/>"
"    </signal>"
"  </interface>"
"</node>";

GDBusNodeInfo* GetMenuNodeInfo()
{
    static GDBusNodeInfo* s_info = nullptr;
    if ( !s_info )
    {
        wxGtkError error;
        s_info = g_dbus_node_info_new_for_xml(MENU_INTROSPECTION, error.Out());
        if ( !s_info )
        {
            wxLogDebug("dbusmenu introspection is malformed: %s",
                       error.GetMessage());
        }
    }

    return s_info;
}

// wx marks a mnemonic with '&' and dbusmenu with '_', and a literal one is
// doubled in both, so the conversion is a straight swap of the escapes.
wxString ToDBusMenuLabel(const wxString& label)
{
    wxString out;
    out.reserve(label.length());

    for ( wxString::const_iterator i = label.begin(); i != label.end(); ++i )
    {
        if ( *i == '_' )
        {
            out += "__";
        }
        else if ( *i == '&' )
        {
            wxString::const_iterator next = i + 1;
            if ( next != label.end() && *next == '&' )
            {
                out += '&';
                ++i;
            }
            else
            {
                out += '_';
            }
        }
        else
        {
            out += *i;
        }
    }

    return out;
}

} // anonymous namespace

struct wxDBusMenuVTable
{
    static void MethodCall(GDBusConnection*,
                           const gchar*,
                           const gchar*,
                           const gchar*,
                           const gchar* method,
                           GVariant* params,
                           GDBusMethodInvocation* invocation,
                           gpointer userData)
    {
        wxDBusMenu* const self = static_cast<wxDBusMenu*>(userData);

        if ( strcmp(method, "GetLayout") == 0 )
        {
            gint32 parentId = 0;
            gint32 depth = -1;
            GVariant* names = nullptr;
            g_variant_get(params, "(ii@as)", &parentId, &depth, &names);
            if ( names )
                g_variant_unref(names);

            GVariant* const layout = self->BuildLayout(parentId, depth);
            if ( !layout )
            {
                g_dbus_method_invocation_return_error(
                    invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                    "No such menu item %d", parentId);
                return;
            }

            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(u@(ia{sv}av))",
                                          self->m_revision, layout));
            return;
        }

        if ( strcmp(method, "GetGroupProperties") == 0 )
        {
            GVariantIter* ids = nullptr;
            GVariant* names = nullptr;
            g_variant_get(params, "(ai@as)", &ids, &names);
            if ( names )
                g_variant_unref(names);

            GVariantBuilder out;
            g_variant_builder_init(&out, G_VARIANT_TYPE("a(ia{sv})"));

            gint32 id;
            while ( ids && g_variant_iter_next(ids, "i", &id) )
            {
                wxMenuItem* const item = self->FindItem(id);
                if ( !item )
                    continue;

                g_variant_builder_add(&out, "(i@a{sv})",
                                      id, self->BuildProperties(item));
            }

            if ( ids )
                g_variant_iter_free(ids);

            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(a(ia{sv}))", &out));
            return;
        }

        if ( strcmp(method, "GetProperty") == 0 )
        {
            gint32 id = 0;
            const gchar* name = nullptr;
            g_variant_get(params, "(i&s)", &id, &name);

            wxMenuItem* const item = self->FindItem(id);
            GVariant* value = nullptr;

            if ( item )
            {
                GVariant* const props = self->BuildProperties(item);
                value = g_variant_lookup_value(props, name, nullptr);
                g_variant_unref(g_variant_ref_sink(props));
            }

            if ( !value )
            {
                g_dbus_method_invocation_return_error(
                    invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                    "No property '%s' on item %d", name, id);
                return;
            }

            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(v)", value));
            return;
        }

        if ( strcmp(method, "Event") == 0 )
        {
            gint32 id = 0;
            const gchar* eventId = nullptr;
            GVariant* data = nullptr;
            guint32 timestamp = 0;
            g_variant_get(params, "(i&s@vu)", &id, &eventId, &data,
                          &timestamp);
            if ( data )
                g_variant_unref(data);

            wxLogTrace(TRACE_DBUSMENU, "event %s on %d", eventId, id);

            if ( strcmp(eventId, "clicked") == 0 )
            {
                wxMenuItem* const item = self->FindItem(id);
                if ( item && self->m_handler )
                    self->m_handler->OnMenuItem(item);
            }

            g_dbus_method_invocation_return_value(invocation, nullptr);
            return;
        }

        if ( strcmp(method, "EventGroup") == 0 )
        {
            // Every event in the group is accepted, so no ids are reported
            // back as unknown. Panels send these in bursts and a per-event
            // reply is not worth the round trips.
            GVariantBuilder errors;
            g_variant_builder_init(&errors, G_VARIANT_TYPE("ai"));

            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(ai)", &errors));
            return;
        }

        if ( strcmp(method, "AboutToShow") == 0 )
        {
            // False: the layout handed over is already current, because it is
            // built from the wxMenu at the moment it is asked for rather than
            // cached and refreshed.
            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(b)", FALSE));
            return;
        }

        if ( strcmp(method, "AboutToShowGroup") == 0 )
        {
            GVariantBuilder updates, errors;
            g_variant_builder_init(&updates, G_VARIANT_TYPE("ai"));
            g_variant_builder_init(&errors, G_VARIANT_TYPE("ai"));

            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(aiai)", &updates, &errors));
            return;
        }

        g_dbus_method_invocation_return_error(
            invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
            "No such method '%s'", method);
    }

    static GVariant* GetProperty(GDBusConnection*,
                                 const gchar*,
                                 const gchar*,
                                 const gchar*,
                                 const gchar* name,
                                 GError** error,
                                 gpointer)
    {
        if ( strcmp(name, "Version") == 0 )
            return g_variant_new_uint32(3);

        if ( strcmp(name, "Status") == 0 )
            return g_variant_new_string("normal");

        if ( strcmp(name, "TextDirection") == 0 )
            return g_variant_new_string("ltr");

        if ( strcmp(name, "IconThemePath") == 0 )
        {
            GVariantBuilder paths;
            g_variant_builder_init(&paths, G_VARIANT_TYPE("as"));
            return g_variant_new("as", &paths);
        }

        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                    "No such property '%s'", name);
        return nullptr;
    }
};

namespace
{

const GDBusInterfaceVTable MENU_VTABLE =
{
    wxDBusMenuVTable::MethodCall,
    wxDBusMenuVTable::GetProperty,
    nullptr,
    { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }
};

} // anonymous namespace

wxDBusMenu::wxDBusMenu(GDBusConnection* connection,
                       const wxString& path,
                       Handler* handler)
    : m_path(path),
      m_handler(handler),
      m_connection(connection)
{
    GDBusNodeInfo* const node = GetMenuNodeInfo();
    if ( !node )
        return;

    wxGtkError error;
    m_objectId = g_dbus_connection_register_object(
        m_connection, m_path.utf8_str(), node->interfaces[0],
        &MENU_VTABLE, this, nullptr, error.Out());

    if ( !m_objectId )
    {
        wxLogTrace(TRACE_DBUSMENU, "cannot export the menu: %s",
                   error.GetMessage());
    }
}

wxDBusMenu::~wxDBusMenu()
{
    if ( m_objectId )
        g_dbus_connection_unregister_object(m_connection, m_objectId);
}

void wxDBusMenu::SetMenu(wxMenu* menu)
{
    m_menu = menu;
    Rebuild();

    if ( !m_objectId )
        return;

    // Parent -1 rather than 0: the specification's way of saying the whole
    // layout changed and none of it should be trusted, which is the case
    // whenever the menu is replaced.
    m_revision++;
    g_dbus_connection_emit_signal(
        m_connection, nullptr, m_path.utf8_str(), MENU_IFACE,
        "LayoutUpdated", g_variant_new("(ui)", m_revision, -1), nullptr);
}

void wxDBusMenu::AppendItems(wxMenu* menu)
{
    if ( !menu )
        return;

    for ( wxMenuItemList::compatibility_iterator node =
              menu->GetMenuItems().GetFirst();
          node;
          node = node->GetNext() )
    {
        wxMenuItem* const item = node->GetData();
        m_items.push_back(item);

        if ( item->IsSubMenu() )
            AppendItems(item->GetSubMenu());
    }
}

void wxDBusMenu::Rebuild()
{
    m_items.clear();

    // Entry 0 is the root, which is not a wxMenuItem, so it holds nothing and
    // ids start at 1.
    m_items.push_back(nullptr);

    AppendItems(m_menu);
}

wxMenuItem* wxDBusMenu::FindItem(gint32 id) const
{
    if ( id <= 0 || static_cast<size_t>(id) >= m_items.size() )
        return nullptr;

    return m_items[id];
}

GVariant* wxDBusMenu::BuildProperties(wxMenuItem* item) const
{
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));

    if ( item->IsSeparator() )
    {
        g_variant_builder_add(&props, "{sv}", "type",
                              g_variant_new_string("separator"));
        return g_variant_builder_end(&props);
    }

    g_variant_builder_add(
        &props, "{sv}", "label",
        g_variant_new_string(
            ToDBusMenuLabel(item->GetItemLabel()).utf8_str()));

    g_variant_builder_add(&props, "{sv}", "enabled",
                          g_variant_new_boolean(item->IsEnabled()));
    g_variant_builder_add(&props, "{sv}", "visible",
                          g_variant_new_boolean(TRUE));

    if ( item->IsCheckable() )
    {
        g_variant_builder_add(
            &props, "{sv}", "toggle-type",
            g_variant_new_string(item->GetKind() == wxITEM_RADIO
                                    ? "radio" : "checkmark"));
        g_variant_builder_add(&props, "{sv}", "toggle-state",
                              g_variant_new_int32(item->IsChecked() ? 1 : 0));
    }

    if ( item->IsSubMenu() )
    {
        g_variant_builder_add(&props, "{sv}", "children-display",
                              g_variant_new_string("submenu"));
    }

    return g_variant_builder_end(&props);
}

GVariant* wxDBusMenu::BuildLayout(gint32 id, gint32 depth) const
{
    // The children of the root are the top level menu; the children of an
    // item are its submenu's, and anything else has none.
    wxMenu* children = nullptr;
    if ( id == 0 )
    {
        children = m_menu;
    }
    else
    {
        wxMenuItem* const item = FindItem(id);
        if ( !item )
            return nullptr;

        children = item->GetSubMenu();
    }

    GVariantBuilder kids;
    g_variant_builder_init(&kids, G_VARIANT_TYPE("av"));

    if ( children && depth != 0 )
    {
        // Ids are positions in the same walk Rebuild() makes, so finding a
        // child's id means finding it in that list rather than keeping a
        // second map that could disagree with the first.
        for ( wxMenuItemList::compatibility_iterator node =
                  children->GetMenuItems().GetFirst();
              node;
              node = node->GetNext() )
        {
            wxMenuItem* const child = node->GetData();

            gint32 childId = -1;
            for ( size_t i = 1; i < m_items.size(); i++ )
            {
                if ( m_items[i] == child )
                {
                    childId = static_cast<gint32>(i);
                    break;
                }
            }

            if ( childId < 0 )
                continue;

            GVariant* const sub = BuildLayout(childId, depth - 1);
            if ( sub )
                g_variant_builder_add(&kids, "v", sub);
        }
    }

    GVariant* props;
    if ( id == 0 )
    {
        GVariantBuilder rootProps;
        g_variant_builder_init(&rootProps, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&rootProps, "{sv}", "children-display",
                              g_variant_new_string("submenu"));
        props = g_variant_builder_end(&rootProps);
    }
    else
    {
        props = BuildProperties(FindItem(id));
    }

    return g_variant_new("(i@a{sv}av)", id, props, &kids);
}

#endif // wxUSE_TASKBARICON && __WXGTK4__
