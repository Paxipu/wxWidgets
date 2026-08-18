// Probe: why wxPizza's size_allocate() and measure() vfuncs were never called.
//
// wxPizza derives from GtkFixed. Under GTK3 that was a convenient base which
// left layout entirely to the subclass. Under GTK4 it is not: GtkFixed installs
// a GtkFixedLayout layout manager, and gtk_widget_allocate() dispatches to the
// layout manager *instead of* the widget class vfunc:
//
//     if (priv->layout_manager != NULL)
//       gtk_layout_manager_allocate (priv->layout_manager, ...);
//     else if (GTK_WIDGET_GET_CLASS (widget)->size_allocate)
//       GTK_WIDGET_GET_CLASS (widget)->size_allocate (widget, ...);
//
// gtk_widget_measure() does the same. So a subclass which overrides either
// vfunc silently gets neither, and its children are laid out by GtkFixedLayout
// -- which, with no transforms set, allocates each child its own measured size
// at the origin. For wx that meant every child window was allocated 0x0.
//
// Nothing about this is an error: the assignment compiles, the vfunc is
// installed, and GTK simply never asks for it.
//
// Build with:
//   gcc -o gtk4-layout-manager gtk4-layout-manager.c $(pkg-config --cflags --libs gtk4)

#include <gtk/gtk.h>

static int g_allocCalled, g_measureCalled;

typedef struct { GtkFixed parent; } MyFixed;
typedef struct { GtkFixedClass parent; } MyFixedClass;

static void my_size_allocate(GtkWidget*, int, int, int)
{
    g_allocCalled++;
}

static void my_measure(GtkWidget*, GtkOrientation, int, int* mi, int* na, int*, int*)
{
    g_measureCalled++;
    *mi = *na = 50;
}

static void my_class_init(gpointer klass, gpointer)
{
    GtkWidgetClass* const wc = GTK_WIDGET_CLASS(klass);
    wc->size_allocate = my_size_allocate;
    wc->measure = my_measure;
}

static GType my_type(void)
{
    static GType t;
    if ( !t )
    {
        const GTypeInfo i = { sizeof(MyFixedClass), NULL, NULL, my_class_init,
                              NULL, NULL, sizeof(MyFixed), 0, NULL, NULL };
        t = g_type_register_static(GTK_TYPE_FIXED, "MyFixed", &i, (GTypeFlags)0);
    }
    return t;
}

// Whether to clear the layout manager, i.e. which half of the comparison to run.
static gboolean g_clear;

static gboolean report(gpointer app)
{
    g_print("  %-22s size_allocate called %d times, measure %d\n",
            g_clear ? "cleared:" : "as GtkFixed leaves it:",
            g_allocCalled, g_measureCalled);

    g_application_quit(G_APPLICATION(app));
    return G_SOURCE_REMOVE;
}

static void activate(GtkApplication* app, gpointer)
{
    GtkWidget* const win = gtk_application_window_new(app);
    GtkWidget* const f = GTK_WIDGET(g_object_new(my_type(), NULL));

    if ( g_clear )
        gtk_widget_set_layout_manager(f, NULL);

    g_print("  layout manager: %-14s",
            gtk_widget_get_layout_manager(f)
                ? G_OBJECT_TYPE_NAME(gtk_widget_get_layout_manager(f))
                : "(none)");

    gtk_widget_set_size_request(f, 200, 100);
    gtk_window_set_child(GTK_WINDOW(win), f);
    gtk_widget_set_visible(win, TRUE);

    g_timeout_add(300, report, app);
}

int main(int argc, char** argv)
{
    // Run the comparison twice in one process would need two displays' worth
    // of setup, so take the half to run from the command line instead.
    g_clear = argc > 1 && g_str_equal(argv[1], "--clear");

    GtkApplication* const a =
        gtk_application_new("org.wxwidgets.probe.layoutmanager",
                            G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(a, "activate", G_CALLBACK(activate), NULL);

    // Don't let the flag reach GApplication's own option parsing.
    int argcApp = 1;
    const int status = g_application_run(G_APPLICATION(a), argcApp, argv);
    g_object_unref(a);
    return status;
}
