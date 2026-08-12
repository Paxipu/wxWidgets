// GTK3 reference: the synthetic-GtkWidgetPath approach wxGtkStyleContext used.
#include <gtk/gtk.h>
#include <stdio.h>
static GtkStyleContext* ctx(GtkWidgetPath* p){
    GtkStyleContext* sc = gtk_style_context_new();
    gtk_style_context_set_path(sc, p); return sc;
}
int main(void){
    gtk_init(NULL,NULL);
    GtkBorder b,pd;
    // statbox: window > frame(.frame) > border
    GtkWidgetPath* p = gtk_widget_path_new();
    gtk_widget_path_append_type(p, GTK_TYPE_WINDOW);
    gtk_widget_path_iter_set_object_name(p,-1,"window");
    gtk_widget_path_append_type(p, GTK_TYPE_FRAME);
    gtk_widget_path_iter_set_object_name(p,-1,"frame");
    gtk_widget_path_iter_add_class(p,-1,"frame");
    gtk_widget_path_append_type(p, G_TYPE_NONE);
    gtk_widget_path_iter_set_object_name(p,-1,"border");
    GtkStyleContext* sc = ctx(p);
    gtk_style_context_get_border(sc, GTK_STATE_FLAG_NORMAL,&b);
    gtk_style_context_get_padding(sc, GTK_STATE_FLAG_NORMAL,&pd);
    printf("statbox frame>border : border=%d,%d,%d,%d padding=%d,%d,%d,%d\n",
      b.left,b.top,b.right,b.bottom, pd.left,pd.top,pd.right,pd.bottom);

    // notebook: window > notebook > header > tabs > tab
    GtkWidgetPath* q = gtk_widget_path_new();
    gtk_widget_path_append_type(q, GTK_TYPE_WINDOW);
    gtk_widget_path_iter_set_object_name(q,-1,"window");
    gtk_widget_path_append_type(q, GTK_TYPE_NOTEBOOK);
    gtk_widget_path_iter_set_object_name(q,-1,"notebook");
    gtk_widget_path_iter_add_class(q,-1,"frame");
    const char* nodes[] = {"header","tabs","tab"};
    for (int i=0;i<3;i++){
        gtk_widget_path_append_type(q, G_TYPE_NONE);
        gtk_widget_path_iter_set_object_name(q,-1,nodes[i]);
    }
    GtkStyleContext* sc2 = ctx(q);
    gtk_style_context_get_padding(sc2, GTK_STATE_FLAG_NORMAL,&pd);
    gtk_style_context_get_margin(sc2, GTK_STATE_FLAG_NORMAL,&b);
    printf("notebook tab        : padding=%d,%d,%d,%d margin=%d,%d,%d,%d\n",
      pd.left,pd.top,pd.right,pd.bottom, b.left,b.top,b.right,b.bottom);
    return 0;
}
