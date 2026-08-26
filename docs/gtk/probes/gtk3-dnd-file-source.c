/*
 * gtk3-dnd-file-source.c -- external X11 file drag source for the GTK4
 * wxDropTarget implementation.
 *
 * A GTK4 source can complete a file transfer without exercising the legacy
 * X selection path still used by GTK3 file managers.  This source deliberately
 * uses GTK3 and offers text/uri-list, allowing that cross-version path to be
 * tested without depending on a particular file manager.
 *
 * Build:
 *   gcc -o gtk3-dnd-file-source gtk3-dnd-file-source.c \
 *       $(pkg-config --cflags --libs gtk+-3.0) -lX11
 *
 * Run the GTK4 dnd sample with fatal criticals, then start this program with a
 * file name and drag its label into the sample's "Drop files here" pane more
 * than once:
 *
 *   G_DEBUG=fatal-criticals ./dnd
 *   ./gtk3-dnd-file-source /tmp/a-file
 *
 * Before the fix for wxWidgets issue #144, GTK 4.14 eventually reports
 *
 *   gdk_drop_set_actions: assertion 'priv->state == GDK_DROP_STATE_NONE'
 *
 * and aborts.  The drop data itself should be received on every attempt.
 */

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#include <stdio.h>
#include <string.h>

static void
on_drag_data_get(GtkWidget* widget, GdkDragContext* context,
                 GtkSelectionData* selection_data, guint info, guint time,
                 gpointer user_data)
{
    (void)widget;
    (void)info;
    (void)time;

    // A leave event arriving while the GTK4 target is synchronously waiting
    // for this selection reproduces issue #144's nested DnD dispatch.  Delay
    // the actual response so that the target necessarily processes the event
    // first. This mode is deliberately opt-in because normal Xdnd sources do
    // not usually send XdndLeave after XdndDrop.
    if ( g_getenv("WX_DND_REENTRANT_LEAVE") )
    {
        GdkWindow* const sourceWindow =
            gdk_drag_context_get_source_window(context);
        GdkWindow* const destinationWindow =
            gdk_drag_context_get_dest_window(context);

        if ( sourceWindow && destinationWindow )
        {
            GdkDisplay* const display =
                gdk_window_get_display(destinationWindow);
            Display* const xdisplay = gdk_x11_display_get_xdisplay(display);

            XEvent event = { 0 };
            event.xclient.type = ClientMessage;
            event.xclient.display = xdisplay;
            event.xclient.window = GDK_WINDOW_XID(destinationWindow);
            event.xclient.message_type =
                gdk_x11_get_xatom_by_name_for_display(display, "XdndLeave");
            event.xclient.format = 32;
            event.xclient.data.l[0] = (long)GDK_WINDOW_XID(sourceWindow);

            XSendEvent(xdisplay, event.xclient.window, False, NoEventMask,
                       &event);
            XFlush(xdisplay);
            g_usleep(100000);

            puts("REENTRANT_LEAVE_SENT");
            fflush(stdout);
        }
    }

    const char* const uri = (const char*)user_data;
    gtk_selection_data_set(selection_data,
                           gdk_atom_intern_static_string("text/uri-list"),
                           8, (const guchar*)uri, (gint)strlen(uri));
    puts("DATA_SENT");
    fflush(stdout);
}

static void
on_drag_end(GtkWidget* widget, GdkDragContext* context, gpointer user_data)
{
    (void)widget;
    (void)context;
    (void)user_data;

    puts("DRAG_ENDED");
    fflush(stdout);
}

int
main(int argc, char** argv)
{
    static const GtkTargetEntry targets[] = {
        { (gchar*)"text/uri-list", 0, 0 }
    };

    if ( argc != 2 )
    {
        fprintf(stderr, "usage: %s FILE\n", argv[0]);
        return 2;
    }

    char* const fileUri = g_filename_to_uri(argv[1], NULL, NULL);
    if ( !fileUri )
    {
        fprintf(stderr, "cannot convert file name to URI: %s\n", argv[1]);
        return 2;
    }

    char* const uriList = g_strconcat(fileUri, "\r\n", NULL);
    g_free(fileUri);

    gtk_init(&argc, &argv);

    GtkWidget* const window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "GTK3 file drag source");
    gtk_window_set_default_size(GTK_WINDOW(window), 300, 180);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget* const source = gtk_event_box_new();
    GtkWidget* const label = gtk_label_new("Drag this file");
    gtk_widget_set_hexpand(label, TRUE);
    gtk_widget_set_vexpand(label, TRUE);
    gtk_container_add(GTK_CONTAINER(source), label);
    gtk_container_add(GTK_CONTAINER(window), source);

    gtk_drag_source_set(source, GDK_BUTTON1_MASK,
                        targets, G_N_ELEMENTS(targets), GDK_ACTION_COPY);
    g_signal_connect(source, "drag-data-get",
                     G_CALLBACK(on_drag_data_get), uriList);
    g_signal_connect(source, "drag-end", G_CALLBACK(on_drag_end), NULL);

    gtk_widget_show_all(window);
    gtk_main();

    g_free(uriList);

    return 0;
}
