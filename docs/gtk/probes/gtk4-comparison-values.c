// GTK4: the new real-widget approach (mirrors the rewritten wxGtkStyleContext).
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
static GtkWidget* find(GtkWidget* p,const char* n){
    for(GtkWidget* c=gtk_widget_get_first_child(p);c;c=gtk_widget_get_next_sibling(c)){
        const char* s=gtk_widget_get_css_name(c);
        if(s&&!strcmp(s,n))return c;
        GtkWidget* f=find(c,n); if(f)return f;
    } return NULL;
}
int main(void){
    if(!gtk_init_check()){printf("NO DISPLAY\n");return 2;}
    GtkBorder b,pd;
    // statbox: frame, then descend to "border" (absent -> stay on frame)
    GtkWidget* win=gtk_window_new(); g_object_ref_sink(win);
    GtkWidget* fr=gtk_frame_new(NULL); gtk_window_set_child(GTK_WINDOW(win),fr);
    GtkWidget* cur=fr; GtkWidget* n=find(fr,"border"); if(n)cur=n;
    GtkStyleContext* sc=gtk_widget_get_style_context(cur);
    gtk_style_context_get_border(sc,&b); gtk_style_context_get_padding(sc,&pd);
    printf("statbox frame>border : border=%d,%d,%d,%d padding=%d,%d,%d,%d%s\n",
      b.left,b.top,b.right,b.bottom, pd.left,pd.top,pd.right,pd.bottom,
      n?"":"   [no 'border' node; stayed on 'frame']");

    // notebook: real notebook with a page, descend header>tabs>tab
    GtkWidget* nb=gtk_notebook_new(); g_object_ref_sink(nb);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb),gtk_label_new(""),gtk_label_new(""));
    GtkWidget* c2=nb;
    const char* nodes[]={"header","tabs","tab"};
    for(int i=0;i<3;i++){GtkWidget* f=find(c2,nodes[i]); if(f)c2=f;}
    GtkStyleContext* s2=gtk_widget_get_style_context(c2);
    gtk_style_context_get_padding(s2,&pd); gtk_style_context_get_margin(s2,&b);
    printf("notebook tab        : padding=%d,%d,%d,%d margin=%d,%d,%d,%d\n",
      pd.left,pd.top,pd.right,pd.bottom, b.left,b.top,b.right,b.bottom);
    return 0;
}
