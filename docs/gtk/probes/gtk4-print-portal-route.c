/*
 * Which route does gtk_print_operation_run() take under GTK4, and does it
 * come back?
 *
 * wxGTK calls gtk_print_operation_run() from wxGtkPrintDialog::ShowModal().
 * Under GTK4 that call may be served either by the in-process
 * GtkPrintUnixDialog or by xdg-desktop-portal over D-Bus, and the two behave
 * very differently when something goes wrong: the in-process dialog is a
 * window on our own display, while the portal spins a nested main loop and
 * waits for a reply from another process that may be showing its dialog
 * somewhere we cannot see or reach.
 *
 * This probe answers, by measurement rather than from the documentation:
 *
 *   1. Does the call return at all, and after how long?
 *   2. Which result does it report, and with what error?
 *   3. Does GDK_DEBUG=no-portals actually change the route?
 *
 * The route is reported from inside the process rather than inferred from a
 * D-Bus capture: while gtk_print_operation_run() spins its nested main loop,
 * a timeout fires inside that loop and asks GTK for its toplevel windows. An
 * in-process GtkPrintUnixDialog is one of them; a portal dialog belongs to
 * another process and never shows up here.
 *
 * The wall-clock number matters as much as the result code: "hangs forever"
 * and "blocks for the 25 s D-Bus timeout" look identical to someone watching
 * an application, but only one of them is a defect that outlives the test
 * environment.
 *
 * Environment:
 *   PROBE_TIMEOUT   seconds before the probe gives up on the call (default 40)
 *   PROBE_PEEK      seconds to wait before looking for a dialog (default 5)
 *   PROBE_NODIALOG  set to 1 to use ACTION_PRINT instead of ACTION_PRINT_DIALOG
 *
 * Note that getenv() returning a non-NULL empty string is not the same as the
 * variable being set to a number, hence the atoi() guard on every read.
 *
 * Build:
 *   gcc -o print-route gtk4-print-portal-route.c $(pkg-config --cflags --libs gtk4)
 */

#include <gtk/gtk.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int env_int(const char *name, int fallback)
{
    const char *v = getenv(name);
    return (v && *v) ? atoi(v) : fallback;
}

/* Reached only if the call under test never comes back. Must stay
 * async-signal-safe, so no printf() and no exit(). */
static void on_alarm(int sig)
{
    static const char msg[] = "PROBE_RESULT=blocked\nPROBE_EXIT=2\n";
    ssize_t ignored = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    (void)ignored;
    (void)sig;
    _exit(2);
}

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Runs inside the nested main loop that gtk_print_operation_run() spins, which
 * is the only moment at which the question "is there a dialog?" has an answer. */
static gboolean report_route(gpointer data)
{
    (void)data;

    GListModel *toplevels = gtk_window_get_toplevels();
    const guint n = g_list_model_get_n_items(toplevels);

    printf("PROBE_TOPLEVELS=%u\n", n);
    for (guint i = 0; i < n; i++)
    {
        GtkWindow *w = GTK_WINDOW(g_list_model_get_item(toplevels, i));
        printf("  toplevel: [%s]\n", gtk_window_get_title(w) ? gtk_window_get_title(w) : "");
        g_object_unref(w);
    }

    /* A dialog of our own means GTK served the call itself. No window at all,
     * while the call is still blocked, means somebody else was asked. */
    printf("PROBE_ROUTE=%s\n", n > 0 ? "in-process" : "portal-or-stalled");
    fflush(stdout);

    return G_SOURCE_REMOVE;
}

static void begin_print(GtkPrintOperation *op, GtkPrintContext *ctx, gpointer d)
{
    (void)ctx; (void)d;
    gtk_print_operation_set_n_pages(op, 1);
}

static void draw_page(GtkPrintOperation *op, GtkPrintContext *ctx,
                      int page, gpointer d)
{
    (void)op; (void)ctx; (void)page; (void)d;
    /* Nothing to draw; we are testing the dialog route, not the output. */
}

static const char *result_name(GtkPrintOperationResult r)
{
    switch (r)
    {
        case GTK_PRINT_OPERATION_RESULT_ERROR:       return "ERROR";
        case GTK_PRINT_OPERATION_RESULT_APPLY:       return "APPLY";
        case GTK_PRINT_OPERATION_RESULT_CANCEL:      return "CANCEL";
        case GTK_PRINT_OPERATION_RESULT_IN_PROGRESS: return "IN_PROGRESS";
        default:                                     return "?";
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    if (!gtk_init_check())
    {
        printf("PROBE_RESULT=no-display\nPROBE_EXIT=3\n");
        return 3;
    }

    printf("gtk %d.%d.%d, GDK_DEBUG=[%s]\n",
           gtk_get_major_version(), gtk_get_minor_version(),
           gtk_get_micro_version(),
           getenv("GDK_DEBUG") ? getenv("GDK_DEBUG") : "");

    GtkPrintOperation *op = gtk_print_operation_new();
    g_signal_connect(op, "begin-print", G_CALLBACK(begin_print), NULL);
    g_signal_connect(op, "draw-page", G_CALLBACK(draw_page), NULL);

    const GtkPrintOperationAction action =
        env_int("PROBE_NODIALOG", 0) ? GTK_PRINT_OPERATION_ACTION_PRINT
                                     : GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG;
    printf("action=%s\n",
           action == GTK_PRINT_OPERATION_ACTION_PRINT ? "PRINT" : "PRINT_DIALOG");
    fflush(stdout);

    g_timeout_add_seconds((guint)env_int("PROBE_PEEK", 5), report_route, NULL);

    signal(SIGALRM, on_alarm);
    alarm((unsigned)env_int("PROBE_TIMEOUT", 40));

    GError *error = NULL;
    const double t0 = now_seconds();
    const GtkPrintOperationResult res =
        gtk_print_operation_run(op, action, NULL, &error);
    const double elapsed = now_seconds() - t0;

    alarm(0);

    printf("PROBE_RESULT=%s\n", result_name(res));
    printf("PROBE_ELAPSED=%.1f\n", elapsed);
    printf("PROBE_ERROR=%s\n", error ? error->message : "(none)");
    printf("PROBE_EXIT=0\n");

    g_clear_error(&error);
    return 0;
}
