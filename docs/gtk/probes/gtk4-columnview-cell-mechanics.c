/* Three questions the wxDataViewCtrl migration cannot guess at, because each
   of them has a plausible wrong answer rather than an error:

     1. wxDataViewChoiceRenderer's natural replacement is GtkDropDown, and
        #183 established that GtkDropDown cannot be returned to "nothing
        selected" once its model is non-empty. Does that bite inside a cell,
        where there is always a value?

     2. wxDataViewCtrl runs an editing protocol -- started, done, cancelled --
        around an in-place editor. Can that be driven from a cell widget?

     3. wxDataViewCtrl::SetRowHeight() sets one height for every row.
        GtkColumnView has no such setting; a self-drawing cell reports its own
        minimum. Which wins, and can wx impose a height at all?

   Traps this probe is written around, each of which cost a round elsewhere in
   this port:
     - GtkTreeListModel wraps items in GtkTreeListRow; a factory must unwrap.
     - Iterating the main loop from inside a callback hangs.
     - Rows are only allocated once the frame clock ticks, so a non-blocking
       g_main_context_iteration() never allocates them.  */
#include <gtk/gtk.h>

/* ---- a trivial item type ---- */
#define ITEM_TYPE (item_get_type())
G_DECLARE_FINAL_TYPE(Item, item, PROBE, ITEM, GObject)
struct _Item { GObject parent; char *text; };
G_DEFINE_TYPE(Item, item, G_TYPE_OBJECT)
static void item_finalize(GObject *o) { g_free(PROBE_ITEM(o)->text);
    G_OBJECT_CLASS(item_parent_class)->finalize(o); }
static void item_class_init(ItemClass *k) { G_OBJECT_CLASS(k)->finalize = item_finalize; }
static void item_init(Item *i) { i->text = NULL; }
static Item *item_new(const char *t) { Item *i = g_object_new(ITEM_TYPE, NULL);
    i->text = g_strdup(t); return i; }

static int edit_started, edit_done;
static int draw_calls;

/* ---- 3: a self-drawing cell that wants to be tall ---- */
static void draw_cell(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer d)
{
    (void)a; (void)d;
    draw_calls++;
    cairo_set_source_rgb(cr, 0.2, 0.4, 0.8);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);
}

static void setup_custom(GtkListItemFactory *f, GtkListItem *li, gpointer d)
{
    (void)f; (void)d;
    GtkWidget *area = gtk_drawing_area_new();
    /* Ask for 40 pixels, which is more than a text row needs. */
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), 40);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_cell, NULL, NULL);
    gtk_list_item_set_child(li, area);
}
static void bind_nothing(GtkListItemFactory *f, GtkListItem *li, gpointer d)
{ (void)f; (void)li; (void)d; }

/* ---- 2: an editable cell ---- */
static void on_editing(GObject *o, GParamSpec *p, gpointer d)
{
    (void)p; (void)d;
    if ( gtk_editable_label_get_editing(GTK_EDITABLE_LABEL(o)) )
        edit_started++;
    else
        edit_done++;
}
static void setup_edit(GtkListItemFactory *f, GtkListItem *li, gpointer d)
{
    (void)f; (void)d;
    GtkWidget *lbl = gtk_editable_label_new("");
    g_signal_connect(lbl, "notify::editing", G_CALLBACK(on_editing), NULL);
    gtk_list_item_set_child(li, lbl);
}
static void bind_edit(GtkListItemFactory *f, GtkListItem *li, gpointer d)
{
    (void)f; (void)d;
    /* THE TRAP: with a GtkTreeListModel in the chain this is a GtkTreeListRow,
       not an Item. Unwrap before casting. */
    GObject *obj = gtk_list_item_get_item(li);
    if ( GTK_IS_TREE_LIST_ROW(obj) )
        obj = gtk_tree_list_row_get_item(GTK_TREE_LIST_ROW(obj));
    gtk_editable_set_text(GTK_EDITABLE(gtk_list_item_get_child(li)),
                          PROBE_ITEM(obj)->text);
}

/* ---- 1: a drop-down cell ---- */
static void setup_drop(GtkListItemFactory *f, GtkListItem *li, gpointer d)
{
    (void)f; (void)d;
    const char * const choices[] = { "one", "two", "three", NULL };
    GtkWidget *dd = gtk_drop_down_new_from_strings(choices);
    gtk_list_item_set_child(li, dd);
}

/* The rows of a GtkColumnView are not its children: the tree is
   columnview > listview > row > cell > the factory's widget. A flat walk over
   the column view's own children finds none of them, which reads as "the
   factory never ran" and is not that at all. */
static GtkWidget *find_type(GtkWidget *parent, GType type)
{
    for ( GtkWidget *c = gtk_widget_get_first_child(parent); c;
          c = gtk_widget_get_next_sibling(c) )
    {
        if ( G_TYPE_CHECK_INSTANCE_TYPE(c, type) )
            return c;
        GtkWidget *found = find_type(c, type);
        if ( found )
            return found;
    }
    return NULL;
}

/* Height of the row a given cell widget sits in: walk up to the widget whose
   CSS name is "row". */
static int row_height_of(GtkWidget *cell)
{
    for ( GtkWidget *w = cell; w; w = gtk_widget_get_parent(w) )
    {
        const char *name = gtk_widget_get_css_name(w);
        if ( name && g_strcmp0(name, "row") == 0 )
            return gtk_widget_get_height(w);
    }
    return -1;
}

/* Rows are only allocated once the frame clock has ticked, so a purely
   non-blocking loop reports every row as 0x0. But a *blocking* iteration with
   nothing left pending never returns, which is how every earlier version of
   this probe hung. So: ask for a frame, then block only while there is
   something to block on. */
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

    GListStore *store = g_list_store_new(ITEM_TYPE);
    for ( int i = 0; i < 4; i++ )
    {
        char *t = g_strdup_printf("row %d", i);
        Item *it = item_new(t);
        g_list_store_append(store, it);
        g_object_unref(it);
        g_free(t);
    }

    GtkNoSelection *sel = gtk_no_selection_new(G_LIST_MODEL(g_object_ref(store)));
    GtkWidget *cv = gtk_column_view_new(GTK_SELECTION_MODEL(sel));

    GtkListItemFactory *fCustom = gtk_signal_list_item_factory_new();
    g_signal_connect(fCustom, "setup", G_CALLBACK(setup_custom), NULL);
    g_signal_connect(fCustom, "bind", G_CALLBACK(bind_nothing), NULL);
    GtkColumnViewColumn *cCustom = gtk_column_view_column_new("custom", fCustom);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(cv), cCustom);
    g_object_unref(cCustom);

    GtkListItemFactory *fEdit = gtk_signal_list_item_factory_new();
    g_signal_connect(fEdit, "setup", G_CALLBACK(setup_edit), NULL);
    g_signal_connect(fEdit, "bind", G_CALLBACK(bind_edit), NULL);
    GtkColumnViewColumn *cEdit = gtk_column_view_column_new("edit", fEdit);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(cv), cEdit);
    g_object_unref(cEdit);

    GtkListItemFactory *fDrop = gtk_signal_list_item_factory_new();
    g_signal_connect(fDrop, "setup", G_CALLBACK(setup_drop), NULL);
    g_signal_connect(fDrop, "bind", G_CALLBACK(bind_nothing), NULL);
    GtkColumnViewColumn *cDrop = gtk_column_view_column_new("choice", fDrop);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(cv), cDrop);
    g_object_unref(cDrop);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), cv);
    GtkWidget *win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(win), 640, 400);
    gtk_window_set_child(GTK_WINDOW(win), scroll);
    gtk_window_present(GTK_WINDOW(win));
    pump(win, 40);

    g_print("mapped: %d, draw calls: %d\n", gtk_widget_get_mapped(win), draw_calls);

    /* ---- 3: how tall did a row become? ---- */
    GtkWidget *area = find_type(cv, GTK_TYPE_DRAWING_AREA);
    g_print("found a self-drawing cell: %s\n", area ? "yes" : "no");
    if ( area )
    {
        g_print("  its own height: %d (it asked for 40)\n",
                gtk_widget_get_height(area));
        g_print("  the row it sits in: %d\n", row_height_of(area));
        g_print("  -> a self-drawing cell that asks for more than the text\n"
                "     rows need %s the row taller.\n",
                row_height_of(area) >= 40 ? "DOES make" : "does NOT make");

        /* Can wx impose a height downwards, the way SetRowHeight() must?
           GtkColumnView has no uniform-height setting -- there is no
           gtk_column_view_set_fixed_height -- so the only lever is the cell
           widget's own size request. */
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), 12);
        gtk_widget_set_size_request(area, -1, 12);
        pump(win, 20);
        g_print("  after asking the cell for 12: cell %d, row %d %s\n",
                gtk_widget_get_height(area), row_height_of(area),
                row_height_of(area) <= 25
                    ? "-> a height CAN be imposed downwards"
                    : "-> the row did NOT shrink");
    }

    /* ---- 1: the drop-down ---- */
    const char * const choices[] = { "one", "two", "three", NULL };
    GtkWidget *dd = gtk_drop_down_new_from_strings(choices);
    g_print("drop-down initial selected: %u (GTK_INVALID_LIST_POSITION=%u)\n",
            gtk_drop_down_get_selected(GTK_DROP_DOWN(dd)),
            GTK_INVALID_LIST_POSITION);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), GTK_INVALID_LIST_POSITION);
    g_print("drop-down after asking for no selection: %u  -> %s\n",
            gtk_drop_down_get_selected(GTK_DROP_DOWN(dd)),
            gtk_drop_down_get_selected(GTK_DROP_DOWN(dd)) == GTK_INVALID_LIST_POSITION
                ? "CAN be deselected" : "CANNOT be deselected (this is #183)");
    g_print("  a cell always has a value, so a renderer never needs to ask\n"
            "  for no selection -- unlike wxChoice, which its tests do.\n");
    g_object_ref_sink(dd);
    g_object_unref(dd);

    /* ---- 2: editing ---- */
    GtkWidget *someLabel = find_type(cv, GTK_TYPE_EDITABLE_LABEL);
    g_print("found an editable label in a cell: %s\n", someLabel ? "yes" : "no");
    if ( someLabel )
    {
        gtk_editable_label_start_editing(GTK_EDITABLE_LABEL(someLabel));
        pump(win, 10);
        gtk_editable_label_stop_editing(GTK_EDITABLE_LABEL(someLabel), TRUE);
        pump(win, 10);
        g_print("editing notifications: started=%d done=%d  -> %s\n",
                edit_started, edit_done,
                edit_started && edit_done
                    ? "the wx editing protocol has somewhere to hang"
                    : "NO -- editing cannot be observed this way");
    }

    return 0;
}
