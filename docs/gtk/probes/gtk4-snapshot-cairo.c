/* Can wx keep its cairo painting under GTK4 via gtk_snapshot_append_cairo()?
 * What coordinate space is the cairo_t in, and what does its clip say about
 * the damage region wx currently uses to skip work? */
#include <gtk/gtk.h>
#include <stdio.h>

#define MY_TYPE_AREA (my_area_get_type())
G_DECLARE_FINAL_TYPE(MyArea, my_area, MY, AREA, GtkWidget)
struct _MyArea { GtkWidget parent; };
G_DEFINE_TYPE(MyArea, my_area, GTK_TYPE_WIDGET)

static int snapshots;

static void my_area_snapshot(GtkWidget* w, GtkSnapshot* snap)
{
    snapshots++;
    const int width  = gtk_widget_get_width(w);
    const int height = gtk_widget_get_height(w);

    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, (float)width, (float)height);
    cairo_t* cr = gtk_snapshot_append_cairo(snap, &bounds);

    double x1, y1, x2, y2;
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);

    double dx = 0, dy = 0;
    cairo_user_to_device(cr, &dx, &dy);

    printf("  snapshot #%d: widget %dx%d\n", snapshots, width, height);
    printf("    cairo clip extents : %.1f,%.1f .. %.1f,%.1f\n", x1, y1, x2, y2);
    printf("    user (0,0) maps to device %.1f,%.1f  -> %s\n", dx, dy,
           (dx == 0 && dy == 0) ? "widget-relative" : "NOT widget-relative");

    /* Draw something so we can confirm it actually rasterises. */
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);
    cairo_destroy(cr);
}

static void my_area_class_init(MyAreaClass* k)
{ GTK_WIDGET_CLASS(k)->snapshot = my_area_snapshot; }
static void my_area_init(MyArea* a) { (void)a; }

int main(void)
{
    if (!gtk_init_check()) { printf("NO DISPLAY\n"); return 2; }

    GtkWidget* area = GTK_WIDGET(g_object_new(MY_TYPE_AREA, NULL));
    gtk_widget_set_size_request(area, 200, 100);

    /* The widget must actually be allocated before it can be snapshotted, so
     * put it in a window and let the main loop settle. */
    GtkWidget* win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(win), 200, 100);
    gtk_window_set_child(GTK_WINDOW(win), area);
    gtk_window_present(GTK_WINDOW(win));
    for (int i = 0; i < 200 && gtk_widget_get_width(area) == 0; i++)
        g_main_context_iteration(NULL, FALSE);

    printf("allocated: %dx%d\n",
           gtk_widget_get_width(area), gtk_widget_get_height(area));

    GdkPaintable* p = gtk_widget_paintable_new(area);
    GtkSnapshot* snap = gtk_snapshot_new();
    gdk_paintable_snapshot(p, snap, gtk_widget_get_width(area),
                                    gtk_widget_get_height(area));
    GskRenderNode* node = gtk_snapshot_free_to_node(snap);

    printf("render node produced: %s\n", node ? "yes" : "NO");
    if (node)
    {
        graphene_rect_t b;
        gsk_render_node_get_bounds(node, &b);
        printf("  node bounds: %.0fx%.0f at %.0f,%.0f\n",
               b.size.width, b.size.height, b.origin.x, b.origin.y);
        gsk_render_node_unref(node);
    }
    printf("snapshot vfunc called %d time(s)\n", snapshots);
    g_object_unref(p);
    return 0;
}
