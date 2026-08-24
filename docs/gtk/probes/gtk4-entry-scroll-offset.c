// Does gtk_text_compute_cursor_extents(.., 0, ..) report the layout origin of
// a GtkEntry's text, including the horizontal scroll offset?
//
// If it does, it is the replacement for gtk_entry_get_layout_offsets(), which
// GTK4 removed along with any other way of asking how far an entry's text has
// been scrolled.
#include <gtk/gtk.h>

static GtkWidget* find_text(GtkWidget* entry)
{
    for (GtkWidget* c = gtk_widget_get_first_child(entry); c;
         c = gtk_widget_get_next_sibling(c))
        if (GTK_IS_TEXT(c))
            return c;
    return NULL;
}

static float origin_x(GtkWidget* text)
{
    graphene_rect_t strong;
    gtk_text_compute_cursor_extents(GTK_TEXT(text), 0, &strong, NULL);
    return strong.origin.x;
}

static void run(GtkApplication* app, gpointer)
{
    GtkWidget* win = gtk_application_window_new(app);
    GtkWidget* entry = gtk_entry_new();
    gtk_widget_set_size_request(entry, 100, -1);
    gtk_window_set_child(GTK_WINDOW(win), entry);
    gtk_window_present(GTK_WINDOW(win));

    for (int i = 0; i < 40; i++)
        g_main_context_iteration(NULL, FALSE);

    GtkWidget* text = find_text(entry);
    g_assert(text);

    gtk_editable_set_text(GTK_EDITABLE(entry), "short");
    for (int i = 0; i < 40; i++) g_main_context_iteration(NULL, FALSE);
    g_print("short text, cursor at home : origin.x = %g\n", origin_x(text));

    char buf[201];
    memset(buf, 'X', 200); buf[200] = 0;
    gtk_editable_set_text(GTK_EDITABLE(entry), buf);
    gtk_editable_set_position(GTK_EDITABLE(entry), 0);
    for (int i = 0; i < 40; i++) g_main_context_iteration(NULL, FALSE);
    g_print("200 chars, cursor at home  : origin.x = %g\n", origin_x(text));

    gtk_editable_set_position(GTK_EDITABLE(entry), -1);
    for (int i = 0; i < 60; i++) g_main_context_iteration(NULL, FALSE);
    g_print("200 chars, cursor at end   : origin.x = %g  <- must be negative\n",
            origin_x(text));

    g_application_quit(G_APPLICATION(app));
}

int main(int argc, char** argv)
{
    GtkApplication* app = gtk_application_new("org.wx.entryscroll", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(run), NULL);
    int rc = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return rc;
}
