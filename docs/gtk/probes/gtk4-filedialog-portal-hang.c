/* GtkFileDialog is what GTK4 offers in place of the deprecated
   GtkFileChooser interface, and #209 has already moved wxDirDialog onto it.
   #182 would move wxFileDialog too. Before that: does it actually open?
 *
 * It does not, on a machine with a D-Bus session bus and no portal answering
 * on it. GtkFileDialog builds its GtkFileChooserDialog and then never shows
 * it -- no error, no warning, no timeout. The same widget presented directly
 * appears at once, so this is not the display, the theme or the harness.
 *
 * Clearing DBUS_SESSION_BUS_ADDRESS makes it appear immediately, which is
 * what names the cause. GTK_USE_PORTAL=0 does not help.
 *
 * This is the same shape as the printing finding in #161: "GTK 4.22 takes the
 * portal route outside a sandbox, where a present-but-silent portal blocks
 * with no timeout; a missing portal is handled fine."
 *
 * Build against the same GTK the library uses:
 *   gcc -o probe gtk4-filedialog-portal-hang.c $(pkg-config --cflags --libs gtk4)
 *   ldd probe | grep gtk
 *
 * Run it twice:
 *   xvfb-run -a ./probe
 *   DBUS_SESSION_BUS_ADDRESS= xvfb-run -a ./probe
 */
#include <gtk/gtk.h>

static GMainLoop* loop;

static gboolean report(gpointer data)
{
    const char* const when = (const char*)data;

    GListModel* const toplevels = gtk_window_get_toplevels();
    const guint n = g_list_model_get_n_items(toplevels);

    g_print("  %-18s %u top levels\n", when, n);
    for ( guint i = 0; i < n; i++ )
    {
        GtkWindow* const w = GTK_WINDOW(g_list_model_get_item(toplevels, i));
        g_print("      %-22s visible=%d  title=%s\n",
                G_OBJECT_TYPE_NAME(w),
                gtk_widget_get_visible(GTK_WIDGET(w)),
                gtk_window_get_title(w) ? gtk_window_get_title(w) : "(none)");
        g_object_unref(w);
    }

    return G_SOURCE_REMOVE;
}

static gboolean stop(gpointer) { g_main_loop_quit(loop); return G_SOURCE_REMOVE; }
static void chosen(GObject*, GAsyncResult*, gpointer) { g_print("  callback ran\n"); }

static gboolean open_new_dialog(gpointer)
{
    GtkFileDialog* const dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "the replacement");
    gtk_file_dialog_open(dialog, NULL, NULL, chosen, NULL);

    return G_SOURCE_REMOVE;
}

int main(void)
{
    gtk_init();

    g_print("GTK %u.%u.%u, session bus %s\n",
            gtk_get_major_version(), gtk_get_minor_version(),
            gtk_get_micro_version(),
            g_getenv("DBUS_SESSION_BUS_ADDRESS") &&
                *g_getenv("DBUS_SESSION_BUS_ADDRESS") ? "present" : "absent");

    GtkWidget* const parent = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(parent), "parent");
    gtk_window_present(GTK_WINDOW(parent));

    /* The deprecated widget, used directly, as a control: if this one does
       not appear either then the environment is at fault, not GtkFileDialog. */
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkWidget* const old =
        gtk_file_chooser_dialog_new("the deprecated widget", GTK_WINDOW(parent),
                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                    "_Cancel", GTK_RESPONSE_CANCEL, NULL);
    gtk_window_present(GTK_WINDOW(old));
    G_GNUC_END_IGNORE_DEPRECATIONS

    g_timeout_add(600, report, (gpointer)"deprecated:");
    g_timeout_add(1200, open_new_dialog, NULL);
    g_timeout_add(5000, report, (gpointer)"replacement:");

    loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add(7000, stop, NULL);
    g_main_loop_run(loop);

    return 0;
}
