// Probe: are GTK4's internal CSS nodes reachable as real child widgets?
// This determines whether wxGtkStyleContext can be rebuilt on real widgets
// (walking to the node you want) now that GtkWidgetPath is gone.
#include <gtk/gtk.h>
#include <stdio.h>

static void dump(GtkWidget* w, int depth, const char* label)
{
    if (!w) return;
    for (int i = 0; i < depth; i++) printf("  ");
    printf("%s<%s> css_name=\"%s\"", label ? label : "",
           G_OBJECT_TYPE_NAME(w), gtk_widget_get_css_name(w));

    char** classes = gtk_widget_get_css_classes(w);
    if (classes && classes[0])
    {
        printf(" classes=[");
        for (int i = 0; classes[i]; i++)
            printf("%s%s", i ? "," : "", classes[i]);
        printf("]");
    }
    g_strfreev(classes);

    // Can we get metrics off this node's style context?
    GtkStyleContext* sc = gtk_widget_get_style_context(w);
    if (sc)
    {
        GtkBorder b;
        gtk_style_context_get_border(sc, &b);
        GtkBorder p;
        gtk_style_context_get_padding(sc, &p);
        if (b.left || b.top || p.left || p.top)
            printf("  border=%d,%d padding=%d,%d", b.left, b.top, p.left, p.top);
    }
    printf("\n");

    for (GtkWidget* c = gtk_widget_get_first_child(w); c;
         c = gtk_widget_get_next_sibling(c))
        dump(c, depth + 1, NULL);
}

int main(void)
{
    if (!gtk_init_check()) { printf("NO DISPLAY - gtk_init_check failed\n"); return 2; }

    printf("=== GtkNotebook (needs header/tabs/tab per notebook.cpp) ===\n");
    GtkWidget* nb = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), gtk_label_new("page"),
                             gtk_label_new("tab-label"));
    dump(nb, 0, NULL);

    printf("\n=== GtkScrollbar (needs contents/trough/slider per settings.cpp) ===\n");
    dump(gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, NULL), 0, NULL);

    printf("\n=== GtkCheckButton (needs 'check' node per renderer.cpp) ===\n");
    dump(gtk_check_button_new(), 0, NULL);

    printf("\n=== GtkFrame (needs 'border' node per statbox.cpp) ===\n");
    dump(gtk_frame_new("t"), 0, NULL);

    printf("\n=== GtkEntry (renderer.cpp) ===\n");
    dump(gtk_entry_new(), 0, NULL);

    // Does an unparented/unrealized widget still resolve theme colours?
    printf("\n=== colour query on unrealized widget ===\n");
    GtkWidget* btn = gtk_button_new();
    GtkStyleContext* sc = gtk_widget_get_style_context(btn);
    GdkRGBA c;
    gtk_style_context_get_color(sc, &c);
    printf("button get_color -> %.3f %.3f %.3f %.3f\n", c.red, c.green, c.blue, c.alpha);
    GdkRGBA bg;
    printf("lookup theme_bg_color -> %s\n",
           gtk_style_context_lookup_color(sc, "theme_bg_color", &bg) ? "YES" : "NO");
    printf("lookup theme_base_color -> %s\n",
           gtk_style_context_lookup_color(sc, "theme_base_color", &bg) ? "YES" : "NO");
    printf("lookup borders -> %s\n",
           gtk_style_context_lookup_color(sc, "borders", &bg) ? "YES" : "NO");
    return 0;
}
