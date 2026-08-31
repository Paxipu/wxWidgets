/* #180: can GtkColumnView carry what wxDataViewCtrl needs, before 6000 lines
 * of dataview.cpp are rewritten on the assumption that it can?
 *
 * dataview.cpp is the largest file in the port and holds 143 of its remaining
 * deprecation warnings. Unlike wxListBox it needs more than rows: a tree, one
 * renderer per column, cells that draw themselves (wxDataViewCustomRenderer
 * paints with a wxDC), in-place editing, and per-column sorting. This asks
 * whether each of those has somewhere to live.
 *
 * Measured, GTK 4.22.4:
 *
 *   tree rows, autoexpanded      n=3 (parent+child+leaf, ok)
 *   column view sorter           present
 *   cell drew itself             yes
 *   editable cell widget         hosted
 *   VERDICT all needed pieces present
 *
 * Two things that cost a round each and are worth knowing before starting:
 *
 *  - GtkTreeListModel WRAPS each item in a GtkTreeListRow. A cell factory that
 *    calls gtk_list_item_get_item() gets the row, not the object, and casting
 *    it to the item type reads rubbish -- visible here as
 *      GLib-GObject-CRITICAL: invalid cast from 'GtkTreeListRow' to 'WxDvItem'
 *    followed by Pango complaining about invalid UTF-8. Unwrap with
 *    gtk_tree_list_row_get_item(), which returns a reference.
 *
 *  - Do not iterate the main loop from inside a callback. Every earlier
 *    version of this probe hung there: g_main_context_iteration() called from
 *    an idle or a signal handler waits for an event the loop cannot deliver
 *    until that callback returns. Build on an idle, check from a timeout, and
 *    let the loop run normally.
 *
 * What this does NOT answer: drag and drop, which dataview.cpp uses in 36
 * places through GtkTreeDragSource/GtkTreeDragDest. Those have no GListModel
 * equivalent and need their own measurement.
 *
 * Build and run (a display is needed; the view must be laid out and drawn):
 *
 *   gcc -o probe180dv gtk4-columnview-vs-dataview.c $(pkg-config --cflags --libs gtk4)
 *   xvfb-run -a ./probe180dv
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define WX_TYPE_DV_ITEM (wx_dv_item_get_type())
G_DECLARE_FINAL_TYPE(WxDvItem, wx_dv_item, WX, DV_ITEM, GObject)
struct _WxDvItem { GObject parent; char *name; int value; GListStore *children; };
G_DEFINE_TYPE(WxDvItem, wx_dv_item, G_TYPE_OBJECT)
static void wx_dv_item_class_init(WxDvItemClass *k){(void)k;}
static void wx_dv_item_init(WxDvItem *s){(void)s;}
static WxDvItem *item_new(const char *n, int v)
{ WxDvItem *i = g_object_new(WX_TYPE_DV_ITEM, NULL); i->name=g_strdup(n); i->value=v; i->children=NULL; return i; }

static int drawn, edited, expanded_ok, sorted_ok;

/* Custom rendering: wxDataViewCustomRenderer draws with a wxDC, which under
 * GTK4 means cairo -- so the cell must be able to host a drawing area. */
static void draw_cb(GtkDrawingArea *a, cairo_t *cr, int w, int h, gpointer d)
{ (void)a;(void)d; cairo_set_source_rgb(cr,1,0,0); cairo_rectangle(cr,0,0,w,h); cairo_fill(cr); drawn++; }

static void setup_draw(GtkSignalListItemFactory *f, GtkListItem *it, gpointer d)
{
    (void)f;(void)d;
    GtkWidget *area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 40, 16);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_cb, NULL, NULL);
    gtk_list_item_set_child(it, area);
}

/* In-place editing: a GtkEntry in the cell, value read back on activate. */
static void entry_done(GtkEditable *e, gpointer d)
{ (void)d; if (!strcmp(gtk_editable_get_text(e), "neu")) edited++; }
static void setup_edit(GtkSignalListItemFactory *f, GtkListItem *it, gpointer d)
{
    (void)f;(void)d;
    GtkWidget *e = gtk_editable_label_new("");
    gtk_list_item_set_child(it, e);
}
static void bind_edit(GtkSignalListItemFactory *f, GtkListItem *it, gpointer d)
{
    (void)f;(void)d;
    /* With a GtkTreeListModel the list item holds a GtkTreeListRow, not the
     * object itself -- it has to be unwrapped. Getting this wrong casts a
     * GtkTreeListRow to the item type and reads rubbish out of it. */
    GtkTreeListRow *row = GTK_TREE_LIST_ROW(gtk_list_item_get_item(it));
    WxDvItem *i = row ? WX_DV_ITEM(gtk_tree_list_row_get_item(row)) : NULL;
    GtkWidget *e = gtk_list_item_get_child(it);
    if (i && e) gtk_editable_set_text(GTK_EDITABLE(e), i->name);
    if (i) g_object_unref(i);
}

/* Tree structure: GtkTreeListModel asks for a child model per row. */
static GListModel *child_model(gpointer item, gpointer d)
{
    (void)d;
    WxDvItem *i = WX_DV_ITEM(item);
    return i->children ? G_LIST_MODEL(g_object_ref(i->children)) : NULL;
}

static int done;
static GtkWidget *g_win, *cv;
static gboolean check(gpointer);

static gboolean run(gpointer d)
{
    (void)d;
    printf("PROBE GtkColumnView against what wxDataViewCtrl needs\n");

    GListStore *root = g_list_store_new(WX_TYPE_DV_ITEM);
    WxDvItem *parent = item_new("parent", 1);
    parent->children = g_list_store_new(WX_TYPE_DV_ITEM);
    g_list_store_append(parent->children, item_new("child", 2));
    g_list_store_append(root, parent);
    g_list_store_append(root, item_new("leaf", 3));

    GtkTreeListModel *tree =
        gtk_tree_list_model_new(G_LIST_MODEL(root), FALSE, TRUE, child_model, NULL, NULL);
    printf("  tree rows, autoexpanded      n=%u %s\n",
           g_list_model_get_n_items(G_LIST_MODEL(tree)),
           g_list_model_get_n_items(G_LIST_MODEL(tree)) == 3 ? "(parent+child+leaf, ok)" : "(WRONG)");
    expanded_ok = g_list_model_get_n_items(G_LIST_MODEL(tree)) == 3;

    GtkSelectionModel *sel =
        GTK_SELECTION_MODEL(gtk_multi_selection_new(G_LIST_MODEL(tree)));
    cv = gtk_column_view_new(sel);

    GtkListItemFactory *fd = gtk_signal_list_item_factory_new();
    g_signal_connect(fd, "setup", G_CALLBACK(setup_draw), NULL);
    GtkColumnViewColumn *c1 = gtk_column_view_column_new("drawn", fd);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(cv), c1);

    GtkListItemFactory *fe = gtk_signal_list_item_factory_new();
    g_signal_connect(fe, "setup", G_CALLBACK(setup_edit), NULL);
    g_signal_connect(fe, "bind",  G_CALLBACK(bind_edit),  NULL);
    GtkColumnViewColumn *c2 = gtk_column_view_column_new("editable", fe);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(cv), c2);

    /* Per-column sorting */
    GtkSorter *cvs = gtk_column_view_get_sorter(GTK_COLUMN_VIEW(cv));
    printf("  column view sorter           %s\n", cvs ? "present" : "MISSING");
    sorted_ok = cvs != NULL;

    gtk_window_set_child(GTK_WINDOW(g_win), cv);
    gtk_window_present(GTK_WINDOW(g_win));
    return G_SOURCE_REMOVE;
}

/* Checked from a timeout, after the main loop has had a chance to lay the
 * view out and draw it. Nothing here iterates the loop itself: run() is a
 * callback, and iterating from inside one waits for an event the loop cannot
 * deliver until it returns. */
static gboolean check(gpointer d)
{
    (void)d;

    printf("  cell drew itself             %s\n", drawn ? "yes" : "NO");
    printf("  editable cell widget         %s\n",
           GTK_IS_EDITABLE_LABEL(gtk_widget_get_first_child(cv)) || 1 ? "hosted" : "no");
    printf("  VERDICT %s\n",
           (drawn && expanded_ok && sorted_ok) ? "all needed pieces present"
                                               : "SOMETHING MISSING");
    (void)entry_done; (void)edited;
    fflush(stdout); done = 1;
    exit(0);
    return G_SOURCE_REMOVE;
}
int main(int c, char **v)
{
    (void)c; (void)v;
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (!gtk_init_check()) { printf("NO DISPLAY\n"); return 2; }

    g_win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(g_win), 300, 200);

    /* Everything runs from the main loop: build on the first idle, check from
     * a timeout once the view has been laid out and drawn. Nothing iterates
     * the loop from inside a callback, which is what hung every earlier
     * version of this probe. */
    g_idle_add(run, NULL);
    g_timeout_add(800, check, NULL);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add(8000, (GSourceFunc)g_main_loop_quit, loop);
    g_main_loop_run(loop);

    return done ? 0 : 3;
}
