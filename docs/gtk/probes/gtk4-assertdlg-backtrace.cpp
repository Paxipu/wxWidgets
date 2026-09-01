///////////////////////////////////////////////////////////////////////////////
// Name:        docs/gtk/probes/gtk4-assertdlg-backtrace.cpp
// Purpose:     Does the assert dialog still work after being moved off the
//              deprecated GtkTreeView and GtkDialog?
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
 * So this compiles assertdlg_gtk.cpp into itself and checks two things.
 *
 * The backtrace list, which moved from GtkTreeView and GtkListStore to
 * GtkColumnView and a GListStore: three frames are appended and read back,
 * including the case with no source file and no line number.
 *
 * And the buttons, which moved from GtkDialog's action area to the window's
 * own -- along with the ::response signal and gtk_dialog_run(), none of which
 * a GtkWindow has. Each button is pressed for real, from inside the nested
 * loop gtk_assert_dialog_run() spins, and the code it produces is checked. The
 * third case is the one worth having: unticking "show this dialog the next
 * time" has to turn Continue into CONTINUE_SUPPRESSING, and nothing else in
 * the tree would notice if it stopped doing so.
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

namespace
{

// The dialog builds its buttons itself now, so they are found the way anything
// else in a GTK4 widget tree is.
GtkWidget* FindButton(GtkWidget* parent, const char* label)
{
    for ( GtkWidget* child = gtk_widget_get_first_child(parent);
          child; child = gtk_widget_get_next_sibling(child) )
    {
        if ( GTK_IS_BUTTON(child) )
        {
            const char* const text = gtk_button_get_label(GTK_BUTTON(child));
            if ( text && strstr(text, label) )
                return child;
        }

        if ( GtkWidget* const found = FindButton(child, label) )
            return found;
    }

    return nullptr;
}

struct PressData
{
    GtkAssertDialog* dlg;
    const char* label;
};

gboolean PressLater(gpointer data)
{
    PressData* const p = static_cast<PressData*>(data);

    GtkWidget* const button = FindButton(GTK_WIDGET(p->dlg), p->label);
    if ( button )
        g_signal_emit_by_name(button, "clicked");
    else
        printf("PROBE no button matching \"%s\"\n", p->label);

    return G_SOURCE_REMOVE;
}

// Press one button and report what gtk_assert_dialog_run() gives back.
int RunAndPress(GtkAssertDialog* dlg, const char* label)
{
    PressData data = { dlg, label };
    g_timeout_add(50, PressLater, &data);

    return gtk_assert_dialog_run(dlg);
}

const char* NameOfResponse(int r)
{
    switch ( r )
    {
        case GTK_ASSERT_DIALOG_STOP:                 return "STOP";
        case GTK_ASSERT_DIALOG_CONTINUE:             return "CONTINUE";
        case GTK_ASSERT_DIALOG_CONTINUE_SUPPRESSING: return "CONTINUE_SUPPRESSING";
    }
    return "(unknown)";
}

} // anonymous namespace

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

        // The buttons, which are the dialog's own now rather than GtkDialog's.
        bool buttonsOk = true;

        const int onContinue = RunAndPress(ad, "Continue");
        printf("PROBE Continue -> %s\n", NameOfResponse(onContinue));
        buttonsOk &= onContinue == GTK_ASSERT_DIALOG_CONTINUE;

        const int onStop = RunAndPress(ad, "Stop");
        printf("PROBE Stop     -> %s\n", NameOfResponse(onStop));
        buttonsOk &= onStop == GTK_ASSERT_DIALOG_STOP;

        // With the box unticked, Continue must mean "and do not ask again".
        gtk_check_button_set_active(GTK_CHECK_BUTTON(ad->shownexttime), FALSE);
        const int onSuppress = RunAndPress(ad, "Continue");
        printf("PROBE Continue with the box unticked -> %s\n",
               NameOfResponse(onSuppress));
        buttonsOk &= onSuppress == GTK_ASSERT_DIALOG_CONTINUE_SUPPRESSING;

        printf("PROBE VERDICT buttons %s\n", buttonsOk ? "ok" : "BROKEN");

        gtk_window_destroy(GTK_WINDOW(dlg));
        return false;
    }
};
wxIMPLEMENT_APP(App);
