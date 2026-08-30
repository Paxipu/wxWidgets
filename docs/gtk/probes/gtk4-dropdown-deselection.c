/* Re-checking a claim I made on #183: that GtkDropDown cannot be returned to
   "nothing selected" once its model is non-empty, which is what blocked
   wxChoice from moving off the deprecated GtkComboBox.

   The claim decides 55 warnings, so it is worth being sure about. This tries
   deselection in every state that differs: before and after realize, before
   and after mapping, with the model set at construction and set afterwards,
   and after the selection has been moved by hand first. */
#include <gtk/gtk.h>

static void report(const char *what, GtkDropDown *dd)
{
    guint before = gtk_drop_down_get_selected(dd);
    gtk_drop_down_set_selected(dd, GTK_INVALID_LIST_POSITION);
    guint after = gtk_drop_down_get_selected(dd);
    g_print("  %-46s %10u -> %-10u %s\n", what, before, after,
            after == GTK_INVALID_LIST_POSITION ? "deselected" : "REFUSED");
}

int main(void)
{
    gtk_init();

    const char * const choices[] = { "one", "two", "three", NULL };

    g_print("model given at construction:\n");
    GtkWidget *a = gtk_drop_down_new_from_strings(choices);
    g_object_ref_sink(a);
    report("never realized", GTK_DROP_DOWN(a));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(a), 1);
    report("after selecting item 1", GTK_DROP_DOWN(a));

    g_print("model set afterwards:\n");
    GtkWidget *b = gtk_drop_down_new(NULL, NULL);
    g_object_ref_sink(b);
    report("empty model", GTK_DROP_DOWN(b));
    GtkStringList *sl = gtk_string_list_new(choices);
    gtk_drop_down_set_model(GTK_DROP_DOWN(b), G_LIST_MODEL(sl));
    report("just after set_model", GTK_DROP_DOWN(b));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(b), 2);
    report("after selecting item 2", GTK_DROP_DOWN(b));

    g_print("inside a mapped window:\n");
    GtkWidget *c = gtk_drop_down_new_from_strings(choices);
    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), c);
    gtk_window_present(GTK_WINDOW(win));
    /* Bounded, and non-blocking once the window is up: a blocking iteration
       with nothing left pending never returns. */
    for ( int i = 0; i < 2000 && !gtk_widget_get_mapped(win); i++ )
        g_main_context_iteration(NULL, FALSE);
    for ( int i = 0; i < 200; i++ )
        g_main_context_iteration(NULL, FALSE);
    g_print("  window mapped: %d, drop-down realized: %d\n",
            gtk_widget_get_mapped(win), gtk_widget_get_realized(c));
    report("realized and mapped", GTK_DROP_DOWN(c));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(c), 1);
    for ( int i = 0; i < 100; i++ )
        g_main_context_iteration(NULL, FALSE);
    report("mapped, after selecting item 1", GTK_DROP_DOWN(c));

    /* And what does it show while deselected? wxChoice with no selection
       shows an empty control, so this matters too. */
    GtkWidget *child = gtk_widget_get_first_child(c);
    g_print("  first child of a deselected drop-down: %s\n",
            child ? G_OBJECT_TYPE_NAME(child) : "(none)");

    /* Appending to the model while deselected: does GTK pick something? */
    g_print("appending to the model while deselected:\n");
    GtkWidget *d = gtk_drop_down_new(NULL, NULL);
    g_object_ref_sink(d);
    GtkStringList *sl2 = gtk_string_list_new(NULL);
    gtk_drop_down_set_model(GTK_DROP_DOWN(d), G_LIST_MODEL(sl2));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(d), GTK_INVALID_LIST_POSITION);
    g_print("  empty and deselected: %u\n", gtk_drop_down_get_selected(GTK_DROP_DOWN(d)));
    gtk_string_list_append(sl2, "first");
    g_print("  after appending one item: %u  -> %s\n",
            gtk_drop_down_get_selected(GTK_DROP_DOWN(d)),
            gtk_drop_down_get_selected(GTK_DROP_DOWN(d)) == GTK_INVALID_LIST_POSITION
                ? "stayed deselected" : "GTK SELECTED IT FOR US");
    gtk_string_list_append(sl2, "second");
    g_print("  after appending a second:  %u\n",
            gtk_drop_down_get_selected(GTK_DROP_DOWN(d)));

    return 0;
}
