/* wxDialog::ShowModal() has to block and return a result. GTK4's dialog
   controllers are asynchronous: you hand them a callback and they return at
   once. This asks whether the two can be bridged the way wx needs -- and, just
   as importantly, whether the pieces wx relies on to *end* the wait work.

   Headless, so nobody can press a button. What can be exercised without a
   user is the machinery: does the callback arrive, does a nested main loop
   started after the call still receive it, and does cancelling produce a
   clean "the user dismissed it" rather than a hang. */
#include <gtk/gtk.h>

static GMainLoop *loop;
static int callback_ran;
static int got_colour;
static int got_cancelled;
static GdkRGBA chosen;

static void on_chosen(GObject *src, GAsyncResult *res, gpointer data)
{
    (void)data;
    callback_ran++;

    GError *error = NULL;
    GdkRGBA *rgba = gtk_color_dialog_choose_rgba_finish(GTK_COLOR_DIALOG(src),
                                                        res, &error);
    if ( rgba )
    {
        got_colour = 1;
        chosen = *rgba;
        gdk_rgba_free(rgba);
    }
    else
    {
        /* Not G_IO_ERROR. GTK4 has its own domain, and the distinction in it
           is the one wx has to make:

             GTK_DIALOG_ERROR_DISMISSED  the user closed the dialog
             GTK_DIALOG_ERROR_CANCELLED  the program cancelled the call
             GTK_DIALOG_ERROR_FAILED     something actually went wrong

           Only the last is worth reporting; the first two are an ordinary
           wxID_CANCEL. Checking G_IO_ERROR_CANCELLED instead -- the obvious
           guess -- matches none of them, so every dismissal would look like a
           failure. */
        got_cancelled = g_error_matches(error, GTK_DIALOG_ERROR,
                                        GTK_DIALOG_ERROR_DISMISSED)
                     || g_error_matches(error, GTK_DIALOG_ERROR,
                                        GTK_DIALOG_ERROR_CANCELLED);
        g_print("  finish() said: %s\n"
                "  domain is GTK_DIALOG_ERROR: %d, G_IO_ERROR: %d\n"
                "  dismissed=%d cancelled=%d failed=%d\n",
                error ? error->message : "(no error, no colour)",
                error && error->domain == GTK_DIALOG_ERROR,
                error && error->domain == G_IO_ERROR,
                g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED),
                g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED),
                g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_FAILED));
        g_clear_error(&error);
    }

    /* wx would call wxEventLoop::Exit() here. */
    if ( loop && g_main_loop_is_running(loop) )
        g_main_loop_quit(loop);
}

static gboolean cancel_it(gpointer data)
{
    g_cancellable_cancel(G_CANCELLABLE(data));
    return G_SOURCE_REMOVE;
}

int main(void)
{
    gtk_init();

    /* A parent window, as wx would have. Mapped, because a modal dialog with
       an unmapped transient parent is a different situation. */
    GtkWidget *parent = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(parent), 300, 200);
    gtk_window_present(GTK_WINDOW(parent));
    for ( int i = 0; i < 200 && !gtk_widget_get_mapped(parent); i++ )
        g_main_context_iteration(NULL, FALSE);
    g_print("parent mapped: %d\n", gtk_widget_get_mapped(parent));

    GtkColorDialog *dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_title(dialog, "Choose colour");
    gtk_color_dialog_set_with_alpha(dialog, FALSE);
    gtk_color_dialog_set_modal(dialog, TRUE);

    GCancellable *cancel = g_cancellable_new();
    GdkRGBA initial = { 0.2, 0.4, 0.6, 1.0 };

    /* The call returns immediately... */
    gtk_color_dialog_choose_rgba(dialog, GTK_WINDOW(parent), &initial,
                                 cancel, on_chosen, NULL);
    g_print("choose_rgba returned, callback_ran=%d (expected 0)\n", callback_ran);

    /* ...and only now does wx start its nested loop. If the callback could
       only be delivered to a loop that was already running when the call was
       made, this is where it would hang. */
    g_timeout_add(1500, cancel_it, cancel);

    loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    loop = NULL;

    g_print("nested loop returned: callback_ran=%d colour=%d cancelled=%d\n",
            callback_ran, got_colour, got_cancelled);
    if ( got_colour )
        g_print("  colour %.2f %.2f %.2f\n", chosen.red, chosen.green, chosen.blue);

    g_object_unref(cancel);
    g_object_unref(dialog);

    g_print("VERDICT %s\n",
            callback_ran == 1 ? "async choose bridges to a nested loop"
                              : "NO -- the callback never arrived");
    return 0;
}
