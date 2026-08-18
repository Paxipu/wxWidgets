// SOLVED -- kept as a regression reproducer. The cause was
// wxWindow::RealizeTabOrder() calling gtk_widget_insert_after(), which under
// GTK4 *parents* a widget rather than merely reordering it, on a child
// wxTopLevelWindow that must never be a GTK-level child. Every mode below
// survives as of that fix; if one starts aborting again, that is the
// regression.
//
// Reproducer: wxGTK4 aborts inside GTK's CSS machinery when a shown
// wxTopLevelWindow has a wx parent.
//
//     Gtk:ERROR:../../../gtk/gtkcssnode.c:1358:gtk_css_node_validate:
//     assertion failed: (cssnode->parent == NULL)
//
// This is the abort that stops test_gui at 445 of 490 test cases, in
// wxPersistTLW.
//
// The trigger is only two things:
//
//   1. a wxTopLevelWindow that has a *wx parent* and has been shown, and
//   2. enough elapsed time in the main loop for a frame clock pass.
//
// That is all. Everything the failing test does around it -- Iconize(),
// wxPersistenceManager, geometry -- is incidental, and so are the system
// colour queries this reproducer used to need: mode 5 below removes them
// entirely and still aborts every time.
//
// The second condition is what made this look intermittent. GTK queues the
// CSS validation and runs it on a later frame clock pass, so with only
// wxYield() the process usually exits first (about 13 aborts in 20). The
// wxMilliSleep() below takes it to 5 in 5. Valgrind does the same thing by
// accident, aborting 3 times in 3 purely from its slowdown -- while reporting
// no invalid read or write at all, which is how we know this is not memory
// corruption.
//
// Build and run (a display is required):
//     g++ -g -o wx-stylecontext-abort wx-stylecontext-abort.cpp \
//         $(../../../../wxbuild-gtk4/wx-config --cxxflags --libs)
//     xvfb-run -a ./wx-stylecontext-abort 5
//
// Modes, and what each establishes:
//
//   0  one shown frame, no child                    -> survives
//   1  child frame WITH a wx parent, shown          -> ABORTS 5/5
//   2  parent frame never shown                     -> survives
//   3  second frame with NO wx parent, shown        -> survives 0/5
//   5  same as 1 but with no style queries at all   -> ABORTS 5/5
//
// So it is wx's handling of a *wx-parented* toplevel that matters, not the
// number of windows on screen: two unrelated toplevels are fine.
//
// Ruled out, each re-tested against this reproducer rather than a single run:
//   - Memory corruption (valgrind clean).
//   - wxPizza tracking the toplevel in m_children without parenting it:
//     removing the tracking entirely changes nothing.
//   - Creating and destroying a scratch GtkWindow per style query: sharing one
//     across queries changes nothing, and mode 5 shows the queries are not
//     needed at all.
//   - wx painting from inside GTK's snapshot vfunc: suppressing the paint
//     changes nothing.
//   - gtk_widget_set_parent() being called with a GtkWindow as the child: a
//     conditional breakpoint on exactly that never fires.
//   - Iconize(), which is what the failing test does and what this hunt was
//     originally built around.

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

        if (g_mode == 1 || g_mode == 3 || g_mode == 5)
        {
            wxFrame* const f = new wxFrame(g_mode == 3 ? nullptr : top,
                                           wxID_ANY, "child");
            f->Show();
            wxYield();
        }

        for (int i = 0; i < 30; i++)
        {
            if (g_mode != 5)
            {
                (void)wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
                (void)wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
                (void)wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
            }

            // Real elapsed time, not just a yield: see the note above.
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
