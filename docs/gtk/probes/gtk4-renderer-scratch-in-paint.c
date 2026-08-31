/* The remaining risk from the previous probe: wxRendererNative draws from
   inside a paint, which under GTK4 is inside the container's snapshot vfunc.
   Doing the allocate-and-snapshot dance there is the part that could still be
   refused.

   This is the design wx would actually use: the themed widget is parented
   once and kept, never added or removed during a draw. The container simply
   does not paint it, and asks it for a render node when it needs one. */
#include <gtk/gtk.h>

static GtkWidget *scratch;          /* the themed widget, parented but unpainted */
static int draws, nodes_ok, nodes_null;
static guint32 last_hash;

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

/* Stands in for wxRendererNative::DrawPushButton(): called while painting,
   given a cairo_t that belongs to the paint in progress. */
static void draw_themed_button(cairo_t *cr, int x, int y, int w, int h,
                               GtkStateFlags state)
{
    gtk_widget_set_state_flags(scratch, state, TRUE);
    gtk_widget_allocate(scratch, w, h, -1, NULL);

    GtkSnapshot *snap = gtk_snapshot_new();
    gtk_widget_snapshot_child(gtk_widget_get_parent(scratch), scratch, snap);
    GskRenderNode *node = gtk_snapshot_free_to_node(snap);

    if ( node )
    {
        nodes_ok++;
        cairo_save(cr);
        cairo_translate(cr, x, y);
        gsk_render_node_draw(node, cr);
        cairo_restore(cr);
        gsk_render_node_unref(node);
    }
    else
        nodes_null++;

    gtk_widget_unset_state_flags(scratch, state);
}

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer d)
{
    (void)area; (void)d;
    draws++;

    /* Three buttons in three states, exactly what wxRendererNative is asked
       for when a generic control paints a header or a set of buttons. */
    draw_themed_button(cr,  10, 10, 100, 30, (GtkStateFlags)0);
    draw_themed_button(cr, 120, 10, 100, 30, GTK_STATE_FLAG_PRELIGHT);
    draw_themed_button(cr, 230, 10, 100, 30, GTK_STATE_FLAG_ACTIVE);

    cairo_surface_t *s = cairo_get_target(cr);
    if ( cairo_surface_get_type(s) == CAIRO_SURFACE_TYPE_IMAGE )
        last_hash = hash_pixels(s, w, h);
}

int main(void)
{
    gtk_init();

    GtkWidget *win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 200);

    GtkWidget *fixed = gtk_fixed_new();
    gtk_window_set_child(GTK_WINDOW(win), fixed);

    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 400, 100);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, NULL, NULL);
    gtk_fixed_put(GTK_FIXED(fixed), area, 0, 0);

    /* The scratch widget: parented so it has a display and a style, put far
       outside the visible area so nothing shows it. wx would instead skip it
       in its own container's snapshot. */
    scratch = gtk_button_new();
    gtk_fixed_put(GTK_FIXED(fixed), scratch, -10000, -10000);

    gtk_window_present(GTK_WINDOW(win));
    for ( int i = 0; i < 3000 && !gtk_widget_get_mapped(win); i++ )
        g_main_context_iteration(NULL, FALSE);

    GdkFrameClock *clock = gtk_widget_get_frame_clock(win);
    for ( int i = 0; i < 30; i++ )
    {
        if ( clock )
            gdk_frame_clock_request_phase(clock, GDK_FRAME_CLOCK_PHASE_PAINT);
        for ( int j = 0; j < 300 && g_main_context_pending(NULL); j++ )
            g_main_context_iteration(NULL, FALSE);
        gtk_widget_queue_draw(area);
    }

    g_print("draw func ran %d times\n", draws);
    g_print("render nodes: %d obtained, %d null\n", nodes_ok, nodes_null);
    g_print("VERDICT %s\n",
            draws > 0 && nodes_ok > 0 && nodes_null == 0
              ? "allocate+snapshot works from inside a paint, with a widget "
                "that is parented once and never reparented"
              : "NOT usable this way");
    return 0;
}
