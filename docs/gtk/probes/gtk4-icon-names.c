/* Which of the icon names src/gtk/artgtk.cpp asks the icon theme for does the
 * theme actually have?
 *
 * The table in that file gives each wxART_* id a GTK stock id, used by GTK+ 2,
 * and an icon name, used by GTK+ 3 and GTK4. Two of the icon names were stock
 * ids as well -- "gtk-refresh" and "gtk-stop" -- and no icon theme has ever
 * shipped those. Under GTK+ 3 they still worked, because that build tries the
 * stock icons GTK carries built in before it tries the theme; GTK4 removed
 * stock icons altogether, so there the lookup could only fail and wxART_REFRESH
 * and wxART_STOP quietly fell back to wx's own bitmaps.
 *
 * Nothing warns about that: a name the theme does not have is not an error, it
 * is an application using its own icon. Hence this, which asks about every name
 * in the table at once.
 *
 * Build against either GTK, and compare -- the two builds have to be given the
 * same theme to be comparable, so run them on the same display:
 *
 *   gcc -DGTK4 -o probe4 gtk4-icon-names.c $(pkg-config --cflags --libs gtk4)
 *   gcc       -o probe3 gtk4-icon-names.c $(pkg-config --cflags --libs gtk+-3.0)
 *
 * Build a probe against the same GTK the library uses, or it is measuring a
 * different program: see the note on gtk4-dropdown-deselection.c.
 */
#include <gtk/gtk.h>

static const char* const names[] = {
    "dialog-error", "dialog-information", "dialog-warning", "dialog-question",
    "preferences-desktop-font", "folder", "text-x-generic", "image-missing",
    "list-add", "list-remove", "go-previous", "go-next", "go-up", "go-down",
    "go-home", "go-first", "go-last", "document-open", "document-print",
    "help-contents", "folder-new", "folder-open", "system-run",
    "media-floppy", "media-optical", "drive-harddisk", "drive-removable-media",
    "document-save", "document-save-as", "edit-copy", "edit-cut", "edit-paste",
    "edit-delete", "document-new", "edit-undo", "edit-redo", "window-close",
    "application-exit", "edit-find", "edit-find-replace", "view-fullscreen",
    "accessories-text-editor",

    /* the two that were wrong, and what they became */
    "gtk-refresh", "view-refresh",
    "gtk-stop", "process-stop",

    /* still stock ids, and still open: see the comment in artgtk.cpp */
    "gtk-apply", "gtk-cancel",
    /* candidates for them, none of which is present on every theme */
    "object-select", "emblem-ok", "emblem-default", "dialog-ok", "dialog-cancel",
};

int main(void)
{
#ifdef GTK4
    gtk_init();
    GtkIconTheme* const theme =
        gtk_icon_theme_get_for_display(gdk_display_get_default());
    g_print("GTK4, theme %s\n", gtk_icon_theme_get_theme_name(theme));
#else
    gtk_init(NULL, NULL);
    GtkIconTheme* const theme = gtk_icon_theme_get_default();
    g_print("GTK+ 3\n");
#endif

    int have = 0, miss = 0;
    for (unsigned i = 0; i < G_N_ELEMENTS(names); i++)
    {
        if (gtk_icon_theme_has_icon(theme, names[i]))
            have++;
        else
        {
            miss++;
            g_print("missing: %s\n", names[i]);
        }
    }

    g_print("--- %d present, %d missing\n", have, miss);
    return 0;
}
