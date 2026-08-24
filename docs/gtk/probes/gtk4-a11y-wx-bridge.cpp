/*
 * Probe: does a wxAccessible reach GTK4's accessibility tree?
 *
 * This is the end-to-end check for src/gtk/accessgtk.cpp. It attaches a
 * wxAccessible describing three virtual children to a wxWindow, pushes it with
 * wxAccessible::NotifyEvent(), and then reads back what GTK4 believes -- role,
 * label, the walk from the widget to its accessible children, and the bounds.
 *
 * It lives here rather than in tests/ because it has to include both wx and
 * GTK headers, and "wx-config --cxxflags" does not name GTK's include paths:
 * they are private to the library build.
 *
 * Build against an accessibility-enabled wxGTK4 build and run it headless:
 *
 *   g++ -o gtk4-a11y-wx-bridge gtk4-a11y-wx-bridge.cpp \
 *       $(path/to/wx-config --cxxflags --libs core,base) \
 *       $(pkg-config --cflags --libs gtk4)
 *   GTK_A11Y=test xvfb-run -a ./gtk4-a11y-wx-bridge
 *
 * Exits 0 if every check passed.
 */

#include "wx/wx.h"
#include "wx/grid.h"

#if !wxUSE_ACCESSIBILITY
    #error "This probe needs a build configured with --enable-accessibility."
#endif

#include "wx/access.h"

#include <gtk/gtk.h>

namespace
{

int g_failures = 0;

void Check(bool ok, const char* what)
{
    wxPrintf("%-58s %s\n", what, ok ? "yes" : "NO");
    if ( !ok )
        g_failures++;
}

// Three children, named and placed predictably so the checks can be exact.
const int CHILD_COUNT = 3;

class TestAccessible : public wxAccessible
{
public:
    explicit TestAccessible(wxWindow* win) : wxAccessible(win) { }

    wxAccStatus GetName(int childId, wxString* name) override
    {
        *name = childId == wxACC_SELF ? wxString("the window")
                                      : wxString::Format("child %d", childId);
        return wxACC_OK;
    }

    wxAccStatus GetRole(int childId, wxAccRole* role) override
    {
        *role = childId == wxACC_SELF ? wxROLE_SYSTEM_TABLE : wxROLE_SYSTEM_CELL;
        return wxACC_OK;
    }

    wxAccStatus GetChildCount(int* childCount) override
    {
        *childCount = CHILD_COUNT;
        return wxACC_OK;
    }

    wxAccStatus GetLocation(wxRect& rect, int elementId) override
    {
        if ( elementId == wxACC_SELF )
            return wxACC_NOT_IMPLEMENTED;

        // Client coordinates (10*id, 20*id), 30x40, reported as screen ones.
        rect = wxRect(GetWindow()->ClientToScreen(wxPoint(10 * elementId,
                                                          20 * elementId)),
                      wxSize(30, 40));
        return wxACC_OK;
    }

    wxAccStatus GetState(int childId, long* state) override
    {
        *state = childId == 2 ? wxACC_STATE_SYSTEM_SELECTED : 0;
        return wxACC_OK;
    }
};

class TestFrame : public wxFrame
{
public:
    TestFrame() : wxFrame(nullptr, wxID_ANY, "a11y probe")
    {
        m_win = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(200, 200));
        m_win->SetAccessible(new TestAccessible(m_win));

        wxAccessible::NotifyEvent(wxACC_EVENT_OBJECT_CREATE, m_win,
                                  wxOBJID_CLIENT, wxACC_SELF);
    }

    wxWindow* GetTestWindow() const { return m_win; }

private:
    wxWindow* m_win;
};

class ProbeApp : public wxApp
{
public:
    bool OnInit() override
    {
        TestFrame* const frame = new TestFrame;
        frame->Show();

        // Let the window be realized: the widget has to exist before anything
        // can be read back off it.
        Yield();

        Run(frame->GetTestWindow());

        frame->Destroy();

        return false;
    }

private:
    void Run(wxWindow* win)
    {
        GtkWidget* const widget = win->GetConnectWidget();
        GtkAccessible* const self = GTK_ACCESSIBLE(widget);

        // Not the role itself: GTK will not let it be changed once the widget
        // has an AT context, which a wx window already has by now. See
        // docs/gtk/gtk4-accessibility.md.
        char* mismatch = gtk_test_accessible_check_property(
            self, GTK_ACCESSIBLE_PROPERTY_ROLE_DESCRIPTION, "grid");
        Check(mismatch == nullptr, "the window's role reached GTK, as a description");
        g_free(mismatch);

        mismatch = gtk_test_accessible_check_property(
            self, GTK_ACCESSIBLE_PROPERTY_LABEL, "the window");
        Check(mismatch == nullptr, "the window's name reached GTK");
        g_free(mismatch);

        // Walk the children the way an assistive technology would.
        GtkAccessible* child = gtk_accessible_get_first_accessible_child(self);
        Check(child != nullptr, "the window has an accessible first child");

        int seen = 0;
        bool namesMatch = true, rolesMatch = true, boundsMatch = true;
        while ( child )
        {
            seen++;

            if ( !gtk_test_accessible_has_role(child, GTK_ACCESSIBLE_ROLE_GRID_CELL) )
                rolesMatch = false;

            char* const bad = gtk_test_accessible_check_property(
                child, GTK_ACCESSIBLE_PROPERTY_LABEL,
                wxString::Format("child %d", seen).utf8_str().data());
            if ( bad )
                namesMatch = false;
            g_free(bad);

            int x, y, w, h;
            if ( !gtk_accessible_get_bounds(child, &x, &y, &w, &h) ||
                    x != 10 * seen || y != 20 * seen || w != 30 || h != 40 )
                boundsMatch = false;

            GtkAccessible* const next =
                gtk_accessible_get_next_accessible_sibling(child);
            g_object_unref(child);
            child = next;
        }

        Check(seen == CHILD_COUNT, "the walk reaches every child, and stops");
        Check(namesMatch, "each child's name reached GTK");
        Check(rolesMatch, "each child's role reached GTK");
        Check(boundsMatch, "each child's bounds arrive in client coordinates");

        // The widget is still alive: the obvious way of chaining the children
        // together drops a reference on it. See gtk4-a11y-virtual-child.c.
        Check(GTK_IS_WIDGET(widget), "the widget survived being described");

        // And the same walk over a real control that describes itself: wxGrid
        // attaches a wxAccessible of its own, and every cell, row header and
        // column header is a child id with no window behind it.
        CheckGrid(win->GetParent());

        // A window with no wxAccessible must be left entirely alone.
        wxWindow* const plain = new wxWindow(win->GetParent(), wxID_ANY);
        GtkAccessible* const plainSelf = GTK_ACCESSIBLE(plain->GetConnectWidget());
        Check(gtk_accessible_get_first_accessible_child(plainSelf) == nullptr,
              "a window with no wxAccessible reports no virtual children");

        wxPrintf("\n%d check(s) failed\n", g_failures);
    }

    void CheckGrid(wxWindow* parent)
    {
        wxGrid* const grid = new wxGrid(parent, wxID_ANY);
        grid->CreateGrid(3, 2);
        grid->SetCellValue(0, 0, "hello");

        GtkAccessible* const self = GTK_ACCESSIBLE(grid->GetConnectWidget());

        char* mismatch = gtk_test_accessible_check_property(
            self, GTK_ACCESSIBLE_PROPERTY_ROLE_DESCRIPTION, "grid");
        Check(mismatch == nullptr, "a wxGrid describes itself as a grid");
        g_free(mismatch);

        // 3 rows and 2 columns, plus the header row and the header column.
        int seen = 0;
        bool foundCellValue = false;
        GtkAccessible* child = gtk_accessible_get_first_accessible_child(self);
        while ( child )
        {
            seen++;

            // Cell (0, 0) holds "hello", and wxGrid puts the value in the name.
            char* const bad = gtk_test_accessible_check_property(
                child, GTK_ACCESSIBLE_PROPERTY_LABEL, "Column A: hello");
            if ( !bad )
                foundCellValue = true;
            g_free(bad);

            GtkAccessible* const next =
                gtk_accessible_get_next_accessible_sibling(child);
            g_object_unref(child);
            child = next;
        }

        Check(seen == 12, "every cell and header of a wxGrid is a child");
        Check(foundCellValue, "a wxGrid cell's value reaches GTK");

        grid->Destroy();
    }
};

} // anonymous namespace

wxIMPLEMENT_APP_NO_MAIN(ProbeApp);

int main(int argc, char** argv)
{
    wxEntry(argc, argv);

    return g_failures != 0;
}
