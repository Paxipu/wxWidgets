/*
 * Regression tests for the GTK4 platform behaviour the wxGTK4 port relies on.
 *
 * These are not tests of wxWidgets code. They pin down assumptions about GTK
 * itself -- how its widget tree is shaped, how style resolution works, how
 * widget ownership behaves -- which the port depends on and which a GTK
 * upgrade could change silently. The failure mode being guarded against is
 * not a crash but a wrong number: if GtkNotebook's interior node structure
 * changes, wxGtkStyleContext keeps returning metrics, just the wrong ones.
 *
 * The reasoning behind each assumption is in docs/gtk/gtk4-stylecontext-design.md,
 * and the exploratory programs they came from are in docs/gtk/probes/.
 *
 * Deliberately asserts STRUCTURE, not pixel values. Exact metrics and colours
 * are theme-dependent, so asserting them would make this fail whenever CI's
 * theme differs rather than when something is actually wrong. Checks that are
 * inherently theme-dependent report but do not fail; they are marked SOFT.
 *
 * This is a standalone program rather than a Catch2 case in tests/ because it
 * needs only GTK, not libwx: the GUI test binary (test_gui) cannot link until
 * the GTK4 port compiles far enough, and these invariants are worth guarding
 * before then. Once test_gui links, this should move into the normal suite.
 *
 * Build and run (a display is required -- widgets need a GdkDisplay even
 * though they are never shown):
 *     gcc -o gtk4-invariants gtk4-invariants.c $(pkg-config --cflags --libs gtk4)
 *     xvfb-run -a ./gtk4-invariants
 *
 * Exits 0 if every hard invariant holds, 1 otherwise.
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

/* The gesture check needs to inject real clicks, which requires XTest. It is
 * optional: without it that one check reports SKIP rather than failing, so
 * this still builds and runs anywhere GTK4 does. */
#ifdef HAVE_XTEST
    #include <gdk/x11/gdkx.h>
    #include <X11/extensions/XTest.h>
#endif

static int g_failures = 0;
static int g_checks = 0;

static void check(int ok, const char* what, const char* detail)
{
    g_checks++;
    if (ok)
    {
        printf("  ok       %s\n", what);
    }
    else
    {
        g_failures++;
        printf("  FAILED   %s\n", what);
        if (detail && *detail)
            printf("           %s\n", detail);
    }
}

static void soft(int ok, const char* what, const char* detail)
{
    printf(ok ? "  ok       %s\n" : "  SOFT     %s\n", what);
    if (!ok && detail && *detail)
        printf("           %s\n", detail);
}

/* Depth-first descendant search by CSS name, matching what
 * wxGtkStyleContext::Descend() does. */
static GtkWidget* find_node(GtkWidget* parent, const char* name)
{
    for (GtkWidget* c = gtk_widget_get_first_child(parent);
         c; c = gtk_widget_get_next_sibling(c))
    {
        const char* s = gtk_widget_get_css_name(c);
        if (s && strcmp(s, name) == 0)
            return c;
        GtkWidget* found = find_node(c, name);
        if (found)
            return found;
    }
    return NULL;
}

/* Walk a chain of CSS node names, as the port's Add()/Descend() calls do. */
static GtkWidget* walk(GtkWidget* root, const char* const* names, int n)
{
    GtkWidget* cur = root;
    for (int i = 0; i < n; i++)
    {
        GtkWidget* next = find_node(cur, names[i]);
        if (!next)
            return NULL;
        cur = next;
    }
    return cur;
}

static void get_color(GtkWidget* w, GdkRGBA* out)
{
    gtk_style_context_get_color(gtk_widget_get_style_context(w), out);
}

/* ---------------------------------------------------------------------- */

/* The interior CSS nodes the port descends to must exist and be reachable as
 * real child widgets. This is the core of the GtkWidgetPath replacement: if
 * GTK renames or reshapes these, descents silently stop short and report a
 * different node's metrics. */
static void test_interior_nodes_reachable(void)
{
    printf("interior CSS nodes are reachable as child widgets\n");

    GtkWidget* nb = gtk_notebook_new();
    g_object_ref_sink(nb);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), gtk_label_new(""), gtk_label_new(""));
    static const char* const tab_chain[] = { "header", "tabs", "tab" };
    check(walk(nb, tab_chain, 3) != NULL,
          "notebook > header > tabs > tab   (notebook.cpp tab metrics)",
          "GtkNotebook's interior node names changed; tab sizing will be wrong");
    g_object_unref(nb);

    GtkWidget* sb = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, NULL);
    g_object_ref_sink(sb);
    static const char* const slider_chain[] = { "trough", "slider" };
    check(walk(sb, slider_chain, 2) != NULL,
          "scrollbar > ... > trough > slider  (settings.cpp scrollbar metrics)",
          "GtkScrollbar's interior node names changed");
    g_object_unref(sb);

    GtkWidget* cb = gtk_check_button_new();
    g_object_ref_sink(cb);
    check(find_node(cb, "check") != NULL,
          "checkbutton > check              (renderer.cpp checkbox metrics)",
          "GtkCheckButton's indicator node changed");
    g_object_unref(cb);

    GtkWidget* e = gtk_entry_new();
    g_object_ref_sink(e);
    check(find_node(e, "text") != NULL,
          "entry > text                     (renderer.cpp entry metrics)",
          "GtkEntry's text node changed");
    g_object_unref(e);
}

/* Style must resolve on widgets that are never realized, mapped, or shown.
 * The port queries theme metrics from throwaway widgets in headless contexts,
 * so if GTK ever required realization these queries would start returning
 * zeroes. */
static void test_resolves_without_realization(void)
{
    printf("style resolves on unrealized, unparented widgets\n");

    GtkWidget* f = gtk_frame_new(NULL);
    g_object_ref_sink(f);
    GtkBorder b;
    gtk_style_context_get_border(gtk_widget_get_style_context(f), &b);
    check(b.left > 0 || b.top > 0 || b.right > 0 || b.bottom > 0,
          "GtkFrame reports a non-zero border unrealized",
          "metrics now require realization, or the theme draws no frame border");

    int min = 0;
    gtk_widget_measure(f, GTK_ORIENTATION_HORIZONTAL, -1, &min, NULL, NULL, NULL);
    check(min >= 0, "gtk_widget_measure() works unrealized", NULL);
    g_object_unref(f);
}

/* The port attaches scratch hierarchies with the generic low-level
 * gtk_widget_set_parent(), because GTK4's type-specific child setters
 * (gtk_button_set_child() etc.) share no common base class. That is only
 * valid while both produce the same style resolution.
 *
 * Note this asserts the two are EQUAL rather than asserting any particular
 * colour: equality is theme-independent, whereas specific values are not. */
static void test_generic_parenting_matches_specific(void)
{
    printf("gtk_widget_set_parent() resolves identically to type-specific setters\n");

    GtkWidget* b1 = gtk_button_new();
    g_object_ref_sink(b1);
    GtkWidget* l1 = gtk_label_new("x");
    gtk_button_set_child(GTK_BUTTON(b1), l1);
    GdkRGBA c1;
    get_color(l1, &c1);

    GtkWidget* b2 = gtk_button_new();
    g_object_ref_sink(b2);
    GtkWidget* l2 = gtk_label_new("x");
    gtk_widget_set_parent(l2, b2);
    GdkRGBA c2;
    get_color(l2, &c2);

    char detail[160];
    snprintf(detail, sizeof(detail),
             "set_child gave %.3f,%.3f,%.3f but set_parent gave %.3f,%.3f,%.3f",
             c1.red, c1.green, c1.blue, c2.red, c2.green, c2.blue);
    check(gdk_rgba_equal(&c1, &c2),
          "label colour identical via set_child and set_parent", detail);

    check(gtk_widget_get_parent(l2) == b2,
          "gtk_widget_set_parent() establishes the parent link", NULL);

    gtk_widget_unparent(l2);
    g_object_unref(b1);
    g_object_unref(b2);
}

/* Structural mismatches the port deliberately relies on.
 *
 * wxGtkStyleContext::Descend() stays put when a node is missing, and
 * statbox.cpp depends on that: it asks for a "border" child of "frame", which
 * GTK4 does not have, and staying on "frame" yields the right numbers. If GTK
 * ever adds such a node, that call site starts reading a different node and
 * must be revisited -- so assert the absence explicitly rather than leaving it
 * as a silent assumption. */
static void test_known_structural_gaps(void)
{
    printf("structural gaps the port compensates for still hold\n");

    GtkWidget* f = gtk_frame_new(NULL);
    g_object_ref_sink(f);
    check(find_node(f, "border") == NULL,
          "GtkFrame still has NO 'border' child node",
          "GTK4 gained a frame>border node; statbox.cpp's fail-soft descent "
          "now lands somewhere new and its borders must be re-checked");
    g_object_unref(f);

    /* An empty notebook has no "tab" node, which is why the port appends a
     * page before querying tab metrics. */
    GtkWidget* nb = gtk_notebook_new();
    g_object_ref_sink(nb);
    check(find_node(nb, "tab") == NULL,
          "empty GtkNotebook still has no 'tab' node",
          "if this changes, PopulateForStyleQuery()'s page-append is redundant");
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), gtk_label_new(""), gtk_label_new(""));
    check(find_node(nb, "tab") != NULL,
          "appending a page makes the 'tab' node appear", NULL);
    g_object_unref(nb);
}

/* Widgets attached with gtk_widget_set_parent() are NOT released when the
 * parent is destroyed -- only a parent that knows about the child unparents it
 * in dispose. wxGtkStyleContext's destructor therefore unparents explicitly,
 * deepest first. This is easy to get wrong and leaks silently, so exercise the
 * exact create/destroy cycle the class performs. */
static void test_scratch_hierarchy_lifecycle(void)
{
    printf("scratch hierarchy create/destroy cycle is leak-free\n");

    /* First: hammer the cycle to shake out criticals/aborts under repetition. */
    const int cycles = 200;
    for (int i = 0; i < cycles; i++)
    {
        GtkWidget* root = NULL;
        GtkWidget* cur = NULL;
        GSList* created = NULL;

        const GType types[] = { GTK_TYPE_WINDOW, GTK_TYPE_BUTTON, GTK_TYPE_LABEL };
        for (int t = 0; t < 3; t++)
        {
            GtkWidget* w = GTK_WIDGET(g_object_new(types[t], NULL));
            if (!cur)
            {
                root = w;
                g_object_ref_sink(root);
            }
            else
            {
                gtk_widget_set_parent(w, cur);
            }
            created = g_slist_prepend(created, w);
            cur = w;
        }

        /* Mirror wxGtkStyleContext::~wxGtkStyleContext(): unparent everything
         * we created, deepest first, then release the root. */
        for (GSList* p = created; p; p = p->next)
        {
            GtkWidget* w = GTK_WIDGET(p->data);
            if (w != root)
                gtk_widget_unparent(w);
        }
        g_slist_free(created);

        if (root)
        {
            if (GTK_IS_WINDOW(root))
                gtk_window_destroy(GTK_WINDOW(root));
            g_object_unref(root);
        }
    }

    /* Then: build one more chain and track it with weak pointers, which is
     * what actually proves nothing was leaked. */
    GtkWidget* root = GTK_WIDGET(g_object_new(GTK_TYPE_WINDOW, NULL));
    g_object_ref_sink(root);
    GtkWidget* mid = GTK_WIDGET(g_object_new(GTK_TYPE_BUTTON, NULL));
    gtk_widget_set_parent(mid, root);
    GtkWidget* leaf = GTK_WIDGET(g_object_new(GTK_TYPE_LABEL, NULL));
    gtk_widget_set_parent(leaf, mid);

    g_object_add_weak_pointer(G_OBJECT(mid), (gpointer*)&mid);
    g_object_add_weak_pointer(G_OBJECT(leaf), (gpointer*)&leaf);

    gtk_widget_unparent(leaf);
    gtk_widget_unparent(mid);
    gtk_window_destroy(GTK_WINDOW(root));
    g_object_unref(root);

    check(leaf == NULL && mid == NULL,
          "explicit unparent (deepest first) frees the whole chain",
          "wxGtkStyleContext's destructor is leaking scratch widgets");
}

/* Bg()/Border() approximate the removed background-color/border-color queries
 * with theme-defined colour names. Those names are an Adwaita convention
 * rather than a guarantee, so this is SOFT: a theme legitimately need not
 * define them, and the port falls back when it doesn't. */
static void test_theme_colour_names(void)
{
    printf("theme colour names used by the Bg()/Border() approximation\n");

    GtkWidget* w = gtk_button_new();
    g_object_ref_sink(w);
    GtkStyleContext* sc = gtk_widget_get_style_context(w);
    GdkRGBA c;

    soft(gtk_style_context_lookup_color(sc, "theme_bg_color", &c),
         "theme defines 'theme_bg_color'",
         "Bg() falls back; window/button backgrounds may be off");
    soft(gtk_style_context_lookup_color(sc, "theme_base_color", &c),
         "theme defines 'theme_base_color'",
         "Bg() falls back for list/text backgrounds");
    soft(gtk_style_context_lookup_color(sc, "theme_selected_bg_color", &c),
         "theme defines 'theme_selected_bg_color'",
         "Bg() falls back for selection backgrounds");
    soft(gtk_style_context_lookup_color(sc, "borders", &c),
         "theme defines 'borders'",
         "Border() falls back; border colours may be off");

    g_object_unref(w);
}

#ifdef HAVE_XTEST

/* Claiming a gesture sequence is what lets wx consume a click so a native
 * control doesn't also act on it -- and, less obviously, what determines
 * whether wx receives the *release* at all. Measured, not assumed: see
 * docs/gtk/probes/gtk4-gesture-semantics.c and the comment on
 * wx_gtk_button_pressed_callback() in src/gtk/window.cpp.
 *
 * If GTK ever changes this, wx clicks break in ways no compile check sees,
 * which is exactly what this is here to catch. */
static int gest_pressed, gest_released, native_clicked;
static gboolean gest_claim;
static GtkWidget *gest_win, *gest_button;

static void gest_on_pressed(GtkGestureClick* g, int n, double x, double y, gpointer d)
{
    (void)n; (void)x; (void)y; (void)d;
    gest_pressed++;
    if (gest_claim)
        gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED);
}
static void gest_on_released(GtkGestureClick* g, int n, double x, double y, gpointer d)
{ (void)g;(void)n;(void)x;(void)y;(void)d; gest_released++; }
static void gest_on_clicked(GtkButton* b, gpointer d)
{ (void)b;(void)d; native_clicked++; }

static gboolean gest_inject(gpointer d)
{
    (void)d;
    GdkSurface* s = gtk_native_get_surface(gtk_widget_get_native(gest_win));
    if (!GDK_IS_X11_SURFACE(s))
        return G_SOURCE_REMOVE;

    Display* dpy = GDK_SURFACE_XDISPLAY(s);
    Window xw = GDK_SURFACE_XID(s);

    graphene_rect_t b;
    if (!gtk_widget_compute_bounds(gest_button, gest_win, &b))
        return G_SOURCE_REMOVE;

    Window child; int rx = 0, ry = 0;
    XTranslateCoordinates(dpy, xw, DefaultRootWindow(dpy),
                          (int)(b.origin.x + b.size.width / 2),
                          (int)(b.origin.y + b.size.height / 2),
                          &rx, &ry, &child);

    XTestFakeMotionEvent(dpy, -1, rx, ry, 0); XFlush(dpy);
    XTestFakeButtonEvent(dpy, 1, True, CurrentTime); XFlush(dpy);
    XTestFakeButtonEvent(dpy, 1, False, CurrentTime); XFlush(dpy);
    return G_SOURCE_REMOVE;
}
static gboolean gest_finish(gpointer d)
{ (void)d; gtk_window_destroy(GTK_WINDOW(gest_win)); return G_SOURCE_REMOVE; }

static void gest_run(gboolean claim)
{
    gest_claim = claim;
    gest_pressed = gest_released = native_clicked = 0;

    gest_win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(gest_win), 300, 200);
    gest_button = gtk_button_new_with_label("target");
    gtk_window_set_child(GTK_WINDOW(gest_win), gest_button);
    g_signal_connect(gest_button, "clicked", G_CALLBACK(gest_on_clicked), NULL);

    GtkGesture* g = gtk_gesture_click_new();
    g_signal_connect(g, "pressed", G_CALLBACK(gest_on_pressed), NULL);
    g_signal_connect(g, "released", G_CALLBACK(gest_on_released), NULL);
    gtk_widget_add_controller(gest_button, GTK_EVENT_CONTROLLER(g));

    gtk_window_present(GTK_WINDOW(gest_win));
    g_timeout_add(500, gest_inject, NULL);
    g_timeout_add(2000, gest_finish, NULL);

    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    g_signal_connect_swapped(gest_win, "destroy", G_CALLBACK(g_main_loop_quit), loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
}

static void test_gesture_claim_semantics(void)
{
    printf("GtkGestureClick claim semantics (real injected clicks)\n");

    gest_run(FALSE);
    const int noclaim_press = gest_pressed, noclaim_release = gest_released,
              noclaim_native = native_clicked;

    if (!noclaim_press)
    {
        /* Injection didn't land -- no window manager, pointer grabbed, XTest
         * refused. Skip rather than report a failure we can't attribute. */
        soft(0, "click injection did not reach the window; gesture checks skipped",
             "not a GTK behaviour change, an environment limitation");
        return;
    }

    gest_run(TRUE);

    check(noclaim_native == 1,
          "not claiming lets the native control act",
          "wx would no longer be able to let a click through to a native control");
    check(native_clicked == 0,
          "claiming suppresses the native control",
          "wx can no longer consume a click; GTK3's 'handler returned TRUE' is lost");
    check(gest_released == 1,
          "claiming delivers the release",
          "press/release pairing is broken even when wx claims the sequence");

    /* Not a failure -- this is the documented GTK4 behaviour the port works
     * around -- but if it ever changes, window.cpp's comment and the residual
     * gap it describes should be revisited. */
    soft(noclaim_release == 0,
         "unclaimed press on a native control still yields no release (as documented)",
         "GTK now delivers it; the caveat in window.cpp can be dropped");
}

#endif /* HAVE_XTEST */

int main(void)
{
    if (!gtk_init_check())
    {
        fprintf(stderr,
                "gtk4-invariants: no display available; run under xvfb-run.\n");
        return 77; /* conventional "skipped" status */
    }

    printf("GTK4 platform invariants for the wxGTK4 port "
           "(GTK %u.%u.%u)\n\n",
           gtk_get_major_version(), gtk_get_minor_version(),
           gtk_get_micro_version());

    test_interior_nodes_reachable();
    printf("\n");
    test_resolves_without_realization();
    printf("\n");
    test_generic_parenting_matches_specific();
    printf("\n");
    test_known_structural_gaps();
    printf("\n");
    test_scratch_hierarchy_lifecycle();
    printf("\n");
    test_theme_colour_names();
#ifdef HAVE_XTEST
    printf("\n");
    test_gesture_claim_semantics();
#else
    printf("\n(built without XTest: gesture claim semantics not checked)\n");
#endif

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    if (g_failures)
    {
        printf("\nA failure here means GTK's own behaviour changed, not that "
               "wxWidgets code is broken.\nSee docs/gtk/gtk4-stylecontext-design.md "
               "for what each assumption is used for.\n");
    }
    return g_failures ? 1 : 0;
}
