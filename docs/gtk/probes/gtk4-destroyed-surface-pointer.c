/*
 * Probe: asking a GdkSurface for the pointer position after its X window is
 * gone.
 *
 * This is the hazard behind #113: GTK keeps synthesizing crossing events for a
 * toplevel that is being torn down, wx answers them by asking where the
 * pointer is, and the query reaches the X server as XIQueryPointer on a window
 * the server no longer knows. The server answers BadWindow and GDK's error
 * handler ends the process -- an application that merely closed a window dies
 * with exit(1).
 *
 * The probe destroys the X window itself, so the situation is reproduced
 * exactly rather than raced into, and reports two things that decide how the
 * guard in wxGTKImpl::GetPointerPosition() has to be built:
 *
 *   - whether gdk_surface_is_destroyed() already knows (it does not: GDK only
 *     learns about it when it processes the DestroyNotify, so that check alone
 *     cannot make the query safe), and
 *   - whether an X error trap around the query keeps the process alive.
 *
 * Build:
 *   gcc -o gtk4-destroyed-surface-pointer gtk4-destroyed-surface-pointer.c \
 *       $(pkg-config --cflags --libs gtk4 x11)
 *
 * Run (needs an X11 display; PROBE_TRAP=1 turns the error trap on):
 *   PROBE_TRAP=1 ./gtk4-destroyed-surface-pointer
 */

#include <gtk/gtk.h>
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>

static GtkWidget *window;
static guint      ticks;

static gboolean
probe(gpointer data)
{
    (void)data;

    /* Give the window a moment to be mapped: an unmapped surface has no X
     * window to destroy yet. */
    if (++ticks < 10)
        return G_SOURCE_CONTINUE;

    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    if (!surface || !GDK_IS_X11_SURFACE(surface))
    {
        printf("probe: no X11 surface, nothing to test\n");
        gtk_window_destroy(GTK_WINDOW(window));
        return G_SOURCE_REMOVE;
    }

    GdkDisplay *display = gdk_surface_get_display(surface);
    Display    *dpy     = GDK_SURFACE_XDISPLAY(surface);
    Window      xid     = GDK_SURFACE_XID(surface);

    /* Take the window away without telling GDK, which is what a teardown
     * looks like from the point of view of a query still in flight. */
    XDestroyWindow(dpy, xid);
    XSync(dpy, False);

    printf("after XDestroyWindow: gdk_surface_is_destroyed() = %s\n",
           gdk_surface_is_destroyed(surface) ? "TRUE" : "FALSE");

    GdkDevice *pointer =
        gdk_seat_get_pointer(gdk_display_get_default_seat(display));

    const char *trapVar = getenv("PROBE_TRAP");
    const int   trap    = trapVar && atoi(trapVar);

    double x = 0, y = 0;

    if (trap)
    {
        gdk_x11_display_error_trap_push(display);
        gdk_surface_get_device_position(surface, pointer, &x, &y, NULL);
        const int err = gdk_x11_display_error_trap_pop(display);

        printf("trapped query: survived, X error = %d\n", err);
    }
    else
    {
        printf("untrapped query: asking now, expect the process to die\n");
        fflush(stdout);

        gdk_surface_get_device_position(surface, pointer, &x, &y, NULL);

        printf("untrapped query: survived (no error was raised)\n");
    }

    fflush(stdout);
    exit(0);
}

static void
activate(GtkApplication *app, gpointer data)
{
    (void)data;

    window = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);
    gtk_window_present(GTK_WINDOW(window));

    g_timeout_add(100, probe, NULL);
}

int
main(int argc, char **argv)
{
    GtkApplication *app =
        gtk_application_new("org.wxwidgets.probe.destroyedsurface",
                            G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
