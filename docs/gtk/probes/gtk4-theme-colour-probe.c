/*
 * How can a GTK4 program read a theme's named colours -- @theme_bg_color and
 * friends -- now that gtk_style_context_lookup_color() is deprecated and has
 * no replacement?
 *
 * CSS still resolves the names, so the question can be put to CSS: install a
 * provider setting "color" to the name, then read the result back with
 * gtk_widget_get_color(). This program establishes the three things that
 * makes rest on, each against the deprecated call as a control.
 *
 * wxGTKLookupThemeColour() does NOT do this, and the deprecated call stays.
 * Bg() and Border() run while GTK is measuring, laying out or painting some
 * other widget, and installing a provider on the display from there
 * segfaults GTK inside gtk_widget_snapshot_child(). What is below is a
 * measurement of GTK, not a description of the library.
 *
 *   1. A widget that is never shown has its style computed once, on demand.
 *      Adding a provider afterwards does not invalidate it, so a probe widget
 *      has to be created *after* its provider -- reusing one silently answers
 *      with whatever cascade was in force the first time it was read.
 *
 *   2. An undefined name makes the declaration invalid, and GTK substitutes a
 *      colour of its own instead of reporting anything. No parsing-error
 *      signal is emitted, and the substitute is a perfectly ordinary colour,
 *      so a single answer cannot say whether the name exists.
 *
 *   3. The substitute does not depend on the expression the name appeared in,
 *      while a name that resolves does. Asking through several expressions
 *      therefore separates the two cases: the answers differ iff the name is
 *      defined. Two mixes rather than one because a single mix collapses for
 *      the theme colour that happens to equal what it is mixed with, and
 *      Adwaita's theme_base_color -- pure white -- is a real example of the
 *      near miss this guards against.
 *
 * Build and run:
 *     gcc -o probe gtk4-theme-colour-probe.c $(pkg-config --cflags --libs gtk4)
 *     xvfb-run -a ./probe
 */

#include <gtk/gtk.h>
#include <stdio.h>

#define PROBE_CLASS "wx-colour-probe"

static GdkDisplay* display;
static int parse_errors;

static void on_parse_error(GtkCssProvider* p, GtkCssSection* s,
                           const GError* e, gpointer d)
{
    (void)p; (void)s; (void)d;
    parse_errors++;
    printf("      parsing-error: %s\n", e->message);
}

static GtkCssProvider* provider_add(const char* css)
{
    GtkCssProvider* p = gtk_css_provider_new();
    g_signal_connect(p, "parsing-error", G_CALLBACK(on_parse_error), NULL);
#if GTK_CHECK_VERSION(4,12,0)
    gtk_css_provider_load_from_string(p, css);
#else
    gtk_css_provider_load_from_data(p, css, -1);
#endif
    gtk_style_context_add_provider_for_display(
        display, GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_USER);
    return p;
}

static void provider_del(GtkCssProvider* p)
{
    gtk_style_context_remove_provider_for_display(display,
                                                  GTK_STYLE_PROVIDER(p));
    g_object_unref(p);
}

/* Compute one "color:" declaration, on a widget created for it. */
static void compute(const char* css, GdkRGBA* out)
{
    GtkCssProvider* p = provider_add(css);

    GtkWidget* w = gtk_label_new("");
    g_object_ref_sink(w);
    gtk_widget_add_css_class(w, PROBE_CLASS);
    gtk_widget_get_color(w, out);
    g_object_unref(w);

    provider_del(p);
}

/* The same, on a widget that existed before the provider did. */
static void compute_on(GtkWidget* w, const char* css, GdkRGBA* out)
{
    GtkCssProvider* p = provider_add(css);
    gtk_widget_get_color(w, out);
    provider_del(p);
}

static const struct { const char* before; const char* after; } expr[] =
{
    { "",     ""                    },
    { "mix(", ", rgb(0,255,0), 0.5)" },
    { "mix(", ", rgb(255,0,0), 0.5)" }
};

static gboolean lookup(const char* name, GdkRGBA* out)
{
    GdkRGBA answer[G_N_ELEMENTS(expr)];

    for (guint i = 0; i < G_N_ELEMENTS(expr); i++)
    {
        char* css = g_strdup_printf("." PROBE_CLASS " { color: %s@%s%s; }",
                                    expr[i].before, name, expr[i].after);
        compute(css, &answer[i]);
        g_free(css);
    }

    printf("    through %-14s %-18s %-18s %s\n", name,
           gdk_rgba_to_string(&answer[0]), gdk_rgba_to_string(&answer[1]),
           gdk_rgba_to_string(&answer[2]));

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

static void activate(GtkApplication* app, gpointer data)
{
    (void)app; (void)data;
    display = gdk_display_get_default();

    static const char* const names[] =
    {
        "theme_fg_color", "theme_bg_color", "theme_base_color",
        "theme_selected_bg_color", "borders", "wx_no_such_colour"
    };

    printf("1. a widget outlives the cascade it was first read under\n");
    {
        GtkWidget* w = gtk_label_new("");
        g_object_ref_sink(w);
        gtk_widget_add_css_class(w, PROBE_CLASS);

        GdkRGBA first, second, fresh;
        gtk_widget_get_color(w, &first);   /* computes it, with no provider */
        compute_on(w, "." PROBE_CLASS " { color: rgb(1,2,3); }", &second);
        compute("." PROBE_CLASS " { color: rgb(1,2,3); }", &fresh);

        printf("    before any provider   %s\n", gdk_rgba_to_string(&first));
        printf("    with one installed    %s   <- unchanged: not invalidated\n",
               gdk_rgba_to_string(&second));
        printf("    on a fresh widget     %s   <- the provider does apply\n",
               gdk_rgba_to_string(&fresh));
        g_object_unref(w);
    }

    printf("\n2. an undefined name is substituted silently, not reported\n");
    {
        GdkRGBA c;
        parse_errors = 0;
        compute("." PROBE_CLASS " { color: @wx_no_such_colour; }", &c);
        printf("    @wx_no_such_colour -> %s, %d parsing errors\n",
               gdk_rgba_to_string(&c), parse_errors);
    }

    printf("\n3. several expressions separate the two cases\n");
    printf("    %-22s %-18s %-18s %s\n", "name", "plain", "mixed w/ green",
           "mixed w/ red");
    for (guint i = 0; i < G_N_ELEMENTS(names); i++)
    {
        GdkRGBA css_answer, deprecated;
        const gboolean found = lookup(names[i], &css_answer);

        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        GtkWidget* w = gtk_label_new("");
        g_object_ref_sink(w);
        const gboolean control = gtk_style_context_lookup_color(
            gtk_widget_get_style_context(w), names[i], &deprecated);
        g_object_unref(w);
        G_GNUC_END_IGNORE_DEPRECATIONS

        printf("  %-24s css %d %-18s deprecated %d %-18s %s\n", names[i],
               found, found ? gdk_rgba_to_string(&css_answer) : "-",
               control, control ? gdk_rgba_to_string(&deprecated) : "-",
               (found == control &&
                (!found || gdk_rgba_equal(&css_answer, &deprecated)))
                   ? "agree" : "DISAGREE");
    }

    fflush(stdout);
    exit(0);
}

int main(int argc, char** argv)
{
    GtkApplication* app = gtk_application_new("org.wxwidgets.themecolourprobe",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    return g_application_run(G_APPLICATION(app), argc, argv);
}
