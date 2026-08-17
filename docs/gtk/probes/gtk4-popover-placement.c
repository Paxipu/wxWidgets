// Probe: can a GtkPopover stand in for GTK3's GTK_WINDOW_POPUP, and where
// exactly does it land?
//
// wxPopupWindow needs a floating surface it can place at an arbitrary position
// relative to its parent.  GTK4 removed both GTK_WINDOW_POPUP and
// gtk_window_move(), leaving GtkPopover as the only widget which still creates
// a surface of its own that may extend outside the parent toplevel.
//
// A popover is positioned by "pointing at" a rectangle in its parent's
// coordinate space, and it centers itself on that rectangle rather than
// aligning to it.  This probe measures the actual placement so wxPopupWindow's
// DoSetSize() can compensate exactly instead of guessing.
//
// Build with:
//   gcc -o gtk4-popover-placement gtk4-popover-placement.c $(pkg-config --cflags --libs gtk4)

#include <gtk/gtk.h>

#define POPUP_W 120
#define POPUP_H  60
#define WANT_X  200
#define WANT_Y  150

static GtkWidget* g_popover;
static GtkWidget* g_parent;
static GtkApplication* g_app;

static gboolean report(gpointer)
{
    GdkSurface* surface;
    double tx = 0, ty = 0;

    g_print("popover mapped: %s\n",
            gtk_widget_get_mapped(g_popover) ? "yes" : "NO");
    g_print("popover widget size: %d x %d (requested %d x %d)\n",
            gtk_widget_get_width(g_popover),
            gtk_widget_get_height(g_popover),
            POPUP_W, POPUP_H);

    // A popover lives on a surface of its own, so widget-relative coordinate
    // maths does not reach across to the parent: gtk_widget_compute_bounds()
    // returns the popover's own origin. The placement has to be read from the
    // GdkPopup instead.
    surface = gtk_native_get_surface(GTK_NATIVE(g_popover));
    if ( GDK_IS_POPUP(surface) )
    {
        gtk_native_get_surface_transform(GTK_NATIVE(g_popover), &tx, &ty);

        g_print("surface size:        %d x %d\n",
                gdk_surface_get_width(surface),
                gdk_surface_get_height(surface));
        g_print("surface transform:   %.1f,%.1f  (widget inset in surface)\n",
                tx, ty);
        g_print("gdk_popup position:  %d,%d\n",
                gdk_popup_get_position_x(GDK_POPUP(surface)),
                gdk_popup_get_position_y(GDK_POPUP(surface)));
        g_print("wanted top-left:     %d,%d\n", WANT_X, WANT_Y);
        g_print("delta:               %d,%d\n",
                gdk_popup_get_position_x(GDK_POPUP(surface)) - WANT_X,
                gdk_popup_get_position_y(GDK_POPUP(surface)) - WANT_Y);
    }
    else
    {
        g_print("popover surface is not a GdkPopup (%s)\n",
                surface ? G_OBJECT_TYPE_NAME(surface) : "(null)");
    }

    g_application_quit(G_APPLICATION(g_app));
    return G_SOURCE_REMOVE;
}

static void activate(GtkApplication* app, gpointer)
{
    GdkRectangle rect;

    g_app = app;

    GtkWidget* const win = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(win), 600, 400);

    // A plain container to hang the popover off, standing in for the wxPizza
    // of the parent wxWindow.
    g_parent = gtk_fixed_new();
    gtk_window_set_child(GTK_WINDOW(win), g_parent);

    g_popover = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(g_popover), FALSE);
    gtk_popover_set_autohide(GTK_POPOVER(g_popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(g_popover), GTK_POS_BOTTOM);
    gtk_widget_set_size_request(g_popover, POPUP_W, POPUP_H);
    gtk_widget_set_parent(g_popover, g_parent);

    // Point at a zero-sized rectangle at the position we want the popover's
    // top-left corner to end up at, then measure where it actually goes.
    rect.x = WANT_X;
    rect.y = WANT_Y;
    rect.width = 0;
    rect.height = 0;
    gtk_popover_set_pointing_to(GTK_POPOVER(g_popover), &rect);

    gtk_widget_set_visible(win, TRUE);
    gtk_popover_popup(GTK_POPOVER(g_popover));

    g_timeout_add(400, report, NULL);
}

int main(int argc, char** argv)
{
    GtkApplication* const app =
        gtk_application_new("org.wxwidgets.probe.popover", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
