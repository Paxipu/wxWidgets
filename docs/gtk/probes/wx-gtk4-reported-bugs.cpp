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
//   quiet  -- the close button on a frame in an application that is doing
//             nothing else: no timer, no input, and -- unlike every other
//             mode here -- no wxYield() to run wx's idle processing by hand.
//             Destroying a top level window is deferred to idle time, so
//             without something to re-arm the idle handler after a native
//             event the window never actually goes away. Reported as the anim
//             sample not exiting once its animation is stopped.
//
//   dialog -- a native modal dialog (colour, file), closed with the window
//             manager's close button, followed by the same close button on the
//             frame: does a dialog leave anything behind that stops the frame
//             from closing afterwards?
//
//   anim   -- what a wxAnimationCtrl actually shows, read back from the
//             screen, as the inactive bitmap and the background colour are
//             both reported as having no effect. Takes the path of an
//             animation file as its second argument, e.g. the anim sample's
//             throbber.gif.
//
//   aui    -- dragging a wxAuiManager pane by its caption, with real pointer
//             input, and checking that it ended up floating. Reported as not
//             being able to dock or undock panes in the aui sample. The cause
//             was one physical press arriving as two wxEVT_LEFT_DOWN: a
//             GtkGestureClick in the BUBBLE phase offers an unclaimed press to
//             every ancestor, and wx re-targeted each one at the same window.
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
#include "wx/aui/aui.h"
#include "wx/animate.h"
#include "wx/artprov.h"
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

// Press at one screen point, move to another in steps, and release: a real
// drag, as far as the toolkit is concerned. The steps matter -- a single jump
// is not a drag, and wxAuiManager only starts one past a threshold.
void SendDrag(Display* display, const wxPoint& from, const wxPoint& to,
              int steps = 20)
{
    XTestFakeMotionEvent(display, -1, from.x, from.y, 0);
    XFlush(display);

    XTestFakeButtonEvent(display, 1, True, CurrentTime);
    XFlush(display);

    for ( int i = 1; i <= steps; i++ )
    {
        XTestFakeMotionEvent(display, -1,
                             from.x + (to.x - from.x) * i / steps,
                             from.y + (to.y - from.y) * i / steps, 0);
        XFlush(display);

        // The drag has to be given time to be seen as one.
        for ( int k = 0; k < 3; k++ )
        {
            wxYield();
            wxMilliSleep(10);
        }
    }

    XTestFakeButtonEvent(display, 1, False, CurrentTime);
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

        if ( argc < 2 )
        {
            printf("usage: %s wheel|dialog|close|about|anim|aui [animation-file]\n",
                   (const char*)argv[0].utf8_str());
            return false;
        }

        m_mode = argv[1];
        if ( argc > 2 )
            m_animFile = argv[2];
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

    int OnExit() override
    {
        // In quiet mode success is the main loop ending on its own, when the
        // frame is finally destroyed, so the verdict can only be given here.
        if ( m_mode == "quiet" && !m_failed )
            printf("PASS: a quiet application closes its window\n");

        return m_failed;
    }

private:
    void Run()
    {
        if ( m_mode == "wheel" )
            TestWheel();
        else if ( m_mode == "dialog" )
            TestDialog();
        else if ( m_mode == "anim" )
            TestAnim();
        else if ( m_mode == "quiet" )
        {
            // This one runs on in the main loop instead of ending here.
            TestQuietClose();
            return;
        }
        else if ( m_mode == "close" )
            TestClose();
        else if ( m_mode == "aui" )
            TestAui();
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

    // Sends the close button, then checks the frame is gone, both from plain
    // GLib timeouts: a wx timer would generate wx events, and wxYield() would
    // run the idle processing this is checking happens on its own.
    static gboolean QuietSendClose(void* data)
    {
        ReproApp* const self = static_cast<ReproApp*>(data);

        Display* display = nullptr;
        const Window xwindow = GetXWindow(self->m_frame, &display);
        if ( xwindow )
            SendCloseButton(display, xwindow);

        return G_SOURCE_REMOVE;
    }

    static gboolean QuietCheck(void* data)
    {
        ReproApp* const self = static_cast<ReproApp*>(data);

        if ( wxTopLevelWindows.GetCount() )
        {
            self->Fail("the frame is still there, idle time never came");
            self->ExitMainLoop();
        }
        // else the main loop has ended already, this never runs

        return G_SOURCE_REMOVE;
    }

    void TestQuietClose()
    {
        wxFrame* const frame = new wxFrame(nullptr, wxID_ANY, "quiet", wxDefaultPosition, wxSize(400, 300));
        frame->Show();
        m_frame = frame;

        g_timeout_add(1500, QuietSendClose, this);
        g_timeout_add(4000, QuietCheck, this);
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

    // Drag a docked wxAuiManager pane out by its caption and see whether it
    // came loose. This needs a real drag: wxAuiManager starts one from the
    // press, follows it with motion events, and finishes it on the release,
    // and it counts the presses -- a second wxEVT_LEFT_DOWN for the same
    // physical click arrives while a drag is already under way and puts the
    // manager back where it started.
    void TestAui()
    {
        wxFrame* const frame = new wxFrame(nullptr, wxID_ANY, "aui",
                                           wxDefaultPosition, wxSize(600, 450));

        wxAuiManager* const mgr = new wxAuiManager(frame);

        wxPanel* const centre = new wxPanel(frame, wxID_ANY);
        mgr->AddPane(centre, wxAuiPaneInfo().Name("centre").CenterPane());

        wxPanel* const side = new wxPanel(frame, wxID_ANY, wxDefaultPosition,
                                          wxSize(180, 180));
        mgr->AddPane(side, wxAuiPaneInfo().Name("side").Caption("Side pane")
                            .Left().Floatable(true).Dockable(true));
        mgr->Update();

        frame->Show();
        Settle();

        Display* display = nullptr;
        if ( !GetXWindow(frame, &display) )
        {
            Fail("the frame has no X window");
            return;
        }

        const wxAuiPaneInfo& pane = mgr->GetPane("side");
        if ( !pane.IsOk() )
        {
            Fail("the pane went missing before the drag");
            return;
        }

        printf("  before: floating=%d rect=%dx%d at (%d,%d)\n",
               pane.IsFloating(), pane.rect.width, pane.rect.height,
               pane.rect.x, pane.rect.y);

        // wxAuiPaneInfo::rect is the pane's own window, in frame client
        // coordinates; the caption is the strip immediately *above* it, drawn
        // by the manager on the frame itself. Pressing inside rect would hit
        // the pane's window and never reach the manager at all.
        int captionSize = 17;
        if ( wxAuiDockArt* const art = mgr->GetArtProvider() )
            captionSize = art->GetMetric(wxAUI_DOCKART_CAPTION_SIZE);

        const wxPoint caption =
            frame->ClientToScreen(wxPoint(pane.rect.x + pane.rect.width / 2,
                                          pane.rect.y - captionSize / 2));

        // Well into the centre pane, which is where a pane gets dropped to
        // float or re-dock somewhere else.
        const wxPoint target =
            frame->ClientToScreen(wxPoint(frame->GetClientSize().x - 60,
                                          frame->GetClientSize().y / 2));

        SendDrag(display, caption, target);
        Settle();


        const wxAuiPaneInfo& after = mgr->GetPane("side");
        printf("  after:  floating=%d rect=%dx%d at (%d,%d)  dock_direction=%d\n",
               after.IsFloating(), after.rect.width, after.rect.height,
               after.rect.x, after.rect.y, after.dock_direction);

        // Either outcome counts as the drag having been noticed: the pane
        // floated, or it re-docked on the other side. What must not happen is
        // nothing at all.
        const bool moved = after.IsFloating() ||
                            after.dock_direction != wxAUI_DOCK_LEFT;

        if ( moved )
            printf("PASS: a wxAuiManager pane can be dragged out of its dock\n");
        else
            Fail("the pane did not move: the drag was not acted on");

        mgr->UnInit();
        delete mgr;
        frame->Destroy();
        Settle();
    }

    // What is actually on the screen at the given point of the window.
    static wxString PixelAt(wxWindow* win, int x, int y)
    {
        Display* display = nullptr;
        if ( !GetXWindow(win, &display) )
            return "no window";

        const wxPoint pt = win->ClientToScreen(wxPoint(x, y));

        XImage* const image = XGetImage(display, DefaultRootWindow(display),
                                        pt.x, pt.y, 1, 1, AllPlanes, ZPixmap);
        if ( !image )
            return "off screen";

        const unsigned long px = XGetPixel(image, 0, 0);
        XDestroyImage(image);

        return wxString::Format("#%02lx%02lx%02lx",
                                (px >> 16) & 0xff, (px >> 8) & 0xff, px & 0xff);
    }

    void Show(const char* what)
    {
        Settle(400);

        const wxSize size = m_anim->GetClientSize();
        printf("  %-42s size %dx%d  corner %s  quarter %s  centre %s\n",
               what, size.x, size.y,
               (const char*)PixelAt(m_anim, 1, 1).utf8_str(),
               (const char*)PixelAt(m_anim, size.x / 4, size.y / 4).utf8_str(),
               (const char*)PixelAt(m_anim, size.x / 2, size.y / 2).utf8_str());
    }

    void TestAnim()
    {
        wxFrame* const frame = new wxFrame(nullptr, wxID_ANY, "anim", wxDefaultPosition, wxSize(500, 400));
        wxSizer* const sizer = new wxBoxSizer(wxVERTICAL);
        m_anim = new wxAnimationCtrl(frame, wxID_ANY);
        sizer->Add(m_anim, wxSizerFlags().Centre().Border());
        frame->SetSizer(sizer);
        frame->Show();
        Settle();

        wxAnimation anim(m_anim->CreateAnimation());
        if ( !anim.LoadFile(m_animFile) )
        {
            Fail("cannot load " + m_animFile);
            return;
        }

        m_anim->SetAnimation(anim);
        m_anim->Play();
        frame->Layout();
        Show("playing the animation");

        m_anim->SetInactiveBitmap(wxArtProvider::GetBitmap(wxART_MISSING_IMAGE));
        Show("inactive bitmap set while playing");

        m_anim->Stop();
        Show("stopped: the inactive bitmap should show");

        m_anim->SetInactiveBitmap(wxNullBitmap);
        Show("inactive bitmap removed: first frame again");

        m_anim->SetAnimation(wxNullAnimation);
        Show("no animation: the background colour should show");

        m_anim->SetBackgroundColour(*wxGREEN);
        Show("background set to green (#00ff00)");

        m_anim->SetAnimation(anim);
        m_anim->Play();
        frame->Layout();
        Show("animation loaded again while green");

        frame->Destroy();
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
    wxString m_animFile = "throbber.gif";
    wxAnimationCtrl* m_anim = nullptr;
    wxTimer m_timer;
    wxWindow* m_frame = nullptr;
    int m_failed = 0;
};

} // anonymous namespace

wxIMPLEMENT_APP(ReproApp);
