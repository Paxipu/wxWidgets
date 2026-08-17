/*
 * gtk4-clipboard.c -- probe the GTK4 clipboard mechanics wxWidgets needs.
 *
 * GTK4 replaced the X11-style selection protocol (gtk_selection_*, GdkAtom,
 * an invisible owner widget) with GdkClipboard: content is offered as a
 * GdkContentProvider and read back ASYNCHRONOUSLY. wxClipboard's API is
 * synchronous -- GetData() must return with the data object filled -- so the
 * central question is whether an async read can be driven to completion from
 * inside a synchronous call, the way wx_gtk_dialog_run() does for dialogs.
 *
 * Also checks the pieces the port needs around that: offering several formats
 * at once, enumerating what a clipboard holds, and whether a format string
 * interned with g_intern_string() round-trips (wxDataFormat compares by
 * pointer, exactly as it did for GdkAtom).
 *
 * WHAT THIS PROBE FOUND, and it is not in the documentation:
 *
 *   The GInputStream handed back by gdk_clipboard_read_finish() must be
 *   drained ASYNCHRONOUSLY. When the clipboard is locally owned -- which is
 *   the normal case for an application reading back what it just copied --
 *   the writer feeding that stream is our own GdkContentProvider, running on
 *   the same main context. Calling the blocking g_output_stream_splice() from
 *   inside the read callback therefore deadlocks: the writer can never run,
 *   because the loop that would run it is blocked inside the callback.
 *
 *   The deadlock is also unrecoverable. A g_timeout_add() watchdog on the
 *   nested loop does NOT fire, because the loop is blocked inside our own
 *   callback and never reaches the point where it would dispatch the timeout.
 *
 *   g_output_stream_splice_async(), completing into the same nested loop,
 *   works. That is what src/gtk/clipbrd.cpp does, and
 *   build/tools/gtk4-invariants.c asserts that it keeps working -- asserting
 *   only the good pattern, since asserting the bad one would hang CI.
 *
 * A second trap, this one merely a documented annotation that is easy to
 * misread: gdk_content_provider_new_union() takes ownership of the providers
 * it is given. Unreffing them afterwards is a use-after-free which surfaces
 * later, as a crash inside gdk_content_provider_ref_formats().
 *
 *   gcc -o probe gtk4-clipboard.c $(pkg-config --cflags --libs gtk4)
 *   xvfb-run -a ./probe
 */

#include <gtk/gtk.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    GMainLoop* loop;
    GOutputStream* out;
    GBytes* bytes;
    GError* error;
} ReadData;

static void on_spliced(GObject* src, GAsyncResult* res, gpointer user_data)
{
    ReadData* d = user_data;

    g_output_stream_splice_finish(G_OUTPUT_STREAM(src), res, &d->error);
    d->bytes = g_memory_output_stream_steal_as_bytes(G_MEMORY_OUTPUT_STREAM(src));
    g_main_loop_quit(d->loop);
}

static void on_read_done(GObject* src, GAsyncResult* res, gpointer user_data)
{
    ReadData* d = user_data;
    GInputStream* stream;
    const char* mime = NULL;

    stream = gdk_clipboard_read_finish(GDK_CLIPBOARD(src), res, &mime, &d->error);
    if (!stream)
    {
        g_main_loop_quit(d->loop);
        return;
    }

    /* Async, NOT g_output_stream_splice(): see the header comment. Blocking
     * here deadlocks against our own content provider. */
    d->out = g_memory_output_stream_new_resizable();
    g_output_stream_splice_async(d->out, stream,
                                 G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
                                 G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
                                 G_PRIORITY_DEFAULT, NULL, on_spliced, d);
    g_object_unref(stream);
}

/* The thing wxClipboard::GetData() would have to do. */
static GBytes* read_sync(GdkClipboard* cb, const char* mime)
{
    const char* mimes[2];
    ReadData d;

    mimes[0] = mime;
    mimes[1] = NULL;

    d.loop = g_main_loop_new(NULL, FALSE);
    d.out = NULL;
    d.bytes = NULL;
    d.error = NULL;

    gdk_clipboard_read_async(cb, mimes, G_PRIORITY_DEFAULT, NULL,
                             on_read_done, &d);
    g_main_loop_run(d.loop);
    g_main_loop_unref(d.loop);

    if (d.error)
        g_clear_error(&d.error);
    if (d.out)
        g_object_unref(d.out);

    return d.bytes;
}

int main(void)
{
    GdkDisplay* display;
    GdkClipboard* cb;
    GdkContentProvider* providers[2];
    GdkContentProvider* provider;
    GBytes* bytes;
    GBytes* got;
    GdkContentFormats* formats;
    gsize n;
    const char* const* mimes;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!gtk_init_check()) { g_printerr("no display\n"); return 77; }

    display = gdk_display_get_default();
    cb = gdk_display_get_clipboard(display);

    /* (1) offering several formats at once, which is what a wxDataObject with
     * more than one format needs */
    bytes = g_bytes_new_static("hello", 5);
    providers[0] = gdk_content_provider_new_for_bytes("text/plain;charset=utf-8", bytes);
    providers[1] = gdk_content_provider_new_for_bytes("application/x-wx-test", bytes);
    /* Note: new_union() takes ownership of the providers passed to it, so
     * they must NOT be unreffed here -- doing so is a use-after-free that
     * only shows up later, when the clipboard asks the union for its formats. */
    provider = gdk_content_provider_new_union(providers, 2);
    g_bytes_unref(bytes);

    g_print("(1) set clipboard with a union provider: %s\n",
            gdk_clipboard_set_content(cb, provider) ? "yes" : "NO");
    g_object_unref(provider);

    for (i = 0; i < 100; i++) g_main_context_iteration(NULL, FALSE);

    /* (2) enumerating what is on the clipboard, for IsSupported()/GetAllFormats() */
    formats = gdk_clipboard_get_formats(cb);
    mimes = gdk_content_formats_get_mime_types(formats, &n);
    g_print("(2) clipboard advertises %d mime types:", (int)n);
    for (i = 0; i < (int)n && i < 6; i++) g_print(" %s", mimes[i]);
    g_print("\n");

    g_print("    contains our private format: %s\n",
            gdk_content_formats_contain_mime_type(formats, "application/x-wx-test")
                ? "yes" : "NO");

    /* (3) THE question: can an async read be completed inside a sync call? */
    got = read_sync(cb, "application/x-wx-test");
    if (got)
    {
        gsize sz = 0;
        const char* p = g_bytes_get_data(got, &sz);
        g_print("(3) synchronous read via nested main loop: yes (%d bytes, \"%.*s\")\n",
                (int)sz, (int)sz, p);
        g_bytes_unref(got);
    }
    else
    {
        g_print("(3) synchronous read via nested main loop: NO\n");
    }

    /* (4) interned format strings compare by pointer, as GdkAtom did */
    g_print("(4) g_intern_string() gives a canonical pointer: %s\n",
            g_intern_string("text/html") == g_intern_string("text/html")
                ? "yes" : "NO");

    /* (5) reading a format that is not offered must fail rather than hang */
    got = read_sync(cb, "application/x-not-offered");
    g_print("(5) reading an unavailable format returns rather than hangs: yes (%s)\n",
            got ? "unexpectedly got data" : "no data, as expected");
    if (got) g_bytes_unref(got);

    return 0;
}
