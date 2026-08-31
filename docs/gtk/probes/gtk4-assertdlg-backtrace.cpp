///////////////////////////////////////////////////////////////////////////////
// Name:        docs/gtk/probes/gtk4-assertdlg-backtrace.cpp
// Purpose:     Does the assert dialog's backtrace list still round-trip after
//              moving it from GtkTreeView to GtkColumnView?
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

/*
 * The assert dialog is not reachable from the test suite -- it appears only
 * when an assertion fails, and its C entry points are not exported from the
 * library -- so converting its four-column backtrace list from GtkTreeView
 * and GtkListStore to GtkColumnView and a GListStore would otherwise have gone
 * in with no functional check at all. That is exactly how #181's substitution
 * shipped drawing nothing.
 *
 * So this compiles assertdlg_gtk.cpp into itself, appends three stack frames
 * and reads them back, including the case with no source file and no line
 * number. Measured on GTK 4.22.4:
 *
 *   [1] wxFoo::Bar() foo.cpp:42
 *   [2] wxBaz::Qux() baz.cpp:7
 *   [3] main()
 *   PROBE VERDICT round-trip ok
 *
 * Build (needs the wx build directory; assertdlg_gtk.cpp and mnemonics.cpp are
 * compiled in because neither is exported):
 *
 *   B=<builddir>
 *   g++ -o adprobe gtk4-assertdlg-backtrace.cpp \
 *       $wxsrc/src/gtk/assertdlg_gtk.cpp $wxsrc/src/gtk/mnemonics.cpp \
 *       $($B/wx-config --cxxflags) $(pkg-config --cflags gtk4) \
 *       $($B/wx-config --libs core,base) $(pkg-config --libs gtk4) \
 *       -I$wxsrc/include
 *   xvfb-run -a ./adprobe
 */

#include "wx/wxprec.h"
#include "wx/app.h"
#include "wx/gtk/private/wrapgtk.h"
#include "wx/gtk/assertdlg_gtk.h"
#include <stdio.h>
#include <string.h>

class App : public wxApp
{
public:
    bool OnInit() override
    {
        GtkWidget* dlg = gtk_assert_dialog_new();
        GtkAssertDialog* ad = GTK_ASSERT_DIALOG(dlg);

        gtk_assert_dialog_append_stack_frame(ad, "wxFoo::Bar()", "foo.cpp", 42);
        gtk_assert_dialog_append_stack_frame(ad, "wxBaz::Qux()", "baz.cpp", 7);
        gtk_assert_dialog_append_stack_frame(ad, "main()", "", 0);

        gchar* bt = gtk_assert_dialog_get_backtrace(ad);
        printf("PROBE backtrace:\n---\n%s---\n", bt ? bt : "(null)");

        const bool ok = bt
            && strstr(bt, "[1] wxFoo::Bar() foo.cpp:42")
            && strstr(bt, "[2] wxBaz::Qux() baz.cpp:7")
            && strstr(bt, "[3] main()");
        printf("PROBE VERDICT %s\n", ok ? "round-trip ok" : "BROKEN");

        g_free(bt);
        gtk_window_destroy(GTK_WINDOW(dlg));
        return false;
    }
};
wxIMPLEMENT_APP(App);
