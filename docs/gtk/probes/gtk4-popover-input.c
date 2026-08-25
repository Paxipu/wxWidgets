// Does a GtkPopover receive a button press when the pointer was already
// inside its parent window before the popover was shown?
//
// wxPopupWindow is a GtkPopover under GTK4 -- GTK_WINDOW_POPUP is gone and a
// popover is the only widget left whose surface may extend past its toplevel.
// This asks the question with no wx involved, so the answer can be reported
// to GTK rather than argued about here. See wxWidgets issue #138.
//
//   gcc -o probe gtk4-popover-input.c $(pkg-config --cflags --libs gtk4)
//   for m in none move; do
//       for a in 0 1; do PROBE_PRE=$m PROBE_AUTOHIDE=$a xvfb-run -a ./probe; done
//   done
//
// Needs xdotool, which is what moves the pointer: the point of the probe is
// what GTK does with a pointer it did not place itself.
//
// Measured with GTK 4.22.4 on X11:
//
//   autohide  pointer moved inside the parent first   who gets the press
//   FALSE     no                                      popover
//   FALSE     yes                                     parent
//   TRUE      no                                      popover
//   TRUE      yes                                     popover
//
// The popover's bounds are the same in all four runs (x=78 y=19 w=144 h=124)
// and the click is at (160, 60), inside it every time -- so this is not the
// click missing the popover. A popover which does not autohide simply stops
// being given the pointer once GTK has processed a motion event over its
// parent, and turning autohide on restores it.
//
// That matters because "the pointer is already in the window" is the ordinary
// case for a popup: it appears under a pointer which is already there.

#include <gtk/gtk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int gParentPress = 0;
static int gPopoverPress = 0;

static GtkWidget* gWindow;
static GtkWidget* gPopover;
static int gStep = 0;

static void on_parent_press(GtkGestureClick* gesture, int n_press,
                            double x, double y, gpointer data)
{
    (void)gesture; (void)n_press; (void)x; (void)y; (void)data;
    ++gParentPress;
}

static void on_popover_press(GtkGestureClick* gesture, int n_press,
                             double x, double y, gpointer data)
{
    (void)gesture; (void)n_press; (void)x; (void)y; (void)data;
    ++gPopoverPress;
}

static void warp(int x, int y)
{
    char cmd[128];

    snprintf(cmd, sizeof(cmd), "xdotool mousemove --sync %d %d", x, y);
    if ( system(cmd) != 0 )
        g_printerr("xdotool mousemove failed -- is it installed?\n");
}

static void click(void)
{
    if ( system("xdotool click 1") != 0 )
        g_printerr("xdotool click failed\n");
}

// One step per tick, so GTK gets to process what the previous step did.
static gboolean drive(gpointer data)
{
    const char* const pre = getenv("PROBE_PRE");

    switch ( gStep++ )
    {
        case 0:
            // With no window manager the window is at 0,0, 400x300.
            if ( pre && !strcmp(pre, "move") )
                warp(300, 250);    // inside the parent, clear of the popover
            else
                warp(900, 700);    // outside the window entirely
            break;

        case 2:
            gtk_popover_popup(GTK_POPOVER(gPopover));
            break;

        case 4:
        {
            // Report where the popover landed, so a miss reads as a miss
            // rather than as "the click was ignored".
            graphene_rect_t bounds;
            if ( gtk_widget_compute_bounds(gPopover, gWindow, &bounds) )
            {
                printf("popover bounds x=%.0f y=%.0f w=%.0f h=%.0f\n",
                       bounds.origin.x, bounds.origin.y,
                       bounds.size.width, bounds.size.height);
            }
            fflush(stdout);
            break;
        }

        case 5:
            warp(160, 60);         // inside the popover
            break;

        case 7:
            click();
            break;

        case 10:
            printf("pre=%-6s autohide=%s parent_presses=%d popover_presses=%d\n",
                   pre ? pre : "none",
                   getenv("PROBE_AUTOHIDE") ? getenv("PROBE_AUTOHIDE") : "0",
                   gParentPress, gPopoverPress);
            fflush(stdout);
            g_application_quit(G_APPLICATION(data));
            return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

static void activate(GtkApplication* app, gpointer data)
{
    (void)data;

    gWindow = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(gWindow), 400, 300);

    GtkWidget* const box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(gWindow), box);

    GtkGesture* const parentGesture = gtk_gesture_click_new();
    g_signal_connect(parentGesture, "pressed",
                     G_CALLBACK(on_parent_press), NULL);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(parentGesture));

    gPopover = gtk_popover_new();
    gtk_popover_set_has_arrow(GTK_POPOVER(gPopover), FALSE);
    gtk_popover_set_autohide(GTK_POPOVER(gPopover),
                             (getenv("PROBE_AUTOHIDE") &&
                              atoi(getenv("PROBE_AUTOHIDE"))) ? TRUE : FALSE);
    gtk_popover_set_position(GTK_POPOVER(gPopover), GTK_POS_BOTTOM);
    {
        GdkRectangle anchor = { 150, 20, 1, 1 };
        gtk_popover_set_pointing_to(GTK_POPOVER(gPopover), &anchor);
    }
    gtk_widget_set_parent(gPopover, box);

    GtkWidget* const content = gtk_label_new("popover content");
    gtk_widget_set_size_request(content, 120, 100);
    gtk_popover_set_child(GTK_POPOVER(gPopover), content);

    GtkGesture* const popoverGesture = gtk_gesture_click_new();
    g_signal_connect(popoverGesture, "pressed",
                     G_CALLBACK(on_popover_press), NULL);
    gtk_widget_add_controller(content, GTK_EVENT_CONTROLLER(popoverGesture));

    gtk_window_present(GTK_WINDOW(gWindow));

    g_timeout_add(300, drive, app);
}

int main(int argc, char** argv)
{
    GtkApplication* const app =
        gtk_application_new("org.wxwidgets.popoverinput",
                            G_APPLICATION_NON_UNIQUE);

    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    const int rc = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);

    return rc;
}
