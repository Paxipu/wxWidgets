#include <gtk/gtk.h>
#include <gdk/x11/gdkx.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>

static int g_pressed, g_released, b_clicked;
static gboolean do_claim; static gboolean g_plain; static GtkPropagationPhase g_phase;
static GtkWidget *win, *button;

static void on_pressed(GtkGestureClick* g, int n, double x, double y, gpointer)
{ (void)n;(void)x;(void)y; g_pressed++;
  if (do_claim) gtk_gesture_set_state(GTK_GESTURE(g), GTK_EVENT_SEQUENCE_CLAIMED); }
static void on_released(GtkGestureClick*, int, double, double, gpointer){ g_released++; }
static void on_clicked(GtkButton*, gpointer){ b_clicked++; }

static gboolean inject(gpointer)
{
    GdkSurface* s = gtk_native_get_surface(gtk_widget_get_native(win));
    Display* d = GDK_SURFACE_XDISPLAY(s);
    Window xw = GDK_SURFACE_XID(s);

    /* button centre in window coords -> root coords */
    graphene_rect_t b;
    gtk_widget_compute_bounds(button, win, &b);
    int wx = (int)(b.origin.x + b.size.width/2);
    int wy = (int)(b.origin.y + b.size.height/2);

    Window child; int rx = 0, ry = 0;
    XTranslateCoordinates(d, xw, DefaultRootWindow(d), wx, wy, &rx, &ry, &child);

    XTestFakeMotionEvent(d, -1, rx, ry, 0); XFlush(d);
    XTestFakeButtonEvent(d, 1, True, CurrentTime); XFlush(d);
    XTestFakeButtonEvent(d, 1, False, CurrentTime); XFlush(d);
    return G_SOURCE_REMOVE;
}
static gboolean finish(gpointer){ gtk_window_destroy(GTK_WINDOW(win)); return G_SOURCE_REMOVE; }

static void run(const char* label, gboolean claim, GtkPropagationPhase phase)
{
    g_phase = phase; do_claim = claim; g_pressed = g_released = b_clicked = 0;
    win = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(win), 300, 200);
    button = g_plain ? gtk_drawing_area_new() : gtk_button_new_with_label("target");
    gtk_window_set_child(GTK_WINDOW(win), button);
    if (!g_plain) g_signal_connect(button, "clicked", G_CALLBACK(on_clicked), NULL);

    GtkGesture* g = gtk_gesture_click_new();
    g_signal_connect(g, "pressed", G_CALLBACK(on_pressed), NULL);
    g_signal_connect(g, "released", G_CALLBACK(on_released), NULL);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(g), g_phase);
    gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(g));

    gtk_window_present(GTK_WINDOW(win));
    g_timeout_add(500, inject, NULL);
    g_timeout_add(1500, finish, NULL);
    GMainLoop* l = g_main_loop_new(NULL, FALSE);
    g_signal_connect_swapped(win,"destroy",G_CALLBACK(g_main_loop_quit),l);
    g_main_loop_run(l); g_main_loop_unref(l);
    printf("  %-26s gesture pressed=%d released=%d | GtkButton 'clicked'=%d\n",
           label, g_pressed, g_released, b_clicked);
}
int main(void){
    if(!gtk_init_check()){printf("NO DISPLAY\n");return 2;}
    printf("GtkGestureClick added to a GtkButton, real injected X click:\n");
    g_plain = FALSE;
    run("button: BUBBLE, no claim", FALSE, GTK_PHASE_BUBBLE);
    run("button: BUBBLE, claim", TRUE, GTK_PHASE_BUBBLE);
    run("button: CAPTURE, no claim", FALSE, GTK_PHASE_CAPTURE);
    run("button: CAPTURE, claim", TRUE, GTK_PHASE_CAPTURE);
    run("button: TARGET, no claim", FALSE, GTK_PHASE_TARGET);
    printf("\nPlain widget with no competing gesture (the wxPizza case):\n");
    g_plain = TRUE;
    run("plain: BUBBLE, no claim", FALSE, GTK_PHASE_BUBBLE);
    run("plain: BUBBLE, claim", TRUE, GTK_PHASE_BUBBLE);
    return 0;
}
