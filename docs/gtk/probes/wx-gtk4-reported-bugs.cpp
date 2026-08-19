// Reproducers for the bugs reported against the GTK4 port after it first got
// eyeballed by a human. Each one is a mode below; all of them need real input
// or real window-manager messages, which is why they live here rather than in
// the test suite.
//
// Build and run (a display is required):
//     g++ -g -o wx-gtk4-reported-bugs wx-gtk4-reported-bugs.cpp \
//         $(../../../../wxbuild-gtk4/wx-config --cxxflags --libs) \
//         $(pkg-config --cflags gtk4) -lXtst -lX11
//     xvfb-run -a ./wx-gtk4-reported-bugs wheel
//
// Modes:
//
//   wheel  -- a mouse wheel event over a multiline wxTextCtrl. GTK4's
//             GdkScrollEvent stores only deltas and no position, so
//             gdk_event_get_position() hands back NaN, which used to travel
//             into wxRound() and assert. Reported as "scrolling the log
//             causes math error" in the menu sample.
//
//   close  -- the window manager's close button on a wxFrame, sent as the
//             WM_DELETE_WINDOW client message that a real "x" click sends.
//             The frame is expected to go away.
//
//   dialog -- a native modal dialog (colour, file), closed with the window
//             manager's close button, followed by the same close button on the
//             frame: does a dialog leave anything behind that stops the frame
//             from closing afterwards?
//
//   about  -- wxAboutBox(), closed with the window manager's close button,
//             then opened again. GTK4's GtkAboutDialog is a GtkWindow rather
//             than a GtkDialog: it has no "response" signal, and closing it
//             destroys it, so the cached dialog pointer went stale and the
//             second call had nothing to show.
//
// Every mode prints PASS or FAIL and sets the exit code, so this can also be
// run as a regression check.

#include "wx/wx.h"
#include "wx/aboutdlg.h"
#include "wx/colordlg.h"
#include "wx/filedlg.h"
#include "wx/modalhook.h"
#include "wx/textctrl.h"

#include <gtk/gtk.h>
#include <gdk/x11/gdkx.h>
#include <X11/extensions/XTest.h>

#include <stdio.h>

namespace
{

int g_asserts = 0;
wxString g_firstAssert;

// The X window behind a wx window, or None if it isn't realized.
Window GetXWindow(wxWindow* win, Display** display = nullptr)
{
    GtkNative* const native = gtk_widget_get_native(win->GetHandle());
    if ( !native )
        return None;

    GdkSurface* const surface = gtk_native_get_surface(native);
    if ( !surface )
        return None;

    if ( display )
        *display = GDK_SURFACE_XDISPLAY(surface);

    return GDK_SURFACE_XID(surface);
}

// Ask the window to close, exactly the way a click on the window manager's
// close button does: WM_DELETE_WINDOW, which GDK turns into "close-request".
void SendCloseButton(Display* display, Window xwindow)
{
    XClientMessageEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.window = xwindow;
    ev.message_type = XInternAtom(display, "WM_PROTOCOLS", False);
    ev.format = 32;
    ev.data.l[0] = XInternAtom(display, "WM_DELETE_WINDOW", False);
    ev.data.l[1] = CurrentTime;

    XSendEvent(display, xwindow, False, NoEventMask, (XEvent*)&ev);
    XFlush(display);
}

// Turn the wheel over the centre of the given window. Buttons 4 and 5 are how
// X11 reports wheel up and down.
void SendWheel(wxWindow* win, int button)
{
    Display* display = nullptr;
    const Window xwindow = GetXWindow(win, &display);
    if ( !xwindow )
        return;

    const wxPoint centre =
        win->ClientToScreen(wxPoint(win->GetClientSize().x / 2,
                                    win->GetClientSize().y / 2));

    XTestFakeMotionEvent(display, -1, centre.x, centre.y, 0);
    XFlush(display);
    XTestFakeButtonEvent(display, button, True, CurrentTime);
    XTestFakeButtonEvent(display, button, False, CurrentTime);
    XFlush(display);
}

// Let GTK deliver whatever the above caused.
void Settle(int ms = 700)
{
    const wxMilliClock_t until = wxGetLocalTimeMillis() + ms;
    while ( wxGetLocalTimeMillis() < until )
    {
        wxYield();
        wxMilliSleep(10);
    }
}

class ReproApp : public wxApp
{
public:
    bool OnInit() override
    {
        setvbuf(stdout, nullptr, _IONBF, 0);

        if ( argc != 2 )
        {
            printf("usage: %s wheel|close|about\n", (const char*)argv[0].utf8_str());
            return false;
        }

        m_mode = argv[1];
        CallAfter(&ReproApp::Run);
        return true;
    }

    // Don't pop up a dialog: record the assert and carry on, so a failing run
    // still finishes and prints its verdict.
    void OnAssertFailure(const wxChar* file,
                         int line,
                         const wxChar* func,
                         const wxChar* cond,
                         const wxChar* msg) override
    {
        if ( !g_asserts++ )
        {
            g_firstAssert = wxString::Format("%s(%d) in %s(): \"%s\" %s",
                                             file, line, func, cond,
                                             msg ? msg : wxT(""));
        }
    }

    int OnExit() override { return m_failed; }

private:
    void Run()
    {
        if ( m_mode == "wheel" )
            TestWheel();
        else if ( m_mode == "dialog" )
            TestDialog();
        else if ( m_mode == "close" )
            TestClose();
        else if ( m_mode == "about" )
            TestAbout();
        else
            printf("unknown mode \"%s\"\n", (const char*)m_mode.utf8_str());

        ExitMainLoop();
    }

    void Fail(const wxString& why)
    {
        printf("FAIL: %s\n", (const char*)why.utf8_str());
        m_failed = 1;
    }

    void TestWheel()
    {
        wxFrame* const frame = new wxFrame(nullptr, wxID_ANY, "wheel", wxDefaultPosition, wxSize(400, 300));
        wxTextCtrl* const text = new wxTextCtrl(frame, wxID_ANY, "",
                                                wxDefaultPosition, wxDefaultSize,
                                                wxTE_MULTILINE);
        for ( int n = 0; n < 200; n++ )
            text->AppendText(wxString::Format("log line %d\n", n));

        frame->Show();
        Settle();

        g_asserts = 0;
        SendWheel(text, 5);
        Settle();
        SendWheel(text, 4);
        Settle();

        if ( g_asserts )
            Fail(wxString::Format("%d assert(s), first: %s", g_asserts, g_firstAssert));
        else
            printf("PASS: wheel over a wxTextCtrl asserts nothing\n");

        frame->Destroy();
    }

    void TestClose()
    {
        wxFrame* const frame = new wxFrame(nullptr, wxID_ANY, "close", wxDefaultPosition, wxSize(400, 300));
        frame->Show();
        Settle();

        Display* display = nullptr;
        const Window xwindow = GetXWindow(frame, &display);
        if ( !xwindow )
        {
            Fail("the frame has no X window");
            return;
        }

        SendCloseButton(display, xwindow);
        Settle();

        if ( wxTopLevelWindows.GetCount() )
            Fail("the frame is still there after the close button");
        else
            printf("PASS: the close button closes a wxFrame\n");
    }

    void TestDialog()
    {
        wxFrame* const frame = new wxFrame(nullptr, wxID_ANY, "dialog", wxDefaultPosition, wxSize(400, 300));
        frame->Show();
        Settle();

        m_timer.SetOwner(this);
        Bind(wxEVT_TIMER, &ReproApp::OnTimer, this);

        m_frame = frame;

        printf("modal count before: %u\n", wxModalDialogHook::GetOpenCount());

        m_timer.Start(1500);
        const wxColour clr = wxGetColourFromUser(frame, *wxRED, "colour");
        printf("colour dialog: returned %s, modal count %u, frame enabled %d\n",
               clr.IsOk() ? "a colour" : "nothing",
               wxModalDialogHook::GetOpenCount(), frame->IsEnabled());

        {
            wxFileDialog dlg(frame, "file", "", "", "*", wxFD_OPEN);
            m_timer.Start(1500);
            const int rc = dlg.ShowModal();
            printf("file dialog: returned %d, modal count %u, frame enabled %d\n",
                   rc, wxModalDialogHook::GetOpenCount(), frame->IsEnabled());
        }

        Settle();

        Display* display = nullptr;
        const Window xwindow = GetXWindow(frame, &display);
        if ( !xwindow )
        {
            Fail("the frame has no X window");
            return;
        }

        SendCloseButton(display, xwindow);
        Settle();

        if ( wxTopLevelWindows.GetCount() )
            Fail("the frame no longer closes after a dialog was shown");
        else
            printf("PASS: the close button still closes the frame\n");
    }

    // Close every visible GTK toplevel other than the frame, i.e. whichever
    // dialog is up, the way the window manager's close button would.
    void OnTimer(wxTimerEvent&)
    {
        Window keep = None;
        Display* display = nullptr;
        if ( m_frame )
            keep = GetXWindow(m_frame, &display);

        GListModel* const toplevels = gtk_window_get_toplevels();
        const guint n = g_list_model_get_n_items(toplevels);
        for ( guint i = 0; i < n; i++ )
        {
            GtkWindow* const win = GTK_WINDOW(g_list_model_get_item(toplevels, i));
            GdkSurface* const surface = gtk_native_get_surface(GTK_NATIVE(win));
            printf("  toplevel %s visible=%d modal=%d surface=%p\n",
                   G_OBJECT_TYPE_NAME(win),
                   gtk_widget_get_visible(GTK_WIDGET(win)),
                   gtk_window_get_modal(win), (void*)surface);
            if ( gtk_widget_get_visible(GTK_WIDGET(win)) )
            {
                if ( surface && GDK_SURFACE_XID(surface) != keep )
                {
                    printf("  -> close button to 0x%lx\n",
                           (unsigned long)GDK_SURFACE_XID(surface));
                    SendCloseButton(GDK_SURFACE_XDISPLAY(surface),
                                    GDK_SURFACE_XID(surface));
                }
            }
            g_object_unref(win);
        }

        wxUnusedVar(display);
    }

    void TestAbout()
    {
        wxFrame* const frame = new wxFrame(nullptr, wxID_ANY, "about", wxDefaultPosition, wxSize(400, 300));
        frame->Show();
        Settle();

        wxAboutDialogInfo info;
        info.SetName("probe");
        info.SetDescription("does the about box come back?");

        wxAboutBox(info, frame);
        Settle();

        GtkWindow* const first = FindAboutWindow();
        if ( !first )
        {
            Fail("no about dialog after the first wxAboutBox()");
            return;
        }

        Display* display = GDK_SURFACE_XDISPLAY(
            gtk_native_get_surface(GTK_NATIVE(first)));
        SendCloseButton(display, GDK_SURFACE_XID(
            gtk_native_get_surface(GTK_NATIVE(first))));
        Settle();

        if ( FindAboutWindow() )
        {
            Fail("the about dialog ignored the close button");
            return;
        }

        wxAboutBox(info, frame);
        Settle();

        if ( FindAboutWindow() )
            printf("PASS: the about dialog opens again after being closed\n");
        else
            Fail("the about dialog did not come back");

        frame->Destroy();
    }

    // The about dialog isn't a wxWindow, so look for it among GTK's toplevels.
    static GtkWindow* FindAboutWindow()
    {
        GtkWindow* found = nullptr;
        GListModel* const toplevels = gtk_window_get_toplevels();
        const guint n = g_list_model_get_n_items(toplevels);
        for ( guint i = 0; i < n; i++ )
        {
            GtkWindow* const win = GTK_WINDOW(g_list_model_get_item(toplevels, i));
            if ( GTK_IS_ABOUT_DIALOG(win) && gtk_widget_get_visible(GTK_WIDGET(win)) )
                found = win;
            g_object_unref(win);
        }

        return found;
    }

    wxString m_mode;
    wxTimer m_timer;
    wxWindow* m_frame = nullptr;
    int m_failed = 0;
};

} // anonymous namespace

wxIMPLEMENT_APP(ReproApp);
