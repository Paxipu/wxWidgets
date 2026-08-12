// Probe 3: lifecycle/ownership + node availability questions that determine
// the implementation shape of a widget-backed wxGtkStyleContext.
#include <gtk/gtk.h>
#include <stdio.h>

static const char* nameof(GtkWidget* w)
{ return w ? gtk_widget_get_css_name(w) : "(null)"; }

int main(void)
{
    if (!gtk_init_check()) { printf("NO DISPLAY\n"); return 2; }

    printf("(a) does 'tab' exist on a notebook with NO pages?\n");
    GtkWidget* nb = gtk_notebook_new();
    GtkWidget* h = gtk_widget_get_first_child(nb);
    printf("  empty notebook first child = %s\n", nameof(h));
    if (h) {
        GtkWidget* tabs = gtk_widget_get_first_child(h);
        printf("  -> %s -> %s\n", nameof(tabs),
               tabs ? nameof(gtk_widget_get_first_child(tabs)) : "-");
    }

    printf("\n(b) scrollbar node names (settings.cpp expects contents/trough/slider)\n");
    GtkWidget* sb = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, NULL);
    for (GtkWidget* w = sb; w; w = gtk_widget_get_first_child(w))
        printf("  %s\n", nameof(w));

    printf("\n(c) ownership: does an unparented widget need ref_sink + unref?\n");
    GtkWidget* orphan = gtk_button_new();
    printf("  fresh button: is_floating=%d refcount=%u\n",
           g_object_is_floating(orphan), G_OBJECT(orphan)->ref_count);
    g_object_ref_sink(orphan);
    printf("  after ref_sink: is_floating=%d refcount=%u\n",
           g_object_is_floating(orphan), G_OBJECT(orphan)->ref_count);

    printf("\n(d) does unreffing a root free its children? (parent owns child)\n");
    GtkWidget* win = gtk_window_new();
    GtkWidget* child = gtk_button_new();
    gtk_window_set_child(GTK_WINDOW(win), child);
    g_object_add_weak_pointer(G_OBJECT(child), (gpointer*)&child);
    printf("  child alive before window destroy: %s\n", child ? "yes" : "no");
    gtk_window_destroy(GTK_WINDOW(win));
    printf("  child alive after gtk_window_destroy: %s\n", child ? "yes" : "no");

    printf("\n(e) gtk_widget_measure as a min-width replacement (settings.cpp GetNodeWidth)\n");
    GtkWidget* sb2 = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, NULL);
    int min = 0, nat = 0;
    gtk_widget_measure(sb2, GTK_ORIENTATION_HORIZONTAL, -1, &min, &nat, NULL, NULL);
    printf("  scrollbar measure width: min=%d nat=%d\n", min, nat);

    printf("\n(f) can we reach treeview header buttons? (AddTreeviewHeaderButton)\n");
    GtkWidget* tv = gtk_tree_view_new();
    GtkTreeViewColumn* c1 = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(c1, "c1");
    gtk_tree_view_append_column(GTK_TREE_VIEW(tv), c1);
    for (GtkWidget* w = gtk_widget_get_first_child(tv); w;
         w = gtk_widget_get_next_sibling(w))
        printf("  treeview child: %s (%s)\n", nameof(w), G_OBJECT_TYPE_NAME(w));

    return 0;
}
