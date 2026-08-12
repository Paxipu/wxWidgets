// Probe 2: design questions for a widget-backed wxGtkStyleContext under GTK4.
//  (a) does generic g_object_new(GType) construction work for these widgets?
//  (b) does parenting change style resolution (i.e. must we build ancestor chains)?
//  (c) do state flags still affect colour queries?
#include <gtk/gtk.h>
#include <stdio.h>

static void col(const char* what, GtkWidget* w, GtkStateFlags state)
{
    GtkStyleContext* sc = gtk_widget_get_style_context(w);
    gtk_style_context_set_state(sc, state);
    GdkRGBA c;
    gtk_style_context_get_color(sc, &c);
    printf("  %-42s -> %.3f %.3f %.3f a=%.2f\n", what, c.red, c.green, c.blue, c.alpha);
}

int main(void)
{
    if (!gtk_init_check()) { printf("NO DISPLAY\n"); return 2; }

    printf("(a) generic construction via g_object_new:\n");
    GtkWidget* nb = GTK_WIDGET(g_object_new(GTK_TYPE_NOTEBOOK, NULL));
    printf("  g_object_new(GTK_TYPE_NOTEBOOK) -> %s css_name=%s\n",
           nb ? G_OBJECT_TYPE_NAME(nb) : "(null)",
           nb ? gtk_widget_get_css_name(nb) : "-");
    GtkWidget* hb = GTK_WIDGET(g_object_new(GTK_TYPE_HEADER_BAR, NULL));
    printf("  g_object_new(GTK_TYPE_HEADER_BAR) -> css_name=%s\n",
           gtk_widget_get_css_name(hb));

    printf("\n(b) does parenting change style resolution?\n");
    // standalone label vs label inside a headerbar vs inside a button
    GtkWidget* lone = gtk_label_new("x");
    col("standalone label", lone, GTK_STATE_FLAG_NORMAL);

    GtkWidget* hb2 = gtk_header_bar_new();
    GtkWidget* labelInHb = gtk_label_new("x");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(hb2), labelInHb);
    col("label parented into headerbar", labelInHb, GTK_STATE_FLAG_NORMAL);

    GtkWidget* btn = gtk_button_new();
    GtkWidget* labelInBtn = gtk_label_new("x");
    gtk_button_set_child(GTK_BUTTON(btn), labelInBtn);
    col("label parented into button", labelInBtn, GTK_STATE_FLAG_NORMAL);

    // and with the whole thing under a GtkWindow, as GTK3 code did
    GtkWidget* win = gtk_window_new();
    GtkWidget* btn2 = gtk_button_new();
    GtkWidget* labelInBtnInWin = gtk_label_new("x");
    gtk_button_set_child(GTK_BUTTON(btn2), labelInBtnInWin);
    gtk_window_set_child(GTK_WINDOW(win), btn2);
    col("label in button in window", labelInBtnInWin, GTK_STATE_FLAG_NORMAL);

    printf("\n(c) do state flags still affect colour queries?\n");
    GtkWidget* l2 = gtk_label_new("x");
    col("label NORMAL", l2, GTK_STATE_FLAG_NORMAL);
    col("label INSENSITIVE", l2, GTK_STATE_FLAG_INSENSITIVE);
    col("label SELECTED", l2, GTK_STATE_FLAG_SELECTED);

    printf("\n(d) CSS classes affect resolution on a real widget?\n");
    GtkWidget* l3 = gtk_label_new("x");
    col("label plain", l3, GTK_STATE_FLAG_NORMAL);
    gtk_widget_add_css_class(l3, "dim-label");
    col("label + .dim-label", l3, GTK_STATE_FLAG_NORMAL);

    printf("\n(e) metrics without realization, deeper nodes:\n");
    GtkWidget* nb2 = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(nb2), gtk_label_new("p"), gtk_label_new("t"));
    GtkWidget* header = gtk_widget_get_first_child(nb2);
    GtkWidget* tabs = gtk_widget_get_first_child(header);
    GtkWidget* tab = gtk_widget_get_first_child(tabs);
    GtkBorder b, p, m;
    GtkStyleContext* sc = gtk_widget_get_style_context(tab);
    gtk_style_context_get_border(sc, &b);
    gtk_style_context_get_padding(sc, &p);
    gtk_style_context_get_margin(sc, &m);
    printf("  notebook>header>tabs>tab border=%d/%d pad=%d/%d margin=%d/%d\n",
           b.left, b.top, p.left, p.top, m.left, m.top);
    return 0;
}
