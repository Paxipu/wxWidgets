/* Reopening #181 properly.

   wxRendererNative::DrawPushButton(wxWindow* win, wxDC& dc, ...) is handed the
   window it is drawing into, and during a paint that window is mapped. My
   earlier reading was that the scratch container wx snapshots from is never
   shown -- true -- and I stopped there instead of asking whether the scratch
   widget could live somewhere that IS mapped.

   So: put the widget inside a real, mapped window, allocate it, snapshot it,
   take it out again. Does that give the same picture as the deprecated
   gtk_render_background()/gtk_render_frame()? */
#include <gtk/gtk.h>

/* Counting non-transparent pixels cannot tell a colour change from no change
   at all, and prelight *is* a colour change. Hash the pixels instead. */
static guint32 hash_pixels(cairo_surface_t *s, int w, int h)
{
    cairo_surface_flush(s);
    unsigned char *d = cairo_image_surface_get_data(s);
    int stride = cairo_image_surface_get_stride(s);
    guint32 h32 = 2166136261u;
    for ( int y = 0; y < h; y++ )
        for ( int x = 0; x < w; x++ )
        {
            guint32 px = ((guint32*)(d + y*stride))[x];
            h32 = (h32 ^ (px & 0xffffffffu)) * 16777619u;
        }
    return h32;
}

static int count_drawn(cairo_surface_t *s, int w, int h)
{
    cairo_surface_flush(s);
    unsigned char *d = cairo_image_surface_get_data(s);
    int stride = cairo_image_surface_get_stride(s), n = 0;
    for ( int y = 0; y < h; y++ )
        for ( int x = 0; x < w; x++ )
            if ( ((uint32_t*)(d + y*stride))[x] & 0x00ffffff )
                n++;
    return n;
}

static GtkWidget *win, *fixed;

static void pump(int frames)
{
    GdkFrameClock *clock = gtk_widget_get_frame_clock(win);
    for ( int i = 0; i < frames; i++ )
    {
        if ( clock )
            gdk_frame_clock_request_phase(clock, GDK_FRAME_CLOCK_PHASE_PAINT);
        for ( int j = 0; j < 300 && g_main_context_pending(NULL); j++ )
            g_main_context_iteration(NULL, FALSE);
    }
}

static int snapshot_widget(GtkWidget *w, int width, int height, const char *what)
{
    GtkSnapshot *snap = gtk_snapshot_new();
    gtk_widget_snapshot_child(fixed, w, snap);
    GskRenderNode *node = gtk_snapshot_free_to_node(snap);

    cairo_surface_t *surf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(surf);
    if ( node )
    {
        gsk_render_node_draw(node, cr);
        gsk_render_node_unref(node);
    }
    cairo_destroy(cr);
    const int n = count_drawn(surf, width, height);
    const guint32 hash = hash_pixels(surf, width, height);
    cairo_surface_destroy(surf);

    g_print("  %-38s node=%-4s pixels=%d hash=%08x\n",
            what, node ? "yes" : "NULL", n, hash);
    return (int)hash;
}

int main(void)
{
    gtk_init();

    win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 200);
    fixed = gtk_fixed_new();
    gtk_window_set_child(GTK_WINDOW(win), fixed);
    gtk_window_present(GTK_WINDOW(win));
    for ( int i = 0; i < 3000 && !gtk_widget_get_mapped(win); i++ )
        g_main_context_iteration(NULL, FALSE);
    pump(20);
    g_print("host window mapped: %d\n\n", gtk_widget_get_mapped(win));

    /* This is what wx would do inside a Draw* call: put the themed widget in
       the window it was handed, size it to the rectangle, snapshot, remove. */
    GtkWidget *button = gtk_button_new();
    gtk_fixed_put(GTK_FIXED(fixed), button, 0, 0);
    gtk_widget_set_size_request(button, 100, 30);
    pump(5);

    /* The warning said "without a current allocation", not "not mapped". A
       layout manager gives a widget its allocation with gtk_widget_allocate(),
       and nothing stops wx doing the same. */
    gtk_widget_allocate(button, 100, 30, -1, NULL);
    pump(5);
    g_print("child mapped: %d, allocated %dx%d\n",
            gtk_widget_get_mapped(button),
            gtk_widget_get_width(button), gtk_widget_get_height(button));

    g_print("\nsnapshotting a child of a mapped window:\n");
    const int normal = snapshot_widget(button, 100, 30, "normal");

    gtk_widget_set_state_flags(button, GTK_STATE_FLAG_PRELIGHT, TRUE);
    gtk_widget_queue_draw(button);
    pump(5);
    const int prelight = snapshot_widget(button, 100, 30, "prelight");

    gtk_widget_unset_state_flags(button, GTK_STATE_FLAG_PRELIGHT);
    gtk_widget_set_state_flags(button, GTK_STATE_FLAG_ACTIVE, TRUE);
    gtk_widget_queue_draw(button);
    pump(5);
    const int active = snapshot_widget(button, 100, 30, "active");

    /* The deprecated path, same widget, for comparison. */
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_widget_unset_state_flags(button, GTK_STATE_FLAG_ACTIVE);
    GtkStyleContext *sc = gtk_widget_get_style_context(button);
    cairo_surface_t *surf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 100, 30);
    cairo_t *cr = cairo_create(surf);
    gtk_render_background(sc, cr, 0, 0, 100, 30);
    gtk_render_frame(sc, cr, 0, 0, 100, 30);
    cairo_destroy(cr);
    const int old = count_drawn(surf, 100, 30);
    const guint32 oldhash = hash_pixels(surf, 100, 30);
    cairo_surface_destroy(surf);
    G_GNUC_END_IGNORE_DEPRECATIONS
    g_print("\n  %-38s pixels=%d hash=%08x\n",
            "deprecated gtk_render_* (same widget)", old, oldhash);
    g_print("  same picture as the snapshot of normal: %s\n",
            (guint32)normal == oldhash ? "YES" : "no (theme draws the widget "
            "differently from the bare style context, which is expected)");

    g_print("\nVERDICT %s\n",
            normal != 0
              ? "a child of a MAPPED window snapshots -- #181 is not blocked"
              : "still nothing: the child has to be mapped too");
    g_print("        states differ: prelight %s normal, active %s normal\n",
            prelight != normal ? "!=" : "== (NO GOOD)",
            active   != normal ? "!=" : "== (NO GOOD)");

    return 0;
}
