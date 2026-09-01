/* src/gtk/window.cpp paints a widget's own CSS background for
   wxBG_STYLE_SYSTEM with gtk_render_background(), which is deprecated. The
   question before replacing it is whether it is also redundant: GTK4 renders
   the CSS boxes of every widget's node in gtk_widget_snapshot() before the
   widget's own snapshot vfunc runs, and if that is so there is nothing left
   for wx to do.

   Give a widget a garish background through CSS, snapshot it as a child of a
   mapped container -- exactly as a paint does -- and look at the middle
   pixel. Nothing in this program ever asks for the background to be drawn. */
#include <gtk/gtk.h>

static guint32 pixel_at(cairo_surface_t* s, int x, int y)
{
    cairo_surface_flush(s);
    unsigned char* d = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    return ((guint32*)(d + y*stride))[x];
}

int main(void)
{
    gtk_init();

    GtkCssProvider* p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p,
        ".probe-bg { background-color: rgb(255,0,255); }\n", -1);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(p),
                                               GTK_STYLE_PROVIDER_PRIORITY_USER);

    GtkWidget* win = gtk_window_new();
    GtkWidget* fixed = gtk_fixed_new();
    gtk_window_set_child(GTK_WINDOW(win), fixed);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(box, "probe-bg");
    gtk_widget_set_size_request(box, 60, 40);
    gtk_fixed_put(GTK_FIXED(fixed), box, 10, 10);

    /* mapped and allocated, or the snapshot is empty for reasons that have
       nothing to do with the question -- see
       gtk4-snapshot-mapped-vs-allocated.c */
    gtk_window_present(GTK_WINDOW(win));
    for ( int i = 0; i < 200 && g_main_context_iteration(NULL, FALSE); i++ )
        ;

    GtkSnapshot* sn = gtk_snapshot_new();
    gtk_widget_snapshot_child(fixed, box, sn);
    GskRenderNode* n = gtk_snapshot_free_to_node(sn);
    if ( !n )
    {
        g_print("no render node at all -- the widget was not drawable\n");
        return 1;
    }

    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 80, 60);
    cairo_t* cr = cairo_create(s);
    gsk_render_node_draw(n, cr);
    cairo_destroy(cr);

    const guint32 v = pixel_at(s, 30, 20);
    const gboolean painted = ((v >> 16) & 0xff) > 200 && (v & 0xff) > 200
                                && ((v >> 8) & 0xff) < 60;
    g_print("centre pixel = %08x -> GTK4 %s the widget's CSS background itself\n",
            v, painted ? "DOES paint" : "does NOT paint");

    return painted ? 0 : 1;
}
