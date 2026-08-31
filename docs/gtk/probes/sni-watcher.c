// A stand-in org.kde.StatusNotifierWatcher, so a tray icon can be tested
// without a desktop.
//
// It owns the watcher name, accepts RegisterStatusNotifierItem, and then does
// what a real panel does next: reads the item's properties back over the bus
// and prints them.  That second half is the point.  "The item registered" only
// says a method call arrived; a panel that cannot then read IconName or Menu
// still shows nothing, so the test has to ask for them.
//
// Prints REGISTERED, then one PROP line per property, then WATCHER-DONE.
// Exits non-zero if nothing registered before the timeout, so a silent
// failure cannot pass for success.
//
//   gcc -o sni-watcher sni-watcher.c $(pkg-config --cflags --libs gio-2.0)

#include <gio/gio.h>
#include <stdio.h>
#include <string.h>

static const char* const WATCHER_NAME = "org.kde.StatusNotifierWatcher";
static const char* const ITEM_IFACE = "org.kde.StatusNotifierItem";

static const char* const WATCHER_XML =
"<node>"
"  <interface name='org.kde.StatusNotifierWatcher'>"
"    <method name='RegisterStatusNotifierItem'>"
"      <arg name='service' type='s' direction='in'/>"
"    </method>"
"    <property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
"  </interface>"
"</node>";

static const char* const PROPS[] = {
    "Category", "Id", "Title", "Status", "IconName", "IconThemePath",
    "Menu", "ItemIsMenu", NULL
};

static GMainLoop* loop;
static int seen;
static gchar* pending;
static GDBusConnection* pending_conn;

static void read_back(GDBusConnection* conn, const char* service)
{
    int i;

    for ( i = 0; PROPS[i]; i++ )
    {
        GError* error = NULL;
        GVariant* res = g_dbus_connection_call_sync(
            conn, service, "/StatusNotifierItem",
            "org.freedesktop.DBus.Properties", "Get",
            g_variant_new("(ss)", ITEM_IFACE, PROPS[i]),
            G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 2000,
            NULL, &error);

        if ( !res )
        {
            printf("PROP %-14s <unreadable: %s>\n", PROPS[i], error->message);
            g_error_free(error);
            continue;
        }

        GVariant* value = NULL;
        g_variant_get(res, "(v)", &value);
        gchar* printed = g_variant_print(value, FALSE);
        printf("PROP %-14s %s\n", PROPS[i], printed);
        g_free(printed);
        g_variant_unref(value);
        g_variant_unref(res);
    }

    fflush(stdout);
}

static gboolean do_read_back(gpointer u)
{
    (void)u;

    read_back(pending_conn, pending);

    printf("WATCHER-DONE\n");
    fflush(stdout);
    g_main_loop_quit(loop);

    return G_SOURCE_REMOVE;
}

static void on_call(GDBusConnection* conn,
                    const gchar* sender,
                    const gchar* object_path,
                    const gchar* interface_name,
                    const gchar* method_name,
                    GVariant* parameters,
                    GDBusMethodInvocation* invocation,
                    gpointer user_data)
{
    (void)object_path; (void)interface_name; (void)user_data;

    if ( strcmp(method_name, "RegisterStatusNotifierItem") != 0 )
    {
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.freedesktop.DBus.Error.UnknownMethod", "no");
        return;
    }

    const gchar* service = NULL;
    g_variant_get(parameters, "(&s)", &service);

    // The specification allows either a bus name or an object path here; wx
    // sends its unique name, so fall back to the sender when it is a path.
    const char* target = (service && service[0] == ':') ? service : sender;

    printf("REGISTERED %s\n", target);
    fflush(stdout);
    seen = 1;

    // Reply first, then read back on an idle. Reading from inside the handler
    // deadlocks against an item that registers synchronously: it is blocked
    // waiting for this very reply and cannot answer a property call until it
    // arrives. A real panel does not block its own reply either, so doing it
    // this way is the accurate model as well as the working one.
    pending = g_strdup(target);
    pending_conn = g_object_ref(conn);
    g_idle_add(do_read_back, NULL);

    g_dbus_method_invocation_return_value(invocation, NULL);
}

static GVariant* on_get(GDBusConnection* c, const gchar* s, const gchar* o,
                        const gchar* i, const gchar* name, GError** e,
                        gpointer u)
{
    (void)c; (void)s; (void)o; (void)i; (void)u;

    if ( strcmp(name, "IsStatusNotifierHostRegistered") == 0 )
        return g_variant_new_boolean(TRUE);

    g_set_error(e, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "no");
    return NULL;
}

static const GDBusInterfaceVTable VTABLE = { on_call, on_get, NULL, { 0 } };

static void on_acquired(GDBusConnection* conn, const gchar* name, gpointer u)
{
    (void)name; (void)u;

    GError* error = NULL;
    GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(WATCHER_XML, &error);
    if ( !node )
    {
        fprintf(stderr, "bad xml: %s\n", error->message);
        g_main_loop_quit(loop);
        return;
    }

    if ( !g_dbus_connection_register_object(conn, "/StatusNotifierWatcher",
                                            node->interfaces[0], &VTABLE,
                                            NULL, NULL, &error) )
    {
        fprintf(stderr, "cannot export watcher: %s\n", error->message);
        g_main_loop_quit(loop);
        return;
    }

    printf("WATCHER-READY\n");
    fflush(stdout);
}

static gboolean give_up(gpointer u)
{
    (void)u;
    printf("WATCHER-TIMEOUT nothing registered\n");
    fflush(stdout);
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(int argc, char** argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 15;

    loop = g_main_loop_new(NULL, FALSE);

    g_bus_own_name(G_BUS_TYPE_SESSION, WATCHER_NAME,
                   G_BUS_NAME_OWNER_FLAGS_NONE,
                   on_acquired, NULL, NULL, NULL, NULL);

    g_timeout_add_seconds(seconds, give_up, NULL);
    g_main_loop_run(loop);

    // Non-zero when nothing arrived: a test whose watcher saw nothing must
    // not be able to report success.
    return seen ? 0 : 1;
}
