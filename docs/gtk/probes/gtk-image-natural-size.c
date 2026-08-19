// Does GtkImage draw a pixbuf at its natural size, or stretch it to the
// widget? wxAnimationCtrl assumes the former: it puts the animation's frames
// into a GtkImage, and both its background colour and the wxAC_NO_AUTORESIZE
// style only mean anything if the parts of the control the image does not
// cover stay visible.
//
// The window is filled with a green CSS background and the image is a solid
// red 32x32 pixbuf in a 100x100 widget, so reading the corner pixel back off
// the screen answers the question: green means the image was drawn at its
// natural size, red means it was stretched over the whole widget.
//
// Build for either version and run under xvfb-run -a:
//     gcc -o p4 gtk-image-natural-size.c $(pkg-config --cflags --libs gtk4) -lX11
//     gcc -o p3 gtk-image-natural-size.c $(pkg-config --cflags --libs gtk+-3.0) \
//         -lX11 -Wno-deprecated-declarations
//
// Measured with GTK 3.24.41 and GTK 4.14.5:
//
//     GTK 3.24.41  GtkImage                           corner #00ff00  centre #ff0000
//     GTK 4.14.5   GtkImage                           corner #ff0000  centre #ff0000
//     GTK 4.14.5   GtkPicture content-fit SCALE_DOWN  corner #00ff00  centre #ff0000
//
// GTK4's GtkImage scales its contents up to the widget -- it is documented as
// being for icons -- while GtkPicture with GTK_CONTENT_FIT_SCALE_DOWN
// reproduces what GtkImage did under GTK3: natural size, centred, scaled down
// only when the image is too big to fit.

#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

#if GTK_CHECK_VERSION(4,0,0)
    #include <gdk/x11/gdkx.h>
#else
    #include <gdk/gdkx.h>
#endif

static GtkWidget* win;
static const char* label;

static GdkPixbuf* red_pixbuf(void)
{
    GdkPixbuf* p = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 32, 32);
    gdk_pixbuf_fill(p, 0xff0000ff);
    return p;
}

static void read_pixel(Display* d, Window root, int x, int y, char* out)
{
    XImage* img = XGetImage(d, root, x, y, 1, 1, AllPlanes, ZPixmap);
    if (!img)
    {
        strcpy(out, "??");
        return;
    }

    unsigned long px = XGetPixel(img, 0, 0);
    sprintf(out, "#%02lx%02lx%02lx", (px >> 16) & 0xff, (px >> 8) & 0xff, px & 0xff);
    XDestroyImage(img);
}

static gboolean measure(gpointer loop)
{
#if GTK_CHECK_VERSION(4,0,0)
    GdkSurface* s = gtk_native_get_surface(gtk_widget_get_native(win));
    Display* d = GDK_SURFACE_XDISPLAY(s);
    Window xw = GDK_SURFACE_XID(s);
#else
    GdkWindow* s = gtk_widget_get_window(win);
    Display* d = GDK_WINDOW_XDISPLAY(s);
    Window xw = GDK_WINDOW_XID(s);
#endif

    Window child;
    int rx = 0, ry = 0;
    XTranslateCoordinates(d, xw, DefaultRootWindow(d), 0, 0, &rx, &ry, &child);

    char corner[16], centre[16];
    read_pixel(d, DefaultRootWindow(d), rx + 2, ry + 2, corner);
    read_pixel(d, DefaultRootWindow(d), rx + 50, ry + 50, centre);

    printf("GTK %d.%d.%d  %-28s corner %s  centre %s\n",
           gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(),
           label, corner, centre);

    g_main_loop_quit((GMainLoop*)loop);
    return G_SOURCE_REMOVE;
}

static void run(const char* what, GtkWidget* child)
{
    label = what;
    win = gtk_window_new(
#if !GTK_CHECK_VERSION(4,0,0)
        GTK_WINDOW_TOPLEVEL
#endif
        );
    gtk_window_set_default_size(GTK_WINDOW(win), 100, 100);
    gtk_widget_set_size_request(child, 100, 100);

    GtkCssProvider* css = gtk_css_provider_new();
    const char* rules = "window, image, picture { background-color: #00ff00; }";
#if GTK_CHECK_VERSION(4,12,0)
    gtk_css_provider_load_from_string(css, rules);
#elif GTK_CHECK_VERSION(4,0,0)
    gtk_css_provider_load_from_data(css, rules, -1);
#else
    gtk_css_provider_load_from_data(css, rules, -1, NULL);
#endif
#if GTK_CHECK_VERSION(4,0,0)
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#else
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#endif

#if GTK_CHECK_VERSION(4,0,0)
    gtk_window_set_child(GTK_WINDOW(win), child);
    gtk_window_present(GTK_WINDOW(win));
#else
    gtk_container_add(GTK_CONTAINER(win), child);
    gtk_widget_show_all(win);
#endif

    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add(700, measure, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);

#if GTK_CHECK_VERSION(4,0,0)
    gtk_window_destroy(GTK_WINDOW(win));
#else
    gtk_widget_destroy(win);
#endif
}

int main(void)
{
#if GTK_CHECK_VERSION(4,0,0)
    if (!gtk_init_check())
#else
    if (!gtk_init_check(NULL, NULL))
#endif
    {
        printf("NO DISPLAY\n");
        return 2;
    }

    GdkPixbuf* pixbuf = red_pixbuf();

    GtkWidget* image = gtk_image_new_from_pixbuf(pixbuf);
    run("GtkImage", image);

#if GTK_CHECK_VERSION(4,8,0)
    GdkTexture* texture = gdk_texture_new_for_pixbuf(pixbuf);
    GtkWidget* picture = gtk_picture_new_for_paintable(GDK_PAINTABLE(texture));
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_SCALE_DOWN);
    run("GtkPicture content-fit SCALE_DOWN", picture);
    g_object_unref(texture);
#endif

    g_object_unref(pixbuf);
    return 0;
}
