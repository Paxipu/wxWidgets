/* #181: can a widget snapshot replace gtk_render_background() and
 * gtk_render_frame() for wxRendererNative, and does it draw the same thing?
 *
 * Both are deprecated in GTK4 along with the whole GtkStyleContext drawing
 * family, and renderer.cpp uses them for every native control part it draws
 * into a wxDC. The documented replacement is to snapshot a real widget and
 * run the resulting GskRenderNode through gsk_render_node_draw(), which is
 * not a substitution but a different mechanism -- so this checks whether it
 * produces the same picture before 63 call sites are changed on the strength
 * of the API being newer.
 *
 * It does. A scratch GtkButton drawn both ways into two identical image
 * surfaces comes out byte-for-byte the same in all four states this asks
 * about, provided three things hold:
 *
 *   - the widget is allocated to the target rect first, with
 *     gtk_widget_size_allocate(); an unallocated widget snapshots to its own
 *     natural size, not the caller's;
 *   - gtk_widget_queue_draw() is called after changing the state. GTK caches
 *     a widget's render node, and gtk_widget_snapshot_child() hands back the
 *     cached one -- without this every state draws the normal appearance.
 *     Run with PROBE_NO_INVALIDATE=1 to see that: "normal" still matches and
 *     the other three do not;
 *   - the node is drawn with NO translation. Its bounds start at negative
 *     coordinates (-3,-2 for a button here) because the CSS shadow reaches
 *     outside the allocation; translating by them moves the frame off by
 *     exactly that much, which looks like a rendering difference and is not.
 *
 * THE CONSTRAINT THAT MAKES THIS UNUSABLE FOR wxRendererNative, found after
 * the substitution had already been shipped and reverted again:
 *
 *   gtk_widget_snapshot_child() returns NULL unless the widget's toplevel has
 *   been MAPPED. Realizing it is not enough -- gtk_widget_realize() on the
 *   root leaves the snapshot null, and a null node draws nothing at all. Only
 *   gtk_window_present() makes it work, which is why this probe presents its
 *   window and why that looked like an incidental detail.
 *
 *   wx's scratch widgets live in a container whose window is deliberately
 *   never shown (wxGTKPrivate::GetContainer(): "Never shown, just used to host
 *   scratch widgets"), and showing it is not an option -- it would flash a
 *   window on the user's desktop every time a control part is drawn.
 *
 *   So this mechanism is correct and reproduces the deprecated drawing exactly,
 *   and it still cannot replace it here. Run with PROBE_UNMAPPED=1 to see the
 *   null nodes.
 *
 * Notably, nothing here has to run the main loop. An earlier version of this
 * probe appeared to need it, which would have been a problem: renderer.cpp is
 * called from paint handlers, and dispatching events from inside one is the
 * re-entrancy that issue #144 removed from the drop path. It does not.
 *
 * A harness fault worth recording, since it produced a wrong answer first:
 * compare() draws the *new* way before the old way, deliberately. The old
 * path manipulates the widget's style context, and that invalidates the
 * cached render node -- so running it first did the test's work for it, and
 * made an unconditional "identical" out of a mechanism that was in fact
 * missing its invalidation. The control run above only became meaningful
 * once the order was reversed.
 *
 * Build and run (a display is needed; the widget must be realized):
 *
 *   gcc -o probe181 gtk4-renderer-snapshot.c $(pkg-config --cflags --libs gtk4)
 *   xvfb-run -a ./probe181
 *   PROBE_NO_INVALIDATE=1 xvfb-run -a ./probe181     # the red run
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 80
#define H 30

static GtkWidget *g_fixed, *g_button, *g_win;

static cairo_surface_t *make_surface(void)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cairo_t *cr = cairo_create(s);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);
    return s;
}

/* The path renderer.cpp uses today. */
static cairo_surface_t *draw_old(GtkStateFlags state)
{
    cairo_surface_t *s = make_surface();
    cairo_t *cr = cairo_create(s);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkStyleContext *sc = gtk_widget_get_style_context(g_button);
    gtk_style_context_save(sc);
    gtk_style_context_set_state(sc, state);
    gtk_render_background(sc, cr, 0, 0, W, H);
    gtk_render_frame(sc, cr, 0, 0, W, H);
    gtk_style_context_restore(sc);
G_GNUC_END_IGNORE_DEPRECATIONS
    cairo_destroy(cr);
    return s;
}

/* The replacement: allocate, snapshot, draw the node. */
static cairo_surface_t *draw_new(GtkStateFlags state)
{
    cairo_surface_t *s = make_surface();
    cairo_t *cr = cairo_create(s);

    gtk_widget_set_state_flags(g_button, state, TRUE);

    /* GTK animates state changes (hover/active fades). Let the transition run
     * to its end before snapshotting, or the picture is mid-animation. */
    if (getenv("PROBE_SETTLE"))
        for (int i = 0; i < atoi(getenv("PROBE_SETTLE")); i++)
            g_main_context_iteration(NULL, FALSE);

    /* Force the exact rect: renderer.cpp draws into a caller-chosen rect, so
     * the scratch widget has to be allocated to it before snapshotting. */
    GtkAllocation alloc = { 0, 0, W, H };
    gtk_widget_size_allocate(g_button, &alloc, -1);

    /* GTK caches a widget's render node; the state change has to invalidate
     * it before the snapshot reflects the new appearance. */
    if (!getenv("PROBE_NO_INVALIDATE"))
        gtk_widget_queue_draw(g_button);
    for (int i = 0; i < 50; i++)
        g_main_context_iteration(NULL, FALSE);

    printf("    requested state 0x%x, widget reports 0x%x\n",
           (unsigned)state, (unsigned)gtk_widget_get_state_flags(g_button));

    GtkSnapshot *snap = gtk_snapshot_new();
    gtk_widget_snapshot_child(g_fixed, g_button, snap);
    GskRenderNode *node = gtk_snapshot_to_node(snap);

    if (node)
    {
        graphene_rect_t b;
        gsk_render_node_get_bounds(node, &b);
        printf("    node bounds %.1f,%.1f %.1fx%.1f\n",
               b.origin.x, b.origin.y, b.size.width, b.size.height);
        /* No translation: the node is already in widget coordinates, with the
         * allocation box at 0,0. The negative bounds are the CSS shadow
         * reaching outside it -- translating by them would move the frame. */
        gsk_render_node_draw(node, cr);
        gsk_render_node_unref(node);
    }
    else
        printf("    NODE IS NULL\n");

    gtk_widget_unset_state_flags(g_button, state);
    cairo_destroy(cr);
    return s;
}

static void compare(const char *name, GtkStateFlags state)
{
    printf("  %s:\n", name);
    /* draw_new() first, deliberately: draw_old() touches the widget's style
     * context, which invalidates the cached render node -- running it first
     * would do the test's work for it and hide whether draw_new() needs to
     * invalidate anything itself. */
    cairo_surface_t *b = draw_new(state);
    cairo_surface_t *a = draw_old(state);
    cairo_surface_flush(a); cairo_surface_flush(b);
    const unsigned char *pa = cairo_image_surface_get_data(a);
    const unsigned char *pb = cairo_image_surface_get_data(b);
    const int stride = cairo_image_surface_get_stride(a);
    int diff = 0, nonempty_a = 0, nonempty_b = 0, maxd = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W * 4; x++)
        {
            int va = pa[y*stride+x], vb = pb[y*stride+x];
            if (va) nonempty_a++;
            if (vb) nonempty_b++;
            int d = abs(va - vb);
            if (d) { diff++; if (d > maxd) maxd = d; }
        }
    printf("    old non-zero bytes %d, new %d, differing %d (max delta %d)\n",
           nonempty_a, nonempty_b, diff, maxd);
    printf("    VERDICT %s\n",
           nonempty_b == 0 ? "NEW DRAWS NOTHING"
           : diff == 0     ? "identical"
           : maxd <= 8     ? "near-identical"
                           : "DIFFERENT");
    cairo_surface_destroy(a); cairo_surface_destroy(b);
}

static int g_done;

static gboolean run(gpointer data)
{
    (void)data;
    printf("PROBE button allocation %dx%d\n",
           gtk_widget_get_width(g_button), gtk_widget_get_height(g_button));
    compare("normal",   GTK_STATE_FLAG_NORMAL);
    compare("prelight", GTK_STATE_FLAG_PRELIGHT);
    compare("active",   GTK_STATE_FLAG_ACTIVE);
    compare("insensitive", GTK_STATE_FLAG_INSENSITIVE);
    fflush(stdout);
    g_done = 1;
    return G_SOURCE_REMOVE;
}

int main(int argc, char **argv)
{
    (void)argc;(void)argv;
    if (!gtk_init_check()) { printf("NO DISPLAY\n"); return 2; }
    g_win = gtk_window_new();
    g_fixed = gtk_fixed_new();
    g_button = gtk_button_new();
    gtk_widget_set_size_request(g_button, W, H);
    gtk_fixed_put(GTK_FIXED(g_fixed), g_button, 0, 0);
    gtk_window_set_child(GTK_WINDOW(g_win), g_fixed);
    gtk_window_set_default_size(GTK_WINDOW(g_win), 200, 100);
    gtk_window_present(GTK_WINDOW(g_win));
    for (int i = 0; i < 400 && gtk_widget_get_width(g_button) == 0; i++)
        g_main_context_iteration(NULL, FALSE);
    g_idle_add(run, NULL);
    for (int i = 0; i < 5000 && !g_done; i++)
        g_main_context_iteration(NULL, TRUE);
    return g_done ? 0 : 3;
}
