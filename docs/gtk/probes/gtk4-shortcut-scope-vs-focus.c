// When a GtkText has the focus and wants Ctrl-A for itself, does a window
// shortcut with the same trigger fire anyway?
//
// wxMenuBar installs its accelerators as a GtkShortcutController on the
// toplevel, because GTK4 removed GtkAccelGroup. The scope chosen for that
// controller decides whether the focused widget is consulted first. Under
// GTK+ 3 a focused wxTextCtrl consumed Ctrl-A and the menu accelerator did
// not run; MenuTestCase::Events checks exactly that, and it fails under
// GTK4. This asks the question with no wx involved. See wxWidgets #221.
//
//   gcc -o probe gtk4-shortcut-scope-vs-focus.c \
//       $(pkg-config --cflags --libs gtk4 x11 xtst)
//   for s in global managed local; do
//       for f in text button; do
//           PROBE_SCOPE=$s PROBE_FOCUS=$f xvfb-run -a ./probe
//       done
//   done
//
// Two controls. PROBE_FOCUS=button: nothing else wants Ctrl-A, so the
// shortcut must fire -- a run where it never fires proves only that the
// synthetic key never arrived. PROBE_SEND=0: no key is sent at all, which
// shows how much of "text-selected" is just a GtkText selecting its
// contents when it takes the focus.
//
// Reports one line per run:
//
//   SCOPE=<s> FOCUS=<f> sent=<yes|no> shortcut=<yes|no>
//       text-selected=<yes|no|n/a> bubble-saw-key=<yes|no>
//
// Measured with GTK 4.14.5 on X11, GtkText focused and holding "Testing":
//
//   scope     key sent   shortcut fired   text selected all
//   global    yes        yes              yes
//   global    no         no               no
//   managed   yes        yes              yes
//   managed   no         no               no
//   local     yes        yes              yes
//   local     no         no               no
//
// So the focused GtkText acts on Ctrl-A *and* the window shortcut runs, and
// no scope changes that. The key also reaches an ordinary bubble-phase key
// controller on the window either way, so nothing along that path treats it
// as consumed. Whatever wxGTK4 wants here, GTK will not do by being asked
// for a different scope.

#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static gboolean g_shortcut_fired = FALSE;
static gboolean g_bubble_saw_key = FALSE;
static GtkWidget *g_text, *g_button;

// Reached only if the focused widget let the key carry on up the tree.
static gboolean
on_bubble_key(GtkEventControllerKey*, guint keyval, guint,
              GdkModifierType state, gpointer)
{
    if ( keyval == GDK_KEY_a && (state & GDK_CONTROL_MASK) )
        g_bubble_saw_key = TRUE;
    return FALSE;
}

static gboolean
on_shortcut(GtkWidget*, GVariant*, gpointer)
{
    g_shortcut_fired = TRUE;
    return TRUE;
}

static gboolean
clear_selection(gpointer)
{
    gtk_editable_select_region(GTK_EDITABLE(g_text), 0, 0);
    return G_SOURCE_REMOVE;
}

// Send Ctrl-A through the X server, the same way wxUIActionSimulator does.
static gboolean
send_ctrl_a(gpointer data)
{
    // A GtkText selects its contents when it takes the focus, so clear that
    // first: otherwise a selection afterwards says nothing about Ctrl-A.
    gtk_editable_select_region(GTK_EDITABLE(g_text), 0, 0);

    Display* dpy = XOpenDisplay(NULL);
    if ( !dpy )
    {
        printf("PROBE-ERROR no X display\n");
        g_main_loop_quit((GMainLoop*)data);
        return G_SOURCE_REMOVE;
    }

    const KeyCode ctrl = XKeysymToKeycode(dpy, XK_Control_L);
    const KeyCode a = XKeysymToKeycode(dpy, XK_a);

    XTestFakeKeyEvent(dpy, ctrl, True, 0);
    XTestFakeKeyEvent(dpy, a, True, 0);
    XTestFakeKeyEvent(dpy, a, False, 0);
    XTestFakeKeyEvent(dpy, ctrl, False, 0);
    XFlush(dpy);
    XCloseDisplay(dpy);

    return G_SOURCE_REMOVE;
}

static gboolean
report_and_quit(gpointer data)
{
    const char* scope = g_getenv("PROBE_SCOPE");
    const char* focus = g_getenv("PROBE_FOCUS");
    const gboolean on_text = focus && strcmp(focus, "text") == 0;

    const char* selected = "n/a";
    if ( on_text )
    {
        int start = 0, end = 0;
        // Ctrl-A in a GtkText selects everything it holds.
        if ( gtk_editable_get_selection_bounds(GTK_EDITABLE(g_text),
                                               &start, &end) &&
             end > start )
            selected = "yes";
        else
            selected = "no";
    }

    const char* send = g_getenv("PROBE_SEND");
    printf("SCOPE=%s FOCUS=%s sent=%s shortcut=%s text-selected=%s"
           " bubble-saw-key=%s\n",
           scope ? scope : "global", focus ? focus : "text",
           (send && strcmp(send, "0") == 0) ? "no" : "yes",
           g_shortcut_fired ? "yes" : "no", selected,
           g_bubble_saw_key ? "yes" : "no");
    fflush(stdout);

    g_main_loop_quit((GMainLoop*)data);
    return G_SOURCE_REMOVE;
}

static void
on_activate(GtkApplication* app, gpointer data)
{
    GtkWidget* win = gtk_application_window_new(app);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    g_text = gtk_text_new();
    gtk_editable_set_text(GTK_EDITABLE(g_text), "Testing");
    g_button = gtk_button_new_with_label("elsewhere");

    gtk_box_append(GTK_BOX(box), g_text);
    gtk_box_append(GTK_BOX(box), g_button);
    gtk_window_set_child(GTK_WINDOW(win), box);

    GtkShortcutScope scope = GTK_SHORTCUT_SCOPE_GLOBAL;
    const char* s = g_getenv("PROBE_SCOPE");
    if ( s && strcmp(s, "managed") == 0 )
        scope = GTK_SHORTCUT_SCOPE_MANAGED;
    else if ( s && strcmp(s, "local") == 0 )
        scope = GTK_SHORTCUT_SCOPE_LOCAL;

    GtkEventController* ctrl = gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(ctrl), scope);
    gtk_shortcut_controller_add_shortcut(
        GTK_SHORTCUT_CONTROLLER(ctrl),
        gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_a, GDK_CONTROL_MASK),
                         gtk_callback_action_new(on_shortcut, NULL, NULL)));
    gtk_widget_add_controller(win, ctrl);

    // Same window, ordinary key controller, bubble phase: this answers
    // whether GtkText stops the key or merely acts on it.
    GtkEventController* keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_BUBBLE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_bubble_key), NULL);
    gtk_widget_add_controller(win, keys);

    gtk_window_present(GTK_WINDOW(win));

    const char* focus = g_getenv("PROBE_FOCUS");
    if ( focus && strcmp(focus, "button") == 0 )
        gtk_widget_grab_focus(g_button);
    else
        gtk_widget_grab_focus(g_text);

    // Let the window map and take the X input focus before typing into it.
    // PROBE_SEND=0 skips the key: a GtkText selects its contents when it
    // takes focus, so without this control "text-selected" says nothing
    // about whether Ctrl-A was handled.
    const char* send = g_getenv("PROBE_SEND");
    if ( !send || strcmp(send, "0") != 0 )
        g_timeout_add(700, send_ctrl_a, data);
    else
        g_timeout_add(700, clear_selection, data);
    g_timeout_add(1500, report_and_quit, data);
}

int main(void)
{
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    GtkApplication* app =
        gtk_application_new("org.wx.probe.shortcutscope",
                            G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), loop);

    g_application_register(G_APPLICATION(app), NULL, NULL);
    g_application_activate(G_APPLICATION(app));
    g_main_loop_run(loop);

    return 0;
}
