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
/* wxGTKLookupThemeColour() reads a theme's named colours through CSS, because
 * GTK4 has no call that does it: it sets "color" to the name on a widget of
 * its own and reads back what GTK computed. Two properties of GTK make that
 * work, and neither is documented, so both are pinned here. See
 * docs/gtk/probes/gtk4-theme-colour-probe.c. */

#define WX_COLOUR_PROBE_CLASS "wx-colour-probe"

static void probe_colour(const char* css, GdkRGBA* out)
{
    GtkCssProvider* p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p, css, -1);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(p),
        GTK_STYLE_PROVIDER_PRIORITY_USER);

    /* After the provider, deliberately: see the invariant below. */
    GtkWidget* w = gtk_label_new("");
    g_object_ref_sink(w);
    gtk_widget_add_css_class(w, WX_COLOUR_PROBE_CLASS);
    gtk_widget_get_color(w, out);
    g_object_unref(w);

    gtk_style_context_remove_provider_for_display(gdk_display_get_default(),
                                                  GTK_STYLE_PROVIDER(p));
    g_object_unref(p);
}

static gboolean probe_lookup(const char* name, GdkRGBA* out)
{
    static const struct { const char* before; const char* after; } expr[] =
    {
        { "",     ""                    },
        { "mix(", ", rgb(0,255,0), 0.5)" },
        { "mix(", ", rgb(255,0,0), 0.5)" }
    };

    GdkRGBA answer[G_N_ELEMENTS(expr)];
    for (guint i = 0; i < G_N_ELEMENTS(expr); i++)
    {
        char* css = g_strdup_printf("." WX_COLOUR_PROBE_CLASS
                                    " { color: %s@%s%s; }",
                                    expr[i].before, name, expr[i].after);
        probe_colour(css, &answer[i]);
        g_free(css);
    }

    for (guint i = 1; i < G_N_ELEMENTS(answer); i++)
    {
        if (!gdk_rgba_equal(&answer[0], &answer[i]))
        {
            *out = answer[0];
            return TRUE;
        }
    }

    return FALSE;
}

static void test_theme_colour_names(void)
{
    printf("theme colour names used by the Bg()/Border() approximation\n");

    /* A widget that is never shown has its style computed once, on demand:
     * installing a provider afterwards does not invalidate it. This is the
     * same GTK behaviour as issue #245, where a wxStaticText measured by
     * SetFont() while hidden keeps the style that measurement computed and
     * never takes the colour set after it.
     *
     * The probe relies on it by creating its widget last -- if GTK started
     * invalidating, reusing a widget would still be wrong, so this is only
     * reported. What would break the probe is the opposite: a fresh widget
     * not seeing the provider, which is the second half of the check. */
    {
        GtkWidget* w = gtk_label_new("");
        g_object_ref_sink(w);
        gtk_widget_add_css_class(w, WX_COLOUR_PROBE_CLASS);

        GdkRGBA before, after, fresh;
        gtk_widget_get_color(w, &before);

        GtkCssProvider* p = gtk_css_provider_new();
        gtk_css_provider_load_from_data(
            p, "." WX_COLOUR_PROBE_CLASS " { color: rgb(1,2,3); }", -1);
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(), GTK_STYLE_PROVIDER(p),
            GTK_STYLE_PROVIDER_PRIORITY_USER);
        gtk_widget_get_color(w, &after);
        gtk_style_context_remove_provider_for_display(
            gdk_display_get_default(), GTK_STYLE_PROVIDER(p));
        g_object_unref(p);
        g_object_unref(w);

        probe_colour("." WX_COLOUR_PROBE_CLASS " { color: rgb(1,2,3); }",
                     &fresh);

        soft(gdk_rgba_equal(&before, &after),
             "an off-screen widget's style survives a provider being added",
             "GTK now invalidates these; the probe still works, but reusing "
             "a widget would no longer be silently wrong");

        const GdkRGBA wanted = { 1/255.0, 2/255.0, 3/255.0, 1.0 };
        check(gdk_rgba_equal(&fresh, &wanted),
              "a widget created after a provider is styled by it",
              "wxGTKLookupThemeColour() would read every colour as the "
              "unstyled default");
    }

    /* An undefined name is substituted silently rather than reported, so the
     * probe tells the two apart by asking through several expressions: a name
     * that resolves answers differently through each, one that does not
     * answers the same. */
    static const char* const defined[] =
    {
        "theme_fg_color", "theme_bg_color", "theme_base_color",
        "theme_selected_bg_color", "borders"
    };

    for (guint i = 0; i < G_N_ELEMENTS(defined); i++)
    {
        GdkRGBA css_answer, control;
        const gboolean found = probe_lookup(defined[i], &css_answer);

        GtkWidget* w = gtk_button_new();
        g_object_ref_sink(w);
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        const gboolean known = gtk_style_context_lookup_color(
            gtk_widget_get_style_context(w), defined[i], &control);
        G_GNUC_END_IGNORE_DEPRECATIONS
        g_object_unref(w);

        char* what = g_strdup_printf("theme defines '%s'", defined[i]);
        soft(found, what, "Bg()/Border() falls back for it");
        g_free(what);

        /* Whether the theme has the name is the theme's business; that both
         * ways of asking give the same answer is not. */
        what = g_strdup_printf("CSS and lookup_color() agree about '%s'",
                               defined[i]);
        check(found == known &&
                  (!found || gdk_rgba_equal(&css_answer, &control)),
              what,
              "the CSS probe no longer reproduces the deprecated call");
        g_free(what);
    }

    GdkRGBA unused;
    check(!probe_lookup("wx_no_such_colour", &unused),
          "a name no theme defines is reported as missing",
          "callers with a fallback would silently take the substituted "
          "colour instead");
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

/* ------------------------------------------------------------------------
 * Menus.
 *
 * The GTK4 menu backend in src/gtk/menu.cpp is built on four behaviours of
 * GTK's own menu model machinery. None of them is a pixel or a metric, so
 * they are all hard checks. See docs/gtk/gtk4-phase-menu-design.md.
 * ------------------------------------------------------------------------ */

static int g_namedActionRan = 0;

static void on_named_action(GSimpleAction* a, GVariant* p, gpointer d)
{
    (void)a; (void)p; (void)d;
    g_namedActionRan++;
}

static void on_radio_state(GSimpleAction* a, GVariant* value, gpointer d)
{
    char** seen = d;
    g_free(*seen);
    *seen = g_strdup(g_variant_get_string(value, NULL));
    g_simple_action_set_state(a, value);
}

static void test_menu_model_mechanics(void)
{
    GtkWidget* win;
    GtkWidget* box;
    GtkWidget* menubar;
    GSimpleActionGroup* group;
    GSimpleAction* act;
    GSimpleAction* radio;
    GtkEventController* shortcuts;
    GtkShortcut* shortcut;
    GMenu* model;
    GMenu* bar;
    GMenuItem* item;
    GVariant* accel;
    char* radioSeen = NULL;
    int i;

    printf("Menu model mechanics:\n");

    win = gtk_window_new();
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(win), box);

    group = g_simple_action_group_new();

    act = g_simple_action_new("i1", NULL);
    g_signal_connect(act, "activate", G_CALLBACK(on_named_action), NULL);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act));

    radio = g_simple_action_new_stateful("r1", G_VARIANT_TYPE_STRING,
                                         g_variant_new_string("0"));
    g_signal_connect(radio, "change-state", G_CALLBACK(on_radio_state),
                     &radioSeen);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(radio));

    /* wxMenu::GTKInstallActions() installs the group on the frame; the
     * shortcut controller for the accelerators is attached to the same widget
     * and must be able to resolve names against it. If this stops holding,
     * every menu accelerator silently stops working. */
    gtk_widget_insert_action_group(win, "wxm0", G_ACTION_GROUP(group));

    shortcuts = gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(shortcuts),
                                      GTK_SHORTCUT_SCOPE_GLOBAL);
    gtk_widget_add_controller(win, shortcuts);

    shortcut = gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_q,
                                                       GDK_CONTROL_MASK),
                                gtk_named_action_new("wxm0.i1"));
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(shortcuts),
                                         shortcut);

    gtk_window_present(GTK_WINDOW(win));
    for (i = 0; i < 200; i++)
        g_main_context_iteration(NULL, FALSE);

    gtk_shortcut_action_activate(gtk_shortcut_get_action(shortcut),
                                 GTK_SHORTCUT_ACTION_EXCLUSIVE, win, NULL);
    for (i = 0; i < 50; i++)
        g_main_context_iteration(NULL, FALSE);

    check(g_namedActionRan == 1,
          "named action resolves against an inserted action group",
          "menu accelerators cannot reach their items any more");

    /* wxMenu::GTKRebuildModel() puts the accelerator text into this attribute
     * instead of into the label, which is where GTK3 kept it. */
    model = g_menu_new();
    item = g_menu_item_new("_Quit", "wxm0.i1");
    g_menu_item_set_attribute(item, "accel", "s", "<Control>q");
    g_menu_append_item(model, item);
    g_object_unref(item);

    accel = g_menu_model_get_item_attribute_value(G_MENU_MODEL(model), 0,
                                                  "accel",
                                                  G_VARIANT_TYPE_STRING);
    check(accel && strcmp(g_variant_get_string(accel, NULL), "<Control>q") == 0,
          "the accel attribute round-trips through a GMenu",
          "accelerators are no longer displayed next to menu item labels");
    if (accel)
        g_variant_unref(accel);

    /* Radio groups are one stateful action shared by the group, with each
     * member identified by its target: GTK only reports which target was
     * activated, and wxMenu::GTKOnRadioSelected() maps it back to the item. */
    g_action_group_activate_action(G_ACTION_GROUP(group), "r1",
                                   g_variant_new_string("2"));
    for (i = 0; i < 20; i++)
        g_main_context_iteration(NULL, FALSE);

    check(radioSeen && strcmp(radioSeen, "2") == 0,
          "a stateful action reports the activated target",
          "radio menu items can no longer tell which one was selected");
    g_free(radioSeen);

    /* Every structural change rebuilds the model wholesale rather than
     * patching it, which happens underneath a menu bar that is already
     * showing that model. */
    bar = g_menu_new();
    g_menu_append_submenu(bar, "_File", G_MENU_MODEL(model));
    menubar = gtk_popover_menu_bar_new_from_model(G_MENU_MODEL(bar));
    gtk_box_append(GTK_BOX(box), menubar);
    for (i = 0; i < 100; i++)
        g_main_context_iteration(NULL, FALSE);

    g_menu_remove_all(model);
    g_menu_append(model, "Something else", "wxm0.i1");
    g_menu_remove(bar, 0);
    g_menu_append_submenu(bar, "_Renamed", G_MENU_MODEL(model));
    for (i = 0; i < 100; i++)
        g_main_context_iteration(NULL, FALSE);

    check(gtk_widget_get_realized(menubar),
          "a live menu bar survives its model being emptied and refilled",
          "menus cannot be modified after being shown");

    g_object_unref(model);
    g_object_unref(bar);
    gtk_window_destroy(GTK_WINDOW(win));
}

/* ------------------------------------------------------------------------
 * Toolbars.
 *
 * GTK4 removed GtkToolbar and every GtkToolItem subclass, so src/gtk/toolbar.cpp
 * builds a toolbar from a GtkBox and plain buttons. Two properties of that
 * substitution are load-bearing and neither is obvious.
 * ------------------------------------------------------------------------ */

static void test_toolbar_substitutes(void)
{
    GtkWidget* win;
    GtkWidget* box;
    GtkWidget* b[3];
    int i, active;

    printf("Toolbar building blocks:\n");

    win = gtk_window_new();
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(box, "toolbar");
    gtk_window_set_child(GTK_WINDOW(win), box);

    for (i = 0; i < 3; i++)
    {
        b[i] = gtk_toggle_button_new();
        if (i)
        {
            gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b[i]),
                                        GTK_TOGGLE_BUTTON(b[0]));
        }
        gtk_box_append(GTK_BOX(box), b[i]);
    }

    gtk_window_present(GTK_WINDOW(win));
    for (i = 0; i < 100; i++)
        g_main_context_iteration(NULL, FALSE);

    /* This is the one that bites. GTK3's gtk_radio_tool_button_new() activated
     * the first button of a group by itself, and wxToolBar relied on it; GTK4's
     * grouped toggle buttons do not, so toolbar.cpp activates it explicitly. If
     * GTK ever starts doing it again, that becomes a double activation. */
    check(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b[0])),
          "a grouped toggle button is NOT active by default",
          "wxToolBar now activates the first radio tool twice");

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b[1]), TRUE);
    for (i = 0; i < 50; i++)
        g_main_context_iteration(NULL, FALSE);

    active = 0;
    for (i = 0; i < 3; i++)
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b[i])))
            active++;

    check(active == 1,
          "grouped toggle buttons are mutually exclusive",
          "radio tools no longer deselect each other");

    /* GtkBox only inserts relative to a sibling, so toolbar.cpp walks the
     * children to find one; that walk assumes stable first/next-sibling order. */
    check(gtk_widget_get_first_child(box) == b[0] &&
          gtk_widget_get_next_sibling(b[0]) == b[1],
          "GtkBox children enumerate in insertion order",
          "inserting a tool at a given position puts it in the wrong place");

    gtk_window_destroy(GTK_WINDOW(win));
}

/* ------------------------------------------------------------------------
 * Clipboard.
 *
 * GTK4 replaced the X11-style selection protocol with GdkClipboard, whose
 * reads are asynchronous, while wxClipboard::GetData() is synchronous. The
 * port bridges that with a nested main loop, which is only sound because the
 * whole read -- including draining the GInputStream -- stays asynchronous and
 * is pumped by that one loop.
 *
 * This matters more than it looks. When the clipboard is locally owned, the
 * writer feeding that stream is our own GdkContentProvider, running on the
 * same main context. Draining the stream with a BLOCKING splice therefore
 * deadlocks, and does so unrecoverably: the loop is blocked inside the read
 * callback, so it cannot even dispatch a timeout to rescue itself.
 *
 * Only the working pattern is asserted here -- deliberately, since asserting
 * the broken one would mean hanging CI. If GTK ever changes so that the async
 * pattern stops completing, the watchdog below turns it into a failure rather
 * than a hung build.
 * ------------------------------------------------------------------------ */

typedef struct {
    GMainLoop* loop;
    GOutputStream* out;
    GBytes* bytes;
    int timedout;
} ClipboardRead;

static gboolean clipboard_watchdog(gpointer data)
{
    ClipboardRead* r = data;
    r->timedout = 1;
    g_main_loop_quit(r->loop);
    return G_SOURCE_REMOVE;
}

static void clipboard_spliced(GObject* src, GAsyncResult* res, gpointer data)
{
    ClipboardRead* r = data;
    g_output_stream_splice_finish(G_OUTPUT_STREAM(src), res, NULL);
    r->bytes = g_memory_output_stream_steal_as_bytes(G_MEMORY_OUTPUT_STREAM(src));
    g_main_loop_quit(r->loop);
}

static void clipboard_read_done(GObject* src, GAsyncResult* res, gpointer data)
{
    ClipboardRead* r = data;
    GInputStream* stream = gdk_clipboard_read_finish(GDK_CLIPBOARD(src), res,
                                                     NULL, NULL);
    if (!stream)
    {
        g_main_loop_quit(r->loop);
        return;
    }

    /* Async, not g_output_stream_splice(): see the comment above. */
    r->out = g_memory_output_stream_new_resizable();
    g_output_stream_splice_async(r->out, stream,
                                 G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
                                 G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
                                 G_PRIORITY_DEFAULT, NULL,
                                 clipboard_spliced, r);
    g_object_unref(stream);
}

static void test_clipboard_sync_bridge(void)
{
    GdkClipboard* cb;
    GdkContentProvider* provider;
    GBytes* bytes;
    ClipboardRead r;
    const char* mimes[2];
    guint watchdog;
    gsize size = 0;
    const char* data;
    int i;

    printf("Clipboard synchronous bridge:\n");

    cb = gdk_display_get_clipboard(gdk_display_get_default());

    bytes = g_bytes_new_static("hello", 5);
    provider = gdk_content_provider_new_for_bytes("application/x-wx-invariant",
                                                  bytes);
    g_bytes_unref(bytes);

    check(gdk_clipboard_set_content(cb, provider),
          "a content provider can be put on the clipboard",
          "wxClipboard cannot offer data at all");
    g_object_unref(provider);

    for (i = 0; i < 100; i++)
        g_main_context_iteration(NULL, FALSE);

    check(gdk_content_formats_contain_mime_type(gdk_clipboard_get_formats(cb),
                                                "application/x-wx-invariant"),
          "the clipboard advertises the offered format",
          "wxClipboard::IsSupported() cannot see its own data");

    memset(&r, 0, sizeof(r));
    r.loop = g_main_loop_new(NULL, FALSE);
    watchdog = g_timeout_add(5000, clipboard_watchdog, &r);

    mimes[0] = "application/x-wx-invariant";
    mimes[1] = NULL;
    gdk_clipboard_read_async(cb, mimes, G_PRIORITY_DEFAULT, NULL,
                             clipboard_read_done, &r);
    g_main_loop_run(r.loop);

    if (!r.timedout)
        g_source_remove(watchdog);

    check(!r.timedout,
          "an async clipboard read completes inside a nested main loop",
          "wxClipboard::GetData() can no longer be made synchronous this way");

    data = r.bytes ? g_bytes_get_data(r.bytes, &size) : NULL;
    check(data && size == 5 && memcmp(data, "hello", 5) == 0,
          "the data read back is what was offered",
          "clipboard round-trip is lossy");

    if (r.bytes)
        g_bytes_unref(r.bytes);
    if (r.out)
        g_object_unref(r.out);
    g_main_loop_unref(r.loop);

    /* wxDataFormat compares formats by pointer, which only works because an
     * interned string is canonical -- the property GdkAtom used to provide. */
    check(g_intern_string("text/html") == g_intern_string("text/html"),
          "g_intern_string() returns a canonical pointer per string",
          "wxDataFormat can no longer compare formats by pointer");
}

/* ------------------------------------------------------------------------
 * Indicator metrics.
 *
 * GTK4 removed style properties and the varargs gtk_style_context_get() that
 * read CSS min-width/min-height, so renderer.cpp sizes check and radio
 * indicators by measuring real widgets instead.
 *
 * That only works while the widget being measured is the right KIND, and the
 * distinction is subtle: a GtkCheckButton's indicator node is "check", but
 * putting it in a group turns it into "radio". Measuring an ungrouped button
 * for a radio indicator would silently return check box metrics -- no error,
 * no crash, just the wrong size. wxGTKPrivate::GetRadioButtonWidget() is
 * grouped specifically for this.
 * ------------------------------------------------------------------------ */

static const char* indicator_node_name(GtkWidget* button)
{
    GtkWidget* child = gtk_widget_get_first_child(button);
    return child ? gtk_widget_get_css_name(child) : "";
}

/* src/gtk/choice.cpp has to reach the widget inside a GtkComboBoxText which
 * actually receives pointer events, because the combo box itself does not.
 * It does so by walking up two levels from gtk_combo_box_get_child(), which
 * is exactly what the GTK3 code did through gtk_bin_get_child().  None of the
 * widgets in between are public API, so nothing but a test keeps us honest if
 * GTK rearranges them.
 *
 * See docs/gtk/probes/gtk4-combobox-tree.c for the tree this was derived from.
 */
static void test_combo_box_internals(void)
{
    GtkWidget* combo;
    GtkWidget* child;
    GtkWidget* parent;
    GtkWidget* grandparent;

    printf("GtkComboBoxText internal structure:\n");

    combo = gtk_combo_box_text_new();
    g_object_ref_sink(combo);

    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "item");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

    child = gtk_combo_box_get_child(GTK_COMBO_BOX(combo));
    check(child != NULL,
          "gtk_combo_box_get_child() reports the cell view of a text combo",
          "choice.cpp has no way in to the combo box internals at all");

    check(child != NULL && GTK_IS_CELL_VIEW(child),
          "that child is a GtkCellView, as it was under GTK3",
          "choice.cpp's DoGetSizeFromTextSize() distinguishes cell view from "
          "entry to tell wxChoice from wxComboBox");

    parent = child ? gtk_widget_get_parent(child) : NULL;
    grandparent = parent ? gtk_widget_get_parent(parent) : NULL;

    check(grandparent != NULL && GTK_IS_TOGGLE_BUTTON(grandparent),
          "the cell view's grandparent is the GtkToggleButton",
          "choice.cpp would attach its motion controller to the wrong widget "
          "and wxChoice would stop reporting enter/leave events");

    /* Negative control: the intermediate widget is not itself the button, so
     * a one-level walk really would be wrong rather than accidentally right. */
    check(parent != NULL && !GTK_IS_TOGGLE_BUTTON(parent),
          "one level up is not already the toggle button",
          "the two-level walk in choice.cpp may be off by one");

    g_object_unref(combo);
}

/* --------------------------------------------------------------------------
 * Two things about GTK4 which are not written down anywhere and which cost
 * this port real, silent misbehaviour before running the test suite exposed
 * them. See docs/gtk/probes/gtk4-css-parser.c.
 * -------------------------------------------------------------------------- */

static int g_cssErrors;

static void on_css_error(GtkCssProvider*, GtkCssSection*, const GError*, gpointer)
{
    g_cssErrors++;
}

static int css_parses(const char* css)
{
    GtkCssProvider* const p = gtk_css_provider_new();

    g_cssErrors = 0;
    g_signal_connect(p, "parsing-error", G_CALLBACK(on_css_error), NULL);
    gtk_css_provider_load_from_data(p, css, -1);
    g_object_unref(p);

    return g_cssErrors == 0;
}

static void test_css_parser_strictness(void)
{
    printf("CSS parser strictness:\n");

    check(!css_parses("*{color:rgb(0,0,0)}"),
          "a block's last declaration must end with a semicolon",
          "if GTK4 has relaxed this, wxGTKLoadCssData() in window.cpp is "
          "inserting semicolons it no longer needs to -- harmless, but the "
          "comment there is then wrong");

    /* Positive control: the same declaration, terminated. */
    check(css_parses("*{color:rgb(0,0,0);}"),
          "the same declaration parses once terminated",
          "something other than the semicolon is being rejected here");

    check(!css_parses("*{font:Sans 10;}"),
          "the font shorthand rejects a size with no unit",
          "wxWindowGTK::GTKApplyWidgetStyle() works around this by dropping "
          "the shorthand when the description has no size");

    check(css_parses("*{font:10pt Sans;}"),
          "the same font parses with a unit on the size",
          "the font shorthand is being rejected for some other reason");
}

static void test_version_check_semantics(void)
{
    printf("gtk_check_version() against GTK3 requirements:\n");

    /* This is the one that hurt: about sixty guards in src/gtk mean "do we
     * have at least this GTK3 feature level", and gtk_check_version() answers
     * "no" to every one of them under GTK4 because it compares the major
     * version for equality rather than for order. gtk3-compat.h shims it. */
    check(gtk_check_version(3, 22, 0) != NULL,
          "gtk_check_version() reports GTK4 as NOT satisfying a GTK 3.x requirement",
          "if GTK4 has changed this to an ordering comparison, the shim in "
          "gtk3-compat.h is redundant and should be removed rather than left "
          "to disagree with the library");

    check(gtk_check_version(4, 0, 0) == NULL,
          "and does report a GTK 4.0 requirement as satisfied",
          "the shim must keep passing GTK4 requirements through unchanged");
}

/* ------------------------------------------------------------------------ *
 * Layout managers take over from the class vfuncs.
 * ------------------------------------------------------------------------ */

static int g_probeMeasureCalls;

typedef struct { GtkFixed parent; } WxProbeFixed;
typedef struct { GtkFixedClass parent; } WxProbeFixedClass;

static void wx_probe_measure(GtkWidget* w, GtkOrientation o, int f,
                             int* mi, int* na, int* bl1, int* bl2)
{
    (void)w; (void)o; (void)f; (void)bl1; (void)bl2;
    g_probeMeasureCalls++;
    if (mi) *mi = 50;
    if (na) *na = 50;
}

static void wx_probe_class_init(gpointer klass, gpointer data)
{
    (void)data;
    GTK_WIDGET_CLASS(klass)->measure = wx_probe_measure;
}

static GType wx_probe_fixed_type(void)
{
    static GType t;
    if (!t)
    {
        const GTypeInfo i = { sizeof(WxProbeFixedClass), NULL, NULL,
                              wx_probe_class_init, NULL, NULL,
                              sizeof(WxProbeFixed), 0, NULL, NULL };
        t = g_type_register_static(GTK_TYPE_FIXED, "WxProbeFixed", &i,
                                   (GTypeFlags)0);
    }
    return t;
}

static void test_layout_manager_dispatch(void)
{
    GtkWidget* f;
    GtkLayoutManager* lm;
    int measuredWithLayout, measuredWithout;
    int mi = 0;

    printf("Layout manager vs. class vfunc dispatch:\n");

    /* This is what made wxPizza's entire layout implementation dead code for
     * the whole port: gtk_widget_measure() and gtk_widget_allocate() call a
     * widget's GtkLayoutManager *instead of* GTK_WIDGET_GET_CLASS()->measure /
     * ->size_allocate when one is installed, and GtkFixed -- wxPizza's base
     * class -- installs GtkFixedLayout.  Every wx child window was allocated
     * 0x0 by GtkFixedLayout, and nothing warned.  win_gtk.cpp therefore calls
     * gtk_widget_set_layout_manager(widget, NULL) in wxPizza::New().
     * See docs/gtk/probes/gtk4-layout-manager.c. */

    f = GTK_WIDGET(g_object_new(wx_probe_fixed_type(), NULL));
    g_object_ref_sink(f);

    lm = gtk_widget_get_layout_manager(f);
    check(lm != NULL && GTK_IS_FIXED_LAYOUT(lm),
          "GtkFixed still installs a GtkFixedLayout on its subclasses",
          "if GtkFixed no longer has a layout manager, clearing it in "
          "wxPizza::New() is a no-op and the comment there is misleading");

    g_probeMeasureCalls = 0;
    gtk_widget_measure(f, GTK_ORIENTATION_HORIZONTAL, -1, &mi, NULL, NULL, NULL);
    measuredWithLayout = g_probeMeasureCalls;

    check(measuredWithLayout == 0,
          "with a layout manager, the class measure() vfunc is NOT called",
          "GTK now calls both; wxPizza may be measuring twice");

    gtk_widget_set_layout_manager(f, NULL);
    g_probeMeasureCalls = 0;
    gtk_widget_measure(f, GTK_ORIENTATION_HORIZONTAL, -1, &mi, NULL, NULL, NULL);
    measuredWithout = g_probeMeasureCalls;

    check(measuredWithout > 0,
          "clearing the layout manager restores the class measure() vfunc",
          "wxPizza's own measure()/size_allocate() are unreachable, and every "
          "wx child window will be laid out at 0x0");

    check(mi == 50,
          "and the value the class vfunc reports is the one GTK uses",
          "the vfunc runs but its result is discarded");

    g_object_unref(f);
}

static void test_entry_caret_reporting(void)
{
    GtkWidget* entry;
    GtkEditable* ed;
    GtkEditable* del;
    int cur = -1, bound = -1;

    printf("Where a GtkEntry reports its caret:\n");

    entry = gtk_entry_new();
    g_object_ref_sink(entry);
    ed = GTK_EDITABLE(entry);
    gtk_editable_set_text(ed, "0123456789");

    del = gtk_editable_get_delegate(ed);
    check(del != NULL && GTK_IS_TEXT(del),
          "a GtkEntry delegates its GtkEditable to a GtkText",
          "textentry.cpp reads the caret off the delegate; without one it "
          "falls back to the GtkEntry, which does not report it");

    /* wxTextEntry::SetSelection() passes the range backwards on purpose, so
     * that the caret ends up at the *start* of the selection as wx and MSW
     * require rather than at its end.  GtkText honours that.  GtkEntry does
     * not report it: it answers out of the selection instead. */
    gtk_editable_select_region(ed, 4, 2);

    check(del && gtk_editable_get_position(del) == 2,
          "select_region(4,2) leaves the GtkText caret at 2",
          "the trick wxTextEntry::SetSelection() relies on to put the caret "
          "at the start of the selection no longer works at all");

    g_object_get(ed, "cursor-position", &cur, "selection-bound", &bound, NULL);
    check(gtk_editable_get_position(ed) != 2 || cur != 2 || bound != 4,
          "and GtkEntry itself still does not simply agree with it",
          "GtkEntry now reports the caret correctly, so the delegate detour "
          "in wxTextEntry::GetInsertionPoint() can be dropped");

    g_object_unref(entry);
}

/* ------------------------------------------------------------------------ *
 * Layout and painting happen on the frame clock, not synchronously.
 * ------------------------------------------------------------------------ */

/* Ask for a frame and run the main loop until the clock has produced one.
   This is exactly what wxWindow::Update() does under GTK4. */
static gboolean pump_one_frame(GtkWidget* w)
{
    GdkFrameClock* clock = gtk_widget_get_frame_clock(w);
    gint64 frame, deadline;

    if (!clock)
        return FALSE;

    frame = gdk_frame_clock_get_frame_counter(clock);
    deadline = g_get_monotonic_time() + 2000000; /* 2s */

    gdk_frame_clock_request_phase(clock, GDK_FRAME_CLOCK_PHASE_PAINT);

    while (gdk_frame_clock_get_frame_counter(clock) == frame &&
           g_get_monotonic_time() < deadline)
    {
        if (!g_main_context_iteration(NULL, FALSE))
            g_usleep(1000);
    }

    return gdk_frame_clock_get_frame_counter(clock) != frame;
}

static void test_frame_clock_layout(void)
{
    GtkWidget* win;
    GtkWidget* fixed;
    GtkWidget* child;
    int i;
    int widthRightAfterShow;

    printf("Layout is deferred to the frame clock:\n");

    win = gtk_window_new();
    fixed = gtk_fixed_new();
    child = gtk_button_new_with_label("x");
    gtk_widget_set_size_request(child, 100, 50);
    gtk_fixed_put(GTK_FIXED(fixed), child, 10, 90);
    gtk_window_set_child(GTK_WINDOW(win), fixed);
    gtk_window_set_default_size(GTK_WINDOW(win), 300, 300);
    gtk_widget_set_visible(win, TRUE);

    /* Give the window a chance to be mapped and laid out once. */
    for (i = 0; i < 5 && gtk_widget_get_width(child) == 0; i++)
        pump_one_frame(win);

    check(gtk_widget_get_width(child) > 0,
          "a mapped window's children are allocated after a frame",
          "the pump loop below cannot test anything, and wxWindow::Update() "
          "has nothing to wait for");

    /* Hiding and re-showing a child is the case that broke wx: the widget's
     * allocation is stale from the moment it is shown until the clock next
     * ticks, and running the main loop -- what wxYield() does -- does not
     * bring it forward. wxWindow::Update() pumps until the frame arrives, and
     * ClientToScreen() does not ask GTK for positions at all. */
    gtk_widget_set_visible(child, FALSE);
    pump_one_frame(win);
    gtk_widget_set_visible(child, TRUE);

    for (i = 0; i < 20; i++)
        g_main_context_iteration(NULL, FALSE);

    widthRightAfterShow = gtk_widget_get_width(child);

    soft(widthRightAfterShow == 0,
         "a re-shown child is NOT allocated by running the main loop alone",
         "GTK now lays out synchronously again, or the clock happened to tick "
         "during those iterations -- this is timing-dependent, so it is only "
         "reported, not failed");

    check(pump_one_frame(win),
          "requesting the paint phase and pumping does produce a frame",
          "wxWindow::Update() would spin to its deadline and then return "
          "without the window having been laid out or painted");

    check(gtk_widget_get_width(child) > 0,
          "and the re-shown child is allocated once that frame has been drawn",
          "waiting for a frame is no longer enough to settle the layout, and "
          "wxWindow::Update() cannot keep its promise");

    gtk_window_destroy(GTK_WINDOW(win));
}

static void test_focus_flags(void)
{
    GtkWidget* win;
    GtkWidget* box;
    GtkWidget* a;
    GtkWidget* b;

    printf("Focusable vs. can-focus, and what happens when the focus dies:\n");

    win = gtk_window_new();
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(win), box);
    gtk_widget_set_visible(win, TRUE);
    pump_one_frame(win);

    /* GTK4 split GTK3's single "can-focus" flag in two and kept the old name
     * for the half wx does not mean by it. Every gtk_widget_set_can_focus()
     * call wx inherited was therefore a no-op, and every
     * gtk_widget_get_can_focus() test answered TRUE for everything -- so no
     * wxWindow could take the focus at all. See wx_gtk_widget_set_focusable()
     * in gtk3-compat.h. */
    a = gtk_fixed_new();
    gtk_box_append(GTK_BOX(box), a);

    check(gtk_widget_get_can_focus(a) && !gtk_widget_get_focusable(a),
          "a container defaults to can-focus=TRUE, focusable=FALSE",
          "the two flags no longer differ in the way the port relies on");

    gtk_widget_set_can_focus(a, TRUE);
    gtk_widget_grab_focus(a);
    check(!gtk_widget_is_focus(a),
          "gtk_widget_set_can_focus() alone does NOT make grab_focus() work",
          "can-focus is enough again, so the focusable shim is redundant");

    gtk_widget_set_focusable(a, TRUE);
    gtk_widget_grab_focus(a);
    check(gtk_widget_is_focus(a),
          "gtk_widget_set_focusable() is what makes grab_focus() work",
          "wxWindow::SetFocus() cannot work at all");

    /* And the behaviour that follows from it, which GTK3 did not have: when
     * the widget holding the focus goes away, GTK4 does not simply drop the
     * focus. It remembers that the window needs one and gives it to the next
     * focusable widget added, even one added before the next frame -- so a
     * newly created wxWindow can find itself focused without wx ever asking.
     * gtk_window_set_focus(NULL) does not cancel it, and a widget with
     * can-focus=FALSE is skipped but then cannot be focused explicitly
     * either, so there is nothing wx can do about it. */
    gtk_box_remove(GTK_BOX(box), a);

    b = gtk_button_new_with_label("b");
    gtk_box_append(GTK_BOX(box), b);

    pump_one_frame(win);

    soft(gtk_window_get_focus(GTK_WINDOW(win)) == b,
         "GTK4 moves the focus to a widget added after the focused one died",
         "if this stops happening, the spurious wxEVT_SET_FOCUS a freshly "
         "created wxWindow can receive is gone and the note in "
         "docs/gtk/gtk4-status.md can go with it");

    gtk_window_destroy(GTK_WINDOW(win));
}

/* ------------------------------------------------------------------------ */

static int g_sliderWarnings;

/* GTK4 emits its warnings through g_log_structured(), which does not consult
 * the handlers g_log_set_handler() installs -- so a library cannot filter
 * them, and only an application (this one) can, by taking over the writer. */
static GLogWriterOutput log_writer(GLogLevelFlags level,
                                   const GLogField* fields,
                                   gsize n_fields,
                                   gpointer user_data)
{
    gsize i;

    for (i = 0; i < n_fields; i++)
    {
        if (strcmp(fields[i].key, "MESSAGE") == 0 && fields[i].value != NULL &&
            strstr((const char*)fields[i].value, "reported min ") != NULL)
        {
            g_sliderWarnings++;
            return G_LOG_WRITER_HANDLED;
        }
    }

    return g_log_writer_default(level, fields, n_fields, user_data);
}

static void test_scrollbar_metric(void)
{
    GtkWidget* sb;
    GtkWidget* win;
    GtkWidget* sw;
    GtkCssProvider* css;
    int min = 0, nat = 0;
    int before;

    printf("Scrollbar width metric, and the theme-driven slider warning:\n");

    /* GetScrollbarWidth() in settings.cpp measures a scrollbar that is in no
     * window at all: GTK4 removed the min-width query the GTK3 code summed
     * node by node down the scrollbar's CSS tree. */
    sb = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, NULL);
    g_object_ref_sink(sb);
    gtk_widget_measure(sb, GTK_ORIENTATION_HORIZONTAL, -1,
                       &min, &nat, NULL, NULL);
    check(min > 0,
          "an unparented GtkScrollbar measures a usable width",
          "wxSYS_VSCROLL_X has nothing left to derive the scrollbar width "
          "from");
    g_object_unref(sb);

    /* A theme that zeroes the slider node's minimum size and its border makes
     * GTK's own slider measurement come out negative, which GTK reports as
     * "GtkGizmo (slider) reported min width -2".  It is produced inside
     * GtkRange, needs no wxWidgets in the process to happen, and cannot be
     * suppressed from a library -- see log_writer() above.  This check exists
     * so that the next person to see that warning in a wx sample does not go
     * looking for it in wx. */
    css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(
        css, "slider { min-width: 0; min-height: 0; border: 0; }", -1);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    before = g_sliderWarnings;

    win = gtk_window_new();
    sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_ALWAYS, GTK_POLICY_ALWAYS);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw),
                                  gtk_label_new("x"));
    gtk_window_set_child(GTK_WINDOW(win), sw);
    gtk_widget_set_visible(win, TRUE);
    pump_one_frame(win);

    soft(g_sliderWarnings > before,
         "a plain GtkScrolledWindow reports a negative slider size under such "
         "a theme",
         "GTK no longer produces it, so this warning appearing in a wx sample "
         "would need looking at again");

    gtk_window_destroy(GTK_WINDOW(win));
    gtk_style_context_remove_provider_for_display(gdk_display_get_default(),
                                                  GTK_STYLE_PROVIDER(css));
    g_object_unref(css);
}

/* ------------------------------------------------------------------------ */

typedef struct
{
    const char* name;
    int is_focus;
    int contains_focus;
    int has_focus;
    int state_focused;
    int root_focus_known;
} FocusReport;

static FocusReport g_lastEnter;

static void on_focus_enter(GtkEventControllerFocus* c, gpointer name)
{
    GtkWidget* const w =
        gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(c));
    GtkRoot* const root = gtk_widget_get_root(w);

    g_lastEnter.name = (const char*)name;
    g_lastEnter.is_focus = gtk_event_controller_focus_is_focus(c);
    g_lastEnter.contains_focus = gtk_event_controller_focus_contains_focus(c);
    g_lastEnter.has_focus = gtk_widget_has_focus(w);
    g_lastEnter.state_focused =
        (gtk_widget_get_state_flags(w) & GTK_STATE_FLAG_FOCUSED) != 0;
    g_lastEnter.root_focus_known =
        root != NULL && gtk_root_get_focus(root) != NULL;
}

static void test_focus_controller_scope(void)
{
    GtkWidget* win;
    GtkWidget* box;
    GtkWidget* entry;
    GtkEventController* c;

    printf("What a focus controller knows when it says the focus arrived:\n");

    win = gtk_window_new();
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    entry = gtk_entry_new();
    gtk_box_append(GTK_BOX(box), entry);
    gtk_window_set_child(GTK_WINDOW(win), box);

    /* The container, not the widget that will take the focus. */
    c = gtk_event_controller_focus_new();
    g_signal_connect(c, "enter", G_CALLBACK(on_focus_enter), (gpointer)"box");
    gtk_widget_add_controller(box, c);

    memset(&g_lastEnter, 0, sizeof(g_lastEnter));
    gtk_widget_set_visible(win, TRUE);
    pump_one_frame(win);

    /* GTK3 delivered focus-in-event only to the widget that took the focus.
     * GTK4's controller reports the focus entering the widget or any of its
     * descendants, so wxWindowGTK's handler runs for a container when a
     * control inside it is focused -- which is where the "lost focus even
     * though it didn't have it" debug message in window.cpp comes from. */
    check(g_lastEnter.name != NULL && g_lastEnter.contains_focus,
          "a container's focus controller reports focus entering a descendant",
          "GTK4 no longer notifies ancestors, so wx could stop filtering");

    /* And none of the obvious ways of telling the two apart works, because
     * none of this is settled yet when ::enter is emitted. Any fix has to
     * decide later -- at idle, or from the root's notify::focus-widget. */
    soft(!g_lastEnter.is_focus && !g_lastEnter.has_focus &&
             !g_lastEnter.state_focused && !g_lastEnter.root_focus_known,
         "nothing says which widget it is yet: no is-focus, no state flag, "
         "no root focus",
         "GTK4 now settles the focus before ::enter, so window.cpp could "
         "filter the ancestor notifications there and stop reporting a "
         "spurious wxEVT_SET_FOCUS for containers");

    gtk_window_destroy(GTK_WINDOW(win));
}

/* ------------------------------------------------------------------------ */

/* Count the pixels a drawing operation actually put on a surface. */
static int painted_pixels(cairo_surface_t* surface)
{
    const unsigned char* data;
    int w, h, stride, x, y, n = 0;

    cairo_surface_flush(surface);
    data = cairo_image_surface_get_data(surface);
    w = cairo_image_surface_get_width(surface);
    h = cairo_image_surface_get_height(surface);
    stride = cairo_image_surface_get_stride(surface);

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            if (data[y * stride + x * 4 + 3] != 0)
                n++;

    return n;
}

static void test_drop_arrow_drawing(void)
{
    GtkWidget* button;
    GtkIconTheme* theme;
    GtkIconPaintable* icon;
    cairo_surface_t* surface;
    cairo_t* cr;
    int arrowPixels, iconPixels = 0;

    printf("Drawing a drop-down arrow:\n");

    /* gtk_render_arrow() is still declared -- among GTK4's deprecated
     * functions -- so wxRendererGTK::DrawDropArrow() went on calling it and
     * went on compiling. It draws nothing at all now, though: a GTK4 theme
     * puts the arrow of a combo box in a node of its own and paints it as an
     * icon, so there is nothing for a button's style context to render. Every
     * wxComboCtrl drop-down button came up blank because of it. */
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 32, 32);
    cr = cairo_create(surface);
    button = gtk_button_new();
    g_object_ref_sink(button);
    gtk_render_arrow(gtk_widget_get_style_context(button), cr, G_PI, 4, 4, 16);
    arrowPixels = painted_pixels(surface);
    g_object_unref(button);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    soft(arrowPixels == 0,
         "gtk_render_arrow() draws nothing through a button style context",
         "GTK4 draws arrows again, so DrawDropArrow() could go back to it");

    /* What it uses instead. */
    theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
    icon = gtk_icon_theme_lookup_icon(theme, "pan-down-symbolic", NULL, 16, 1,
                                      GTK_TEXT_DIR_LTR,
                                      GTK_ICON_LOOKUP_FORCE_SYMBOLIC);

    check(icon != NULL,
          "the icon theme has pan-down-symbolic",
          "there is nothing left to draw a drop-down arrow with");

    if (icon != NULL)
    {
        const GdkRGBA black = { 0, 0, 0, 1 };
        GtkSnapshot* snapshot;
        GskRenderNode* node;

        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 32, 32);
        cr = cairo_create(surface);

        snapshot = gtk_snapshot_new();
        gtk_symbolic_paintable_snapshot_symbolic(GTK_SYMBOLIC_PAINTABLE(icon),
                                                 snapshot, 16, 16, &black, 1);
        node = gtk_snapshot_free_to_node(snapshot);
        if (node != NULL)
        {
            gsk_render_node_draw(node, cr);
            gsk_render_node_unref(node);
        }

        iconPixels = painted_pixels(surface);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        g_object_unref(icon);
    }

    check(iconPixels > 0,
          "and drawing it as a symbolic icon does put pixels on the surface",
          "the route DrawDropArrow() takes under GTK4 stopped working");
}

/* ------------------------------------------------------------------------ */

#ifdef HAVE_XTEST

static void test_destroyed_surface_still_typed(void)
{
    GtkWidget* win;
    GdkSurface* surface;
    int stillX11, destroyed;

    printf("What GDK_IS_X11_SURFACE() answers after the window is gone:\n");

    win = gtk_window_new();
    gtk_widget_set_visible(win, TRUE);
    pump_one_frame(win);

    surface = gtk_native_get_surface(GTK_NATIVE(win));
    if (surface == NULL)
    {
        soft(0, "the window has a surface to test", "no X11 surface here");
        gtk_window_destroy(GTK_WINDOW(win));
        return;
    }

    /* Hold the surface alive so it can still be asked about afterwards. */
    g_object_ref(surface);
    gtk_window_destroy(GTK_WINDOW(win));

    stillX11 = GDK_IS_X11_SURFACE(surface);
    destroyed = gdk_surface_is_destroyed(surface);

    /* This is why wxGTKGetOriginInRoot() cannot use GDK_IS_X11_SURFACE() on
     * its own: it is a type check, and a type does not stop being itself when
     * the X window behind it is destroyed. GDK_SURFACE_XID() then hands the
     * server a stale XID, the server answers BadWindow, and GDK's error
     * handler exits the process. */
    check(stillX11,
          "a destroyed surface still passes GDK_IS_X11_SURFACE()",
          "GDK now clears the type on destroy, so the extra guard in "
          "wxGTKGetOriginInRoot() could go");

    check(destroyed,
          "and gdk_surface_is_destroyed() reports it",
          "there is no way left to tell a live surface from a dead one");

    g_object_unref(surface);
}

#endif /* HAVE_XTEST */

static void test_indicator_nodes(void)
{
    GtkWidget* checkbtn;
    GtkWidget* radio;
    GtkWidget* partner;
    GtkWidget* expander;
    int w = 0, h = 0;

    printf("Check and radio indicator nodes:\n");

    checkbtn = gtk_check_button_new();
    g_object_ref_sink(checkbtn);
    check(strcmp(indicator_node_name(checkbtn), "check") == 0,
           "an ungrouped check button's indicator node is 'check'",
           "renderer.cpp measures the wrong node for check boxes");

    radio = gtk_check_button_new();
    g_object_ref_sink(radio);
    partner = gtk_check_button_new();
    g_object_ref_sink(partner);
    gtk_check_button_set_group(GTK_CHECK_BUTTON(radio), GTK_CHECK_BUTTON(partner));

    check(strcmp(indicator_node_name(radio), "radio") == 0,
           "grouping a check button makes its indicator node 'radio'",
           "radio indicators are now measured as check boxes, silently");

    gtk_widget_measure(checkbtn, GTK_ORIENTATION_HORIZONTAL, -1, &w, NULL, NULL, NULL);
    gtk_widget_measure(checkbtn, GTK_ORIENTATION_VERTICAL, -1, &h, NULL, NULL, NULL);
    check(w > 0 && h > 0,
           "an unrealized check button measures non-zero",
           "indicator sizes collapse to zero without realization");

    g_object_unref(checkbtn);
    g_object_unref(radio);
    g_object_unref(partner);

    /* The expander arrow, which used to be the "expander-size" style property. */
    expander = gtk_expander_new(NULL);
    g_object_ref_sink(expander);
    w = 0;
    gtk_widget_measure(expander, GTK_ORIENTATION_HORIZONTAL, -1, &w, NULL, NULL, NULL);
    check(w > 0,
           "an expander measures non-zero, replacing 'expander-size'",
           "tree expander arrows are sized to nothing");
    g_object_unref(expander);
}

/* wxChoice and wxComboBox cannot move off the deprecated GtkComboBox unless
 * GtkDropDown can be returned to "nothing selected": wxWidgets requires that
 * SetSelection(wxNOT_FOUND) is honoured after items have been added, and
 * tests/controls/itemcontainertest.cpp asserts exactly that.
 *
 * This was reported on #183 as impossible, on the strength of reading rather
 * than measuring, and the claim held for a while and shaped planning. It is
 * measured here so that it stays measured, and on whichever GTK the CI runs
 * rather than only on a developer's newer one.
 */
static void test_dropdown_selection_semantics(void)
{
    printf("Item selection semantics (#183):\n");

    const char* const choices[] = { "one", "two", "three", NULL };

    /* What this used to assert, and why it no longer does.
     *
     * wxChoice's whole migration was planned around GtkDropDown, on the
     * strength of a probe showing that gtk_drop_down_set_selected() accepts
     * GTK_INVALID_LIST_POSITION with items present -- which is what
     * SetSelection(wxNOT_FOUND) needs. That probe was linked against the
     * distribution's GTK 4.14. Against GTK 4.22 the same call is refused:
     * a GtkDropDown with any items in it cannot be returned to "nothing
     * selected".
     *
     * So this is reported rather than failed. It is a real difference between
     * GTK versions, it is worth knowing which one is in front of you, and wx
     * no longer depends on either answer. */
    GtkWidget* dd = gtk_drop_down_new_from_strings(choices);
    g_object_ref_sink(dd);

    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), 1);
    check(gtk_drop_down_get_selected(GTK_DROP_DOWN(dd)) == 1,
          "GtkDropDown selects the item it is asked for",
          "set_selected(1) did not take");

    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), GTK_INVALID_LIST_POSITION);
    soft(gtk_drop_down_get_selected(GTK_DROP_DOWN(dd))
            == GTK_INVALID_LIST_POSITION,
         "GtkDropDown can be deselected with items present",
         "this GTK refuses it -- true from 4.22 on, and the reason wxChoice "
         "uses a GtkSingleSelection of its own rather than a GtkDropDown");
    printf("           running against GTK %u.%u.%u\n",
           gtk_get_major_version(), gtk_get_minor_version(),
           gtk_get_micro_version());

    /* What wx does depend on, and what therefore has to hold.
     *
     * A GtkSingleSelection with autoselect off and can-unselect on must be
     * clearable with items present, because that is what carries every
     * wxChoice, wxComboBox and wxBitmapComboBox selection now. */
    GtkStringList* items = gtk_string_list_new(choices);
    GtkSingleSelection* sel =
        gtk_single_selection_new(G_LIST_MODEL(items));
    gtk_single_selection_set_autoselect(sel, FALSE);
    gtk_single_selection_set_can_unselect(sel, TRUE);

    gtk_single_selection_set_selected(sel, 1);
    check(gtk_single_selection_get_selected(sel) == 1,
          "GtkSingleSelection selects the item it is asked for",
          "set_selected(1) did not take");

    gtk_single_selection_set_selected(sel, GTK_INVALID_LIST_POSITION);
    check(gtk_single_selection_get_selected(sel) == GTK_INVALID_LIST_POSITION,
          "GtkSingleSelection can be cleared with items present",
          "autoselect=FALSE, can-unselect=TRUE was not enough, so wxChoice "
          "cannot honour SetSelection(wxNOT_FOUND) on this GTK");

    /* And the catch a port cannot avoid either way: adding the first item to
     * an empty model makes GTK select it, so wx has to put its own idea of
     * the selection back after every model change.
     *
     * Reported rather than failed: if a later GTK stopped doing this, wx's
     * compensation would merely be redundant, not wrong. */
    GtkStringList* empty = gtk_string_list_new(NULL);
    GtkSingleSelection* growing =
        gtk_single_selection_new(G_LIST_MODEL(empty));
    gtk_single_selection_set_autoselect(growing, FALSE);
    gtk_single_selection_set_can_unselect(growing, TRUE);
    gtk_single_selection_set_selected(growing, GTK_INVALID_LIST_POSITION);
    gtk_string_list_append(empty, "first");

    const guint afterAppend = gtk_single_selection_get_selected(growing);
    soft(afterAppend == GTK_INVALID_LIST_POSITION,
         "appending to an empty model leaves the selection alone",
         "GTK selected the new item by itself, so a port has to restore its "
         "own selection after every model change");
    printf("           selection after appending to an empty model: %u\n",
           afterAppend);

    g_object_unref(growing);
    g_object_unref(sel);
    g_object_unref(dd);
}

int main(void)
{
    g_log_set_writer_func(log_writer, NULL, NULL);

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
    printf("\n");
    test_menu_model_mechanics();
    printf("\n");
    test_toolbar_substitutes();
    printf("\n");
    test_clipboard_sync_bridge();
    printf("\n");
    test_indicator_nodes();
    printf("\n");
    test_scrollbar_metric();
    printf("\n");
    test_drop_arrow_drawing();
    printf("\n");
#ifdef HAVE_XTEST
    test_destroyed_surface_still_typed();
    printf("\n");
#endif
    test_combo_box_internals();
    printf("\n");
    test_dropdown_selection_semantics();
    printf("\n");
    test_css_parser_strictness();
    printf("\n");
    test_version_check_semantics();
    printf("\n");
    test_layout_manager_dispatch();
    printf("\n");
    test_entry_caret_reporting();
    printf("\n");
    test_frame_clock_layout();
    printf("\n");
    test_focus_flags();
    printf("\n");
    test_focus_controller_scope();
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
