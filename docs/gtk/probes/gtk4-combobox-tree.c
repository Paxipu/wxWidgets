// Probe: what does a GtkComboBoxText look like from the inside under GTK4?
//
// src/gtk/choice.cpp needs to reach the widget which actually receives the
// pointer events, because the GtkComboBox itself does not, and it does so by
// walking up from the child returned by what used to be gtk_bin_get_child().
// GTK4 kept gtk_combo_box_get_child(), but the widgets between that child and
// the combo box are private implementation detail which the GTK3 code's
// comment describes and which may well have changed.  Print the real tree.
//
// Build with:
//   gcc -o gtk4-combobox-tree gtk4-combobox-tree.c $(pkg-config --cflags --libs gtk4)

#include <gtk/gtk.h>

static void dump(GtkWidget* w, int depth, GtkWidget* mark)
{
    for ( int i = 0; i < depth; i++ )
        g_print("  ");

    g_print("%s%s\n", G_OBJECT_TYPE_NAME(w), w == mark ? "   <-- get_child()" : "");

    for ( GtkWidget* c = gtk_widget_get_first_child(w);
          c;
          c = gtk_widget_get_next_sibling(c) )
    {
        dump(c, depth + 1, mark);
    }
}

static void activate(GtkApplication* app, gpointer)
{
    GtkWidget* const win = gtk_application_window_new(app);
    GtkWidget* const combo = gtk_combo_box_text_new();

    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "first");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "second");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

    gtk_window_set_child(GTK_WINDOW(win), combo);

    GtkWidget* const child = gtk_combo_box_get_child(GTK_COMBO_BOX(combo));

    g_print("gtk_combo_box_get_child() = %s\n",
            child ? G_OBJECT_TYPE_NAME(child) : "(null)");

    if ( child )
    {
        GtkWidget* const parent = gtk_widget_get_parent(child);
        GtkWidget* const grandparent =
            parent ? gtk_widget_get_parent(parent) : NULL;

        g_print("  parent      = %s\n",
                parent ? G_OBJECT_TYPE_NAME(parent) : "(null)");
        g_print("  grandparent = %s (is toggle button: %s)\n",
                grandparent ? G_OBJECT_TYPE_NAME(grandparent) : "(null)",
                grandparent && GTK_IS_TOGGLE_BUTTON(grandparent) ? "yes" : "NO");
    }

    g_print("\nfull tree:\n");
    dump(combo, 1, child);

    g_idle_add((GSourceFunc)g_application_quit, app);
}

int main(int argc, char** argv)
{
    GtkApplication* const app =
        gtk_application_new("org.wxwidgets.probe.combobox", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
