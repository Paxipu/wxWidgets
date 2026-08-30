/* #180: can GtkListView plus a GListModel do everything wxListBox needs,
 * before 1000 lines of listbox.cpp are rewritten on the assumption that it can?
 *
 * This is the fourth of the deprecation families to be checked this way and the
 * first where the answer is yes. The other three each failed on something the
 * successor cannot do: a widget snapshot needs a mapped toplevel (#181), GtkFileDialog
 * has no extra widget, preview or shortcut folders (#182), and GtkDropDown cannot
 * be deselected while its model is non-empty (#183). So this asks rather than assumes.
 *
 * Measured here, GTK 4.22.4:
 *
 *   unselect_item(2)   -> INVALID      wxListBox::SetSelection(wxNOT_FOUND)
 *   MultiSelection     -> 2 selected   wxLB_MULTIPLE / wxLB_EXTENDED
 *   scroll_to(40)      -> ok           wxListBox::EnsureVisible()
 *   HitTest y=10..90   -> 0,1,2,3,4    wxListBox::HitTest(), one row per 20px
 *   outside            -> -1
 *
 * Two things that are wrong by default and cost an hour each:
 *
 *  - GtkListView has no hit test of its own. gtk_widget_pick() does it, but only
 *    with GTK_PICK_NON_TARGETABLE: the label inside a row is not targetable, and
 *    with GTK_PICK_DEFAULT the pick stops at the GtkListView itself and every
 *    query answers "nothing here".
 *
 *  - The rows have to be ALLOCATED, and they are not allocated by pumping the
 *    main loop non-blocking. g_main_context_iteration(NULL, FALSE) returns at
 *    once when nothing is pending, so the frame clock never ticks and every row
 *    keeps the unallocated 4x4 bounds it was created with -- 50 row widgets,
 *    all mapped, all can-target, all at (-2,-2). Blocking iteration lays them
 *    out and the hit test starts working. In an application this holds anyway,
 *    because the list is on screen; it matters for anyone writing a test.
 *
 * Also worth knowing: gtk_single_selection_set_autoselect(FALSE) after
 * construction is too late -- the model has already selected item 0 by then.
 *
 * Build and run (needs a display; the rows must be realized and laid out):
 *
 *   gcc -o probe180 gtk4-listview-vs-listbox.c $(pkg-config --cflags --libs gtk4)
 *   xvfb-run -a ./probe180
 *   PROBE_DIAG=1 xvfb-run -a ./probe180     # shows what each pick() returned
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

static GtkWidget *g_win, *g_lv;
static GtkStringList *g_items;
static int done;

static void setup(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d)
{ (void)f;(void)d; gtk_list_item_set_child(li, gtk_label_new(NULL)); }
static void bind(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d)
{
    (void)f;(void)d;
    GtkStringObject *so = GTK_STRING_OBJECT(gtk_list_item_get_item(li));
    gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(li)), gtk_string_object_get_string(so));
}

/* wxListBox::DoListHitTest(): which item is at (x, y)? */
static int hit_test(double x, double y)
{
    GtkWidget *w = gtk_widget_pick(g_lv, x, y, (GtkPickFlags)(GTK_PICK_NON_TARGETABLE | GTK_PICK_INSENSITIVE));
    if (getenv("PROBE_DIAG"))
    {
        printf("      pick(%.0f,%.0f) -> %s", x, y, w ? G_OBJECT_TYPE_NAME(w) : "NULL");
        for (GtkWidget *p = w; p && p != g_lv; p = gtk_widget_get_parent(p))
            printf(" < %s%s", G_OBJECT_TYPE_NAME(p),
                   g_object_get_data(G_OBJECT(p), "li") ? "[LI]" : "");
        printf("\n");
    }
    /* Walk up from the picked widget to its GtkListItem. */
    for (; w && w != g_lv; w = gtk_widget_get_parent(w))
    {
        GtkListItem *li = GTK_LIST_ITEM(g_object_get_data(G_OBJECT(w), "li"));
        if (li) return (int)gtk_list_item_get_position(li);
    }
    return -1;
}
static void setup2(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d)
{
    (void)f;(void)d;
    GtkWidget *lbl = gtk_label_new(NULL);
    g_object_set_data(G_OBJECT(lbl), "li", li);
    gtk_list_item_set_child(li, lbl);
}

static gboolean run(gpointer d)
{
    (void)d;
    GtkSelectionModel *sel = GTK_SELECTION_MODEL(
        gtk_single_selection_new(G_LIST_MODEL(g_object_ref(g_items))));
    gtk_single_selection_set_autoselect(GTK_SINGLE_SELECTION(sel), FALSE);
    gtk_single_selection_set_can_unselect(GTK_SINGLE_SELECTION(sel), TRUE);
    gtk_list_view_set_model(GTK_LIST_VIEW(g_lv), sel);

    printf("PROBE GtkListView against what wxListBox needs\n");

    guint s = gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(sel));
    printf("  fresh, autoselect=FALSE      sel=%s\n", s==GTK_INVALID_LIST_POSITION?"INVALID (as wxListBox starts)":"set");

    gtk_selection_model_select_item(sel, 2, TRUE);
    printf("  select_item(2)               sel=%u\n", gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(sel)));

    gtk_selection_model_unselect_item(sel, 2);
    s = gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(sel));
    printf("  unselect_item(2)             sel=%s\n", s==GTK_INVALID_LIST_POSITION?"INVALID (deselection works)":"SET (deselection does NOT work)");

    /* Multiple selection. */
    GtkSelectionModel *msel = GTK_SELECTION_MODEL(
        gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(g_items))));
    gtk_selection_model_select_item(msel, 1, FALSE);
    gtk_selection_model_select_item(msel, 3, FALSE);
    int n=0; for (guint i=0;i<g_list_model_get_n_items(G_LIST_MODEL(msel));i++)
        if (gtk_selection_model_is_selected(msel, i)) n++;
    printf("  MultiSelection, 2 selected   n=%d %s\n", n, n==2?"(ok)":"(WRONG)");
    g_object_unref(msel);

    /* Hit testing and geometry need a mapped window. */
    for (int i=0;i<300 && gtk_widget_get_width(g_lv)==0;i++) g_main_context_iteration(NULL,FALSE);
    printf("  list view allocated          %dx%d\n", gtk_widget_get_width(g_lv), gtk_widget_get_height(g_lv));

    /* Rows only appear once the view has actually drawn. */
    for (int round=0; round<10; round++)
    {
        /* Blocking: otherwise the frame clock never ticks and nothing is allocated. */
        for (int i=0;i<60;i++) g_main_context_iteration(NULL,TRUE);
        int kids=0;
        for (GtkWidget *c=gtk_widget_get_first_child(g_lv); c; c=gtk_widget_get_next_sibling(c)) kids++;
        printf("  round %d: row widgets=%d\n", round, kids);
        graphene_rect_t b; GtkWidget *c0 = gtk_widget_get_first_child(g_lv);
        if (c0 && gtk_widget_compute_bounds(c0, g_lv, &b) && b.size.height > 4)
        { printf("  round %d: rows allocated (%.0fx%.0f)\n", round, b.size.width, b.size.height); break; }
    }
    {
        int i=0;
        for (GtkWidget *c=gtk_widget_get_first_child(g_lv); c && i<3; c=gtk_widget_get_next_sibling(c), i++)
        {
            graphene_rect_t b;
            const gboolean ok = gtk_widget_compute_bounds(c, g_lv, &b);
            printf("  row %d: %s bounds=%s %.0f,%.0f %.0fx%.0f  can_target=%d mapped=%d\n",
                   i, G_OBJECT_TYPE_NAME(c), ok?"ok":"FEHLT",
                   b.origin.x, b.origin.y, b.size.width, b.size.height,
                   gtk_widget_get_can_target(c), gtk_widget_get_mapped(c));
        }
    }
    printf("  HitTest:");
    int ok = 1;
    for (int i = 0; i < 5; i++)
    {
        const double y = i * 20 + 10;      /* centre of row i */
        const int h = hit_test(20, y);
        printf("  y=%.0f->%d", y, h);
        if (h != i) ok = 0;
    }
    const int hOut = hit_test(20, 100000);
    printf("  outside->%d\n", hOut);
    printf("  VERDICT HitTest %s\n", (ok && hOut < 0) ? "usable" : "UNUSABLE");

    /* Scrolling to an item -- EnsureVisible(). */
    gtk_list_view_scroll_to(GTK_LIST_VIEW(g_lv), 40, GTK_LIST_SCROLL_NONE, NULL);
    printf("  scroll_to(40)                did not crash\n");

    fflush(stdout); done=1; return G_SOURCE_REMOVE;
}

int main(int c, char**v)
{
    (void)c;(void)v;
    if (!gtk_init_check()) { printf("NO DISPLAY\n"); return 2; }
    g_items = gtk_string_list_new(NULL);
    for (int i=0;i<50;i++) { char b[16]; g_snprintf(b,sizeof b,"Item %02d",i); gtk_string_list_append(g_items,b); }
    GtkListItemFactory *f = gtk_signal_list_item_factory_new();
    g_signal_connect(f, "setup", G_CALLBACK(setup2), NULL);
    g_signal_connect(f, "bind",  G_CALLBACK(bind),  NULL);
    g_lv = gtk_list_view_new(NULL, f);
    GtkWidget *sw = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sw), g_lv);
    g_win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(g_win), 200, 300);
    gtk_window_set_child(GTK_WINDOW(g_win), sw);
    gtk_window_present(GTK_WINDOW(g_win));
    (void)setup;
    g_idle_add(run, NULL);
    for (int i=0;i<8000 && !done;i++) g_main_context_iteration(NULL, TRUE);
    return done?0:3;
}
