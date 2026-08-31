/* The question the earlier probes conflated: is a MAPPED toplevel actually
   required, or was the whole problem the missing allocation?
   wx's scratch container is never shown, so if allocation alone is enough,
   #181 is a small change rather than a large one. */
#include <gtk/gtk.h>

static guint32 hash_pixels(cairo_surface_t *s, int w, int h)
{
    cairo_surface_flush(s);
    unsigned char *d = cairo_image_surface_get_data(s);
    int stride = cairo_image_surface_get_stride(s);
    guint32 h32 = 2166136261u;
    for ( int y = 0; y < h; y++ )
        for ( int x = 0; x < w; x++ )
            h32 = (h32 ^ ((guint32*)(d + y*stride))[x]) * 16777619u;
    return h32;
}

static void try_it(const char *what, gboolean present, gboolean realize)
{
    GtkWidget *win = gtk_window_new();
    GtkWidget *fixed = gtk_fixed_new();
    gtk_window_set_child(GTK_WINDOW(win), fixed);

    GtkWidget *button = gtk_button_new();
    gtk_fixed_put(GTK_FIXED(fixed), button, 0, 0);

    if ( present )
    {
        gtk_window_present(GTK_WINDOW(win));
        for ( int i = 0; i < 2000 && !gtk_widget_get_mapped(win); i++ )
            g_main_context_iteration(NULL, FALSE);
    }
    else if ( realize )
    {
        gtk_widget_realize(win);
    }

    gtk_widget_allocate(button, 100, 30, -1, NULL);

    GtkSnapshot *snap = gtk_snapshot_new();
    gtk_widget_snapshot_child(fixed, button, snap);
    GskRenderNode *node = gtk_snapshot_free_to_node(snap);

    guint32 hash = 0;
    if ( node )
    {
        cairo_surface_t *s =
            cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 30);
        cairo_t *cr = cairo_create(s);
        gsk_render_node_draw(node, cr);
        cairo_destroy(cr);
        hash = hash_pixels(s, 100, 30);
        cairo_surface_destroy(s);
        gsk_render_node_unref(node);
    }

    g_print("  %-40s mapped=%d realized=%d node=%-4s hash=%08x\n",
            what, gtk_widget_get_mapped(win), gtk_widget_get_realized(win),
            node ? "yes" : "NULL", hash);

    gtk_window_destroy(GTK_WINDOW(win));
}

int main(void)
{
    gtk_init();
    g_print("does the toplevel have to be mapped?\n");
    try_it("never shown, never realized", FALSE, FALSE);
    try_it("realized but never mapped",   FALSE, TRUE);
    try_it("presented and mapped",        TRUE,  FALSE);
    return 0;
}
