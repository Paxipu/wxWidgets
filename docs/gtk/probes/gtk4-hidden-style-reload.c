/* A style computed for a widget that is not on screen is not replaced when the
   rules behind it are loaded again.
 *
 * wx hit this as #245: a wxStaticText given a font and then a colour, before
 * its frame was shown, came out in the theme's colour. wxStaticText is the
 * control it shows on because SetFont() makes it measure itself (for #16088),
 * and measuring computes a style; the colour then arrives in a second load of
 * the same provider, and that load does not reach the widget.
 *
 * Only the window styled *last* before the window is shown keeps the wrong
 * style, because styling any other window afterwards rescues the ones before
 * it -- which is what made this look like four different bugs before it looked
 * like one. Hence four labels here, and only the last one judged.
 *
 * PROBE_REMEDY picks one thing to try, applied to every label. The list is
 * mostly negative results, which is the useful part: none of the obvious ways
 * of asking GTK to think again works.
 *
 *   PKG_CONFIG_PATH=<gtk4 prefix>/lib/pkgconfig \
 *     gcc -o probe gtk4-hidden-style-reload.c $(pkg-config --cflags --libs gtk4)
 *   PROBE_REMEDY='nothing (control)' ./probe
 */
#include <gtk/gtk.h>

enum
{
    PLAIN,          /* the fault itself                                     */
    QUEUE_DRAW,     /* ask for a repaint                                    */
    QUEUE_RESIZE,   /* ask for a measure and an allocation                  */
    FROM_STRING,    /* reload through the 4.12 entry point instead          */
    READD,          /* take the provider off the display and put it back    */
    NEW_PROVIDER,   /* retire the provider and add a fresh one              */
    MARKER_CLASS,   /* add a CSS class the theme does not use               */
    AFTER_SHOW,     /* load the rules again once the window is on screen    */
    N_REMEDIES
};

static const char* const name[N_REMEDIES] =
{
    "nothing (control)",
    "queue_draw",
    "queue_resize",
    "load_from_string",
    "remove+add provider",
    "fresh provider",
    "marker css class",
    "reloaded after show",
};

#define N_LABELS 4

int main(void)
{
    gtk_init();

    const char* const want = g_getenv("PROBE_REMEDY");
    int remedy = PLAIN;
    for (int i = 0; i < N_REMEDIES; i++)
        if (want && g_str_equal(want, name[i]))
            remedy = i;

    GdkDisplay* const display = gdk_display_get_default();

    GtkWidget* win = gtk_window_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_window_set_child(GTK_WINDOW(win), box);

    GtkWidget* label[N_LABELS];
    GtkCssProvider* prov[N_LABELS];
    char cls[N_LABELS][32];

    for (int i = 0; i < N_LABELS; i++)
    {
        g_snprintf(cls[i], sizeof cls[i], "probe%d", i);

        label[i] = gtk_label_new("label");
        gtk_box_append(GTK_BOX(box), label[i]);

        prov[i] = gtk_css_provider_new();

        char css[192];
        g_snprintf(css, sizeof css, ".%s{font:italic 14pt \"Sans\";}", cls[i]);
        gtk_css_provider_load_from_data(prov[i], css, -1);

        gtk_style_context_add_provider_for_display(
            display, GTK_STYLE_PROVIDER(prov[i]),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        gtk_widget_add_css_class(label[i], cls[i]);

        /* The measure is what makes this fail: it computes a style, and the
           window is not on screen yet. Take it out and every label is green,
           whatever else is done. */
        int min = 0, nat = 0;
        gtk_widget_measure(label[i], GTK_ORIENTATION_HORIZONTAL, -1,
                           &min, &nat, NULL, NULL);

        g_snprintf(css, sizeof css,
                   ".%s{color:rgb(0,130,0);font:italic 14pt \"Sans\";}", cls[i]);

        if (remedy == FROM_STRING)
            gtk_css_provider_load_from_string(prov[i], css);
        else if (remedy == NEW_PROVIDER)
        {
            gtk_style_context_remove_provider_for_display(
                display, GTK_STYLE_PROVIDER(prov[i]));
            g_object_unref(prov[i]);

            prov[i] = gtk_css_provider_new();
            gtk_css_provider_load_from_data(prov[i], css, -1);
            gtk_style_context_add_provider_for_display(
                display, GTK_STYLE_PROVIDER(prov[i]),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }
        else
            gtk_css_provider_load_from_data(prov[i], css, -1);

        switch (remedy)
        {
        case QUEUE_DRAW:
            gtk_widget_queue_draw(label[i]);
            break;

        case QUEUE_RESIZE:
            gtk_widget_queue_resize(label[i]);
            break;

        case READD:
            gtk_style_context_remove_provider_for_display(
                display, GTK_STYLE_PROVIDER(prov[i]));
            gtk_style_context_add_provider_for_display(
                display, GTK_STYLE_PROVIDER(prov[i]),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            break;

        case MARKER_CLASS:
            gtk_widget_add_css_class(label[i], "probe-restyled");
            break;
        }
    }

    gtk_window_present(GTK_WINDOW(win));
    for (int n = 0; n < 400 && g_main_context_iteration(NULL, FALSE); n++) {}

    if (remedy == AFTER_SHOW)
    {
        for (int i = 0; i < N_LABELS; i++)
        {
            char css[192];
            g_snprintf(css, sizeof css,
                       ".%s{color:rgb(0,130,0);font:italic 14pt \"Sans\";}",
                       cls[i]);
            gtk_css_provider_load_from_data(prov[i], css, -1);
        }
        for (int n = 0; n < 200 && g_main_context_iteration(NULL, FALSE); n++) {}
    }

    GdkRGBA c;
    gtk_widget_get_color(label[N_LABELS - 1], &c);

    g_print("%-22s last label %.0f,%.0f,%.0f -- %s\n", name[remedy],
            c.red * 255, c.green * 255, c.blue * 255,
            (c.green > 0.4 && c.red < 0.1) ? "the colour arrived"
                                           : "the colour was lost");

    return 0;
}
