// Does dismissing a wxPopupTransientWindow stop keyboard input reaching the
// windows that come after it?
//
// Six test cases in issue #82 pass on their own and fail once
// Window::TransientPopupClientSize has run earlier in the suite: a window
// created afterwards gets no key events at all. Bisecting 533 test cases to
// find that pair is expensive, and the recorded diagnosis -- that the top
// level never gets the X input focus again -- turned out to describe a
// symptom rather than the cause. Nine attempts at restoring the focus had
// failed before this probe was written.
//
// This reproduces the same poisoning in twenty seconds, outside the suite,
// and separates the steps so the responsible one can be named. What it
// establishes:
//
//   full        Popup + Dismiss + Destroy   -> poisoned
//   nopopup     never shown                 -> clean
//   nodismiss   Popup + Destroy, no Dismiss -> clean
//   nodestroy   Popup + Dismiss, no Destroy -> poisoned
//   plain       wxPopupWindow               -> clean
//   plain-ctrls wxPopupWindow, autohide on  -> clean
//
// So it is Dismiss(), not destruction, and not autohide by itself: it is
// wxPopupTransientWindow::Show(false) clearing autohide while the popover is
// still up, which stops GTK from ever returning the grab it took when the
// popover was shown.
//
// The X input focus is reported alongside because it is the thing everyone
// suspects: it sits on the top level in the poisoned run exactly as it does
// in the clean one, and neither gtk_window_present() nor XSetInputFocus()
// (KEYS82_EXTRA below) changes the outcome. That is the measurement which
// rules the focus out.
//
// Environment:
//   KEYS82_MODE   one of the modes above (default "full")
//   KEYS82_EXTRA  "present" or "xfocus" to try forcing the focus as well
//
// Build (adjust wx-config for your build directory), then run under Xvfb:
//   g++ -o keys82 gtk4-popup-dismiss-poisons-keys.cpp \
//       $(wx-config --cxxflags) $(wx-config --libs core,base) \
//       $(pkg-config --cflags --libs gtk4) -lX11
//   xvfb-run -a ./keys82

#include <wx/wx.h>
#include <wx/popupwin.h>
#include <wx/uiaction.h>
#include <gtk/gtk.h>
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>

namespace
{

class KeyWin : public wxWindow
{
public:
    explicit KeyWin(wxWindow* parent) : wxWindow(parent, wxID_ANY)
    {
        Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& e){ ++m_down; e.Skip(); });
    }

    int m_down = 0;
};

// wxYield() alone returns too early for injected input to have been
// delivered; this keeps the loop running for a fixed stretch instead.
void Pump(int ms)
{
    const wxMilliClock_t end = wxGetLocalTimeMillis() + ms;
    while ( wxGetLocalTimeMillis() < end )
    {
        wxYield();
        wxMilliSleep(1);
    }
}

class Frame : public wxFrame
{
public:
    Frame() : wxFrame(nullptr, wxID_ANY, "keys82",
                      wxDefaultPosition, wxSize(500, 400))
    {
        Bind(wxEVT_IDLE, &Frame::OnIdle, this);
    }

private:
    // One round of what KeyboardEventTestCase::setUp() plus NormalLetter() do.
    int TypeOnce(const char* tag)
    {
        KeyWin* const win = new KeyWin(this);
        wxYield();
        win->SetFocus();

        wxString extra;
        wxGetEnv("KEYS82_EXTRA", &extra);

        GtkWidget* const tl = gtk_widget_get_ancestor(
            static_cast<GtkWidget*>(GetHandle()), GTK_TYPE_WINDOW);

        if ( extra.Contains("present") )
            gtk_window_present(GTK_WINDOW(tl));

        GdkSurface* const surface = gtk_native_get_surface(GTK_NATIVE(tl));
        Display* const xdisplay = GDK_SURFACE_XDISPLAY(surface);

        if ( extra.Contains("xfocus") )
        {
            XSetInputFocus(xdisplay, GDK_SURFACE_XID(surface),
                           RevertToParent, CurrentTime);
            XSync(xdisplay, False);
        }

        Pump(50);

        Window focus = None;
        int revert = 0;
        XGetInputFocus(xdisplay, &focus, &revert);
        printf("   [%s] X focus=%s is_active=%d\n", tag,
               focus == GDK_SURFACE_XID(surface) ? "top level"
                 : focus == PointerRoot ? "PointerRoot" : "something else",
               gtk_window_is_active(GTK_WINDOW(tl)));

        win->m_down = 0;

        wxUIActionSimulator sim;
        sim.Char('a');
        Pump(200);

        const int n = win->m_down;
        printf("%-24s KeyDown=%d\n", tag, n);
        fflush(stdout);

        // Left alive deliberately: the frame is about to go, and destroying a
        // just-focused window here would only add another teardown to reason
        // about in a probe whose whole subject is what teardown leaves behind.
        win->Hide();
        return n;
    }

    void RunPopup(const wxString& mode)
    {
        const bool transient = !mode.Contains("plain");
        const bool doPopup   = !mode.Contains("nopopup");
        const bool doDismiss = !mode.Contains("nodismiss");
        const bool doDestroy = !mode.Contains("nodestroy");

        wxPopupWindow* popup;
        if ( transient )
            popup = new wxPopupTransientWindow(this);
        else
            popup = new wxPopupWindow(
                        this, mode.Contains("ctrls") ? wxPU_CONTAINS_CONTROLS : 0);

        new wxScrolledWindow(popup, wxID_ANY, wxDefaultPosition, wxSize(300, 300));
        popup->SetClientSize(300, 300);
        popup->Position(ClientToScreen(wxPoint(20, 20)), wxSize(1, 1));

        if ( doPopup )
        {
            if ( transient )
                static_cast<wxPopupTransientWindow*>(popup)->Popup();
            else
                popup->Show();
            Pump(100);
        }

        if ( doDismiss )
        {
            if ( transient )
                static_cast<wxPopupTransientWindow*>(popup)->Dismiss();
            else
                popup->Show(false);
            Pump(100);
        }

        if ( doDestroy )
        {
            popup->Destroy();
            Pump(100);
        }
    }

    void OnIdle(wxIdleEvent&)
    {
        // Pump() below lets idle events back in; without this the steps nest.
        if ( m_running )
            return;
        m_running = true;

        wxString mode;
        if ( !wxGetEnv("KEYS82_MODE", &mode) )
            mode = "full";

        const int before = TypeOnce("1 before the popup");
        RunPopup(mode);
        const int after = TypeOnce("2 after the popup");

        printf("MODE=%-12s before=%d after=%d -> %s\n",
               static_cast<const char*>(mode.utf8_str()), before, after,
               (before > 0 && after == 0) ? "POISONED" : "clean");
        fflush(stdout);

        wxTheApp->ExitMainLoop();
    }

    bool m_running = false;
};

class App : public wxApp
{
public:
    bool OnInit() override { (new Frame())->Show(); return true; }
};

} // anonymous namespace

wxIMPLEMENT_APP(App);
