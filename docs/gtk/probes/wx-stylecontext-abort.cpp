// Reproducer: wxGTK4 aborts inside GTK's CSS machinery during style queries
// when a wxTopLevelWindow with a wx parent exists.
//
//     Gtk:ERROR:../../../gtk/gtkcssnode.c:1358:gtk_css_node_validate:
//     assertion failed: (cssnode->parent == NULL)
//
// This is the abort that stops test_gui at 445 of 490 test cases, in
// wxPersistTLW. It first looked like an Iconize() bug, because that is what
// the failing test does; it is not. Iconize() is not needed at all.
//
// Unlike the test, this is deterministic -- but only if the main loop is
// given real elapsed time. The queued CSS validation runs on a later frame
// clock pass, so without the wxMilliSleep()/wxYield() loop below the process
// usually exits first and the abort looks intermittent (about 13 runs in 20).
// That is why this sleeps rather than just yielding: it is the difference
// between a flaky reproducer and a reliable one.
//
// Build and run (a display is required):
//     g++ -g -o wx-stylecontext-abort wx-stylecontext-abort.cpp \
//         $(../../../../wxbuild-gtk4/wx-config --cxxflags --libs)
//     xvfb-run -a ./wx-stylecontext-abort 1
//
// Modes, and what each one establishes:
//
//   0  one shown frame, no child           -> survives
//   1  child frame WITH a wx parent, shown -> ABORTS, every time
//   2  frame never shown                   -> survives
//   3  second frame with NO parent, shown  -> survives
//
// So the trigger is a *parented*, *shown* wxTopLevelWindow existing while
// wxGtkStyleContext builds and tears down its scratch widget tree. Two
// unrelated toplevels are fine, so it is wx's handling of the parented one
// that matters, not the number of windows.
//
// Ruled out so far, each re-tested against this reproducer:
//   - Memory corruption. Valgrind reports no invalid read or write; under
//     valgrind it aborts 3 times out of 3, purely from the slowdown.
//   - wxGtkStyleContext::AddWidget() populating a widget before attaching it,
//     leaving a queued root validation. Reordering changed nothing.
//   - Tearing down the scratch toplevel. Leaking it deliberately changed
//     nothing.
//   - gtk_widget_set_parent() being called with a GtkWindow as the child. A
//     conditional breakpoint on that never fires.

#include "wx/wx.h"
#include "wx/settings.h"

static int g_mode = 0;

class App : public wxApp
{
public:
    bool OnInit() override
    {
        wxFrame* const top = new wxFrame(nullptr, wxID_ANY, "top");
        if (g_mode != 2)
            top->Show();
        wxYield();

        if (g_mode == 1 || g_mode == 3)
        {
            wxFrame* const f = new wxFrame(g_mode == 3 ? nullptr : top,
                                           wxID_ANY, "child");
            f->Show();
            wxYield();
        }

        // The style queries: each builds a scratch widget tree rooted at a
        // real GtkWindow, reads values off it, and destroys it again.
        for (int i = 0; i < 30; i++)
        {
            (void)wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
            (void)wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
            (void)wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);

            wxMilliSleep(10);
            wxYield();
        }

        wxPrintf("mode %d: SURVIVED\n", g_mode);
        fflush(stdout);
        return false;
    }
};

int main(int argc, char** argv)
{
    if (argc > 1)
        g_mode = atoi(argv[1]);

    wxApp::SetInstance(new App);
    wxEntryStart(argc, argv);
    wxTheApp->CallOnInit();
    wxEntryCleanup();
    return 0;
}
