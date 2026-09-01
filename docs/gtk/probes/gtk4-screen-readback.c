/* wxScreenDC does nothing under GTK4.
 *
 * GTK4 removed GdkWindow, and with it gdk_get_default_root_window() and
 * gdk_cairo_create(), which is how the GTK+ 3 build obtained a cairo context
 * for the screen. src/gtk/dc.cpp says so and leaves the drawing to go
 * nowhere, which means an application that takes a screenshot gets a black
 * image with no error -- measured: under GTK+ 3 every control reads back its
 * real colours, under GTK4 every pixel is (0,0,0).
 *
 * docs/gtk/gtk4-status.md records this as needing "a scope decision (X11-only
 * fallback, or unsupported under GTK4)". This is the measurement that decision
 * needs: can cairo still be pointed at the X root window directly, with no
 * GdkWindow in between?
 *
 * Build against the same GTK the library uses, or this is measuring a
 * different program:
 *   gcc -o probe gtk4-screen-readback.c $(pkg-config --cflags --libs gtk4 cairo-xlib)
 *   ldd probe | grep gtk        # must be the GTK wx links against
 */
#include <gtk/gtk.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#include <cairo-xlib.h>

int main(void)
{
    gtk_init();

    GdkDisplay* const display = gdk_display_get_default();
    if ( !GDK_IS_X11_DISPLAY(display) )
    {
        g_print("not an X11 display -- nothing to try\n");
        return 77;
    }

    Display* const xdpy = gdk_x11_display_get_xdisplay(display);
    const Window root = DefaultRootWindow(xdpy);

    XWindowAttributes attrs;
    if ( !XGetWindowAttributes(xdpy, root, &attrs) )
    {
        g_print("XGetWindowAttributes failed\n");
        return 1;
    }
    g_print("root window is %dx%d\n", attrs.width, attrs.height);

    /* A window with a loud colour, so that what is read back can be told from
       an empty screen. */
    GtkWidget* const win = gtk_window_new();
    GtkCssProvider* const css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "window { background-color: rgb(255,0,255); }", -1);
    gtk_style_context_add_provider_for_display(display,
                                               GTK_STYLE_PROVIDER(css),
                                               GTK_STYLE_PROVIDER_PRIORITY_USER);
    gtk_window_set_default_size(GTK_WINDOW(win), 200, 120);
    gtk_window_present(GTK_WINDOW(win));
    for ( int i = 0; i < 400 && g_main_context_iteration(NULL, FALSE); i++ )
        ;

    cairo_surface_t* const screen =
        cairo_xlib_surface_create(xdpy, root, attrs.visual,
                                  attrs.width, attrs.height);
    if ( cairo_surface_status(screen) != CAIRO_STATUS_SUCCESS )
    {
        g_print("cairo_xlib_surface_create failed: %s\n",
                cairo_status_to_string(cairo_surface_status(screen)));
        return 1;
    }

    /* Read it back the way wxDC::Blit() would: copy a piece of the screen
       into an image surface and look at it. */
    cairo_surface_t* const shot =
        cairo_image_surface_create(CAIRO_FORMAT_RGB24, 60, 40);
    cairo_t* const cr = cairo_create(shot);
    cairo_set_source_surface(cr, screen, -20, -20);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(shot);

    const unsigned char* const data = cairo_image_surface_get_data(shot);
    const guint32 px = *(const guint32*)data;
    const int r = (px >> 16) & 0xff, g = (px >> 8) & 0xff, b = px & 0xff;

    g_print("pixel read back from the root window: %d,%d,%d -> %s\n",
            r, g, b,
            (r > 200 && b > 200 && g < 60)
                ? "the window's colour, so the screen CAN be read"
                : "not the window's colour");

    cairo_surface_destroy(shot);
    cairo_surface_destroy(screen);

    return (r > 200 && b > 200 && g < 60) ? 0 : 1;
}

#else // !GDK_WINDOWING_X11

int main(void)
{
    g_print("built without the X11 backend\n");
    return 77;
}

#endif // GDK_WINDOWING_X11
