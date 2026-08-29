/*
 * Does a negative CSS margin in the active theme make a GTK4 scrollbar's
 * slider gizmo report a negative minimum size?  Issue #24.
 *
 * @gunterkoenigsmann sees this while hovering over an opened menu in the
 * display sample:
 *
 *   GtkGizmo 0x... (slider) reported min width -2, but sizes must be >= 0
 *   GtkGizmo 0x... (slider) reported min height -2, but sizes must be >= 0
 *
 * The obvious reading is that wx allocated something below its minimum --
 * that is what #95 was, and wxPizza's measure has clamped since #96.  But a
 * GTK4 menu puts its items in a GtkScrolledWindow, "slider" is the CSS node
 * of a scrollbar's handle, and nothing in wx creates or measures it.  So the
 * question this asks is whether the warning can be produced with no
 * application code at all.
 *
 * It can.  There is no wxWidgets in this program: one GtkScrollbar, one CSS
 * rule, and the same warning.  The rule is installed at USER priority so it
 * outranks the theme rather than competing with it -- loading the same CSS
 * through gtk4-builder-tool's --css is not enough, the theme wins and the
 * minimum stays positive, which is a good way to conclude "not reproducible"
 * from a run that never applied the rule under test.
 *
 * Build and run (a display is needed, the widget has to be realized):
 *
 *   gcc -o probe24 gtk4-slider-negative-css-margin.c \
 *       $(pkg-config --cflags --libs gtk4)
 *   xvfb-run -a ./probe24                 # sweeps the margin
 *   PROBE_MARGIN=-6 xvfb-run -a ./probe24 # one value
 *
 * Measured here, GTK 4.22.4, no theme installed (so GTK's built-in Adwaita).
 * This is the program's own output, one sweep, one process:
 *
 *   PROBE margin=theme  min=46x14        <- Adwaita is not affected
 *   PROBE margin=+1px   min=24x13
 *   PROBE margin=-1px   min=11x9
 *   PROBE margin=-3px   min=6x6
 *   PROBE margin=-4px   min=2x2
 *   PROBE margin=-6px   min=0x0
 *     Gtk-WARNING: GtkGizmo (slider) reported min width -2, but sizes must be >= 0
 *     Gtk-WARNING: GtkGizmo (slider) reported min height -2, ...
 *     ... then -4 as the style settles over the next frames
 *   PROBE margin=-8px   min=0x0
 *
 * -2 is the value in the issue, reproduced from a stylesheet rather than
 * from anything wx did.  Note the sweep reuses one scrollbar, so a margin
 * measured here is a few pixels off the same margin measured in a fresh
 * process; the sign and the warning are what this establishes, not the exact
 * pixel counts.
 *
 * So the warning is a property of the stylesheet, not of the application:
 * the reported number follows the margin, and the widget's own minimum is
 * clamped to 0 afterwards, which is why nothing is visibly broken.  The same
 * conclusion was reached independently in GNOME/gtk#4446 (a theme with
 * "margin: 0 -1px" on a progress node) and in KDE's Breeze bug 486766, where
 * a bare GtkScrollbar in gtk4-builder-tool reproduces it under Breeze and not
 * under Adwaita.
 */
#include <gtk/gtk.h>

#include <stdio.h>
#include <stdlib.h>

static const int k_sweep[] = { 1, -1, -3, -4, -6, -8 };

static int   s_index    = -1;   /* -1 = the themed run, before the sweep */
static int   s_single   = 0;    /* PROBE_MARGIN given: measure just that */
static GtkCssProvider *s_provider = NULL;

static void apply_margin(int px)
{
    char css[128];

    g_snprintf(css, sizeof(css),
               "scrollbar slider { min-width: 0; min-height: 0; margin: %dpx; }",
               px);

    if (s_provider)
    {
        gtk_style_context_remove_provider_for_display(
            gdk_display_get_default(), GTK_STYLE_PROVIDER(s_provider));
        g_object_unref(s_provider);
    }

    s_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(s_provider, css);

    /* USER outranks both the theme and application styles.  At the lower
     * priorities the theme's own min-width wins and nothing is measured. */
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(s_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
}

static gboolean measure_step(gpointer data)
{
    GtkWidget *sb = GTK_WIDGET(data);
    int min_w = 0, min_h = 0;

    gtk_widget_measure(sb, GTK_ORIENTATION_HORIZONTAL, -1, &min_w, NULL, NULL, NULL);
    gtk_widget_measure(sb, GTK_ORIENTATION_VERTICAL,   -1, &min_h, NULL, NULL, NULL);

    if (s_index < 0)
        printf("PROBE margin=theme  min=%dx%d\n", min_w, min_h);
    else
        printf("PROBE margin=%+dpx  min=%dx%d\n", k_sweep[s_index], min_w, min_h);
    fflush(stdout);

    if (s_single || ++s_index >= (int)G_N_ELEMENTS(k_sweep))
    {
        gtk_window_destroy(GTK_WINDOW(gtk_widget_get_root(sb)));
        return G_SOURCE_REMOVE;
    }

    apply_margin(k_sweep[s_index]);
    return G_SOURCE_CONTINUE;
}

static void activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    const char *one = getenv("PROBE_MARGIN");
    GtkWidget *win, *sb;

    if (one && *one)
    {
        s_single = 1;
        apply_margin(atoi(one));
        printf("PROBE margin=%+dpx (single)\n", atoi(one));
    }

    win = gtk_application_window_new(app);
    sb  = gtk_scrollbar_new(GTK_ORIENTATION_HORIZONTAL,
                            gtk_adjustment_new(0, 0, 100, 1, 10, 10));
    gtk_window_set_child(GTK_WINDOW(win), sb);
    gtk_window_present(GTK_WINDOW(win));

    /* One measurement per main loop pass, so each margin gets a full style
     * invalidation before it is measured. */
    g_timeout_add(120, measure_step, sb);
}

int main(int argc, char **argv)
{
    (void)argc;

    GtkApplication *app;
    int rc;

    app = gtk_application_new("org.wxwidgets.probe24", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    rc = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);

    return rc;
}
