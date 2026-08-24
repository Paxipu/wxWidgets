/*
 * Probe: releasing the clipboard and claiming it again in the same turn of
 * the main loop loses what was just put on it.
 *
 * wxClipboard::Clear() releases ownership -- gdk_clipboard_set_content(NULL)
 * -- and SetData() claims it again. Applications do the two back to back all
 * the time, and tests/controls/richtextctrltest.cpp does it directly:
 *
 *     wxTheClipboard->Clear();
 *     wxTheClipboard->SetData(new wxTextDataObject(text));
 *
 * On GTK 4.14.5 under X11 that combination usually ends with an empty
 * clipboard a few main-loop iterations later. Nothing wx is involved: this
 * reproduces it with GDK alone, and shows which sequences are safe.
 *
 *   cc -o gtk4-clipboard-reclaim gtk4-clipboard-reclaim.c $(pkg-config --cflags --libs gtk4)
 *   xvfb-run -a ./gtk4-clipboard-reclaim
 *
 * Observed, ten runs of each:
 *
 *     set                    kept 10/10
 *     clear-then-set          kept 0/10   <-- the one wx used to do
 *     clear-iterate-then-set kept 10/10
 *     set-twice              kept 10/10
 *
 * So it is specifically release-then-claim without going through the main
 * loop in between: the SelectionClear caused by the release is acted on after
 * the claim has already happened, and takes the new content with it.
 * Claiming over an existing claim is fine, and so is releasing if the loop
 * runs before the next claim.
 *
 * Exits non-zero if any sequence other than clear-then-set loses the
 * clipboard, i.e. if the assumptions src/gtk/clipbrd.cpp relies on stop
 * holding.
 */

#include <gtk/gtk.h>
#include <string.h>

static void iterate(int n)
{
    for ( int i = 0; i < n; i++ )
        g_main_context_iteration(NULL, FALSE);
}

static void set_bytes(GdkClipboard* clipboard, const char* text)
{
    GBytes* bytes = g_bytes_new(text, strlen(text));
    GdkContentProvider* provider =
        gdk_content_provider_new_for_bytes("UTF8_STRING", bytes);

    gdk_clipboard_set_content(clipboard, provider);

    g_object_unref(provider);
    g_bytes_unref(bytes);
}

/* Returns true if the clipboard still holds what was put on it. */
static gboolean run_once(const char* mode)
{
    GdkDisplay* display = gdk_display_get_default();
    GdkClipboard* clipboard = gdk_display_get_clipboard(display);

    /* Start from a claim that is not the one under test, so every mode below
       begins from the same state: owned by us, with something on it. */
    set_bytes(clipboard, "starting point");
    iterate(50);

    if ( g_str_equal(mode, "set") )
    {
        set_bytes(clipboard, "hello");
    }
    else if ( g_str_equal(mode, "clear-then-set") )
    {
        gdk_clipboard_set_content(clipboard, NULL);
        set_bytes(clipboard, "hello");
    }
    else if ( g_str_equal(mode, "clear-iterate-then-set") )
    {
        gdk_clipboard_set_content(clipboard, NULL);
        iterate(50);
        set_bytes(clipboard, "hello");
    }
    else if ( g_str_equal(mode, "set-twice") )
    {
        set_bytes(clipboard, "first");
        set_bytes(clipboard, "hello");
    }

    iterate(200);

    return gdk_clipboard_is_local(clipboard);
}

int main(void)
{
    static const char* const modes[] =
    {
        "set", "clear-then-set", "clear-iterate-then-set", "set-twice"
    };

    gtk_init();

    /* A real window, as any application would have. */
    GtkWidget* window = gtk_window_new();
    gtk_widget_set_visible(window, TRUE);
    iterate(50);

    int failures = 0;

    for ( unsigned m = 0; m < G_N_ELEMENTS(modes); m++ )
    {
        int kept = 0;
        for ( int run = 0; run < 10; run++ )
        {
            if ( run_once(modes[m]) )
                kept++;
        }

        g_print("%-24s kept %2d/10\n", modes[m], kept);

        /* clear-then-set is the broken one and is expected to lose it; the
           others are what src/gtk/clipbrd.cpp relies on. */
        if ( g_str_equal(modes[m], "clear-then-set") )
            continue;

        if ( kept != 10 )
            failures++;
    }

    g_print("\n%d sequence(s) that should be reliable were not\n", failures);

    return failures != 0;
}
