// Mimic the rewritten wxGtkStyleContext's create/destroy cycle exactly,
// to check the unparent-deepest-first destructor neither leaks nor warns.
#include <gtk/gtk.h>
#include <stdio.h>
static int alive = 0;
static void died(gpointer d, GObject* o){ (void)d;(void)o; alive--; }

static void cycle(void)
{
    GtkWidget* root = NULL; GtkWidget* cur = NULL; GSList* created = NULL;
    // AddWindow().AddButton().AddLabel()  -- the deepest real chain in use
    GType types[] = { GTK_TYPE_WINDOW, GTK_TYPE_BUTTON, GTK_TYPE_LABEL };
    for (int i = 0; i < 3; i++) {
        GtkWidget* w = GTK_WIDGET(g_object_new(types[i], NULL));
        if (!cur) { root = w; g_object_ref_sink(root); }
        else gtk_widget_set_parent(w, cur);
        created = g_slist_prepend(created, w);
        cur = w;
        alive++; g_object_weak_ref(G_OBJECT(w), died, NULL);
    }
    // destructor
    for (GSList* p = created; p; p = p->next) {
        GtkWidget* w = GTK_WIDGET(p->data);
        if (w != root) gtk_widget_unparent(w);
    }
    g_slist_free(created);
    if (root) { if (GTK_IS_WINDOW(root)) gtk_window_destroy(GTK_WINDOW(root)); g_object_unref(root); }
}
int main(void){
    if(!gtk_init_check()){printf("NO DISPLAY\n");return 2;}
    for (int i = 0; i < 500; i++) cycle();
    printf("after 500 create/destroy cycles: %d widgets still alive %s\n",
           alive, alive ? "<-- LEAK" : "(clean)");
    return alive != 0;
}
