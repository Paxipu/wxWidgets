/* I have been saying that gtk_style_context_get_padding() and get_border()
   "have no replacement at all" in GTK4, and using that to write off about 50
   warnings across settings.cpp, window.cpp, control.cpp, statbox.cpp,
   spinbutt.cpp and win_gtk.cpp.

   That is the same sentence I just had to withdraw for #181. So: what does a
   real widget answer, and does it agree with what the deprecated calls say? */
#include <gtk/gtk.h>

static void compare(const char *what, GtkWidget *w)
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkStyleContext *sc = gtk_widget_get_style_context(w);
    GtkBorder pad, bor;
    gtk_style_context_get_padding(sc, &pad);
    gtk_style_context_get_border(sc, &bor);
    G_GNUC_END_IGNORE_DEPRECATIONS

    int minw = 0, natw = 0, minh = 0, nath = 0;
    gtk_widget_measure(w, GTK_ORIENTATION_HORIZONTAL, -1, &minw, &natw, NULL, NULL);
    gtk_widget_measure(w, GTK_ORIENTATION_VERTICAL, -1, &minh, &nath, NULL, NULL);

    g_print("  %-16s deprecated: padding %d,%d,%d,%d  border %d,%d,%d,%d\n",
            what, pad.left, pad.right, pad.top, pad.bottom,
            bor.left, bor.right, bor.top, bor.bottom);
    g_print("  %-16s measure:    min %dx%d  nat %dx%d\n",
            "", minw, minh, natw, nath);
    g_print("  %-16s padding+border horizontally = %d, and an empty widget's\n"
            "  %-16s minimum width = %d  -> %s\n",
            "", pad.left + pad.right + bor.left + bor.right, "", minw,
            (pad.left + pad.right + bor.left + bor.right) == minw
              ? "SAME NUMBER" : "different (content or CSS min-width too)");
}

int main(void)
{
    gtk_init();

    GtkWidget *win = gtk_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(win), box);

    struct { const char *name; GtkWidget *w; } items[] = {
        { "button",   gtk_button_new() },
        { "entry",    gtk_entry_new() },
        { "frame",    gtk_frame_new(NULL) },
        { "check",    gtk_check_button_new() },
    };

    for ( unsigned i = 0; i < G_N_ELEMENTS(items); i++ )
        gtk_box_append(GTK_BOX(box), items[i].w);

    gtk_window_present(GTK_WINDOW(win));
    for ( int i = 0; i < 2000 && !gtk_widget_get_mapped(win); i++ )
        g_main_context_iteration(NULL, FALSE);
    for ( int i = 0; i < 200; i++ )
        g_main_context_iteration(NULL, FALSE);

    g_print("what a real widget can tell you without the deprecated calls:\n\n");
    for ( unsigned i = 0; i < G_N_ELEMENTS(items); i++ )
    {
        compare(items[i].name, items[i].w);
        g_print("\n");
    }

    /* The other half of the question: colours. gtk_style_context_get_color()
       is deprecated, gtk_widget_get_color() is not. */
    GdkRGBA c1, c2;
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gtk_style_context_get_color(gtk_widget_get_style_context(items[0].w), &c1);
    G_GNUC_END_IGNORE_DEPRECATIONS
    gtk_widget_get_color(items[0].w, &c2);
    g_print("colour: deprecated %.3f,%.3f,%.3f  widget %.3f,%.3f,%.3f  -> %s\n",
            c1.red, c1.green, c1.blue, c2.red, c2.green, c2.blue,
            gdk_rgba_equal(&c1, &c2) ? "IDENTICAL" : "differ");
    return 0;
}
