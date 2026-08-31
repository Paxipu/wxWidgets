/* GtkEntryCompletion is deprecated in GTK 4.10 with no replacement, so
   wxTextEntry::AutoComplete() has to be rebuilt out of parts. Three things
   decide whether the obvious construction -- a GtkPopover under the entry
   holding a GtkListView over a filtered model -- actually behaves like a
   completion popup, and each has a plausible wrong answer:

     1. Does a popover with autohide=FALSE leave the keyboard focus in the
        entry, so that typing carries on? A popup that steals focus is not a
        completion popup, it is a modal list.

     2. Does GtkStringFilter + GtkFilterListModel filter on a prefix the way
        GtkEntryCompletion did, and does the filtered model update when the
        search text changes?

     3. Can the entry's own key handling be intercepted so that Up/Down move
        the list selection and Return accepts it, while every other key still
        reaches the entry?  */
#include <gtk/gtk.h>

static GtkWidget *entry, *popover, *listview;
static GtkStringFilter *filter;
static GtkSingleSelection *selection;
static int key_seen_by_entry;

static void on_changed(GtkEditable *e, gpointer d)
{
    (void)d;
    const char *text = gtk_editable_get_text(e);
    gtk_string_filter_set_search(filter, text);
    g_print("  typed %-8s -> %u match(es)\n", text,
            g_list_model_get_n_items(G_LIST_MODEL(selection)));
}

static gboolean on_key(GtkEventControllerKey *c, guint keyval, guint code,
                       GdkModifierType state, gpointer d)
{
    (void)c; (void)code; (void)state; (void)d;
    if ( keyval == GDK_KEY_Down || keyval == GDK_KEY_Up )
    {
        const guint n = g_list_model_get_n_items(G_LIST_MODEL(selection));
        guint sel = gtk_single_selection_get_selected(selection);
        if ( n )
        {
            if ( keyval == GDK_KEY_Down )
                sel = (sel == GTK_INVALID_LIST_POSITION || sel + 1 >= n) ? 0 : sel + 1;
            else
                sel = (sel == GTK_INVALID_LIST_POSITION || sel == 0) ? n - 1 : sel - 1;
            gtk_single_selection_set_selected(selection, sel);
        }
        return TRUE;   /* consumed: the entry must not move the caret */
    }
    key_seen_by_entry++;
    return FALSE;      /* everything else goes on to the entry */
}

static void pump(GtkWidget *w, int frames)
{
    GdkFrameClock *clock = gtk_widget_get_frame_clock(w);
    for ( int i = 0; i < frames; i++ )
    {
        if ( clock )
            gdk_frame_clock_request_phase(clock, GDK_FRAME_CLOCK_PHASE_PAINT);
        for ( int j = 0; j < 200 && g_main_context_pending(NULL); j++ )
            g_main_context_iteration(NULL, FALSE);
    }
}

int main(void)
{
    gtk_init();

    const char * const words[] = { "alpha", "alpine", "beta", "gamma", NULL };
    GtkStringList *all = gtk_string_list_new(words);

    filter = gtk_string_filter_new(
        GTK_EXPRESSION(gtk_property_expression_new(GTK_TYPE_STRING_OBJECT,
                                                   NULL, "string")));
    gtk_string_filter_set_match_mode(filter, GTK_STRING_FILTER_MATCH_MODE_PREFIX);
    gtk_string_filter_set_ignore_case(filter, TRUE);

    GtkFilterListModel *filtered =
        gtk_filter_list_model_new(G_LIST_MODEL(all), GTK_FILTER(g_object_ref(filter)));
    selection = gtk_single_selection_new(G_LIST_MODEL(filtered));
    gtk_single_selection_set_autoselect(selection, FALSE);
    gtk_single_selection_set_can_unselect(selection, TRUE);

    GtkListItemFactory *f = gtk_builder_list_item_factory_new_from_bytes(
        NULL,
        g_bytes_new_static(
            "<interface><template class=\"GtkListItem\">"
            "<property name=\"child\"><object class=\"GtkLabel\">"
            "<property name=\"xalign\">0</property>"
            "<binding name=\"label\"><lookup name=\"string\" type=\"GtkStringObject\">"
            "<lookup name=\"item\">GtkListItem</lookup></lookup></binding>"
            "</object></property></template></interface>",
            -1));
    listview = gtk_list_view_new(GTK_SELECTION_MODEL(selection), f);

    entry = gtk_entry_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(box), entry);

    popover = gtk_popover_new();
    gtk_popover_set_autohide(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    gtk_widget_set_parent(popover, entry);
    gtk_popover_set_child(GTK_POPOVER(popover), listview);

    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), NULL);
    gtk_widget_add_controller(entry, keys);
    g_signal_connect(entry, "changed", G_CALLBACK(on_changed), NULL);

    GtkWidget *win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(win), 320, 120);
    gtk_window_set_child(GTK_WINDOW(win), box);
    gtk_window_present(GTK_WINDOW(win));
    for ( int i = 0; i < 3000 && !gtk_widget_get_mapped(win); i++ )
        g_main_context_iteration(NULL, FALSE);
    pump(win, 20);

    gtk_widget_grab_focus(entry);
    pump(win, 5);

    /* A GtkEntry delegates its editing to an inner GtkText, and it is that
       which takes the focus -- gtk_widget_has_focus(entry) is FALSE even when
       the entry is where the typing goes. Ask the root what has the focus
       instead, and report the type. */
    GtkWidget *focused = gtk_root_get_focus(GTK_ROOT(win));
    g_print("focus before the popup: %s\n\n",
            focused ? G_OBJECT_TYPE_NAME(focused) : "(nothing)");

    /* ---- 2: filtering ---- */
    g_print("filtering:\n");
    gtk_editable_set_text(GTK_EDITABLE(entry), "al");
    pump(win, 5);
    gtk_editable_set_text(GTK_EDITABLE(entry), "alp");
    pump(win, 5);
    gtk_editable_set_text(GTK_EDITABLE(entry), "z");
    pump(win, 5);
    gtk_editable_set_text(GTK_EDITABLE(entry), "al");
    pump(win, 5);

    /* ---- 1: does showing the popover keep the focus in the entry? ---- */
    gtk_popover_popup(GTK_POPOVER(popover));
    pump(win, 10);
    GtkWidget *after = gtk_root_get_focus(GTK_ROOT(win));
    g_print("\npopover shown: %d\n", gtk_widget_get_visible(popover));
    g_print("focus after the popup:  %s\n",
            after ? G_OBJECT_TYPE_NAME(after) : "(nothing)");
    g_print("  -> %s\n",
            after == focused
                ? "unchanged: typing carries on, this can be a completion popup"
                : "the popup MOVED the focus");

    /* ---- 3: key routing ---- */
    g_print("\nkey routing (2 matches for \"al\"):\n");
    g_print("  selected before: %u\n",
            gtk_single_selection_get_selected(selection));
    on_key(NULL, GDK_KEY_Down, 0, (GdkModifierType)0, NULL);
    g_print("  after Down:      %u\n",
            gtk_single_selection_get_selected(selection));
    on_key(NULL, GDK_KEY_Down, 0, (GdkModifierType)0, NULL);
    g_print("  after Down:      %u\n",
            gtk_single_selection_get_selected(selection));
    on_key(NULL, GDK_KEY_Up, 0, (GdkModifierType)0, NULL);
    g_print("  after Up:        %u\n",
            gtk_single_selection_get_selected(selection));

    guint sel = gtk_single_selection_get_selected(selection);
    if ( sel != GTK_INVALID_LIST_POSITION )
    {
        GtkStringObject *o = GTK_STRING_OBJECT(
            g_list_model_get_item(G_LIST_MODEL(selection), sel));
        g_print("  accepting it would insert: %s\n",
                gtk_string_object_get_string(o));
        g_object_unref(o);
    }

    gtk_widget_unparent(popover);
    return 0;
}
