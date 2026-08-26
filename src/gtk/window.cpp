/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/window.cpp
// Purpose:     wxWindowGTK implementation
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling, Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifdef __VMS
#define XWarpPointer XWARPPOINTER
#endif

#include "wx/window.h"

#ifndef WX_PRECOMP
    #include "wx/log.h"
    #include "wx/app.h"
    #include "wx/toplevel.h"
    #include "wx/dcclient.h"
    #include "wx/menu.h"
    #include "wx/settings.h"
    #include "wx/msgdlg.h"
    #include "wx/math.h"
    #include "wx/module.h"
#endif

#include "wx/display.h"
#include "wx/dnd.h"
#include "wx/evtloop.h"
#include "wx/tooltip.h"
#include "wx/caret.h"
#include "wx/fontutil.h"
#include "wx/recguard.h"
#include "wx/sysopt.h"
#ifdef __WXGTK4__
    // For wxTextEntry::GTKEntryOnKeypressEnd(), called from the key
    // controller: GTK4 has no "event-after" signal to drive it from.
    #include "wx/textentry.h"
    #include "wx/weakref.h"
#endif
#ifdef __WXGTK3__
    #include "wx/gtk/dc.h"
#endif
#ifdef __WINDOWS__
    #include <gdk/gdkwin32.h>
#endif

#include <ctype.h>

#include "wx/gtk/private.h"
#include "wx/gtk/private/gtk3-compat.h"
#include "wx/gtk/private/object.h"
#include "wx/gtk/private/event.h"
#include "wx/gtk/private/wayland.h"
#include "wx/gtk/private/win_gtk.h"
#include "wx/gtk/private/stylecontext.h"
#include "wx/gtk/private/backend.h"
#include "wx/private/textmeasure.h"
using namespace wxGTKImpl;

#ifdef GDK_WINDOWING_X11
#ifdef __WXGTK4__
#include <gdk/x11/gdkx.h>
#else
#include <gdk/gdkx.h>
#endif
#include "wx/x11/private/wrapxkb.h"
#else
typedef guint KeySym;
#endif

// Use libxkbcommon for key code translation if we need and have it.
#if (defined (GDK_WINDOWING_X11) || defined (GDK_WINDOWING_WAYLAND)) && defined (HAVE_XKBCOMMON)
#define wxHAS_XKB
#include <xkbcommon/xkbcommon.h>
#endif

#ifdef __WXGTK4__
#define wxGTK_HAS_COMPOSITING_SUPPORT 0
#else
// gdk_window_set_composited() is only supported since 2.12
#define wxGTK_HAS_COMPOSITING_SUPPORT (GTK_CHECK_VERSION(2,12,0) && wxUSE_CAIRO)
#endif

#ifndef PANGO_VERSION_CHECK
    #define PANGO_VERSION_CHECK(a,b,c) 0
#endif

constexpr const char* TRACE_MOUSE = "mouse";

//-----------------------------------------------------------------------------
// documentation on internals
//-----------------------------------------------------------------------------

/*
   I have been asked several times about writing some documentation about
   the GTK port of wxWidgets, especially its internal structures. Obviously,
   you cannot understand wxGTK without knowing a little about the GTK, but
   some more information about what the wxWindow, which is the base class
   for all other window classes, does seems required as well.

   I)

   What does wxWindow do? It contains the common interface for the following
   jobs of its descendants:

   1) Define the rudimentary behaviour common to all window classes, such as
   resizing, intercepting user input (so as to make it possible to use these
   events for special purposes in a derived class), window names etc.

   2) Provide the possibility to contain and manage children, if the derived
   class is allowed to contain children, which holds true for those window
   classes which do not display a native GTK widget. To name them, these
   classes are wxPanel, wxScrolledWindow, wxDialog, wxFrame. The MDI frame-
   work classes are a special case and are handled a bit differently from
   the rest. The same holds true for the wxNotebook class.

   3) Provide the possibility to draw into a client area of a window. This,
   too, only holds true for classes that do not display a native GTK widget
   as above.

   4) Provide the entire mechanism for scrolling widgets. This actual inter-
   face for this is usually in wxScrolledWindow, but the GTK implementation
   is in this class.

   5) A multitude of helper or extra methods for special purposes, such as
   Drag'n'Drop, managing validators etc.

   6) Display a border (sunken, raised, simple or none).

   Normally one might expect, that one wxWidgets window would always correspond
   to one GTK widget. Under GTK, there is no such all-round widget that has all
   the functionality. Moreover, the GTK defines a client area as a different
   widget from the actual widget you are handling. Last but not least some
   special classes (e.g. wxFrame) handle different categories of widgets and
   still have the possibility to draw something in the client area.
   It was therefore required to write a special purpose GTK widget, that would
   represent a client area in the sense of wxWidgets capable to do the jobs
   2), 3) and 4). I have written this class and it resides in win_gtk.c of
   this directory.

   All windows must have a widget, with which they interact with other under-
   lying GTK widgets. It is this widget, e.g. that has to be resized etc and
   the wxWindow class has a member variable called m_widget which holds a
   pointer to this widget. When the window class represents a GTK native widget,
   this is (in most cases) the only GTK widget the class manages. E.g. the
   wxStaticText class handles only a GtkLabel widget a pointer to which you
   can find in m_widget (defined in wxWindow)

   When the class has a client area for drawing into and for containing children
   it has to handle the client area widget (of the type wxPizza, defined in
   win_gtk.cpp), but there could be any number of widgets, handled by a class.
   The common rule for all windows is only, that the widget that interacts with
   the rest of GTK must be referenced in m_widget and all other widgets must be
   children of this widget on the GTK level. The top-most widget, which also
   represents the client area, must be in the m_wxwindow field and must be of
   the type wxPizza.

   As I said, the window classes that display a GTK native widget only have
   one widget, so in the case of e.g. the wxButton class m_widget holds a
   pointer to a GtkButton widget. But windows with client areas (for drawing
   and children) have a m_widget field that is a pointer to a GtkScrolled-
   Window and a m_wxwindow field that is pointer to a wxPizza and this
   one is (in the GTK sense) a child of the GtkScrolledWindow.

   If the m_wxwindow field is set, then all input to this widget is inter-
   cepted and sent to the wxWidgets class. If not, all input to the widget
   that gets pointed to by m_widget gets intercepted and sent to the class.

   II)

   The design of scrolling in wxWidgets is markedly different from that offered
   by the GTK itself and therefore we cannot simply take it as it is. In GTK,
   clicking on a scrollbar belonging to scrolled window will inevitably move
   the window. In wxWidgets, the scrollbar will only emit an event, send this
   to (normally) a wxScrolledWindow and that class will call ScrollWindow()
   which actually moves the window and its sub-windows. Note that wxPizza
   memorizes how much it has been scrolled but that wxWidgets forgets this
   so that the two coordinates systems have to be kept in synch. This is done
   in various places using the pizza->m_scroll_x and pizza->m_scroll_y values.

   III)

   Singularly the most broken code in GTK is the code that is supposed to
   inform subwindows (child windows) about new positions. Very often, duplicate
   events are sent without changes in size or position, equally often no
   events are sent at all (All this is due to a bug in the GtkContainer code
   which got fixed in GTK 1.2.6). For that reason, wxGTK completely ignores
   GTK's own system and it simply waits for size events for toplevel windows
   and then iterates down the respective size events to all window. This has
   the disadvantage that windows might get size events before the GTK widget
   actually has the reported size. This doesn't normally pose any problem, but
   the OpenGL drawing routines rely on correct behaviour. Therefore, I have
   added the m_nativeSizeEvents flag, which is true only for the OpenGL canvas,
   i.e. the wxGLCanvas will emit a size event, when (and not before) the X11
   window that is used for OpenGL output really has that size (as reported by
   GTK).

   IV)

   If someone at some point of time feels the immense desire to have a look at,
   change or attempt to optimise the Refresh() logic, this person will need an
   intimate understanding of what "draw" and "expose" events are and what
   they are used for, in particular when used in connection with GTK's
   own windowless widgets. Beware.

   V)

   Cursors, too, have been a constant source of pleasure. The main difficulty
   is that a GdkWindow inherits a cursor if the programmer sets a new cursor
   for the parent. To prevent this from doing too much harm, SetCursor calls
   GTKUpdateCursor, which will recursively re-set the cursors of all child windows.
   Also don't forget that cursors (like much else) are connected to GdkWindows,
   not GtkWidgets and that the "window" field of a GtkWidget might very well
   point to the GdkWindow of the parent widget (-> "window-less widget") and
   that the two obviously have very different meanings.
*/

//-----------------------------------------------------------------------------
// data
//-----------------------------------------------------------------------------

// Don't allow event propagation during drag
bool g_blockEventsOnDrag;
// Don't allow mouse event propagation during scroll
bool g_blockEventsOnScroll;
extern wxCursor g_globalCursor;
extern wxCursor g_busyCursor;

// mouse capture state: the window which has it and if the mouse is currently
// inside it
static wxWindowGTK  *g_captureWindow = nullptr;
static bool g_captureWindowHasMouse = false;

// The window that currently has focus:
static wxWindowGTK *gs_currentFocus = nullptr;
// The window that is scheduled to get focus in the next event loop iteration
// or nullptr if there's no pending focus change:
static wxWindowGTK *gs_pendingFocus = nullptr;
// The window that had focus before we lost it last time:
static wxWindowGTK *gs_lastFocus = nullptr;

// the window that has deferred focus-out event pending, if any (see
// GTKAddDeferredFocusOut() for details)
static wxWindowGTK *gs_deferredFocusOut = nullptr;

#ifdef __WXGTK4__
// When the widget holding the focus is destroyed, GTK4 does not simply drop
// the focus: it remembers that the window needs one and gives it to the next
// focusable widget added, even one added before the next frame. So a wxWindow
// created straight after another one was destroyed can find itself focused
// without wx having asked for it -- and, for something like wxDataViewCtrl,
// selecting a row as a result. GTK3 had no such behaviour, and there is no way
// to decline it at GTK level: gtk_window_set_focus(nullptr) does not cancel it
// and a widget with can-focus=FALSE is skipped by the restore but then cannot
// be focused explicitly either.
//
// Recognize it instead. Every wxWindow gets a creation serial; when a focused
// one is destroyed the serial of that moment is remembered here, and a focus
// arriving at a window created after it, which wx did not ask for, is the
// restore rather than anything real. The mark is one-shot and is dropped again
// at the end of the event loop turn, so it can never suppress a focus change
// the user actually made.
static unsigned gs_windowSerial = 0;
static unsigned gs_focusRestoreAfter = 0;

// The window a restored focus was declined for, if any. GTK still considers it
// focused -- taking the focus away again would only make GTK look for somewhere
// else to put it, and it would land on the next window created, so the
// alternative is an endless game of catch. wx simply does not report it, and
// ignores the matching focus-out when it eventually arrives.
static wxWindowGTK *gs_focusDeclined = nullptr;

// Called from the event loop, at a point by which GTK's deferred focus move
// has either happened or is not going to.
void wxGTKClearFocusRestoreMark()
{
    gs_focusRestoreAfter = 0;
}
#endif // __WXGTK4__

// global variables because GTK+ DnD want to have the
// mouse event that caused it
GdkEvent    *g_lastMouseEvent = nullptr; // use SetLastMouseEvent below
int          g_lastButtonNumber = 0;

namespace wxGTKImpl
{

// Small RAII helper setting g_lastMouseEvent until the scope exit.
class SetLastMouseEvent
{
public:
#ifdef __WXGTK4__
    // GdkEventButton and GdkEventMotion don't exist under GTK4, where the
    // controller callbacks already have a plain GdkEvent to pass.
    explicit SetLastMouseEvent(GdkEvent* event)
    {
        g_lastMouseEvent = event;
    }
#else // !__WXGTK4__
    explicit SetLastMouseEvent(GdkEventButton* event)
    {
        g_lastMouseEvent = reinterpret_cast<GdkEvent*>(event);
    }

    explicit SetLastMouseEvent(GdkEventMotion* event)
    {
        g_lastMouseEvent = reinterpret_cast<GdkEvent*>(event);
    }
#endif // __WXGTK4__/!__WXGTK4__

    ~SetLastMouseEvent()
    {
        g_lastMouseEvent = nullptr;
    }
};


wxWindowGTK* g_windowUnderMouse = nullptr;

bool SetWindowUnderMouse(wxWindowGTK* win)
{
    if ( g_windowUnderMouse == win )
        return false;

    g_windowUnderMouse = win;

    return true;
}

template <typename EventType>
gboolean SendEnterLeaveEvents(wxWindowGTK* win, EventType* gdk_event);

#ifdef __WXGTK4__

// See the declaration in wx/gtk/private/event.h.
bool GetPointerPosition(GtkWidget* widget, double* x, double* y)
{
    GtkNative* const native = gtk_widget_get_native(widget);
    if ( !native )
        return false;

    GdkSurface* const surface = gtk_native_get_surface(native);

    // gtk_native_get_surface() keeps returning the surface of a toplevel being
    // torn down, and GTK goes on synthesizing crossing events for it, so this
    // is reached with a surface that is already destroyed. Asking such a
    // surface for anything is at best useless and, under X11, fatal: see
    // below.
    if ( !surface || gdk_surface_is_destroyed(surface) )
        return false;

    GdkDisplay* const display = gtk_widget_get_display(widget);
    GdkDevice* const pointer =
        gdk_seat_get_pointer(gdk_display_get_default_seat(display));
    if ( !pointer )
        return false;

#ifdef GDK_WINDOWING_X11
    if ( GDK_IS_X11_SURFACE(surface) )
    {
        // The query goes to the X server as XIQueryPointer on the surface's
        // window. The check above doesn't make that safe on its own: X is
        // asynchronous, so the window can be destroyed between the check and
        // the request reaching the server, and the server then answers
        // BadWindow, on which GDK's error handler exits the process. This is
        // exactly how an application closing a window with the pointer inside
        // it died on shutdown, see #113. Trapping the error turns an unknown
        // pointer position into a harmless "don't know", which is all the
        // caller needs. The same hazard is handled the same way in
        // wxWindowGTK::GTKGetOrigin().
        wxGCC_WARNING_SUPPRESS(deprecated-declarations)
        gdk_x11_display_error_trap_push(display);

        const bool ok = gdk_surface_get_device_position(surface, pointer,
                                                        x, y, nullptr) != 0;

        // Popping syncs, so the error, if there was one, is caught here rather
        // than arriving later, outside the trap, where it would be fatal.
        const int xerror = gdk_x11_display_error_trap_pop(display);
        wxGCC_WARNING_RESTORE(deprecated-declarations)

        return xerror == 0 && ok;
    }
#endif // GDK_WINDOWING_X11

    return gdk_surface_get_device_position(surface, pointer, x, y, nullptr) != 0;
}

#endif // __WXGTK4__

} // namespace wxGTKImpl

#ifdef wxHAS_XKB
namespace
{

// Global data used for raw key codes translation.
class XkbData
{
public:
    XkbData() = default;

    // Get the state pointer allocating it on demand if necessary.
    xkb_state* GetState()
    {
        if ( !m_state )
        {
            m_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            xkb_rule_names names{};
            names.layout = "us";
            m_keymap = xkb_keymap_new_from_names(m_ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
            m_state = xkb_state_new(m_keymap);
        }

        return m_state;
    }

    // Called by wxXKBModule::OnExit() to free all our data.
    void Free()
    {
        if ( m_state )
        {
            xkb_state_unref(m_state);
            m_state = nullptr;
        }

        if ( m_keymap )
        {
            xkb_keymap_unref(m_keymap);
            m_keymap = nullptr;
        }

        if ( m_ctx )
        {
            xkb_context_unref(m_ctx);
            m_ctx = nullptr;
        }
    }

private:
    xkb_context *m_ctx = nullptr;
    xkb_keymap *m_keymap = nullptr;
    xkb_state *m_state = nullptr;

    wxDECLARE_NO_COPY_CLASS(XkbData);
};

XkbData gs_xkbData;

} // anonymous namespace

// wxXKBModule: used for freeing global xkb data
class wxXKBModule : public wxModule
{
public:
    bool OnInit() override { return true; }
    void OnExit() override { gs_xkbData.Free(); }

private:
    wxDECLARE_DYNAMIC_CLASS(wxXKBModule);
};

wxIMPLEMENT_DYNAMIC_CLASS(wxXKBModule, wxModule);

#endif // wxHAS_XKB

#ifdef __WXGTK3__
static GList* gs_sizeRevalidateList;
static GSList* gs_setSizeRequestList;
#endif
wxRecursionGuardFlag g_inSizeAllocate = 0;

#if GTK_CHECK_VERSION(3,14,0)
    #define wxGTK_HAS_GESTURES_SUPPORT
#endif

#ifdef wxGTK_HAS_GESTURES_SUPPORT

#include "wx/private/extfield.h"

#include <unordered_map>

namespace
{

// Per-window data for gestures support.
class wxWindowGesturesData
{
public:
    // This class has rather unusual "resurrectable" semantics: it is
    // initialized by the ctor as usual, but may then be uninitialized by
    // calling Free() and re-initialized again by calling Reinit().
    wxWindowGesturesData(wxWindowGTK* win, GtkWidget *widget, int eventsMask)
    {
        Reinit(win, widget, eventsMask);
    }

    ~wxWindowGesturesData()
    {
        Free();
    }

    void Reinit(wxWindowGTK* win, GtkWidget *widget, int eventsMask);
    void Free();

    unsigned int         m_touchCount;
    unsigned int         m_lastTouchTime;
    int                  m_gestureState;
    int                  m_allowedGestures;
    int                  m_activeGestures;
    wxPoint              m_lastTouchPoint;
    GdkEventSequence*    m_touchSequence;
    bool                 m_rawTouchEvents;
    double               m_lastPanOffset;    // Last offset for the pan gesture, used to calculate deltas for pan gesture event
    gdouble              m_lastScale;        // Last scale provided by GTK, used when zoom gesture ends
    gdouble              m_lastAngleDelta;   // Last angle provided by GTK, used when rotate gesture ends
    wxPoint              m_lastGesturePoint; // Last zoom/rotate gesture point

    GtkGesture* m_vertical_pan_gesture;
    GtkGesture* m_horizontal_pan_gesture;
    GtkGesture* m_zoom_gesture;
    GtkGesture* m_rotate_gesture;
    GtkGesture* m_long_press_gesture;
};

using wxWindowGesturesMap = std::unordered_map<wxWindow*, wxWindowGesturesData*>;

typedef wxExternalField<wxWindow,
                        wxWindowGesturesData,
                        wxWindowGesturesMap> wxWindowGestures;

} // anonymous namespace

#endif // wxGTK_HAS_GESTURES_SUPPORT

//-----------------------------------------------------------------------------
// debug
//-----------------------------------------------------------------------------

// the trace mask used for the focus debugging messages
#define TRACE_FOCUS wxT("focus")

// A handy function to run from under gdb to show information about the given
// GtkWidget. Right now it only shows its type, we could enhance it to show
// more information later but this is already pretty useful.
const char* wxDumpGtkWidget(GtkWidget* w)
{
    static wxString s;
    s.Printf("GtkWidget %p, type \"%s\"", w, G_OBJECT_TYPE_NAME(w));

    return s.c_str();
}

//-----------------------------------------------------------------------------
// global top level GtkWidget/GdkWindow
//-----------------------------------------------------------------------------

// GTK4 removed GdkWindow entirely, so there is no way to fill in a
// GdkWindow** output parameter for it; only toplevels have a native
// surface at all (via GdkSurface/GtkNative) and none of the current
// callers need that, so the GTK4 overload only ever reports the widget.
// See docs/gtk/gtk4-phase2-window-model-design.md for the full picture.
#ifdef __WXGTK4__
static bool wxGetTopLevel(GtkWidget** widget)
{
    wxWindowList::const_iterator i = wxTopLevelWindows.begin();
    for (; i != wxTopLevelWindows.end(); ++i)
    {
        const wxWindow* win = *i;
        if (win->m_widget && gtk_widget_get_realized(win->m_widget))
        {
            if (widget)
                *widget = win->m_widget;
            return true;
        }
    }
    return false;
}
#else
static bool wxGetTopLevel(GtkWidget** widget, GdkWindow** window)
{
    wxWindowList::const_iterator i = wxTopLevelWindows.begin();
    for (; i != wxTopLevelWindows.end(); ++i)
    {
        const wxWindow* win = *i;
        if (win->m_widget)
        {
            GdkWindow* gdkwin = gtk_widget_get_window(win->m_widget);
            if (gdkwin)
            {
                if (widget)
                    *widget = win->m_widget;
                if (window)
                    *window = gdkwin;
                return true;
            }
        }
    }
    return false;
}
#endif // __WXGTK4__/!__WXGTK4__

GtkWidget* wxGetTopLevelGTK()
{
    GtkWidget* widget = nullptr;
#ifdef __WXGTK4__
    wxGetTopLevel(&widget);
#else
    wxGetTopLevel(&widget, nullptr);
#endif
    return widget;
}

#ifndef __WXGTK4__
GdkWindow* wxGetTopLevelGDK()
{
    GdkWindow* window;
    if (!wxGetTopLevel(nullptr, &window))
        window = gdk_get_default_root_window();
    return window;
}
#endif // !__WXGTK4__

// Cross-version replacement for the common "I just want a display" use of
// wxGetTopLevelGDK() (gdk_window_get_display(wxGetTopLevelGDK())): under
// GTK4 there's no window to get a display from any more, but wx apps only
// ever use a single (the default) display in practice, so this is
// equivalent to the old behaviour for all realistic use cases.
#ifdef __WXGTK4__
GdkDisplay* wxGetTopLevelGdkDisplay()
{
    return gdk_display_get_default();
}
#else
GdkDisplay* wxGetTopLevelGdkDisplay()
{
    return gdk_window_get_display(wxGetTopLevelGDK());
}
#endif // __WXGTK4__/!__WXGTK4__

PangoContext* wxGetPangoContext()
{
    PangoContext* context = nullptr;
    GtkWidget* widget;
#ifdef __WXGTK4__
    if (wxGetTopLevel(&widget))
    {
        context = gtk_widget_get_pango_context(widget);
        g_object_ref(context);
    }
    else
    {
        // GdkScreen doesn't exist any more under GTK4, so there's no
        // screen-based fallback available; go straight to the same
        // default-font-map fallback used below for console applications.
        context = pango_font_map_create_context(
                        pango_cairo_font_map_get_default());
    }
#else
    if (wxGetTopLevel(&widget, nullptr))
    {
        context = gtk_widget_get_pango_context(widget);
        g_object_ref(context);
    }
    else
    {
        if ( GdkScreen *screen = gdk_screen_get_default() )
        {
            context = gdk_pango_context_get_for_screen(screen);
        }
#if PANGO_VERSION_CHECK(1,22,0)
        else // No default screen.
        {
            // This may happen in console applications which didn't open the
            // display, use the default font map for them -- it's better than
            // nothing.
            if (wx_pango_version_check(1,22,0) == nullptr)
            {
                context = pango_font_map_create_context(
                                pango_cairo_font_map_get_default ());
            }
            //else: pango_font_map_create_context() not available
        }
#endif // Pango 1.22+
    }
#endif // __WXGTK4__/!__WXGTK4__

    return context;
}

#ifdef __WXGTK3__
static bool IsBackend(void* instance, const char* string)
{
    if (instance == nullptr)
    {
        // The backend (X11/Wayland/...) is a property of the display, not
        // of any particular window, so using the default display's type
        // instead of a toplevel's GdkWindow type (not available under
        // GTK4 any more) is equivalent: both give e.g. "GdkWaylandWindow"
        // vs. "GdkWaylandDisplay", which match the same "GdkWayland"/
        // "GdkX11" prefixes checked by the callers below.
#ifdef __WXGTK4__
        instance = wxGetTopLevelGdkDisplay();
#else
        instance = wxGetTopLevelGDK();
#endif
    }
    const char* name = g_type_name(G_TYPE_FROM_INSTANCE(instance));
    return strncmp(string, name, strlen(string)) == 0;
}

WXDLLIMPEXP_CORE
bool wxGTKImpl::IsWayland(void* instance)
{
    static wxByte is = 2;
    if (is > 1)
        is = IsBackend(instance, "GdkWayland");
    return bool(is);
}

WXDLLIMPEXP_CORE
bool wxGTKImpl::IsX11(void* instance)
{
    static wxByte is = 2;
    if (is > 1)
        is = IsBackend(instance, "GdkX11");
    return bool(is);
}
#endif // __WXGTK3__

//-----------------------------------------------------------------------------
// "expose_event"/"draw" from m_wxwindow
//-----------------------------------------------------------------------------

extern "C" {
#if defined(__WXGTK3__) && !defined(__WXGTK4__)
static gboolean draw(GtkWidget*, cairo_t* cr, wxWindow* win)
{
    if (gtk_cairo_should_draw_window(cr, win->GTKGetDrawingWindow()))
        win->GTKSendPaintEvents(cr);

    return false;
}
#elif !defined(__WXGTK4__) // !__WXGTK3__
static gboolean expose_event(GtkWidget*, GdkEventExpose* gdk_event, wxWindow* win)
{
    if (gdk_event->window == win->GTKGetDrawingWindow())
        win->GTKSendPaintEvents(gdk_event->region);

    return false;
}
#endif // !__WXGTK3__
}

#ifndef __WXUNIVERSAL__
//-----------------------------------------------------------------------------
// "expose_event"/"draw" from m_wxwindow->parent, for drawing border
//-----------------------------------------------------------------------------

#ifndef __WXGTK4__
extern "C" {
static gboolean
#ifdef __WXGTK3__
draw_border(GtkWidget* widget, cairo_t* cr, wxWindow* win)
#else
draw_border(GtkWidget* widget, GdkEventExpose* gdk_event, wxWindow* win)
#endif
{
#ifdef __WXGTK3__
    if (!gtk_cairo_should_draw_window(cr, gtk_widget_get_parent_window(win->m_wxwindow)))
#else
    if (gdk_event->window != gtk_widget_get_parent_window(win->m_wxwindow))
#endif
        return false;

    if (!win->IsShown())
        return false;

    GtkAllocation alloc;
    gtk_widget_get_allocation(win->m_wxwindow, &alloc);
    int x = alloc.x;
    int y = alloc.y;
    const int w = alloc.width;
    const int h = alloc.height;
#ifdef __WXGTK3__
    if (!wx_gtk_widget_get_has_window(widget))
    {
        // cairo_t origin is set to widget's origin, need to adjust
        // coordinates for child when they are not relative to parent
        gtk_widget_get_allocation(widget, &alloc);
        x -= alloc.x;
        y -= alloc.y;
    }
#endif

    if (w <= 0 || h <= 0)
        return false;

    if (win->HasFlag(wxBORDER_SIMPLE))
    {
#ifdef __WXGTK3__
        GtkStyleContext* sc = gtk_widget_get_style_context(win->m_wxwindow);
        GdkRGBA* c;
        gtk_style_context_save(sc);
        gtk_style_context_set_state(sc, GTK_STATE_FLAG_NORMAL);
        gtk_style_context_get(sc, GTK_STATE_FLAG_NORMAL, "border-color", &c, nullptr);
        gtk_style_context_restore(sc);
        gdk_cairo_set_source_rgba(cr, c);
        gdk_rgba_free(c);
        cairo_set_line_width(cr, 1);
        cairo_rectangle(cr, x + 0.5, y + 0.5, w - 1, h - 1);
        cairo_stroke(cr);
#else
        gdk_draw_rectangle(gdk_event->window,
            gtk_widget_get_style(widget)->black_gc, false, x, y, w - 1, h - 1);
#endif
    }
    else if (win->HasFlag(wxBORDER_RAISED | wxBORDER_SUNKEN | wxBORDER_THEME))
    {
#ifdef __WXGTK3__
        //TODO: wxBORDER_RAISED/wxBORDER_SUNKEN
        GtkStyleContext*
            sc = gtk_widget_get_style_context(wxGTKPrivate::GetEntryWidget());

        gtk_render_frame(sc, cr, x, y, w, h);
#else // !__WXGTK3__
        GtkShadowType shadow = GTK_SHADOW_IN;
        if (win->HasFlag(wxBORDER_RAISED))
            shadow = GTK_SHADOW_OUT;

        GtkStyle* style;
        const char* detail;
        if (win->HasFlag(wxHSCROLL | wxVSCROLL))
        {
            style = gtk_widget_get_style(wxGTKPrivate::GetTreeWidget());
            detail = "viewport";
        }
        else
        {
            style = gtk_widget_get_style(wxGTKPrivate::GetEntryWidget());
            detail = "entry";
        }

        // clip rect is required to avoid painting background
        // over upper left (w,h) of parent window
        GdkRectangle clipRect = { x, y, w, h };
        gtk_paint_shadow(
           style, gdk_event->window, GTK_STATE_NORMAL,
           shadow, &clipRect, widget, detail, x, y, w, h);
#endif // !__WXGTK3__
    }
    return false;
}
}

//-----------------------------------------------------------------------------
// "parent_set" from m_wxwindow
//-----------------------------------------------------------------------------

#ifndef __WXGTK4__

extern "C" {
static void
parent_set(GtkWidget* widget, GtkWidget* old_parent, wxWindow* win)
{
    if (old_parent)
    {
        g_signal_handlers_disconnect_by_func(
            old_parent, (void*)draw_border, win);
    }
    GtkWidget* parent = gtk_widget_get_parent(widget);
    if (parent)
    {
#ifdef __WXGTK3__
        g_signal_connect_after(parent, "draw", G_CALLBACK(draw_border), win);
#else
        g_signal_connect_after(parent, "expose_event", G_CALLBACK(draw_border), win);
#endif
    }
}
}

#endif // !__WXGTK4__
#endif // !__WXGTK4__
#endif // !__WXUNIVERSAL__

//-----------------------------------------------------------------------------
// "key_press_event" from any window
//-----------------------------------------------------------------------------

// set WXTRACE to this to see the key event codes on the console
#define TRACE_KEYS  wxT("keyevent")

// translates an X key symbol to WXK_XXX value
//
// if isChar is true it means that the value returned will be used for EVT_CHAR
// event and then we choose the logical WXK_XXX, i.e. '/' for GDK_KP_Divide,
// for example, while if it is false it means that the value is going to be
// used for KEY_DOWN/UP events and then we translate GDK_KP_Divide to
// WXK_NUMPAD_DIVIDE
static long wxTranslateKeySymToWXKey(KeySym keysym, bool isChar)
{
    long key_code;

    switch ( keysym )
    {
        // Shift, Control and Alt don't generate the CHAR events at all
        case GDK_KEY_Shift_L:
        case GDK_KEY_Shift_R:
            key_code = isChar ? 0 : WXK_SHIFT;
            break;
        case GDK_KEY_Control_L:
        case GDK_KEY_Control_R:
            key_code = isChar ? 0 : WXK_CONTROL;
            break;
        case GDK_KEY_Meta_L:
        case GDK_KEY_Meta_R:
        case GDK_KEY_Alt_L:
        case GDK_KEY_Alt_R:
        case GDK_KEY_Super_L:
        case GDK_KEY_Super_R:
            key_code = isChar ? 0 : WXK_ALT;
            break;

        // neither do the toggle modifies
        case GDK_KEY_Scroll_Lock:
            key_code = isChar ? 0 : WXK_SCROLL;
            break;

        case GDK_KEY_Caps_Lock:
            key_code = isChar ? 0 : WXK_CAPITAL;
            break;

        case GDK_KEY_Num_Lock:
            key_code = isChar ? 0 : WXK_NUMLOCK;
            break;


        // various other special keys
        case GDK_KEY_Menu:
            key_code = WXK_MENU;
            break;

        case GDK_KEY_Help:
            key_code = WXK_HELP;
            break;

        case GDK_KEY_BackSpace:
            key_code = WXK_BACK;
            break;

        case GDK_KEY_ISO_Left_Tab:
        case GDK_KEY_Tab:
            key_code = WXK_TAB;
            break;

        case GDK_KEY_Linefeed:
        case GDK_KEY_Return:
            key_code = WXK_RETURN;
            break;

        case GDK_KEY_Clear:
            key_code = WXK_CLEAR;
            break;

        case GDK_KEY_Pause:
            key_code = WXK_PAUSE;
            break;

        case GDK_KEY_Select:
            key_code = WXK_SELECT;
            break;

        case GDK_KEY_Print:
            key_code = WXK_PRINT;
            break;

        case GDK_KEY_Execute:
            key_code = WXK_EXECUTE;
            break;

        case GDK_KEY_Escape:
            key_code = WXK_ESCAPE;
            break;

        // cursor and other extended keyboard keys
        case GDK_KEY_Delete:
            key_code = WXK_DELETE;
            break;

        case GDK_KEY_Home:
            key_code = WXK_HOME;
            break;

        case GDK_KEY_Left:
            key_code = WXK_LEFT;
            break;

        case GDK_KEY_Up:
            key_code = WXK_UP;
            break;

        case GDK_KEY_Right:
            key_code = WXK_RIGHT;
            break;

        case GDK_KEY_Down:
            key_code = WXK_DOWN;
            break;

        case GDK_KEY_Prior:     // == GDK_KEY_Page_Up
            key_code = WXK_PAGEUP;
            break;

        case GDK_KEY_Next:      // == GDK_KEY_Page_Down
            key_code = WXK_PAGEDOWN;
            break;

        case GDK_KEY_End:
            key_code = WXK_END;
            break;

        case GDK_KEY_Begin:
            key_code = WXK_HOME;
            break;

        case GDK_KEY_Insert:
            key_code = WXK_INSERT;
            break;


        // numpad keys
        case GDK_KEY_KP_0:
        case GDK_KEY_KP_1:
        case GDK_KEY_KP_2:
        case GDK_KEY_KP_3:
        case GDK_KEY_KP_4:
        case GDK_KEY_KP_5:
        case GDK_KEY_KP_6:
        case GDK_KEY_KP_7:
        case GDK_KEY_KP_8:
        case GDK_KEY_KP_9:
            key_code = (isChar ? '0' : int(WXK_NUMPAD0)) + keysym - GDK_KEY_KP_0;
            break;

        case GDK_KEY_KP_Space:
            key_code = isChar ? ' ' : int(WXK_NUMPAD_SPACE);
            break;

        case GDK_KEY_KP_Tab:
            key_code = isChar ? WXK_TAB : WXK_NUMPAD_TAB;
            break;

        case GDK_KEY_KP_Enter:
            key_code = isChar ? WXK_RETURN : WXK_NUMPAD_ENTER;
            break;

        case GDK_KEY_KP_F1:
            key_code = isChar ? WXK_F1 : WXK_NUMPAD_F1;
            break;

        case GDK_KEY_KP_F2:
            key_code = isChar ? WXK_F2 : WXK_NUMPAD_F2;
            break;

        case GDK_KEY_KP_F3:
            key_code = isChar ? WXK_F3 : WXK_NUMPAD_F3;
            break;

        case GDK_KEY_KP_F4:
            key_code = isChar ? WXK_F4 : WXK_NUMPAD_F4;
            break;

        case GDK_KEY_KP_Home:
            key_code = isChar ? WXK_HOME : WXK_NUMPAD_HOME;
            break;

        case GDK_KEY_KP_Left:
            key_code = isChar ? WXK_LEFT : WXK_NUMPAD_LEFT;
            break;

        case GDK_KEY_KP_Up:
            key_code = isChar ? WXK_UP : WXK_NUMPAD_UP;
            break;

        case GDK_KEY_KP_Right:
            key_code = isChar ? WXK_RIGHT : WXK_NUMPAD_RIGHT;
            break;

        case GDK_KEY_KP_Down:
            key_code = isChar ? WXK_DOWN : WXK_NUMPAD_DOWN;
            break;

        case GDK_KEY_KP_Prior: // == GDK_KP_Page_Up
            key_code = isChar ? WXK_PAGEUP : WXK_NUMPAD_PAGEUP;
            break;

        case GDK_KEY_KP_Next: // == GDK_KP_Page_Down
            key_code = isChar ? WXK_PAGEDOWN : WXK_NUMPAD_PAGEDOWN;
            break;

        case GDK_KEY_KP_End:
            key_code = isChar ? WXK_END : WXK_NUMPAD_END;
            break;

        case GDK_KEY_KP_Begin:
            key_code = WXK_NUMPAD_BEGIN;
            break;

        case GDK_KEY_KP_Insert:
            key_code = isChar ? WXK_INSERT : WXK_NUMPAD_INSERT;
            break;

        case GDK_KEY_KP_Delete:
            key_code = isChar ? WXK_DELETE : WXK_NUMPAD_DELETE;
            break;

        case GDK_KEY_KP_Equal:
            key_code = isChar ? '=' : int(WXK_NUMPAD_EQUAL);
            break;

        case GDK_KEY_KP_Multiply:
            key_code = isChar ? '*' : int(WXK_NUMPAD_MULTIPLY);
            break;

        case GDK_KEY_KP_Add:
            key_code = isChar ? '+' : int(WXK_NUMPAD_ADD);
            break;

        case GDK_KEY_KP_Separator:
            // FIXME: what is this?
            key_code = isChar ? '.' : int(WXK_NUMPAD_SEPARATOR);
            break;

        case GDK_KEY_KP_Subtract:
            key_code = isChar ? '-' : int(WXK_NUMPAD_SUBTRACT);
            break;

        case GDK_KEY_KP_Decimal:
            key_code = isChar ? '.' : int(WXK_NUMPAD_DECIMAL);
            break;

        case GDK_KEY_KP_Divide:
            key_code = isChar ? '/' : int(WXK_NUMPAD_DIVIDE);
            break;


        // function keys
        case GDK_KEY_F1:
        case GDK_KEY_F2:
        case GDK_KEY_F3:
        case GDK_KEY_F4:
        case GDK_KEY_F5:
        case GDK_KEY_F6:
        case GDK_KEY_F7:
        case GDK_KEY_F8:
        case GDK_KEY_F9:
        case GDK_KEY_F10:
        case GDK_KEY_F11:
        case GDK_KEY_F12:
            key_code = WXK_F1 + keysym - GDK_KEY_F1;
            break;
#if GTK_CHECK_VERSION(2,18,0)
        case GDK_KEY_Back:
            key_code = WXK_BROWSER_BACK;
            break;
        case GDK_KEY_Forward:
            key_code = WXK_BROWSER_FORWARD;
            break;
        case GDK_KEY_Refresh:
            key_code = WXK_BROWSER_REFRESH;
            break;
        case GDK_KEY_Stop:
            key_code = WXK_BROWSER_STOP;
            break;
        case GDK_KEY_Search:
            key_code = WXK_BROWSER_SEARCH;
            break;
        case GDK_KEY_Favorites:
            key_code = WXK_BROWSER_FAVORITES;
            break;
        case GDK_KEY_HomePage:
            key_code = WXK_BROWSER_HOME;
            break;
        case GDK_KEY_AudioMute:
            key_code = WXK_VOLUME_MUTE;
            break;
        case GDK_KEY_AudioLowerVolume:
            key_code = WXK_VOLUME_DOWN;
            break;
        case GDK_KEY_AudioRaiseVolume:
            key_code = WXK_VOLUME_UP;
            break;
        case GDK_KEY_AudioNext:
            key_code = WXK_MEDIA_NEXT_TRACK;
            break;
        case GDK_KEY_AudioPrev:
            key_code = WXK_MEDIA_PREV_TRACK;
            break;
        case GDK_KEY_AudioStop:
            key_code = WXK_MEDIA_STOP;
            break;
        case GDK_KEY_AudioPlay:
            key_code = WXK_MEDIA_PLAY_PAUSE;
            break;
        case GDK_KEY_Mail:
            key_code = WXK_LAUNCH_MAIL;
            break;

        case GDK_KEY_Launch0:
        case GDK_KEY_Launch1:
        case GDK_KEY_Launch2:
        case GDK_KEY_Launch3:
        case GDK_KEY_Launch4:
        case GDK_KEY_Launch5:
        case GDK_KEY_Launch6:
        case GDK_KEY_Launch7:
        case GDK_KEY_Launch8:
        case GDK_KEY_Launch9:
        case GDK_KEY_LaunchA:
        case GDK_KEY_LaunchB:
        case GDK_KEY_LaunchC:
        case GDK_KEY_LaunchD:
        case GDK_KEY_LaunchE:
        case GDK_KEY_LaunchF:
            key_code = WXK_LAUNCH_0 + (keysym - GDK_KEY_Launch0);
            break;
#endif // GTK_CHECK_VERSION(2,18,0)

        default:
            key_code = 0;
    }

    return key_code;
}

#if wxDEBUG_LEVEL
static wxString wxDumpUniChar(wxChar unichar)
{
    // Represent control characters as Ctrl-<char> for readability.
    if ( unichar == 0 )
        return "NUL";
    else if ( unichar < 0x20 )
        return wxString::Format("Ctrl-%c", unichar + 0x40);
    else if ( unichar == 0x7F )
        return "DEL";
    else
        return wxString::Format("'%c'", unichar);
}
#endif // wxDEBUG_LEVEL

// The parts of a native key event that wx actually uses.
//
// Under GTK3 these are simply GdkEventKey's fields. Under GTK4 GdkEvent is
// opaque and GtkEventControllerKey hands these values straight to its signal
// handlers, so there is no struct to point at -- hence collecting them into a
// value type, which lets the (large, and entirely value-based) translation
// logic below stay shared between the two backends instead of being
// duplicated. Field names deliberately match GdkEventKey's.
struct wxGTKKeyEventData
{
    guint keyval;
    guint hardware_keycode;
    guint state;
    guint32 time;
    bool isPress;   // GTK3 read this off gdk_event->type
};

#ifdef __WXGTK4__

// Build the above from an opaque GdkEvent. Needed on the input-method path,
// which gets a GdkEvent* rather than the signal arguments.
static wxGTKKeyEventData wxGTKMakeKeyEventData(GdkEvent* gdk_event)
{
    wxGTKKeyEventData keyData;
    keyData.keyval = gdk_key_event_get_keyval(gdk_event);
    keyData.hardware_keycode = gdk_key_event_get_keycode(gdk_event);
    keyData.state = gdk_event_get_modifier_state(gdk_event);
    keyData.time = gdk_event_get_time(gdk_event);
    keyData.isPress = gdk_event_get_event_type(gdk_event) == GDK_KEY_PRESS;
    return keyData;
}

#else // !__WXGTK4__

static wxGTKKeyEventData wxGTKMakeKeyEventData(GdkEventKey* gdk_event)
{
    wxGTKKeyEventData keyData;
    keyData.keyval = gdk_event->keyval;
    keyData.hardware_keycode = gdk_event->hardware_keycode;
    keyData.state = gdk_event->state;
    keyData.time = gdk_event->time;
    keyData.isPress = gdk_event->type == GDK_KEY_PRESS;
    return keyData;
}

#endif // __WXGTK4__/!__WXGTK4__

static void wxFillOtherKeyEventFields(wxKeyEvent& event,
                                      wxWindowGTK *win,
                                      const wxGTKKeyEventData& keyData)
{
    event.SetTimestamp( keyData.time );
    event.SetId(win->GetId());

    event.m_shiftDown = (keyData.state & GDK_SHIFT_MASK) != 0;
    event.m_controlDown = (keyData.state & GDK_CONTROL_MASK) != 0;
#ifdef __WXGTK4__
    event.m_altDown = (keyData.state & GDK_ALT_MASK) != 0;
#else
    event.m_altDown = (keyData.state & GDK_MOD1_MASK) != 0;
#endif
    event.m_metaDown = (keyData.state & GDK_META_MASK) != 0;

    // At least with current Linux systems, MOD5 corresponds to AltGr key and
    // we represent it, for consistency with Windows, which really allows to
    // use Ctrl+Alt as a replacement for AltGr if this key is not present, as a
    // combination of these two modifiers.
#ifdef __WXGTK4__
    // GDK_MOD5_MASK is gone under GTK4: the modifier enum was trimmed to the
    // named modifiers, so the AltGr-as-Ctrl+Alt convention below cannot be
    // detected. Known gap; AltGr will report as neither.
    if ( false )
    {
#else
    if ( keyData.state & GDK_MOD5_MASK )
    {
#endif
        event.m_controlDown =
        event.m_altDown = true;
    }

    // Normally we take the state of modifiers directly from the low level GDK
    // event but unfortunately GDK uses a different convention from MSW for the
    // key events corresponding to the modifier keys themselves: in it, when
    // e.g. Shift key is pressed, GDK_SHIFT_MASK is not set while it is set
    // when Shift is released. Under MSW the situation is exactly reversed and
    // the modifier corresponding to the key is set when it is pressed and
    // unset when it is released. To ensure consistent behaviour between
    // platforms (and because it seems to make slightly more sense, although
    // arguably both behaviours are reasonable) we follow MSW here.
    //
    // Final notice: we set the flags to the desired value instead of just
    // inverting them because they are not set correctly (i.e. in the same way
    // as for the real events generated by the user) for wxUIActionSimulator-
    // produced events and it seems better to keep that class code the same
    // among all platforms and fix the discrepancy here instead of adding
    // wxGTK-specific code to wxUIActionSimulator.
    const bool isPress = keyData.isPress;
    switch ( keyData.keyval )
    {
        case GDK_KEY_Shift_L:
        case GDK_KEY_Shift_R:
            event.m_shiftDown = isPress;
            break;

        case GDK_KEY_Control_L:
        case GDK_KEY_Control_R:
            event.m_controlDown = isPress;
            break;

        case GDK_KEY_Alt_L:
        case GDK_KEY_Alt_R:
            event.m_altDown = isPress;
            break;

        case GDK_KEY_Meta_L:
        case GDK_KEY_Meta_R:
        case GDK_KEY_Super_L:
        case GDK_KEY_Super_R:
            event.m_metaDown = isPress;
            break;
    }

    event.m_rawCode = (wxUint32) keyData.keyval;
    event.m_rawFlags = keyData.hardware_keycode;

    event.m_isRepeat = false; // Detecting key repeat not implemented.

    event.SetEventObject( win );
}


// This function is used for KEY events only, not CHAR ones and so never sets
// wxKeyEvent::m_uniChar to non-ASCII values.
static void
wxTranslateGTKKeyEventToWx(wxKeyEvent& event,
                           wxWindowGTK *win,
                           const wxGTKKeyEventData& keyData)
{
    const KeySym keysym = keyData.keyval;

    wxString extraTraceInfo;

    // Check for special keys first: we need to do it even for the keys that
    // could have an ASCII equivalent because we need to distinguish numpad
    // keys from the ones on the main keyboard.
    long key_code = wxTranslateKeySymToWXKey(keysym, false /* !isChar */);

    guint32 unichar = 0;
    if ( !key_code )
        unichar = gdk_keyval_to_unicode(keysym);

    if ( unichar )
    {
        // The convention used here is rather strange, but conforms to what
        // wxMSW does: if we get a Latin letter, we use its upper case version
        // as key code independently of the current layout, so that pressing
        // the letter marked "Q" on a French keyboard in AZERTY layout
        // generates the events with "Q" key code.
        //
        // But for the non-letters, or non-Latin letters, we use the character
        // that is generated by the key which produced it in the US keyboard
        // layout. The rationale is that otherwise we wouldn't be able to set
        // key code at all (for non-Latin letters) or generate key events with
        // key codes that can't be generated in the US layout (e.g. continuing
        // with the French example, "1" would generate "&" key code which can
        // never be entered in the standard US layout).
        //
        // However see also the hack inside the hack for some non-letter
        // characters below.
        if ( (unichar >= 'A' && unichar <= 'Z') ||
                (unichar >= 'a' && unichar <= 'z') )
        {
            // We'll convert lower-case to upper below.
            key_code = unichar;
        }
        else
        {
#ifdef wxHAS_XKB
            char key_code_str[64];
            xkb_state_key_get_utf8(gs_xkbData.GetState(),
                                   keyData.hardware_keycode,
                                   key_code_str,
                                   sizeof(key_code_str));
            if ( strlen(key_code_str) == 1 )
            {
                extraTraceInfo = " [XKB]";

                key_code = key_code_str[0];

                // Another hack for wxMSW compatibility: for the non-digit keys
                // (not characters), we still use their value if it is ASCII,
                // so that the key marked as "$" on a French keyboard generates
                // this key and not "]" that it would generate in the US layout
                // but which is located on a completely different key of the
                // French keyboard.
                //
                // See also the code handling VK_OEM_xxx keys in wxMSW.
                switch ( key_code )
                {
                    case ';':
                    case '=':
                    case ',':
                    case '-':
                    case '.':
                    case '/':
                    case '`':
                    case '[':
                    case '\\':
                    case ']':
                    case '\'':
                        if ( unichar < 0x100 )
                            key_code = unichar;
                        break;
                }
            }
            else
#endif // wxHAS_XKB
            {
                // Without XKB we can only fall back on using the Unicode key
                // code if possible.
                if ( unichar < 256 )
                {
                    key_code = unichar;
                }
                else
                {
                    extraTraceInfo = " [not Latin-1]";
                }
            }
        }

        if ( key_code >= 'a' && key_code <= 'z' )
        {
            key_code = toupper(key_code);
        }
    }

    wxLogTrace(TRACE_KEYS, "Key %s event: %lu -> char='%c' key=%ld%s",
               event.GetEventType() == wxEVT_KEY_UP ? "release" : "press",
               static_cast<unsigned long>(keysym),
               unichar,
               key_code,
               extraTraceInfo);

    event.m_keyCode = key_code;
    if ( event.m_keyCode < 0x100 )
    {
        // Set Unicode key code to the Latin-1 equivalent for compatibility.
        // E.g. let RETURN generate the key event with both key and Unicode key
        // codes of 13.
        event.m_uniChar = event.m_keyCode;
    }

    // now fill all the other fields
    wxFillOtherKeyEventFields(event, win, keyData);
}


namespace
{

// Send wxEVT_CHAR_HOOK event to the parent of the window and return true only
// if it was processed (and not skipped).
bool SendCharHookEvent(const wxKeyEvent& event, wxWindow *win)
{
    // wxEVT_CHAR_HOOK must be sent to allow the parent windows (e.g. a dialog
    // which typically closes when Esc key is pressed in any of its controls)
    // to handle key events in all of its children unless the mouse is captured
    // in which case we consider that the keyboard should be "captured" too.
    if ( !g_captureWindow )
    {
        wxKeyEvent eventCharHook(wxEVT_CHAR_HOOK, event);
        if ( win->HandleWindowEvent(eventCharHook)
                && !event.IsNextEventAllowed() )
            return true;
    }

    return false;
}

// If a widget does not handle a key or mouse event, GTK+ sends it up the
// parent chain until it is handled. These events are not supposed to propagate
// in wxWidgets, so this code avoids handling them in any parent wxWindow,
// while still allowing the event to propagate so things like native keyboard
// navigation will work.
#ifndef __WXGTK4__

static bool gs_isNewEvent;

template <typename EventType>
bool EventAlreadyProcessed(const EventType* event)
{
    // The cast is safe because we can only have windows when using GUI.
    auto* const loop = static_cast<wxGUIEventLoop*>(wxEventLoop::GetActive());
    if ( !loop )
    {
        // This really shouldn't happen, but don't crash if it does.
        return false;
    }

    auto* const ev = reinterpret_cast<const GdkEvent*>(event);

    // Ensure we call GTKIsSameAsLastEvent() in any case to always update the
    // last stored event (i.e. the order of checks here matters).
    if ( loop->GTKIsSameAsLastEvent(ev, sizeof(EventType)) && !gs_isNewEvent )
        return true;

    gs_isNewEvent = false;

    return false;
}

#endif // !__WXGTK4__

} // anonymous namespace

// The body of key-press handling, shared between the GTK3 signal callback and
// the GTK4 event-controller callback below. Takes the values it needs plus the
// native event, which is only used for the input-method path -- opaque under
// GTK4, but the IM context consumes it either way.
static bool
wxGTKHandleKeyPress(wxWindow* win,
                    const wxGTKKeyEventData& keyData,
                    wxGTKNativeKeyEvent* nativeEvent)
{
    wxKeyEvent event( wxEVT_KEY_DOWN );
    bool ret = false;

    wxTranslateGTKKeyEventToWx(event, win, keyData);
    // Send the CHAR_HOOK event first
    if ( SendCharHookEvent(event, win) )
    {
        // Don't do anything at all with this event any more.
        return true;
    }

    // Next check for accelerators.
#if wxUSE_ACCEL
    wxWindowGTK *ancestor = win;
    while (ancestor)
    {
        int command = ancestor->GetAcceleratorTable()->GetCommand( event );
        if (command != -1)
        {
            wxCommandEvent menu_event( wxEVT_MENU, command );
            ret = ancestor->HandleWindowEvent( menu_event );

            if ( !ret )
            {
                // if the accelerator wasn't handled as menu event, try
                // it as button click (for compatibility with other
                // platforms):
                wxCommandEvent button_event( wxEVT_BUTTON, command );
                ret = ancestor->HandleWindowEvent( button_event );
            }

            break;
        }
        if (ancestor->IsTopNavigationDomain(wxWindow::Navigation_Accel))
            break;
        ancestor = ancestor->GetParent();
    }
#endif // wxUSE_ACCEL

    // If not an accelerator, then emit KEY_DOWN event
    if ( !ret )
        ret = win->HandleWindowEvent( event );

    if ( !ret )
    {
        // Indicate that IM handling is in process by setting this pointer
        // (which will remain valid for all the code called during IM key
        // handling).
        win->m_imKeyEvent = nativeEvent;

        // We should let GTK+ IM filter key event first. According to GTK+ 2.0 API
        // docs, if IM filter returns true, no further processing should be done.
        // we should send the key_down event anyway.
        const int intercepted_by_IM = win->GTKIMFilterKeypress(nativeEvent);

        win->m_imKeyEvent = nullptr;

        if ( intercepted_by_IM )
        {
            wxLogTrace(TRACE_KEYS, wxT("Key event intercepted by IM"));
            return true;
        }
    }

    // Only send wxEVT_CHAR event if not processed yet. Thus, ALT-x
    // will only be sent if it is not in an accelerator table.
    //
    // This "loop" is executed at most once and only exists to be able to break
    // from it below.
    while ( !ret )
    {
        KeySym keysym = keyData.keyval;

        wxKeyEvent eventChar(wxEVT_CHAR, event);

        long keyCode = wxTranslateKeySymToWXKey(keysym, true /* isChar */);
        if ( keyCode )
        {
            // Set Unicode value to the key code if possible, this is useful
            // for keys such as BACKSPACE or ENTER.
            eventChar.m_keyCode = keyCode;
            eventChar.m_uniChar = keyCode < WXK_DELETE ? keyCode : 0;
        }
        else if ( guint32 uniChar = gdk_keyval_to_unicode(keysym) )
        {
            // We generate CHAR events for Ctrl-[@-_] key presses with key
            // codes in 0..31 range because it may make sense to handle them in
            // the same way as "real" CHARs (e.g. to handle Ctrl-H as backspace
            // etc).
            if ( eventChar.ControlDown() )
            {
                // We should already have the corresponding key in US layout,
                // translated from GTK using XKB, in the event.
                keyCode = event.m_keyCode;

                if ( (keyCode >= 'A' && keyCode <= 'Z') ||
                        keyCode == '[' ||
                        keyCode == '\\' ||
                        keyCode == ']' ||
                        keyCode == '^' ||
                        keyCode == '_' )
                {
                    // Convert to ASCII control character.
                    keyCode &= 0x1f;
                }
                else if ( keyCode != ' ' )
                {
                    // For the printable characters other than Space (for which
                    // we still do generate CHAR event, for compatibility with
                    // both previous versions of wxGTK and wxMSW) we don't
                    // generate these events at all, as this doesn't seem
                    // very useful and wxMSW doesn't do it.
                    wxLogTrace(TRACE_KEYS, "Not generating char event for Ctrl-%s",
                               wxDumpUniChar(uniChar));
                    break;
                }

                eventChar.m_keyCode = keyCode;
                eventChar.m_uniChar = keyCode;
            }
            else // Not a control character.
            {
                // Set the key code to the Unicode value if possible to allow
                // even Unicode-unaware applications to handle ASCII keys.
                eventChar.m_keyCode = uniChar < WXK_DELETE ? uniChar : 0;
                eventChar.m_uniChar = uniChar;
            }
        }
        else // Not a printable character nor one of recognized special keys.
        {
            break;
        }

        wxLogTrace(TRACE_KEYS, "Char event: key=%ld, char=%s",
                   eventChar.m_keyCode,
                   wxDumpUniChar(eventChar.m_uniChar));

        ret = win->HandleWindowEvent(eventChar);
        break;
    }

    return ret;
}

#ifdef __WXGTK4__

extern "C" {

// GtkEventControllerKey::key-pressed(keyval, keycode, state) -> gboolean.
// The values the GTK3 code read out of GdkEventKey are handed over directly;
// the native event is still available from the controller for the IM path.
static gboolean
wx_gtk_key_pressed_callback(GtkEventControllerKey* controller,
                            guint keyval, guint keycode, GdkModifierType state,
                            wxWindow* win)
{
    if (g_blockEventsOnDrag)
        return FALSE;

    // No EventAlreadyProcessed() check here, deliberately: that guarded
    // against GTK3 propagating one native event up the widget hierarchy so
    // that several wxWindows saw it. A controller is attached to one widget
    // and only fires for it, so the duplication it defended against cannot
    // arise. See docs/gtk/gtk4-phase3-input-model-design.md section 2.

    GtkEventController* const c = GTK_EVENT_CONTROLLER(controller);

    wxGTKKeyEventData keyData;
    keyData.keyval = keyval;
    keyData.hardware_keycode = keycode;
    keyData.state = state;
    keyData.time = gtk_event_controller_get_current_event_time(c);
    keyData.isPress = true;

    // Handling the press can destroy the window it happened in -- wxGrid
    // removes its in-place editor on Enter and on Escape, for one -- so hold a
    // weak reference across the call rather than dereferencing win afterwards.
    wxWeakRef<wxWindow> const winGuard(win);

    const gboolean handled =
        wxGTKHandleKeyPress(win, keyData,
                            gtk_event_controller_get_current_event(c));

    // GTK3 learned that the press had been fully processed from the
    // "event-after" signal, which GTK4 removed. This is the same point in
    // time, and wxTextEntry needs it to flush the single wxEVT_TEXT it
    // coalesces the several "changed" signals of one key press into.
    if ( winGuard )
    {
        if ( wxTextEntry* const entry = dynamic_cast<wxTextEntry*>(winGuard.get()) )
            entry->GTKEntryOnKeypressEnd();
    }

    return handled;
}

// GtkEventControllerKey::key-released(keyval, keycode, state) -> void.
//
// Note this returns void, unlike GTK3's key_release_event which returned a
// gboolean used to stop further handling. wx's return value from the key-up
// event therefore cannot suppress GTK's own processing on release any more --
// a real behavioural difference, but only for key *releases*, which wx code
// very rarely vetoes.
static void
wx_gtk_key_released_callback(GtkEventControllerKey* controller,
                             guint keyval, guint keycode, GdkModifierType state,
                             wxWindowGTK* win)
{
    if (g_blockEventsOnDrag)
        return;

    wxGTKKeyEventData keyData;
    keyData.keyval = keyval;
    keyData.hardware_keycode = keycode;
    keyData.state = state;
    keyData.time = gtk_event_controller_get_current_event_time(
                        GTK_EVENT_CONTROLLER(controller));
    keyData.isPress = false;

    wxKeyEvent event( wxEVT_KEY_UP );
    wxTranslateGTKKeyEventToWx(event, win, keyData);
    win->GTKProcessEvent(event);
}

} // extern "C"

#else // !__WXGTK4__

extern "C" {
static gboolean
gtk_window_key_press_callback( GtkWidget *WXUNUSED(widget),
                               GdkEventKey *gdk_event,
                               wxWindow *win )
{
    if (g_blockEventsOnDrag)
        return FALSE;

    if (EventAlreadyProcessed(gdk_event))
        return FALSE;

    return wxGTKHandleKeyPress(win, wxGTKMakeKeyEventData(gdk_event), gdk_event)
                ? TRUE : FALSE;
}
}

#endif // __WXGTK4__/!__WXGTK4__

int wxWindowGTK::GTKIMFilterKeypress(wxGTKNativeKeyEvent* event) const
{
    return m_imContext ? gtk_im_context_filter_keypress(m_imContext, event)
                       : FALSE;
}

extern "C" {
static void
gtk_wxwindow_commit_cb (GtkIMContext * WXUNUSED(context),
                        const gchar  *str,
                        wxWindow     *window)
{
    // Ignore the return value here, it doesn't matter for the "commit" signal.
    window->GTKDoInsertTextFromIM(str);
}
}

bool wxWindowGTK::GTKDoInsertTextFromIM(const char* str)
{
    wxKeyEvent event( wxEVT_CHAR );

    // take modifiers, cursor position, timestamp etc. from the last
    // key_press_event that was fed into Input Method:
    if ( m_imKeyEvent )
    {
        wxFillOtherKeyEventFields(event, this,
                                  wxGTKMakeKeyEventData(m_imKeyEvent));
    }
    else
    {
        event.SetEventObject(this);
    }

    const wxString data(wxString::FromUTF8Unchecked(str));
    if( data.empty() )
        return false;

    bool processed = false;
    for ( const auto ch : data )
    {
        event.m_uniChar = ch;

        // Set key code to the Unicode value for ASCII characters.
        if ( event.m_uniChar < WXK_DELETE )
            event.m_keyCode = event.m_uniChar;

        wxLogTrace(TRACE_KEYS, "IM sent %s", wxDumpUniChar(event.m_uniChar));

        if ( HandleWindowEvent(event) )
            processed = true;
    }

    return processed;
}

bool wxWindowGTK::GTKOnInsertText(const char* text)
{
    if ( !m_imKeyEvent )
    {
        // We're not inside IM key handling at all.
        return false;
    }

    return GTKDoInsertTextFromIM(text);
}


//-----------------------------------------------------------------------------
// "key_release_event" from any window
//-----------------------------------------------------------------------------

#ifndef __WXGTK4__
extern "C" {
static gboolean
gtk_window_key_release_callback( GtkWidget * WXUNUSED(widget),
                                 GdkEventKey *gdk_event,
                                 wxWindowGTK *win )
{
    if (g_blockEventsOnDrag)
        return FALSE;

    if (EventAlreadyProcessed(gdk_event))
        return FALSE;

    wxKeyEvent event( wxEVT_KEY_UP );
    wxTranslateGTKKeyEventToWx(event, win, wxGTKMakeKeyEventData(gdk_event));

    return win->GTKProcessEvent(event);
}
}
#endif // !__WXGTK4__

// ============================================================================
// the mouse events
// ============================================================================

// ----------------------------------------------------------------------------
// mouse event processing helpers
// ----------------------------------------------------------------------------

static void AdjustEventButtonState(wxMouseEvent& event)
{
    // GDK reports the old state of the button for a button press event, but
    // for compatibility with MSW and common sense we want m_leftDown be TRUE
    // for a LEFT_DOWN event, not FALSE, so we will invert
    // left/right/middleDown for the corresponding click events

    if ((event.GetEventType() == wxEVT_LEFT_DOWN) ||
        (event.GetEventType() == wxEVT_LEFT_DCLICK) ||
        (event.GetEventType() == wxEVT_LEFT_UP))
    {
        event.m_leftDown = !event.m_leftDown;
        return;
    }

    if ((event.GetEventType() == wxEVT_MIDDLE_DOWN) ||
        (event.GetEventType() == wxEVT_MIDDLE_DCLICK) ||
        (event.GetEventType() == wxEVT_MIDDLE_UP))
    {
        event.m_middleDown = !event.m_middleDown;
        return;
    }

    if ((event.GetEventType() == wxEVT_RIGHT_DOWN) ||
        (event.GetEventType() == wxEVT_RIGHT_DCLICK) ||
        (event.GetEventType() == wxEVT_RIGHT_UP))
    {
        event.m_rightDown = !event.m_rightDown;
        return;
    }

    if ((event.GetEventType() == wxEVT_AUX1_DOWN) ||
        (event.GetEventType() == wxEVT_AUX1_DCLICK))
    {
        event.m_aux1Down = true;
        return;
    }

    if ((event.GetEventType() == wxEVT_AUX2_DOWN) ||
        (event.GetEventType() == wxEVT_AUX2_DCLICK))
    {
        event.m_aux2Down = true;
        return;
    }
}

// find the window to send the mouse event to
static
wxWindowGTK *FindWindowForMouseEvent(wxWindowGTK *win, wxCoord& x, wxCoord& y)
{
    // When a window has mouse capture, it should get all the events.
    if ( g_captureWindow )
    {
        win->ClientToScreen(&x, &y);
        g_captureWindow->ScreenToClient(&x, &y);

        return g_captureWindow;
    }

    wxCoord xx = x;
    wxCoord yy = y;

    if (win->m_wxwindow)
    {
        wxPizza* pizza = WX_PIZZA(win->m_wxwindow);
        xx += pizza->m_scroll_x;
        yy += pizza->m_scroll_y;
    }

    wxWindowList::compatibility_iterator node = win->GetChildren().GetFirst();
    while (node)
    {
        wxWindow* child = static_cast<wxWindow*>(node->GetData());

        node = node->GetNext();
        if (!child->IsShown())
            continue;

        if (child->GTKIsTransparentForMouse())
        {
            // wxStaticBox is transparent in the box itself
            int xx1 = child->m_x;
            int yy1 = child->m_y;
            int xx2 = child->m_x + child->m_width;
            int yy2 = child->m_y + child->m_height;

            // left
            if (((xx >= xx1) && (xx <= xx1+10) && (yy >= yy1) && (yy <= yy2)) ||
            // right
                ((xx >= xx2-10) && (xx <= xx2) && (yy >= yy1) && (yy <= yy2)) ||
            // top
                ((xx >= xx1) && (xx <= xx2) && (yy >= yy1) && (yy <= yy1+10)) ||
            // bottom
                ((xx >= xx1) && (xx <= xx2) && (yy >= yy2-1) && (yy <= yy2)))
            {
                win = child;
                x -= child->m_x;
                y -= child->m_y;
                break;
            }

        }
        else
        {
            if ((child->m_wxwindow == nullptr) &&
                win->IsClientAreaChild(child) &&
                (child->m_x <= xx) &&
                (child->m_y <= yy) &&
                (child->m_x+child->m_width  >= xx) &&
                (child->m_y+child->m_height >= yy))
            {
                win = child;
                x -= child->m_x;
                y -= child->m_y;
                break;
            }
        }
    }

    return win;
}

#ifdef __WXGTK3__

extern "C" {

static void
gtk_window_scale_factor_notify(GtkWidget* WXUNUSED(widget),
                               GParamSpec* WXUNUSED(pspec),
                               wxWindowGTK *win)
{
    // Window cursor may depend on the scale factor, so update it to reflect
    // the new value.
    win->WXUpdateCursor();
}

} // extern "C"

#endif // __WXGTK3__

// ----------------------------------------------------------------------------
// common event handlers helpers
// ----------------------------------------------------------------------------

bool wxWindowGTK::GTKProcessEvent(wxEvent& event) const
{
    // nothing special at this level
    return HandleWindowEvent(event);
}

bool wxWindowGTK::GTKShouldIgnoreEvent() const
{
    return g_blockEventsOnDrag;
}

// Some callbacks check for just g_blockEventsOnDrag but others check for both
// it and g_blockEventsOnScroll. It's not really clear why, but define a helper
// function performing the latter check too for now to avoid changing the
// behaviour of the existing code.
namespace
{

bool AreGTKEventsBlocked()
{
    return g_blockEventsOnDrag || g_blockEventsOnScroll;
}

} // anonymous namespace

//-----------------------------------------------------------------------------
// "button_press_event"
//-----------------------------------------------------------------------------

#ifndef __WXGTK4__
gboolean
wxGTKImpl::WindowButtonPressCallback(GtkWidget* WXUNUSED_IN_GTK3(widget),
                                     GdkEventButton* gdk_event,
                                     wxWindowGTK* win, bool synthesized)
{
    wxLogTrace(TRACE_MOUSE, "Press for button %d at %g,%g in %s at t=%u",
               gdk_event->button, gdk_event->x, gdk_event->y,
               wxDumpWindow(win),
               gdk_event->time);

    /*
      GTK does not set the button1 mask when the event comes from the left
      button of a mouse. but for some reason, it sets it when the event comes
      from a touchscreen, so we simply remove it here for consistency.
    */
    gdk_event->state &= ~GDK_BUTTON1_MASK;

    if (EventAlreadyProcessed(gdk_event))
        return FALSE;

    if ( AreGTKEventsBlocked() )
        return FALSE;

    g_lastButtonNumber = gdk_event->button;

    wxEventType event_type;
    wxEventType down;
    wxEventType dclick;
    switch (gdk_event->button)
    {
        case 1:
            down = wxEVT_LEFT_DOWN;
            dclick = wxEVT_LEFT_DCLICK;
            break;
        case 2:
            down = wxEVT_MIDDLE_DOWN;
            dclick = wxEVT_MIDDLE_DCLICK;
            break;
        case 3:
            down = wxEVT_RIGHT_DOWN;
            dclick = wxEVT_RIGHT_DCLICK;
            break;
        case 8:
            down = wxEVT_AUX1_DOWN;
            dclick = wxEVT_AUX1_DCLICK;
            break;
        case 9:
            down = wxEVT_AUX2_DOWN;
            dclick = wxEVT_AUX2_DCLICK;
            break;
        default:
            return false;
    }
    switch (gdk_event->type)
    {
        case GDK_BUTTON_PRESS:
            event_type = down;
            // GDK sends surplus button down events
            // before a double click event. We
            // need to filter these out.
            if (win->m_wxwindow)
            {
                GdkEvent* peek_event = gdk_event_peek();
                if (peek_event)
                {
                    const GdkEventType peek_event_type = peek_event->type;
                    gdk_event_free(peek_event);
                    if (peek_event_type == GDK_2BUTTON_PRESS ||
                        peek_event_type == GDK_3BUTTON_PRESS)
                    {
                        return true;
                    }
                }
            }
            break;
        case GDK_2BUTTON_PRESS:
            event_type = dclick;
#ifndef __WXGTK3__
            if (gdk_event->button >= 1 && gdk_event->button <= 3)
            {
                // Reset GDK internal timestamp variables in order to disable GDK
                // triple click events. GDK will then next time believe no button has
                // been clicked just before, and send a normal button click event.
                GdkDisplay* display = gtk_widget_get_display(widget);
                display->button_click_time[1] = 0;
                display->button_click_time[0] = 0;
            }
#endif // !__WXGTK3__
            break;
        // we shouldn't get triple clicks at all for GTK2 because we
        // suppress them artificially using the code above but we still
        // should map them to something for GTK3 and not just ignore them
        // as this would lose clicks
        case GDK_3BUTTON_PRESS:
            event_type = down;
            break;
        default:
            return false;
    }

    SetLastMouseEvent setLastMouse(gdk_event);

    wxMouseEvent event( event_type );
    InitMouseEvent( win, event, gdk_event );
    event.m_synthesized = synthesized;

    AdjustEventButtonState(event);

    // find the correct window to send the event to: it may be a different one
    // from the one which got it at GTK+ level because some controls don't have
    // their own X window and thus cannot get any events.
    win = FindWindowForMouseEvent(win, event.m_x, event.m_y);

    // reset the event object and id in case win changed.
    event.SetEventObject( win );
    event.SetId( win->GetId() );

    if ( win->GTKProcessEvent( event ) )
        return TRUE;

    if ((event_type == wxEVT_LEFT_DOWN) && !win->IsOfStandardClass() &&
        (gs_currentFocus != win) && win->IsFocusable())
    {
        win->SetFocus();
    }

    if (event_type == wxEVT_RIGHT_DOWN)
    {
        // generate a "context menu" event: this is similar to right mouse
        // click under many GUIs except that it is generated differently
        // (right up under MSW, ctrl-click under Mac, right down here) and
        //
        // (a) it's a command event and so is propagated to the parent
        // (b) under some ports it can be generated from kbd too
        // (c) it uses screen coords (because of (a))
        const wxPoint pos = win->ClientToScreen(event.GetPosition());
        return win->WXSendContextMenuEvent(pos);
    }

    return FALSE;
}

extern "C"
{

static gboolean
gtk_window_button_press_callback( GtkWidget* widget,
                                  GdkEventButton *gdk_event,
                                  wxWindowGTK *win )
{
    return wxGTKImpl::WindowButtonPressCallback(widget, gdk_event, win);
}

} // extern "C"

//-----------------------------------------------------------------------------
// "button_release_event"
//-----------------------------------------------------------------------------

gboolean
wxGTKImpl::WindowButtonReleaseCallback(GtkWidget* WXUNUSED(widget),
                                       GdkEventButton* gdk_event,
                                       wxWindowGTK* win, bool synthesized)
{
    wxLogTrace(TRACE_MOUSE, "Release for button %d at %g,%g in %s at t=%u",
               gdk_event->button, gdk_event->x, gdk_event->y,
               wxDumpWindow(win),
               gdk_event->time);

    if (EventAlreadyProcessed(gdk_event))
        return FALSE;

    if ( AreGTKEventsBlocked() )
        return FALSE;

    g_lastButtonNumber = 0;

    wxEventType event_type = wxEVT_NULL;

    switch (gdk_event->button)
    {
        case 1:
            event_type = wxEVT_LEFT_UP;
            break;

        case 2:
            event_type = wxEVT_MIDDLE_UP;
            break;

        case 3:
            event_type = wxEVT_RIGHT_UP;
            break;

        case 8:
            event_type = wxEVT_AUX1_UP;
            break;

        case 9:
            event_type = wxEVT_AUX2_UP;
            break;

        default:
            // unknown button, don't process
            return FALSE;
    }

    SetLastMouseEvent setLastMouse(gdk_event);

    wxMouseEvent event( event_type );
    InitMouseEvent( win, event, gdk_event );
    event.m_synthesized = synthesized;

    AdjustEventButtonState(event);

    win = FindWindowForMouseEvent(win, event.m_x, event.m_y);

    // reset the event object and id in case win changed.
    event.SetEventObject( win );
    event.SetId( win->GetId() );

    if ( win->GTKProcessEvent(event) )
        return TRUE;

    return FALSE;
}

extern "C"
{

static gboolean
gtk_window_button_release_callback( GtkWidget *widget,
                                    GdkEventButton *gdk_event,
                                    wxWindowGTK *win )
{
    return wxGTKImpl::WindowButtonReleaseCallback(widget, gdk_event, win);
}

} // extern "C"
#endif // !__WXGTK4__

//-----------------------------------------------------------------------------

static void SendSetCursorEvent(wxWindowGTK* win, int x, int y)
{
    wxPoint posClient(x, y);
    const wxPoint posScreen = win->ClientToScreen(posClient);

    wxWindowGTK* w = win;
    for ( ;; )
    {
        wxSetCursorEvent event(posClient.x, posClient.y);
        event.SetId(win->GetId());
        event.SetEventObject(win);

        if (w->GTKProcessEvent(event))
        {
            win->GTKSetCursor(event.GetCursor());
            win->m_needCursorReset = true;
            return;
        }
        // this is how wxMSW works...
        if (w->GetCursor().IsOk())
            break;

        w = w->GetParent();
        if (w == nullptr || w->m_widget == nullptr || !gtk_widget_get_visible(w->m_widget))
            break;
        posClient = w->ScreenToClient(posScreen);
    }
    if (win->m_needCursorReset)
        win->GTKUpdateCursor();
}

//-----------------------------------------------------------------------------
// "motion_notify_event"
//-----------------------------------------------------------------------------

#ifndef __WXGTK4__
gboolean
wxGTKImpl::WindowMotionCallback(GtkWidget* WXUNUSED(widget),
                                       GdkEventMotion* gdk_event,
                                       wxWindowGTK* win, bool synthesized)
{
    if (EventAlreadyProcessed(gdk_event))
        return FALSE;

    if ( AreGTKEventsBlocked() )
        return FALSE;

    SetLastMouseEvent setLastMouse(gdk_event);

    wxMouseEvent event( wxEVT_MOTION );
    InitMouseEvent(win, event, gdk_event);
    event.m_synthesized = synthesized;

    if ( g_captureWindow )
    {
        // synthesise a mouse enter or leave event if needed
        GdkWindow* winUnderMouse = nullptr;
        bool isOut = true;

        if (gdk_event->x >= 0 && gdk_event->y >= 0)
        {
            const wxSize size(win->GetClientSize());
            if (gdk_event->x < size.x && gdk_event->y < size.y)
            {
                isOut = false;
                winUnderMouse =
                    wx_gdk_device_get_window_at_position(
                        gdk_event->device, nullptr, nullptr);
            }
        }

        const bool hadMouse = g_captureWindowHasMouse;
        g_captureWindowHasMouse = false;

        if (winUnderMouse == gdk_event->window)
            g_captureWindowHasMouse = true;
#ifdef __WXGTK3__
        else if (winUnderMouse)
        {
            // Avoid treating overlay scrollbar as a different window
            void* widgetUnderMouse;
            gdk_window_get_user_data(winUnderMouse, &widgetUnderMouse);
            if (GTK_IS_SCROLLBAR(widgetUnderMouse))
            {
                GtkWidget* parent = gtk_widget_get_parent(GTK_WIDGET(widgetUnderMouse));
                if (parent == win->m_widget && GTK_IS_SCROLLED_WINDOW(parent))
                    g_captureWindowHasMouse = true;
            }
        }
#endif

        if (g_captureWindowHasMouse != hadMouse)
        {
            // the mouse changed window
            wxMouseEvent eventM(g_captureWindowHasMouse ? wxEVT_ENTER_WINDOW
                                                        : wxEVT_LEAVE_WINDOW);
            if (!g_captureWindowHasMouse && isOut)
            {
                // Ensure fractional coordinate is outside window when converted to int
                if (gdk_event->x < 0)
                    gdk_event->x = floor(gdk_event->x);
                if (gdk_event->y < 0)
                    gdk_event->y = floor(gdk_event->y);
            }
            InitMouseEvent(win, eventM, gdk_event);
            eventM.SetEventObject(win);
            win->GTKProcessEvent(eventM);
        }
    }
    else // no capture
    {
        auto* const winUnderMouse =
            FindWindowForMouseEvent(win, event.m_x, event.m_y);

        // If our idea of the window under mouse is different from the actual
        // window under it, we need to send enter or leave events.
        bool setCursorEventAlreadySent = false;
        if ( winUnderMouse != g_windowUnderMouse )
        {
            SendEnterLeaveEvents(winUnderMouse, gdk_event);

            // This is done by SendEnterLeaveEvents() internally.
            setCursorEventAlreadySent = true;
        }

        // Also redirect the event to the window under mouse if it's different.
        if ( winUnderMouse != win )
        {
            win = winUnderMouse;

            event.SetEventObject( win );
            event.SetId( win->GetId() );
        }

        if ( !setCursorEventAlreadySent )
            SendSetCursorEvent(win, event.m_x, event.m_y);
    }

    bool ret = win->GTKProcessEvent(event);

    // Request additional motion events. Done at the end to increase the
    // chances that lower priority events requested by the handler above, such
    // as painting, can be processed before the next motion event occurs.
    // Otherwise a long-running handler can cause paint events to be entirely
    // blocked while the mouse is moving.
    if (gdk_event->is_hint)
    {
#ifdef __WXGTK3__
        gdk_event_request_motions(gdk_event);
#else
        gdk_window_get_pointer(gdk_event->window, nullptr, nullptr, nullptr);
#endif
    }

    return ret;
}

extern "C"
{

static gboolean
gtk_window_motion_notify_callback( GtkWidget * widget,
                                   GdkEventMotion *gdk_event,
                                   wxWindowGTK *win )
{
    return wxGTKImpl::WindowMotionCallback(widget, gdk_event, win);
}

} // extern "C"
#endif // !__WXGTK4__

//-----------------------------------------------------------------------------
// "scroll_event" (mouse wheel event)
//-----------------------------------------------------------------------------

static void AdjustRangeValue(wxGtkScrollbar* range, double step)
{
    if (gtk_widget_get_visible(GTK_WIDGET(range)))
    {
        GtkAdjustment* adj = wxGtkScrollbarGetAdjustment(range);
        double value = gtk_adjustment_get_value(adj);
        value += step * gtk_adjustment_get_step_increment(adj);
        wxGtkScrollbarSetValue(range, value);
    }
}

#if GTK_CHECK_VERSION(3,4,0)

// Handle a smooth (delta-based) scroll. GTK3 reaches this from the
// GDK_SCROLL_SMOOTH case of its direction switch; GTK4 has no direction enum
// on this path at all -- GtkEventControllerScroll reports everything as deltas
// -- so it is the only path there, which is why this is factored out rather
// than left inline. Neither caller exists before GTK+ 3.4, where
// GDK_SCROLL_SMOOTH was added, so building it there would just be an unused
// function.
static bool
wxGTKProcessScrollDeltas(wxWindow* win, wxMouseEvent& event,
                         wxGtkScrollbar* range_h, wxGtkScrollbar* range_v,
                         bool is_range_h, bool is_range_v,
                         double delta_x, double delta_y)
{
    // A wheel event landing on an embedded scrollbar scrolls along that
    // scrollbar's axis, whichever axis the wheel itself reported.
    if (delta_x == 0)
    {
        if (is_range_h)
        {
            delta_x = delta_y;
            delta_y = 0;
        }
    }
    else if (delta_y == 0)
    {
        if (is_range_v)
        {
            delta_y = delta_x;
            delta_x = 0;
        }
    }

    bool handled = false;
    if (delta_x != 0)
    {
        event.m_wheelAxis = wxMOUSE_WHEEL_HORIZONTAL;
        event.m_wheelRotation = int(event.m_wheelDelta * delta_x);
        handled = win->GTKProcessEvent(event);
        if (!handled && range_h)
        {
            AdjustRangeValue(range_h, event.m_columnsPerAction * delta_x);
            handled = true;
        }
    }
    if (delta_y != 0)
    {
        event.m_wheelAxis = wxMOUSE_WHEEL_VERTICAL;
        event.m_wheelRotation = int(event.m_wheelDelta * -delta_y);
        handled = win->GTKProcessEvent(event);
        if (!handled && range_v)
        {
            AdjustRangeValue(range_v, event.m_linesPerAction * delta_y);
            handled = true;
        }
    }
    return handled;
}

#endif // GTK_CHECK_VERSION(3,4,0)

extern "C"
{

#ifdef __WXGTK4__

// GtkEventControllerScroll::scroll(dx, dy) -> gboolean.
//
// The controller is created with BOTH_AXES, so this covers what GTK3 split
// between discrete direction values and GDK_SCROLL_SMOOTH: GTK4 reports
// discrete wheel clicks as deltas of +/-1 too.
static gboolean
wx_gtk_scroll_callback(GtkEventControllerScroll* controller,
                       double delta_x, double delta_y, wxWindow* win)
{
    GtkEventController* const c = GTK_EVENT_CONTROLLER(controller);
    GdkEvent* const gdk_event = gtk_event_controller_get_current_event(c);
    if (!gdk_event)
        return FALSE;

    GtkWidget* const widget = gtk_event_controller_get_widget(c);

    // Unlike the pointer controllers, the scroll signal carries no
    // coordinates, and neither does the event behind it, so GetEventPosition()
    // falls back to the pointer position. It is surface-relative and
    // InitMouseEvent() wants it widget-relative.
    double x = 0, y = 0;
    wxGTKImpl::GetEventPosition(gdk_event, widget, &x, &y);
    if (GtkNative* const native = gtk_widget_get_native(widget))
    {
        // Note GRAPHENE_POINT_INIT() can't be used inline here: it expands to
        // a compound literal, which is an rvalue in C++, so its address can't
        // be taken.
        graphene_point_t in;
        in.x = float(x);
        in.y = float(y);

        graphene_point_t out;
        if (gtk_widget_compute_point(GTK_WIDGET(native), widget, &in, &out))
        {
            x = out.x;
            y = out.y;
        }
    }

    wxMouseEvent event(wxEVT_MOUSEWHEEL);
    wxGTKImpl::InitMouseEvent(win, event, gdk_event, x, y);

    event.m_wheelDelta = 120;
    event.m_linesPerAction = 3;
    event.m_columnsPerAction = 3;

#if GTK_CHECK_VERSION(4,8,0)
    // GTK 4.8 started saying what unit the deltas are in, and the two are not
    // interchangeable: a wheel reports detents, where 1.0 is one click, but a
    // touchpad reports "surface logical pixels to scroll directly on screen",
    // where 1.0 is one pixel.
    //
    // wxMouseEvent's rotation is in detents scaled by m_wheelDelta, so passing
    // a pixel delta through unconverted asks for as many detents as the finger
    // moved pixels -- a modest two-finger swipe of 50 px becomes 50 clicks.
    // That is what makes touchpad scrolling feel several times too fast.
    //
    // One detent scrolls m_linesPerAction lines vertically and
    // m_columnsPerAction columns horizontally, so that much text is what a
    // detent is worth in pixels on each axis.
    if ( gtk_check_version(4,8,0) == nullptr &&
            gdk_scroll_event_get_unit(gdk_event) == GDK_SCROLL_UNIT_SURFACE )
    {
        // A zero char size would be a degenerate font: leave the delta alone
        // rather than divide by zero.
        if ( const int charWidth = win->GetCharWidth() )
            delta_x /= double(event.m_columnsPerAction) * charWidth;

        if ( const int charHeight = win->GetCharHeight() )
            delta_y /= double(event.m_linesPerAction) * charHeight;
    }
#endif // GTK_CHECK_VERSION(4,8,0)

    wxGtkScrollbar* const range_h = win->m_scrollBar[wxWindow::ScrollDir_Horz];
    wxGtkScrollbar* const range_v = win->m_scrollBar[wxWindow::ScrollDir_Vert];

    return wxGTKProcessScrollDeltas(win, event, range_h, range_v,
                                    (void*)widget == range_h,
                                    (void*)widget == range_v,
                                    delta_x, delta_y) ? TRUE : FALSE;
}

#else // !__WXGTK4__

static gboolean
scroll_event(GtkWidget* widget, GdkEventScroll* gdk_event, wxWindow* win)
{
    wxMouseEvent event(wxEVT_MOUSEWHEEL);
    InitMouseEvent(win, event, gdk_event);

    event.m_wheelDelta = 120;
    event.m_linesPerAction = 3;
    event.m_columnsPerAction = 3;

    wxGtkScrollbar* range_h = win->m_scrollBar[wxWindow::ScrollDir_Horz];
    wxGtkScrollbar* range_v = win->m_scrollBar[wxWindow::ScrollDir_Vert];
    const bool is_range_h = (void*)widget == range_h;
    const bool is_range_v = (void*)widget == range_v;
    GdkScrollDirection direction = gdk_event->direction;
    switch (direction)
    {
        case GDK_SCROLL_UP:
            if (is_range_h)
                direction = GDK_SCROLL_LEFT;
            break;
        case GDK_SCROLL_DOWN:
            if (is_range_h)
                direction = GDK_SCROLL_RIGHT;
            break;
        case GDK_SCROLL_LEFT:
            if (is_range_v)
                direction = GDK_SCROLL_UP;
            break;
        case GDK_SCROLL_RIGHT:
            if (is_range_v)
                direction = GDK_SCROLL_DOWN;
            break;
        default:
            break;
#if GTK_CHECK_VERSION(3,4,0)
        case GDK_SCROLL_SMOOTH:
            return wxGTKProcessScrollDeltas(win, event, range_h, range_v,
                                            is_range_h, is_range_v,
                                            gdk_event->delta_x,
                                            gdk_event->delta_y);
#endif // GTK_CHECK_VERSION(3,4,0)
    }
    GtkRange *range;
    double step;
    switch (direction)
    {
        case GDK_SCROLL_UP:
        case GDK_SCROLL_DOWN:
            range = range_v;
            event.m_wheelAxis = wxMOUSE_WHEEL_VERTICAL;
            step = event.m_linesPerAction;
            break;
        case GDK_SCROLL_LEFT:
        case GDK_SCROLL_RIGHT:
            range = range_h;
            event.m_wheelAxis = wxMOUSE_WHEEL_HORIZONTAL;
            step = event.m_columnsPerAction;
            break;
        default:
            return false;
    }

    event.m_wheelRotation = event.m_wheelDelta;
    if (direction == GDK_SCROLL_DOWN || direction == GDK_SCROLL_LEFT)
        event.m_wheelRotation = -event.m_wheelRotation;

    if (!win->GTKProcessEvent(event))
    {
        if (!range)
            return false;

        if (direction == GDK_SCROLL_UP || direction == GDK_SCROLL_LEFT)
            step = -step;
        AdjustRangeValue(range, step);
    }

    return true;
}

#endif // __WXGTK4__/!__WXGTK4__

//-----------------------------------------------------------------------------
// "popup-menu"
//-----------------------------------------------------------------------------

static gboolean wxgtk_window_popup_menu_callback(GtkWidget*, wxWindowGTK* win)
{
    wxContextMenuEvent event(wxEVT_CONTEXT_MENU, win->GetId(), wxPoint(-1, -1));
    event.SetEventObject(win);
    return win->GTKProcessEvent(event);
}

#ifdef __WXGTK4__
// GtkWidget::popup-menu is gone: GTK4 widgets which have a context menu expose
// it as a "menu.popup" action instead, and there is no signal for a widget
// which doesn't. What the signal actually reported was the two key bindings
// that emitted it, so those are watched for directly.
static gboolean
wxgtk_window_context_menu_key(GtkEventControllerKey* controller,
                              guint keyval,
                              guint WXUNUSED(keycode),
                              GdkModifierType state,
                              wxWindowGTK* win)
{
    const bool isMenuKey = keyval == GDK_KEY_Menu;
    const bool isShiftF10 = keyval == GDK_KEY_F10 && (state & GDK_SHIFT_MASK);

    if ( !isMenuKey && !isShiftF10 )
        return FALSE;

    return wxgtk_window_popup_menu_callback(
        gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)), win);
}
#endif // __WXGTK4__

//-----------------------------------------------------------------------------
// "focus_in_event"
//-----------------------------------------------------------------------------

#ifdef __WXGTK4__

// Set on the widget each wxWindow watches the focus on, pointing back at it.
static const char* const WX_FOCUS_OWNER = "wx-focus-owner";

// Set on a GtkWindow once wx_root_focus_changed() is connected to it.
static const char* const WX_FOCUS_WATCHED = "wx-focus-watched";

// The wxWindow the given focus widget belongs to: the innermost marked widget
// at or above it, since GTK usually focuses a widget one of wx's controls is
// built from rather than the control itself -- the GtkText inside a GtkEntry
// being the usual one.
static wxWindowGTK* wxGTKFocusOwner(GtkWidget* focusWidget)
{
    for ( GtkWidget* w = focusWidget; w != nullptr;
          w = gtk_widget_get_parent(w) )
    {
        if ( gpointer const owner = g_object_get_data(G_OBJECT(w),
                                                      WX_FOCUS_OWNER) )
            return static_cast<wxWindowGTK*>(owner);
    }

    return nullptr;
}

// Send whatever wx events the difference between what GTK now says and what wx
// last reported calls for. Doing it from one place per toplevel, rather than
// from each widget's own controller, is what makes it possible to tell "the
// focus is in this window" from "the focus is in a control inside it": see
// wx_window_focus_in() below.
static void wxGTKResolveFocus(GtkWindow* toplevel)
{
    wxWindowGTK* const owner =
        wxGTKFocusOwner(gtk_window_get_focus(toplevel));

    if ( owner == gs_currentFocus )
        return;

    if ( gs_currentFocus )
        gs_currentFocus->GTKHandleFocusOut();

    if ( owner )
        owner->GTKHandleFocusIn();
}

static void
wx_root_focus_changed( GObject* toplevel, GParamSpec*, gpointer )
{
    wxGTKResolveFocus(GTK_WINDOW(toplevel));
}

static void wxGTKWatchRootFocus(GtkWidget* widget)
{
    GtkRoot* const root = gtk_widget_get_root(widget);
    if ( !GTK_IS_WINDOW(root) )
        return;

    if ( g_object_get_data(G_OBJECT(root), WX_FOCUS_WATCHED) )
        return;

    g_object_set_data(G_OBJECT(root), WX_FOCUS_WATCHED, GINT_TO_POINTER(1));
    g_signal_connect(root, "notify::focus-widget",
                     G_CALLBACK(wx_root_focus_changed), nullptr);
}

static void
wx_window_focus_in( GtkEventControllerFocus* controller, wxWindowGTK* )
{
    // GTK3 sent focus-in-event to the widget that took the focus. GTK4's
    // controller instead reports the focus entering the widget *or any of its
    // descendants*, so this runs for every container above the control that
    // was really focused -- and a wxPanel would report a wxEVT_SET_FOCUS of
    // its own, which GTK3 never sent and which then left gs_currentFocus
    // pointing at the control when the panel's own leave arrived.
    //
    // The notification cannot be filtered where it arrives, because none of it
    // is settled yet: at ::enter, gtk_event_controller_focus_is_focus() is
    // FALSE, gtk_widget_has_focus() is FALSE, GTK_STATE_FLAG_FOCUSED is unset,
    // the gtk_widget_get_focus_child() chain is still empty and
    // gtk_window_get_focus() is null. All of it is in place one step later,
    // when the toplevel notifies its focus-widget property, so that is where
    // wx decides. Connecting here is in time for the very change that led
    // here: ::enter runs first and in the same operation.
    GtkWidget* const widget =
        gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));

    wxGTKWatchRootFocus(widget);

    // With one exception. A window regaining the focus does not change which
    // widget in it is focused, so it notifies nothing -- and there GTK does
    // already know where the focus is, which is exactly what tells the two
    // apart.
    GtkRoot* const root = gtk_widget_get_root(widget);
    if ( GTK_IS_WINDOW(root) && gtk_window_get_focus(GTK_WINDOW(root)) )
        wxGTKResolveFocus(GTK_WINDOW(root));
}

static void
wx_window_focus_out( GtkEventControllerFocus* controller, wxWindowGTK *win )
{
    wxGTKWatchRootFocus(
        gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)));

    // Every container above the focused control is told the focus left, too.
    // Only the window wx has something recorded about has anything to say
    // about it -- and unlike the focus arriving, this does not have to wait
    // for the toplevel to notify: GTK keeps its focus widget when a window is
    // merely deactivated, so this is the only notice wx gets of that.
    if ( win != gs_currentFocus &&
            win != gs_focusDeclined && win != gs_pendingFocus )
        return;

    win->GTKHandleFocusOut();
}
#else
static gboolean
gtk_window_focus_in_callback( GtkWidget * WXUNUSED(widget),
                              GdkEventFocus *WXUNUSED(event),
                              wxWindowGTK *win )
{
    return win->GTKHandleFocusIn();
}
#endif

//-----------------------------------------------------------------------------
// "focus_out_event"
//-----------------------------------------------------------------------------

#ifndef __WXGTK4__
static gboolean
gtk_window_focus_out_callback( GtkWidget * WXUNUSED(widget),
                               GdkEventFocus * WXUNUSED(gdk_event),
                               wxWindowGTK *win )
{
    return win->GTKHandleFocusOut();
}
#endif

//-----------------------------------------------------------------------------
// "focus"
//-----------------------------------------------------------------------------

// GTK4 turned this signal into a plain vfunc; see the connection site.
#ifndef __WXGTK4__
static gboolean
wx_window_focus_callback(GtkWidget *widget,
                         GtkDirectionType WXUNUSED(direction),
                         wxWindowGTK *win)
{
    // the default handler for focus signal in GtkScrolledWindow sets
    // focus to the window itself even if it doesn't accept focus, i.e. has no
    // GTK_CAN_FOCUS in its style -- work around this by forcibly preventing
    // the signal from reaching gtk_scrolled_window_focus() if we don't have
    // any children which might accept focus (we know we don't accept the focus
    // ourselves as this signal is only connected in this case)
    if ( win->GetChildren().empty() )
        g_signal_stop_emission_by_name(widget, "focus");

    // we didn't change the focus
    return FALSE;
}
#endif // !__WXGTK4__

} // extern "C"

//-----------------------------------------------------------------------------
// "enter_notify_event"
//-----------------------------------------------------------------------------

namespace wxGTKImpl
{

// Helper function used by both "enter" and "motion" signal handlers.
template <typename EventType>
gboolean SendEnterLeaveEvents(wxWindowGTK* win, EventType* gdk_event)
{
    if ( g_windowUnderMouse )
    {
        // We must not have got the leave event for the previous window, so
        // generate it now -- better late than never.
        wxMouseEvent event( wxEVT_LEAVE_WINDOW );
        InitMouseEvent(g_windowUnderMouse, event, gdk_event);

        (void)g_windowUnderMouse->GTKProcessEvent(event);
    }

    g_windowUnderMouse = win;

    wxMouseEvent event( wxEVT_ENTER_WINDOW );
    InitMouseEvent(win, event, gdk_event);

    if ( !g_captureWindow )
        SendSetCursorEvent(win, event.m_x, event.m_y);

    return win->GTKProcessEvent(event) ? TRUE : FALSE;
}

} // namespace wxGTKImpl

#ifndef __WXGTK4__
// This is a (internally) public function used by wxChoice too.
gboolean
wxGTKImpl::WindowEnterCallback(GtkWidget* WXUNUSED_UNLESS_DEBUG(widget),
                               GdkEventCrossing* gdk_event,
                               wxWindowGTK* win)
{
    wxLogTrace(TRACE_MOUSE, "Window enter in %s (window %p) for window %p",
               wxDumpWindow(win), gtk_widget_get_window(widget), gdk_event->window);

    if ( AreGTKEventsBlocked() )
        return FALSE;

    // Event was emitted after a grab
    if (gdk_event->mode != GDK_CROSSING_NORMAL)
    {
        wxLogTrace(TRACE_MOUSE, "Ignore enter event mode=%d", gdk_event->mode);
        return FALSE;
    }

    if ( g_windowUnderMouse == win )
    {
        // This can happen if the enter event was generated from another
        // callback, as is the case for wxSearchCtrl, for example.
        wxLogTrace(TRACE_MOUSE, "Reentering window %s", wxDumpWindow(win));
        return FALSE;
    }

    return SendEnterLeaveEvents(win, gdk_event);
}

extern "C" {

static gboolean
gtk_window_enter_callback( GtkWidget* widget,
                           GdkEventCrossing *gdk_event,
                           wxWindowGTK *win )
{
    return wxGTKImpl::WindowEnterCallback(widget, gdk_event, win);
}

} // extern "C"
#endif // !__WXGTK4__

//-----------------------------------------------------------------------------
// "leave_notify_event"
//-----------------------------------------------------------------------------

#ifndef __WXGTK4__
gboolean
wxGTKImpl::WindowLeaveCallback(GtkWidget* WXUNUSED_UNLESS_DEBUG(widget),
                               GdkEventCrossing* gdk_event,
                               wxWindowGTK* win)
{
    wxLogTrace(TRACE_MOUSE, "Window leave in %s (window %p) for window %p",
               wxDumpWindow(win), gtk_widget_get_window(widget), gdk_event->window);

    if ( AreGTKEventsBlocked() )
        return FALSE;

    if (win->m_needCursorReset)
        win->GTKUpdateCursor();

    // Event was emitted after an ungrab
    if (gdk_event->mode != GDK_CROSSING_NORMAL)
    {
        wxLogTrace(TRACE_MOUSE, "Ignore leave event mode=%d", gdk_event->mode);
        return FALSE;
    }

    if ( win == g_windowUnderMouse )
        g_windowUnderMouse = nullptr;

    wxMouseEvent event( wxEVT_LEAVE_WINDOW );
    InitMouseEvent(win, event, gdk_event);

    return win->GTKProcessEvent(event);
}
#endif // !__WXGTK4__

#ifdef __WXGTK4__

// Defined further down, next to the button state it reads: are any mouse
// buttons currently held? Declared with the same linkage as the definition,
// which sits inside the extern "C" block with the button handlers.
extern "C" {
static bool wxGTKAnyButtonDown();
}

namespace wxGTKImpl
{

// Send the leave event for the previously-entered window, if any, then the
// enter event for this one. GTK4 counterpart of the GdkEventCrossing-templated
// version above; the coordinates come from the controller rather than from the
// event, which is opaque.
static bool SendEnterLeaveEvents(wxWindowGTK* win, GdkEvent* gdk_event,
                                 double x, double y)
{
    // While a mouse button is held the pointer belongs to whatever it was
    // pressed on, and crossing a window boundary is not something the
    // application should hear about: dragging past a control must not look
    // like entering it. X11 enforced that by delivering crossing events only
    // to the window holding the implicit grab, and GTK3 dropped the rest by
    // testing GdkEventCrossing::mode. GTK4 has neither -- the controllers fire
    // as usual and the event they carry is null here -- so the button state,
    // which is tracked for wxGetMouseState() anyway, stands in for it.
    //
    // The bookkeeping below is still updated, so that releasing the button
    // over a different window does not then produce a leave for a window the
    // pointer left long ago.
    const bool quiet = wxGTKAnyButtonDown();

    if ( g_windowUnderMouse && !quiet )
    {
        // We must not have got the leave event for the previous window, so
        // generate it now -- better late than never.
        wxMouseEvent event( wxEVT_LEAVE_WINDOW );
        InitMouseEvent(g_windowUnderMouse, event, gdk_event, x, y);

        (void)g_windowUnderMouse->GTKProcessEvent(event);
    }

    g_windowUnderMouse = win;

    if ( quiet )
        return false;

    wxMouseEvent event( wxEVT_ENTER_WINDOW );
    InitMouseEvent(win, event, gdk_event, x, y);

    if ( !g_captureWindow )
        SendSetCursorEvent(win, event.m_x, event.m_y);

    return win->GTKProcessEvent(event);
}

// Is a child of this window, which has a client area of its own and so gets
// its own motion events, under the pointer at (x, y) in win's coordinates?
//
// This exists only under GTK4. GTK3 gave every wxWindow with a client area its
// own GdkWindow, so a motion over a child never reached the parent at all.
// GTK4 has no per-widget windows: the same motion is delivered to the event
// controller of every widget on the way up, so the parent sees motions which
// happen over its children.
static wxWindowGTK* ChildWithOwnWindowUnder(wxWindowGTK* win, double x, double y)
{
    if ( !win->m_wxwindow )
        return nullptr;

    wxCoord xx = wxRound(x);
    wxCoord yy = wxRound(y);

    wxPizza* const pizza = WX_PIZZA(win->m_wxwindow);
    xx += pizza->m_scroll_x;
    yy += pizza->m_scroll_y;

    for ( wxWindowList::compatibility_iterator node = win->GetChildren().GetFirst();
          node;
          node = node->GetNext() )
    {
        wxWindow* const child = static_cast<wxWindow*>(node->GetData());

        // Only children with a client area of their own are relevant: those
        // are exactly the ones whose own controller will be called as well.
        // A native control without one is found by FindWindowForMouseEvent()
        // instead, which is how it worked under GTK3 too.
        if ( !child->m_wxwindow || !child->IsShown() )
            continue;

        if ( !win->IsClientAreaChild(child) )
            continue;

        if ( xx >= child->m_x && xx < child->m_x + child->m_width &&
             yy >= child->m_y && yy < child->m_y + child->m_height )
        {
            return child;
        }
    }

    return nullptr;
}

// Is the pointer, at (x, y) in widget coordinates, actually over this widget?
//
// GTK3 answered this by asking GDK which GdkWindow was under the pointer and
// comparing it with the one the event was delivered for. GTK4 has no per-widget
// windows, so the equivalent question is which *widget* is picked at that
// point, and whether it is this widget or something inside it.
static bool PointerIsOverWidget(wxWindowGTK* win, GtkWidget* widget,
                                double x, double y)
{
    GtkNative* const native = gtk_widget_get_native(widget);
    if ( !native )
        return false;

    graphene_point_t in;
    in.x = float(x);
    in.y = float(y);

    graphene_point_t out;
    if ( !gtk_widget_compute_point(widget, GTK_WIDGET(native), &in, &out) )
        return false;

    GtkWidget* const picked =
        gtk_widget_pick(GTK_WIDGET(native), out.x, out.y, GTK_PICK_DEFAULT);

    for ( GtkWidget* w = picked; w; w = gtk_widget_get_parent(w) )
    {
        if ( w == widget )
            return true;

        // Don't treat an overlay scrollbar belonging to this window as a
        // different window: same special case the GTK3 code made, expressed
        // in terms of widgets rather than GdkWindows.
        if ( GTK_IS_SCROLLBAR(w) )
        {
            GtkWidget* const parent = gtk_widget_get_parent(w);
            if ( parent == win->m_widget && GTK_IS_SCROLLED_WINDOW(parent) )
                return true;
        }
    }

    return false;
}

} // namespace wxGTKImpl

bool
wxGTKImpl::WindowEnterCallback(wxWindowGTK* win, GdkEvent* gdk_event,
                               double x, double y)
{
    if ( AreGTKEventsBlocked() )
        return false;

    // GTK3 ignored crossing events whose mode wasn't GDK_CROSSING_NORMAL, i.e.
    // those synthesised by a grab being taken or released. GTK4 doesn't
    // deliver those to a motion controller at all unless it was created with
    // GTK_EVENT_CONTROLLER_SCOPE_CAPTURE, so there is nothing to filter here.

    if ( g_windowUnderMouse == win )
    {
        // This can happen if the enter event was generated from another
        // callback, as is the case for wxSearchCtrl, for example.
        wxLogTrace(TRACE_MOUSE, "Reentering window %s", wxDumpWindow(win));
        return false;
    }

    return SendEnterLeaveEvents(win, gdk_event, x, y);
}

bool
wxGTKImpl::WindowLeaveCallback(wxWindowGTK* win, GdkEvent* gdk_event)
{
    if ( AreGTKEventsBlocked() )
        return false;

    if (win->m_needCursorReset)
        win->GTKUpdateCursor();

    if ( win == g_windowUnderMouse )
        g_windowUnderMouse = nullptr;

    // Same rule as in SendEnterLeaveEvents(): no crossing reaches the
    // application while a button is held.
    if ( wxGTKAnyButtonDown() )
        return false;

    // GtkEventControllerMotion::leave carries no coordinates, unlike GTK3's
    // GdkEventCrossing. Recover them from the event where possible so the
    // wxMouseEvent still reports where the pointer left; GetEventPosition()
    // falls back to the origin if the event doesn't have a position either.
    double x = 0, y = 0;
    GetEventPosition(gdk_event, win->m_wxwindow ? win->m_wxwindow : win->m_widget,
                     &x, &y);

    wxMouseEvent event( wxEVT_LEAVE_WINDOW );
    InitMouseEvent(win, event, gdk_event, x, y);

    return win->GTKProcessEvent(event);
}

bool
wxGTKImpl::WindowMotionCallback(wxWindowGTK* win, GdkEvent* gdk_event,
                                double x, double y, bool synthesized)
{
    // No EventAlreadyProcessed() check: see the comment on the key-pressed
    // callback. A controller only fires for the widget it is attached to, so
    // the same native event is not seen by several wxWindows.

    if ( AreGTKEventsBlocked() )
        return false;

    // The same motion is delivered to every ancestor's controller under GTK4,
    // and each delivery goes on to decide which window the pointer is in. A
    // parent would answer "me", the child would answer "me" on its own
    // delivery, and the two would send each other a leave and themselves an
    // enter on every single motion event -- which is what made the panel in
    // EnterLeaveEvents count two enters and two leaves for one move.
    //
    // Let the innermost window deal with it. The child has a controller of its
    // own and is called too, so nothing is lost by dropping the redundant
    // delivery here. A window holding the capture is exempt: it is supposed to
    // get everything, wherever the pointer is.
    if ( !g_captureWindow && ChildWithOwnWindowUnder(win, x, y) )
        return false;

    SetLastMouseEvent setLastMouse(gdk_event);

    wxMouseEvent event( wxEVT_MOTION );
    InitMouseEvent(win, event, gdk_event, x, y);
    event.m_synthesized = synthesized;

    if ( g_captureWindow )
    {
        // Synthesise a mouse enter or leave event if needed.
        bool isOut = true;
        bool hasMouse = false;

        if ( x >= 0 && y >= 0 )
        {
            const wxSize size(win->GetClientSize());
            if ( x < size.x && y < size.y )
            {
                isOut = false;
                hasMouse = PointerIsOverWidget(win, win->GetConnectWidget(), x, y);
            }
        }

        const bool hadMouse = g_captureWindowHasMouse;
        g_captureWindowHasMouse = hasMouse;

        if (g_captureWindowHasMouse != hadMouse)
        {
            // The mouse changed window.
            wxMouseEvent eventM(g_captureWindowHasMouse ? wxEVT_ENTER_WINDOW
                                                        : wxEVT_LEAVE_WINDOW);

            // Ensure a fractional coordinate stays outside the window when
            // converted to int.
            double mx = x, my = y;
            if (!g_captureWindowHasMouse && isOut)
            {
                if (mx < 0)
                    mx = floor(mx);
                if (my < 0)
                    my = floor(my);
            }

            InitMouseEvent(win, eventM, gdk_event, mx, my);
            eventM.SetEventObject(win);
            win->GTKProcessEvent(eventM);
        }
    }
    else // no capture
    {
        auto* const winUnderMouse =
            FindWindowForMouseEvent(win, event.m_x, event.m_y);

        // If our idea of the window under mouse is different from the actual
        // window under it, we need to send enter or leave events.
        bool setCursorEventAlreadySent = false;
        if ( winUnderMouse != g_windowUnderMouse )
        {
            SendEnterLeaveEvents(winUnderMouse, gdk_event, x, y);
            setCursorEventAlreadySent = true;
        }

        // Also redirect the event to the window under mouse if it's different.
        if ( winUnderMouse != win )
        {
            win = winUnderMouse;

            event.SetEventObject( win );
            event.SetId( win->GetId() );
        }

        if ( !setCursorEventAlreadySent )
            SendSetCursorEvent(win, event.m_x, event.m_y);
    }

    // GTK3 ended by re-requesting motion events for hint-mode pointers
    // (gdk_event_request_motions()). GTK4 has no motion hints -- it compresses
    // motion events internally -- so there is nothing to do here.

    return win->GTKProcessEvent(event);
}

bool
wxGTKImpl::WindowButtonPressCallback(wxWindowGTK* win, GdkEvent* gdk_event,
                                     int button, int nPress,
                                     double x, double y, bool synthesized)
{
    wxLogTrace(TRACE_MOUSE, "Press %d for button %d at %g,%g in %s",
               nPress, button, x, y, wxDumpWindow(win));

    if ( AreGTKEventsBlocked() )
        return false;

    g_lastButtonNumber = button;

    wxEventType down;
    wxEventType dclick;
    switch (button)
    {
        case 1: down = wxEVT_LEFT_DOWN;   dclick = wxEVT_LEFT_DCLICK;   break;
        case 2: down = wxEVT_MIDDLE_DOWN; dclick = wxEVT_MIDDLE_DCLICK; break;
        case 3: down = wxEVT_RIGHT_DOWN;  dclick = wxEVT_RIGHT_DCLICK;  break;
        case 8: down = wxEVT_AUX1_DOWN;   dclick = wxEVT_AUX1_DCLICK;   break;
        case 9: down = wxEVT_AUX2_DOWN;   dclick = wxEVT_AUX2_DCLICK;   break;
        default:
            return false;
    }

    // GtkGestureClick reports the click count directly, which removes the
    // whole GDK_2BUTTON_PRESS/GDK_3BUTTON_PRESS dance the GTK3 code needed --
    // including its gdk_event_peek() lookahead to suppress the surplus single
    // press GDK sent before a double click. Triple clicks map to a plain down
    // event, as they did under GTK3.
    const wxEventType event_type = nPress == 2 ? dclick : down;

    // wxDropSource::DoDragDrop() refuses to start a drag unless a mouse event
    // is being handled, so a handler that starts one must find it recorded.
    SetLastMouseEvent setLastMouse(gdk_event);

    wxMouseEvent event( event_type );
    InitMouseEvent( win, event, gdk_event, x, y );
    event.m_synthesized = synthesized;

    AdjustEventButtonState(event);

    // Find the correct window to send the event to: it may be a different one
    // from the one which got it at GTK level.
    win = FindWindowForMouseEvent(win, event.m_x, event.m_y);

    event.SetEventObject( win );
    event.SetId( win->GetId() );

    if ( win->GTKProcessEvent( event ) )
        return true;

    if ((event_type == wxEVT_LEFT_DOWN) && !win->IsOfStandardClass() &&
        (gs_currentFocus != win) && win->IsFocusable())
    {
        win->SetFocus();
    }

    if (event_type == wxEVT_RIGHT_DOWN)
    {
        // Generate a "context menu" event.
        const wxPoint pos = win->ClientToScreen(event.GetPosition());
        return win->WXSendContextMenuEvent(pos);
    }

    return false;
}

bool
wxGTKImpl::WindowButtonReleaseCallback(wxWindowGTK* win, GdkEvent* gdk_event,
                                       int button, double x, double y,
                                       bool synthesized)
{
    wxLogTrace(TRACE_MOUSE, "Release for button %d at %g,%g in %s",
               button, x, y, wxDumpWindow(win));

    if ( AreGTKEventsBlocked() )
        return false;

    g_lastButtonNumber = 0;

    SetLastMouseEvent setLastMouse(gdk_event);

    wxEventType event_type;
    switch (button)
    {
        case 1: event_type = wxEVT_LEFT_UP;   break;
        case 2: event_type = wxEVT_MIDDLE_UP; break;
        case 3: event_type = wxEVT_RIGHT_UP;  break;
        case 8: event_type = wxEVT_AUX1_UP;   break;
        case 9: event_type = wxEVT_AUX2_UP;   break;
        default:
            // unknown button, don't process
            return false;
    }

    wxMouseEvent event( event_type );
    InitMouseEvent( win, event, gdk_event, x, y );
    event.m_synthesized = synthesized;

    AdjustEventButtonState(event);

    win = FindWindowForMouseEvent(win, event.m_x, event.m_y);

    event.SetEventObject( win );
    event.SetId( win->GetId() );

    return win->GTKProcessEvent(event);
}

// GTK3's EventAlreadyProcessed() guard is still needed for the pointer, even
// though it genuinely is not for the keyboard.
//
// A GtkEventControllerKey lives on the focus widget and fires only for it, so
// the key handlers below rightly have no such check. A GtkGestureClick in the
// default BUBBLE phase is different: when nobody claims the sequence -- which
// is exactly what happens when wx does not handle the press -- the same press
// is offered to every ancestor in turn, each with its own gesture. wx's
// callback then re-targets the event with FindWindowForMouseEvent(), so one
// physical click reached the same wxWindow once per ancestor.
//
// Two wxEVT_LEFT_DOWN for one press breaks anything that toggles state on a
// click. It is why wxAuiManager could not start a pane drag: the second down
// arrived while the first had already begun one.
//
// A reference is held on the remembered event so that the next one cannot be
// allocated at the same address and be mistaken for it.
namespace
{

GdkEvent* gs_lastButtonEvent = nullptr;

bool ButtonEventAlreadyProcessed(GdkEvent* gdk_event)
{
    if ( !gdk_event )
        return false;

    if ( gdk_event == gs_lastButtonEvent )
        return true;

    if ( gs_lastButtonEvent )
        gdk_event_unref(gs_lastButtonEvent);

    gs_lastButtonEvent = gdk_event_ref(gdk_event);

    return false;
}

} // anonymous namespace

extern "C" {

// GtkGestureClick::pressed(n_press, x, y).
//
// Whether to claim the sequence is the crux of this port. Measured behaviour
// (docs/gtk/probes/gtk4-gesture-semantics.c), clicking a GtkButton that has
// its own gesture:
//
//   phase    claim   wx press   wx release   native control still acts
//   BUBBLE   no      yes        NO           yes
//   BUBBLE   yes     yes        yes          no
//   CAPTURE  no      yes        yes          yes
//
// So claiming exactly reproduces GTK3's "handler returned TRUE" (wx consumes
// the click, the native control does not act), and not claiming reproduces
// "returned FALSE". Hence: claim if and only if wx handled the press.
//
// The cost is the BUBBLE/no-claim row: on a widget that has its own gesture,
// an unhandled press means the native gesture claims the sequence and ours is
// cancelled, so no release is delivered -- GTK3 delivered one regardless. This
// only affects native controls; on ordinary wx windows (wxPizza) nothing
// competes for the sequence and both events always arrive, which is why
// CAPTURE is not used instead: it would fix the release at the cost of wx no
// longer being able to stop a native control acting at all, which is worse.
// GTK4 dropped the query that used to answer "which mouse buttons are down
// right now": gdk_device_get_modifier_state() covers the keyboard only, so
// wxGetMouseState() would report every button as up, always. Events do still
// carry the button mask, so it is remembered from them as they arrive.
//
// Motion keeps it honest -- a press or release that wx never saw, because a
// native widget took it, is corrected by the next movement -- while the press
// and release handlers make it right immediately, without waiting for one.
static GdkModifierType gs_buttonState = GdkModifierType(0);

static constexpr GdkModifierType wxGTK_ALL_BUTTONS_MASK =
    GdkModifierType(GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK |
                    GDK_BUTTON4_MASK | GDK_BUTTON5_MASK);

static bool wxGTKAnyButtonDown()
{
    return (gs_buttonState & wxGTK_ALL_BUTTONS_MASK) != 0;
}

static GdkModifierType wxGTKButtonMaskFor(int button)
{
    switch ( button )
    {
        case 1: return GDK_BUTTON1_MASK;
        case 2: return GDK_BUTTON2_MASK;
        case 3: return GDK_BUTTON3_MASK;
        case 4: return GDK_BUTTON4_MASK;
        case 5: return GDK_BUTTON5_MASK;
    }

    return GdkModifierType(0);
}

// Take the buttons from an event which reports them, i.e. any pointer event.
static void wxGTKRefreshButtonState(GdkEvent* gdk_event)
{
    if ( !gdk_event )
        return;

    gs_buttonState = GdkModifierType(
        gdk_event_get_modifier_state(gdk_event) & wxGTK_ALL_BUTTONS_MASK);
}

static void
wx_gtk_button_pressed_callback(GtkGestureClick* gesture,
                               int nPress, double x, double y,
                               wxWindowGTK* win)
{
    GtkEventController* const c = GTK_EVENT_CONTROLLER(gesture);

    GdkEvent* const gdk_event = gtk_event_controller_get_current_event(c);

    const int button = int(gtk_gesture_single_get_current_button(
                                GTK_GESTURE_SINGLE(gesture)));

    // Before the duplicate check below can return: a second delivery of the
    // same press is still a press, and the state has to hold either way.
    gs_buttonState =
        GdkModifierType(gs_buttonState | wxGTKButtonMaskFor(button));

    if ( ButtonEventAlreadyProcessed(gdk_event) )
        return;

    if ( wxGTKImpl::WindowButtonPressCallback(
                win, gdk_event,
                button, nPress, x, y) )
    {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    }
}

static void
wx_gtk_button_released_callback(GtkGestureClick* gesture,
                                int WXUNUSED(nPress), double x, double y,
                                wxWindowGTK* win)
{
    GtkEventController* const c = GTK_EVENT_CONTROLLER(gesture);

    GdkEvent* const gdk_event = gtk_event_controller_get_current_event(c);

    const int button = int(gtk_gesture_single_get_current_button(
                                GTK_GESTURE_SINGLE(gesture)));

    gs_buttonState =
        GdkModifierType(gs_buttonState & ~wxGTKButtonMaskFor(button));

    if ( ButtonEventAlreadyProcessed(gdk_event) )
        return;

    if ( wxGTKImpl::WindowButtonReleaseCallback(
                win, gdk_event,
                button, x, y) )
    {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    }
}

static void
wx_gtk_motion_callback(GtkEventControllerMotion* controller,
                       double x, double y, wxWindowGTK* win)
{
    GdkEvent* const gdk_event =
        gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(controller));

    wxGTKRefreshButtonState(gdk_event);

    wxGTKImpl::WindowMotionCallback(win, gdk_event, x, y);
}

static void
wx_gtk_enter_callback(GtkEventControllerMotion* controller,
                      double x, double y, wxWindowGTK* win)
{
    wxGTKImpl::WindowEnterCallback(
        win,
        gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(controller)),
        x, y);
}

static void
wx_gtk_leave_callback(GtkEventControllerMotion* controller, wxWindowGTK* win)
{
    wxGTKImpl::WindowLeaveCallback(
        win,
        gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(controller)));
}

} // extern "C"

#endif // __WXGTK4__

extern "C" {

#ifndef __WXGTK4__
static gboolean
gtk_window_leave_callback( GtkWidget* widget,
                           GdkEventCrossing *gdk_event,
                           wxWindowGTK *win )
{
    return wxGTKImpl::WindowLeaveCallback(widget, gdk_event, win);
}
#endif // !__WXGTK4__

//-----------------------------------------------------------------------------
// "value_changed" from scrollbar
//-----------------------------------------------------------------------------

// GTK4's GtkScrollbar has no "value-changed" signal of its own: the value
// belongs to its adjustment, so that is what this is connected to, and it has
// to find the scrollbar again from there.
#ifdef __WXGTK4__
static void
gtk_scrollbar_value_changed(GtkAdjustment* adj, wxWindow* win)
{
    wxGtkScrollbar* const range = win->GTKScrollbarFromAdjustment(adj);
    if ( !range )
        return;
#else
static void
gtk_scrollbar_value_changed(GtkRange* range, wxWindow* win)
{
#endif
    wxEventType eventType = win->GTKGetScrollEventType(range);
    if (eventType != wxEVT_NULL)
    {
        // Convert scroll event type to scrollwin event type
        eventType += wxEVT_SCROLLWIN_TOP - wxEVT_SCROLL_TOP;

        // find the scrollbar which generated the event
        wxWindowGTK::ScrollDir dir = win->ScrollDirFromRange(range);

        // generate the corresponding wx event
        const int orient = wxWindow::OrientFromScrollDir(dir);
        wxScrollWinEvent event(eventType, win->GetScrollPos(orient), orient);
        event.SetEventObject(win);

        win->GTKProcessEvent(event);
    }
}

//-----------------------------------------------------------------------------
// "button_press_event" from scrollbar
//-----------------------------------------------------------------------------

#ifdef __WXGTK4__

// GtkRange has no button-press-event under GTK4, and the "event_after" dance
// the GTK3 code needs -- because GtkRange consumes the release, so the thumb
// release event has to be deferred until after its own handler has run -- is
// unnecessary here: a gesture in the capture phase sees the press and the
// release before GtkRange does, so both can be handled directly.
extern "C" {
static void
wx_scrollbar_pressed(GtkGestureClick*, int, double, double, wxWindow* win)
{
    g_blockEventsOnScroll = true;
    win->m_mouseButtonDown = true;
}

static void
wx_scrollbar_released(GtkGestureClick* gesture, int, double, double, wxWindow* win)
{
    g_blockEventsOnScroll = false;
    win->m_mouseButtonDown = false;

    if (win->m_isScrolling)
    {
        win->m_isScrolling = false;

        wxGtkScrollbar* const range = GTK_SCROLLBAR(gtk_event_controller_get_widget(
                                          GTK_EVENT_CONTROLLER(gesture)));

        const int orient = wxWindow::OrientFromScrollDir(
                                        win->ScrollDirFromRange(range));
        wxScrollWinEvent evt(wxEVT_SCROLLWIN_THUMBRELEASE,
                                win->GetScrollPos(orient), orient);
        evt.SetEventObject(win);
        win->GTKProcessEvent(evt);
    }
}
}

#else // !__WXGTK4__

static gboolean
gtk_scrollbar_button_press_event(GtkRange*, GdkEventButton*, wxWindow* win)
{
    g_blockEventsOnScroll = true;
    win->m_mouseButtonDown = true;

    return false;
}

//-----------------------------------------------------------------------------
// "event_after" from scrollbar
//-----------------------------------------------------------------------------

static void
gtk_scrollbar_event_after(GtkRange* range, GdkEvent* event, wxWindow* win)
{
    if (event->type == GDK_BUTTON_RELEASE)
    {
        g_signal_handlers_block_by_func(range, (void*)gtk_scrollbar_event_after, win);

        const int orient = wxWindow::OrientFromScrollDir(
                                        win->ScrollDirFromRange(range));
        wxScrollWinEvent evt(wxEVT_SCROLLWIN_THUMBRELEASE,
                                win->GetScrollPos(orient), orient);
        evt.SetEventObject(win);
        win->GTKProcessEvent(evt);
    }
}

//-----------------------------------------------------------------------------
// "button_release_event" from scrollbar
//-----------------------------------------------------------------------------

static gboolean
gtk_scrollbar_button_release_event(GtkRange* range, GdkEventButton*, wxWindow* win)
{
    g_blockEventsOnScroll = false;
    win->m_mouseButtonDown = false;
    // If thumb tracking
    if (win->m_isScrolling)
    {
        win->m_isScrolling = false;
        // Hook up handler to send thumb release event after this emission is finished.
        // To allow setting scroll position from event handler, sending event must
        // be deferred until after the GtkRange handler for this signal has run
        g_signal_handlers_unblock_by_func(range, (void*)gtk_scrollbar_event_after, win);
    }

    return false;
}

#endif // __WXGTK4__/!__WXGTK4__

//-----------------------------------------------------------------------------
// "realize" from m_widget
//-----------------------------------------------------------------------------

static void
gtk_window_realized_callback(GtkWidget* WXUNUSED(widget), wxWindowGTK* win)
{
    win->GTKHandleRealized();
}

//-----------------------------------------------------------------------------
// "size_allocate" from m_wxwindow or m_widget
//-----------------------------------------------------------------------------

static void
#ifdef __WXGTK4__
// Connected to wxPizza's own signal, which carries no allocation: see the
// comment at the connection site. (The clip workaround which used the widget
// argument is GTK3 only, see below.)
size_allocate(GtkWidget* WXUNUSED(widget), wxWindow* win)
#else
size_allocate(GtkWidget* WXUNUSED_IN_GTK2(widget), GtkAllocation* alloc, wxWindow* win)
#endif
{
#ifdef __WXGTK4__
    // The allocation the GTK3 signal supplied is that of whichever of the two
    // widgets below the handler was connected to, which is exactly what is
    // read back here.
    GtkAllocation allocStorage;
    gtk_widget_get_allocation(win->m_wxwindow ? win->m_wxwindow : win->m_widget,
                              &allocStorage);
    GtkAllocation* const alloc = &allocStorage;
#endif

    int w = alloc->width;
    int h = alloc->height;
#if GTK_CHECK_VERSION(3,14,0) && !defined(__WXGTK4__)
    // GTK4 removed the widget clip entirely: a widget's drawing is bounded by
    // its own snapshot rather than by a separately declared clip rectangle, so
    // there is nothing to widen and nothing to prevent.
    if (wx_is_at_least_gtk3(14))
    {
        // Prevent under-allocated widgets from drawing outside their allocation
        GtkAllocation clip;
        gtk_widget_get_clip(widget, &clip);
        if (clip.width > w || clip.height > h)
        {
            GtkStyleContext* sc = gtk_widget_get_style_context(widget);
            int outline_offset, outline_width;
            gtk_style_context_get(sc, gtk_style_context_get_state(sc),
                "outline-offset", &outline_offset, "outline-width", &outline_width, nullptr);
            const int outline = outline_offset + outline_width;
            GtkAllocation a = *alloc;
            if (outline > 0)
            {
                // Allow enough room for focus indicator "outline", it's drawn
                // outside of GtkCheckButton allocation with Adwaita theme
                a.x -= outline;
                a.y -= outline;
                a.width += outline + outline;
                a.height += outline + outline;
            }
            gtk_widget_set_clip(widget, &a);
        }
    }
#endif
    if (win->m_wxwindow)
    {
        GtkBorder border;
        WX_PIZZA(win->m_wxwindow)->get_border(border);
        w -= border.left + border.right;
        h -= border.top + border.bottom;
        if (w < 0) w = 0;
        if (h < 0) h = 0;
    }
    GtkAllocation a;
    gtk_widget_get_allocation(win->m_widget, &a);
    // update position for widgets in native containers, such as wxToolBar
    if (!WX_IS_PIZZA(gtk_widget_get_parent(win->m_widget)))
    {
        win->m_x = a.x;
        win->m_y = a.y;
    }
    win->m_useCachedClientSize = true;
    win->m_isGtkPositionValid = true;
    if (win->m_clientWidth != w || win->m_clientHeight != h)
    {
        win->m_clientWidth  = w;
        win->m_clientHeight = h;
        // this callback can be connected to m_wxwindow,
        // so always get size from m_widget->allocation
        win->m_width  = a.width;
        win->m_height = a.height;
        {
            wxRecursionGuard setInSizeAllocate(g_inSizeAllocate);
            wxSizeEvent event(win->GetSize(), win->GetId());
            event.SetEventObject(win);
            win->GTKProcessEvent(event);
        }
    }
}

//-----------------------------------------------------------------------------
// "grab_broken_event"
//-----------------------------------------------------------------------------

#if GTK_CHECK_VERSION(2, 8, 0) && !defined(__WXGTK4__)
// GTK4 has no explicit grabs, so none can be broken.
static gboolean
gtk_window_grab_broken( GtkWidget*,
                        GdkEventGrabBroken *event,
                        wxWindow *win )
{
    // Mouse capture has been lost involuntarily, notify the application
    if(!event->keyboard && wxWindow::GetCapture() == win)
    {
        wxWindowGTK::GTKHandleCaptureLost();
    }
    return false;
}
#endif

//-----------------------------------------------------------------------------
// "unrealize"
//-----------------------------------------------------------------------------

static void gtk_window_unrealized_callback(GtkWidget*, wxWindow* win)
{
    win->GTKHandleUnrealized();
}

#if GTK_CHECK_VERSION(3,8,0)
//-----------------------------------------------------------------------------
// "layout" from GdkFrameClock
//-----------------------------------------------------------------------------

static void frame_clock_layout(GdkFrameClock*, wxWindow* win)
{
    win->GTKSizeRevalidate();
}

static void frame_clock_layout_after(GdkFrameClock*, wxWindowGTK* win)
{
    win->GTKSendSizeEventIfNeeded();

    if (gs_setSizeRequestList)
    {
        for (GSList* p = gs_setSizeRequestList; p; p = p->next)
        {
            if (p->data == nullptr)
                continue;

            wxWindowGTK* w = static_cast<wxWindowGTK*>(p->data);
            g_object_remove_weak_pointer(G_OBJECT(w->m_widget), &p->data);
            if (WX_IS_PIZZA(gtk_widget_get_parent(w->m_widget)))
                gtk_widget_queue_resize(w->m_widget);
            else
            {
                GtkAllocation a;
                gtk_widget_get_allocation(w->m_widget, &a);
                gtk_widget_set_size_request(w->m_widget, a.width, a.height);
            }
        }
        g_slist_free(gs_setSizeRequestList);
        gs_setSizeRequestList = nullptr;
    }
}
#endif // GTK_CHECK_VERSION(3,8,0)

} // extern "C"

#if GTK_CHECK_VERSION(3,8,0)
// Drop the "layout" handlers GTKHandleRealized() puts on the frame clock.
//
// Under GTK3 not doing this was survivable: the frame clock belonged to the
// toplevel's GdkWindow and died with it, taking the handlers along. GTK4 has
// no GdkWindow, the clock belongs to the GdkSurface and outlives the wxWindow,
// so a handler left behind is called back with a dangling "this" -- caught by
// ASAN as a wild-pointer read of m_needSizeEvent in
// GTKSendSizeEventIfNeeded(), from a frame clock still driving a window
// destroyed several test cases earlier.
void wxWindowGTK::GTKDisconnectFrameClock()
{
    if (!m_frameClock)
        return;

    g_signal_handlers_disconnect_by_data(m_frameClock, this);
    g_object_remove_weak_pointer(G_OBJECT(m_frameClock),
                                 reinterpret_cast<gpointer*>(&m_frameClock));
    m_frameClock = nullptr;
}
#endif // GTK_CHECK_VERSION(3,8,0)

#ifdef __WXGTK4__
// Detach m_widget from whatever currently holds it.
//
// gtk_widget_unparent() is not that operation. GTK4 containers keep their own
// record of their children, and unparenting behind their back leaves it
// pointing at a widget that is going away: wxPizza kept a stale m_children
// entry per departed child, and GtkFixed a stale layout child, which surfaced
// as "unknown auxiliary child surface" and then as GTK aborting inside
// gtk_css_node_validate(). Only a widget parented with
// gtk_widget_set_parent() may be detached with gtk_widget_unparent().
//
// wxPizza is the parent for wx's own children, and is the case that had to be
// fixed. Other GTK containers wx puts children into (notebooks, scrolled
// windows) each have their own removal call and are not handled here.
void wxWindowGTK::GTKDetachFromParent()
{
    if (GtkWidget* const parent = gtk_widget_get_parent(m_widget))
    {
        if (WX_IS_PIZZA(parent))
            WX_PIZZA(parent)->remove(m_widget);
        else
            gtk_widget_unparent(m_widget);

        return;
    }

    // A wxTopLevelWindow with a wx parent has no GTK parent -- wxPizza::put()
    // deliberately does not make a toplevel a child at GTK level -- but it is
    // still recorded in that pizza's m_children, so it has to be looked up
    // through wx's own parent instead. Missing this left the pizza holding an
    // entry for a destroyed GtkWindow, which later layout passes walked.
    if (m_parent != nullptr && m_parent->m_wxwindow != nullptr &&
            WX_IS_PIZZA(m_parent->m_wxwindow))
    {
        WX_PIZZA(m_parent->m_wxwindow)->remove(m_widget);
    }
}
#endif // __WXGTK4__

void wxWindowGTK::GTKHandleRealized()
{
#ifndef __WXGTK4__
    GdkWindow* const window = GTKGetDrawingWindow();
#endif

    if (m_wxwindow)
    {
        if (m_imContext == nullptr)
        {
            // Create input method handler
            m_imContext = gtk_im_multicontext_new();

            // Cannot handle drawing preedited text yet
            gtk_im_context_set_use_preedit(m_imContext, false);

            g_signal_connect(m_imContext,
                "commit", G_CALLBACK(gtk_wxwindow_commit_cb), this);
        }
#ifdef __WXGTK4__
        gtk_im_context_set_client_widget(m_imContext, GetConnectWidget());
#else
        gtk_im_context_set_client_window(m_imContext, window);
#endif
    }

    // Use composited window if background is transparent, if supported.
    if (m_backgroundStyle == wxBG_STYLE_TRANSPARENT)
    {
#if wxGTK_HAS_COMPOSITING_SUPPORT && !defined(__WXGTK4__)
        if (IsTransparentBackgroundSupported())
        {
            wxGCC_WARNING_SUPPRESS(deprecated-declarations)
            if (window && !IsTopLevel())
                gdk_window_set_composited(window, true);
            wxGCC_WARNING_RESTORE()
        }
        else
#elif defined(__WXGTK4__)
        // Every GTK4 surface is composited; there is no per-widget window to
        // mark as such, and transparency is handled by the snapshot itself.
        if (IsTransparentBackgroundSupported())
        {
        }
        else
#endif // wxGTK_HAS_COMPOSITING_SUPPORT
        {
            // We revert to erase mode if transparency is not supported
            m_backgroundStyle = wxBG_STYLE_ERASE;
        }
    }

#ifndef __WXGTK3__
    if (window && (
        m_backgroundStyle == wxBG_STYLE_PAINT ||
        m_backgroundStyle == wxBG_STYLE_TRANSPARENT))
    {
        gdk_window_set_back_pixmap(window, nullptr, false);
    }
#endif

#if GTK_CHECK_VERSION(3,8,0)
    if (IsTopLevel() && gtk_check_version(3,8,0) == nullptr)
    {
        GdkFrameClock* clock = gtk_widget_get_frame_clock(m_widget);

        // Re-realizing can hand us a different clock, and the old one keeps
        // its handlers -- and its pointer to this window -- until told
        // otherwise, so let go of it first.
        if (clock != m_frameClock)
            GTKDisconnectFrameClock();

        if (clock && !m_frameClock)
        {
            g_signal_connect(clock, "layout", G_CALLBACK(frame_clock_layout), this);
            g_signal_connect_after(clock, "layout", G_CALLBACK(frame_clock_layout_after), this);

            m_frameClock = clock;
            g_object_add_weak_pointer(G_OBJECT(clock),
                                      reinterpret_cast<gpointer*>(&m_frameClock));
        }
    }
#endif

    wxWindowCreateEvent event(static_cast<wxWindow*>(this));
    event.SetEventObject( this );
    GTKProcessEvent( event );

    WXUpdateCursor();
}

void wxWindowGTK::GTKHandleUnrealized()
{
    m_isGtkPositionValid = false;

#if GTK_CHECK_VERSION(3,8,0)
    // The frame clock is still reachable here; it may not be by the time the
    // window is destroyed, and it can be a different one after re-realizing.
    GTKDisconnectFrameClock();
#endif

    if (m_wxwindow)
    {
        if (m_imContext)
        {
#ifdef __WXGTK4__
            gtk_im_context_set_client_widget(m_imContext, nullptr);
#else
            gtk_im_context_set_client_window(m_imContext, nullptr);
#endif
        }
    }
}

// ----------------------------------------------------------------------------
// this wxWindowBase function is implemented here (in platform-specific file)
// because it is static and so couldn't be made virtual
// ----------------------------------------------------------------------------

wxWindow *wxWindowBase::DoFindFocus()
{
#if wxUSE_MENUS
    // For compatibility with wxMSW, pretend that showing a popup menu doesn't
    // change the focus and that it remains on the window showing it, even
    // though the real focus does change in GTK.
    extern wxMenu *wxCurrentPopupMenu;
    if ( wxCurrentPopupMenu )
        return wxCurrentPopupMenu->GetInvokingWindow();
#endif // wxUSE_MENUS

    wxWindowGTK *focus = gs_pendingFocus ? gs_pendingFocus : gs_currentFocus;
    // the cast is necessary when we compile in wxUniversal mode
    return static_cast<wxWindow*>(focus);
}

void wxWindowGTK::AddChildGTK(wxWindowGTK* child)
{
    wxASSERT_MSG(m_wxwindow, "Cannot add a child to a window without a client area");

    // the window might have been scrolled already, we
    // have to adapt the position
    wxPizza* pizza = WX_PIZZA(m_wxwindow);
    child->m_x += pizza->m_scroll_x;
    child->m_y += pizza->m_scroll_y;

    pizza->put(child->m_widget,
        child->m_x, child->m_y, child->m_width, child->m_height);
}

//-----------------------------------------------------------------------------
// global functions
//-----------------------------------------------------------------------------

wxWindow *wxGetActiveWindow()
{
    return wxWindow::FindFocus();
}


// Under Unix this is implemented using X11 functions in utilsx11.cpp but we
// need to have this function under Windows too, so provide at least a stub.
#ifdef GDK_WINDOWING_WIN32
bool wxGetKeyState(wxKeyCode WXUNUSED(key))
{
    wxFAIL_MSG(wxS("Not implemented under Windows"));
    return false;
}
#endif // __WINDOWS__

#ifdef __WXGTK4__

// Ask the X server where the pointer is, in root -- i.e. screen -- coordinates,
// and which buttons are down while it is at it.
//
// GTK4 has no call for this: gdk_device_get_surface_at_position() answers
// relative to whichever surface the pointer happens to be over, which is not
// the same thing and is wrong for anything comparing positions across windows.
// Wayland does not allow the question to be asked at all, by design, so this
// only helps under X11 -- but that is where the callers that need it, such as
// wxAUI's docking hit test, are typically running.
static bool
wxGTKQueryPointerX11(GdkDisplay* display, int* x, int* y, GdkModifierType* mask)
{
#ifdef GDK_WINDOWING_X11
    if ( !wxGTKImpl::IsX11(display) )
        return false;

    Display* const xdisplay = GDK_DISPLAY_XDISPLAY(display);
    if ( !xdisplay )
        return false;

    Window rootRet, childRet;
    int rootX = 0, rootY = 0, winX = 0, winY = 0;
    unsigned int stateRet = 0;

    if ( !XQueryPointer(xdisplay, DefaultRootWindow(xdisplay),
                        &rootRet, &childRet,
                        &rootX, &rootY, &winX, &winY, &stateRet) )
    {
        // The pointer is on another screen: nothing sensible to report.
        return false;
    }

    if ( x )
        *x = rootX;
    if ( y )
        *y = rootY;
    if ( mask )
        *mask = GdkModifierType(stateRet);

    return true;
#else
    wxUnusedVar(display);
    wxUnusedVar(x);
    wxUnusedVar(y);
    wxUnusedVar(mask);

    return false;
#endif // GDK_WINDOWING_X11
}

#endif // __WXGTK4__

wxMouseState wxGetMouseState()
{
    wxMouseState ms;

    gint x = 0;
    gint y = 0;
    GdkModifierType mask = GdkModifierType(0);

#ifdef __WXGTK4__
    // GTK4 (particularly under Wayland, by deliberate design) provides no
    // API to query the pointer's position in global screen coordinates
    // any more -- gdk_device_get_surface_at_position() is the closest
    // replacement, but it's relative to whatever surface the pointer
    // happens to be over, not the screen. This means the position is only
    // meaningful while the pointer is over one of this application's own
    // windows; see docs/gtk/gtk4-status.md for the tracked limitation.
    GdkDisplay* display = wxGetTopLevelGdkDisplay();
    if ( !wxGTKQueryPointerX11(display, &x, &y, &mask) )
    {
        GdkSeat* seat = gdk_display_get_default_seat(display);
        GdkDevice* device = gdk_seat_get_pointer(seat);
        double dx = 0, dy = 0;
        if (gdk_device_get_surface_at_position(device, &dx, &dy))
        {
            x = gint(dx);
            y = gint(dy);
            mask = gdk_device_get_modifier_state(device);
        }

        // That state has the keyboard modifiers but no mouse buttons at all,
        // so the buttons come from what the events said; see gs_buttonState.
        mask = GdkModifierType(mask | gs_buttonState);
    }
#else
    GdkDisplay* display = wxGetTopLevelGdkDisplay();
#ifdef __WXGTK3__
    GdkWindow* window = wxGetTopLevelGDK();
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    GdkDeviceManager* manager = gdk_display_get_device_manager(display);
    GdkDevice* device = gdk_device_manager_get_client_pointer(manager);
    wxGCC_WARNING_RESTORE()
    gdk_device_get_position(device, nullptr, &x, &y);
    gdk_device_get_state(device, window, nullptr, &mask);
#else
    gdk_display_get_pointer(display, nullptr, &x, &y, &mask);
#endif
#endif // __WXGTK4__/!__WXGTK4__

    ms.SetX(x);
    ms.SetY(y);
    ms.SetLeftDown((mask & GDK_BUTTON1_MASK) != 0);
    ms.SetMiddleDown((mask & GDK_BUTTON2_MASK) != 0);
    ms.SetRightDown((mask & GDK_BUTTON3_MASK) != 0);
    // see the comment in InitMouseEvent()
    ms.SetAux1Down((mask & GDK_BUTTON4_MASK) != 0);
    ms.SetAux2Down((mask & GDK_BUTTON5_MASK) != 0);

    ms.SetControlDown((mask & GDK_CONTROL_MASK) != 0);
    ms.SetShiftDown((mask & GDK_SHIFT_MASK) != 0);
#ifdef __WXGTK4__
    ms.SetAltDown((mask & GDK_ALT_MASK) != 0);
#else
    ms.SetAltDown((mask & GDK_MOD1_MASK) != 0);
#endif
    ms.SetMetaDown((mask & GDK_META_MASK) != 0);

    return ms;
}

//-----------------------------------------------------------------------------
// wxWindowGTK
//-----------------------------------------------------------------------------

// in wxUniv/MSW this class is abstract because it doesn't have DoPopupMenu()
// method
#ifdef __WXUNIVERSAL__
    wxIMPLEMENT_ABSTRACT_CLASS(wxWindowGTK, wxWindowBase);
#endif // __WXUNIVERSAL__

void wxWindowGTK::Init()
{
#ifdef __WXGTK4__
    m_creationSerial = ++gs_windowSerial;
#endif // __WXGTK4__

    // GTK specific
    m_widget = nullptr;
    m_wxwindow = nullptr;
    m_focusWidget = nullptr;

    // position/size
    m_x = 0;
    m_y = 0;
    m_width = 0;
    m_height = 0;

    m_showOnIdle = false;
    m_needCursorReset = false;
    m_noExpose = false;
    m_nativeSizeEvent = false;
#ifdef __WXGTK3__
    m_paintContext = nullptr;
    m_styleProvider = nullptr;
    m_needSizeEvent = false;
#endif

    m_isScrolling = false;
    m_mouseButtonDown = false;

    // initialize scrolling stuff
    for ( int dir = 0; dir < ScrollDir_Max; dir++ )
    {
        m_scrollBar[dir] = nullptr;
        m_scrollPos[dir] = 0;
    }

    m_clientWidth =
    m_clientHeight = 0;
    m_useCachedClientSize = false;
    m_isGtkPositionValid = false;

    m_clipPaintRegion = false;

    m_imContext = nullptr;
    m_imKeyEvent = nullptr;

    m_dirtyTabOrder = false;
}

wxWindowGTK::wxWindowGTK()
{
    Init();
}

wxWindowGTK::wxWindowGTK( wxWindow *parent,
                          wxWindowID id,
                          const wxPoint &pos,
                          const wxSize &size,
                          long style,
                          const wxString &name  )
{
    Init();

    Create( parent, id, pos, size, style, name );
}

void wxWindowGTK::GTKCreateScrolledWindowWith(GtkWidget* view)
{
    wxASSERT_MSG( HasFlag(wxHSCROLL) || HasFlag(wxVSCROLL),
                  wxS("Must not be called if scrolling is not needed.") );

    #ifdef __WXGTK4__
    m_widget = gtk_scrolled_window_new();
#else
    m_widget = gtk_scrolled_window_new( nullptr, nullptr );
#endif

    GtkScrolledWindow *scrolledWindow = GTK_SCROLLED_WINDOW(m_widget);

    // There is a conflict with default bindings at GTK+
    // level between scrolled windows and notebooks both of which want to use
    // Ctrl-PageUp/Down: scrolled windows for scrolling in the horizontal
    // direction and notebooks for changing pages -- we decide that if we don't
    // have wxHSCROLL style we can safely sacrifice horizontal scrolling if it
    // means we can get working keyboard navigation in notebooks
#ifndef __WXGTK4__
    if ( !HasFlag(wxHSCROLL) )
    {
        GtkBindingSet *
            bindings = gtk_binding_set_by_class(G_OBJECT_GET_CLASS(m_widget));
        if ( bindings )
        {
            gtk_binding_entry_remove(bindings, GDK_KEY_Page_Up, GDK_CONTROL_MASK);
            gtk_binding_entry_remove(bindings, GDK_KEY_Page_Down, GDK_CONTROL_MASK);
        }
    }
#else
    // GTK4 replaced binding sets with GtkShortcut, and a class's shortcuts are
    // not removable from outside it, so the Ctrl-PageUp/Down conflict between
    // scrolled windows and notebooks described above cannot be resolved this
    // way any more. Recorded in docs/gtk/gtk4-status.md.
#endif

    // If wx[HV]SCROLL is not given, the corresponding scrollbar is not shown
    // at all. Otherwise it may be shown only on demand (default) or always, if
    // the wxALWAYS_SHOW_SB is specified.
    GtkPolicyType horzPolicy = HasFlag(wxHSCROLL)
                                ? HasFlag(wxALWAYS_SHOW_SB)
                                    ? GTK_POLICY_ALWAYS
                                    : GTK_POLICY_AUTOMATIC
                                : GTK_POLICY_NEVER;
    GtkPolicyType vertPolicy = HasFlag(wxVSCROLL)
                                ? HasFlag(wxALWAYS_SHOW_SB)
                                    ? GTK_POLICY_ALWAYS
                                    : GTK_POLICY_AUTOMATIC
                                : GTK_POLICY_NEVER;
    gtk_scrolled_window_set_policy( scrolledWindow, horzPolicy, vertPolicy );

#ifdef __WXGTK4__
    m_scrollBar[ScrollDir_Horz] = GTK_SCROLLBAR(gtk_scrolled_window_get_hscrollbar(scrolledWindow));
    m_scrollBar[ScrollDir_Vert] = GTK_SCROLLBAR(gtk_scrolled_window_get_vscrollbar(scrolledWindow));
#else
    m_scrollBar[ScrollDir_Horz] = GTK_RANGE(gtk_scrolled_window_get_hscrollbar(scrolledWindow));
    m_scrollBar[ScrollDir_Vert] = GTK_RANGE(gtk_scrolled_window_get_vscrollbar(scrolledWindow));
#endif

#ifdef __WXGTK4__
    gtk_scrolled_window_set_child( scrolledWindow, view );
#else
    gtk_container_add( GTK_CONTAINER(m_widget), view );
#endif

    // connect various scroll-related events
    for ( int dir = 0; dir < ScrollDir_Max; dir++ )
    {
        // these handlers block mouse events to any window during scrolling
        // such as motion events and prevent GTK and wxWidgets from fighting
        // over where the slider should be
#ifdef __WXGTK4__
        {
            GtkGesture* const click = gtk_gesture_click_new();
            gtk_event_controller_set_propagation_phase(
                GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
            g_signal_connect(click, "pressed",
                             G_CALLBACK(wx_scrollbar_pressed), this);
            g_signal_connect(click, "released",
                             G_CALLBACK(wx_scrollbar_released), this);
            gtk_widget_add_controller(GTK_WIDGET(m_scrollBar[dir]),
                                      GTK_EVENT_CONTROLLER(click));
        }
#else
        g_signal_connect(m_scrollBar[dir], "button_press_event",
                     G_CALLBACK(gtk_scrollbar_button_press_event), this);
        g_signal_connect(m_scrollBar[dir], "button_release_event",
                     G_CALLBACK(gtk_scrollbar_button_release_event), this);

        gulong handler_id = g_signal_connect(m_scrollBar[dir], "event_after",
                            G_CALLBACK(gtk_scrollbar_event_after), this);
        g_signal_handler_block(m_scrollBar[dir], handler_id);
#endif

        // these handlers get notified when scrollbar slider moves
        g_signal_connect_after(wxGtkScrollbarValueNotifier(m_scrollBar[dir]),
                     "value_changed",
                     G_CALLBACK(gtk_scrollbar_value_changed), this);
    }

    gtk_widget_show( view );
}

bool wxWindowGTK::Create( wxWindow *parent,
                          wxWindowID id,
                          const wxPoint &pos,
                          const wxSize &size,
                          long style,
                          const wxString &name  )
{
    // Get default border
    wxBorder border = GetBorder(style);

    style &= ~wxBORDER_MASK;
    style |= border;

    if (!PreCreation( parent, pos, size ) ||
        !CreateBase( parent, id, pos, size, style, wxDefaultValidator, name ))
    {
        wxFAIL_MSG( wxT("wxWindowGTK creation failed") );
        return false;
    }

        // We should accept the native look
#if 0
        GtkScrolledWindowClass *scroll_class = GTK_SCROLLED_WINDOW_CLASS( GTK_OBJECT_GET_CLASS(m_widget) );
        scroll_class->scrollbar_spacing = 0;
#endif


    m_wxwindow = wxPizza::New(m_windowStyle);
#if !defined(__WXUNIVERSAL__) && !defined(__WXGTK4__)
    // GTK4 has no "parent-set" signal, and needs none here: the border is
    // painted by wxPizza's own snapshot rather than by hooking the parent's
    // draw handler, see GTKDrawBorder().
    if (HasFlag(wxPizza::BORDER_STYLES))
    {
        g_signal_connect(m_wxwindow, "parent_set",
            G_CALLBACK(parent_set), this);
    }
#endif
    if (!HasFlag(wxHSCROLL) && !HasFlag(wxVSCROLL))
        m_widget = m_wxwindow;
    else
        GTKCreateScrolledWindowWith(m_wxwindow);
    g_object_ref(m_widget);

    if (m_parent)
        m_parent->DoAddChild( this );

    m_focusWidget = m_wxwindow;

    SetCanFocus(AcceptsFocus());

    PostCreation();

    return true;
}

#ifdef __WXGTK4__
// Defined with the other focus-controller helpers further down.
static gpointer wxGTKGetFocusController(GtkWidget* widget);
#endif

void wxWindowGTK::GTKDisconnect(void* instance)
{
    g_signal_handlers_disconnect_by_data(instance, this);
}

wxWindowGTK::~wxWindowGTK()
{
    SendDestroyEvent();

#ifdef __WXGTK4__
    // See gs_focusRestoreAfter: this is the destruction whose focus GTK4 will
    // try to pass on to whatever is created next.
    // gs_focusDeclined too: GTK still considers that window focused even
    // though wx does not, so destroying it starts the same restore again.
    if (gs_currentFocus == this || gs_focusDeclined == this)
        gs_focusRestoreAfter = gs_windowSerial;

    if (gs_focusDeclined == this)
        gs_focusDeclined = nullptr;
#endif // __WXGTK4__

    if (gs_currentFocus == this)
        gs_currentFocus = nullptr;
    if (gs_pendingFocus == this)
        gs_pendingFocus = nullptr;
    if (gs_lastFocus == this)
        gs_lastFocus = nullptr;

    if ( gs_deferredFocusOut == this )
        gs_deferredFocusOut = nullptr;

    // This is a real error, unlike the above, but it's already checked for in
    // the base class dtor and asserting here results is useless and, even
    // worse, results in abnormal termination when running unit tests which
    // throw exceptions from their assert handler, so don't assert here.
    if ( g_captureWindow == this )
        g_captureWindow = nullptr;

    if ( g_windowUnderMouse == this )
        g_windowUnderMouse = nullptr;

#ifdef __WXGTK4__
    // The focus handlers are connected to the GtkEventControllerFocus rather
    // than to the widget, so GTKDisconnect() on the widget does not reach
    // them -- and GTK keeps the focus controller alive long enough to report
    // the focus leaving a widget that is being destroyed, which called
    // GTKHandleFocusOut() on a freed wxWindow.
    if ( gpointer const focus = wxGTKGetFocusController(m_focusWidget) )
        g_signal_handlers_disconnect_by_data(focus, this);

    // And wxGTKFocusOwner() must not answer with a window that is going away.
    if ( m_focusWidget )
        g_object_set_data(G_OBJECT(m_focusWidget), WX_FOCUS_OWNER, nullptr);
#endif // __WXGTK4__

    if (m_wxwindow)
    {
        GTKDisconnect(m_wxwindow);
        GtkWidget* parent = gtk_widget_get_parent(m_wxwindow);
        if (parent)
            GTKDisconnect(parent);
    }
    if (m_widget && m_widget != m_wxwindow)
    {
        GTKDisconnect(m_widget);

#ifdef __WXGTK4__
        // A window with no wxPizza of its own has its size-allocated handler
        // on its *parent's* pizza -- see PostCreation() -- which outlives it,
        // so that one has to go here too. The m_wxwindow case above already
        // does the same thing for the same reason.
        GtkWidget* const parent = gtk_widget_get_parent(m_widget);
        if (parent)
            GTKDisconnect(parent);
#endif // __WXGTK4__
    }

#if GTK_CHECK_VERSION(3,8,0)
    // Backstop for a window destroyed without being unrealized first.
    GTKDisconnectFrameClock();
#endif

    // destroy children before destroying this window itself
    DestroyChildren();

    // delete before the widgets to avoid a crash on solaris
    if ( m_imContext )
    {
        g_object_unref(m_imContext);
        m_imContext = nullptr;
    }

#ifdef __WXGTK3__
    if (m_styleProvider)
        g_object_unref(m_styleProvider);

    gs_sizeRevalidateList = g_list_remove_all(gs_sizeRevalidateList, this);
#endif

#ifdef wxGTK_HAS_GESTURES_SUPPORT
    wxWindowGestures::EraseForObject(static_cast<wxWindow*>(this));
#endif // wxGTK_HAS_GESTURES_SUPPORT

    if (m_widget)
    {
#ifdef __WXGTK4__
        // gtk_widget_destroy() doesn't exist under GTK4: GtkWindow
        // (toplevels) has gtk_window_destroy() instead, while a plain
        // widget's closest equivalent is simply detaching it from its
        // parent (if it still has one -- DestroyChildren() above may
        // already have done this indirectly). The explicit
        // g_object_unref() below drops wx's own reference either way,
        // same as the GTK3 path. Not yet runtime-verified against a
        // live app -- this is core lifecycle code for every wxWindow,
        // see docs/gtk/gtk4-status.md.
        // Drop the parent's record of this widget first, while it is still
        // valid, whether or not it was ever a GTK-level child.
        GTKDetachFromParent();

        if (GTK_IS_WINDOW(m_widget))
            gtk_window_destroy(GTK_WINDOW(m_widget));
#else
        // Note that gtk_widget_destroy() does not destroy the widget, it just
        // emits the "destroy" signal. The widget is not actually destroyed
        // until its reference count drops to zero.
        gtk_widget_destroy(m_widget);
#endif // __WXGTK4__/!__WXGTK4__
        // Release our reference, should be the last one
        g_object_unref(m_widget);
        m_widget = nullptr;
    }
    m_wxwindow = nullptr;
}

bool wxWindowGTK::PreCreation( wxWindowGTK *parent, const wxPoint &pos,  const wxSize &size )
{
    if ( GTKNeedsParent() )
    {
        wxCHECK_MSG( parent, false, wxT("Must have non-null parent") );
    }

    // Use either the given size, or the default if -1 is given.
    // See wxWindowBase for these functions.
    m_width = WidthDefault(size.x) ;
    m_height = HeightDefault(size.y);

    if (pos != wxDefaultPosition)
    {
        m_x = pos.x;
        m_y = pos.y;
    }

    return true;
}

void wxWindowGTK::PostCreation()
{
    wxASSERT_MSG( (m_widget != nullptr), wxT("invalid window") );

    SetLayoutDirection(wxLayout_Default);

    GTKConnectFreezeWidget(m_widget);
    if (m_wxwindow && m_wxwindow != m_widget)
        GTKConnectFreezeWidget(m_wxwindow);

#if wxGTK_HAS_COMPOSITING_SUPPORT
    // Set RGBA visual as soon as possible to minimize the possibility that
    // somebody uses the wrong one.
    if ( m_backgroundStyle == wxBG_STYLE_TRANSPARENT &&
            IsTransparentBackgroundSupported() )
    {
        gtk_widget_set_app_paintable(m_widget, true);
        GdkScreen *screen = gtk_widget_get_screen (m_widget);
#ifdef __WXGTK3__
        gtk_widget_set_visual(m_widget, gdk_screen_get_rgba_visual(screen));
#else
        GdkColormap *rgba_colormap = gdk_screen_get_rgba_colormap (screen);

        if (rgba_colormap)
            gtk_widget_set_colormap(m_widget, rgba_colormap);
#endif
    }
#endif // wxGTK_HAS_COMPOSITING_SUPPORT

    if (m_wxwindow)
    {
        if (!m_noExpose)
        {
            // these get reported to wxWidgets -> wxPaintEvent
#ifdef __WXGTK4__
            // There is no "draw" signal under GTK4: wxPizza paints from its
            // snapshot vfunc, which has no user data, so record the owner for
            // it to find. See pizza_snapshot() in win_gtk.cpp.
            g_object_set_data(G_OBJECT(m_wxwindow), "wx-pizza-owner", this);
#elif defined(__WXGTK3__)
            g_signal_connect(m_wxwindow, "draw", G_CALLBACK(draw), this);
#else
            g_signal_connect(m_wxwindow, "expose_event", G_CALLBACK(expose_event), this);
#endif

#ifndef __WXGTK4__
            // Gone under GTK4, which always redraws a widget on resize --
            // i.e. behaves as this being unconditionally TRUE. Means
            // wxFULL_REPAINT_ON_RESIZE cannot be turned off there.
            if (GetLayoutDirection() == wxLayout_LeftToRight)
                gtk_widget_set_redraw_on_allocate(m_wxwindow, HasFlag(wxFULL_REPAINT_ON_RESIZE));
#endif
        }
    }

    // focus handling

    // Check for GTKNeedsParent() || IsTopLevel() is a hack: it catches the
    // case of wxMenuBar, which isn't supposed to generate any focus events,
    // and which is the only non-TLW which returns false from this function.
    //
    // The TLW check overlaps with !GTK_IS_WINDOW() check, but it's not 100%
    // obvious if GTK_IS_WINDOW() and wxWindow::IsTopLevel() are really exactly
    // equivalent, so for now ensure we don't change the existing check which
    // only used !GTK_IS_WINDOW().
    if (!GTK_IS_WINDOW(m_widget) && (GTKNeedsParent() || IsTopLevel()))
    {
        if (m_focusWidget == nullptr)
            m_focusWidget = m_widget;

#ifdef __WXGTK4__
        // GTK4 has no focus-in/out-event; a focus controller reports both, and
        // its signals carry no event and return nothing. The controller is
        // remembered on the widget so that GTKDisableFocusOutEvent() below can
        // find it again to block the handler.
        {
            GtkEventController* const focus = gtk_event_controller_focus_new();
            g_signal_connect(focus, "enter",
                             G_CALLBACK(wx_window_focus_in), this);
            g_signal_connect(focus, "leave",
                             G_CALLBACK(wx_window_focus_out), this);
            gtk_widget_add_controller(m_focusWidget, focus);

            g_object_set_data(G_OBJECT(m_focusWidget),
                              "wx-focus-controller", focus);

            // wxGTKFocusOwner() finds the window this widget belongs to here.
            g_object_set_data(G_OBJECT(m_focusWidget), WX_FOCUS_OWNER, this);
        }
#else
        if (m_wxwindow)
        {
            g_signal_connect (m_focusWidget, "focus_in_event",
                          G_CALLBACK (gtk_window_focus_in_callback), this);
            g_signal_connect (m_focusWidget, "focus_out_event",
                                G_CALLBACK (gtk_window_focus_out_callback), this);
        }
        else
        {
            g_signal_connect_after (m_focusWidget, "focus_in_event",
                          G_CALLBACK (gtk_window_focus_in_callback), this);
            g_signal_connect_after (m_focusWidget, "focus_out_event",
                                G_CALLBACK (gtk_window_focus_out_callback), this);
        }
#endif // __WXGTK4__/!__WXGTK4__
    }

    if ( !AcceptsFocusFromKeyboard() )
    {
        SetCanFocus(false);

#ifndef __WXGTK4__
        // GtkWidget::focus is a vfunc rather than a signal under GTK4, so
        // there is nothing to connect to -- and nothing to work around
        // either: SetCanFocus(false) above sets the "focusable" property,
        // which GTK4 honours, whereas GTK3's GtkScrolledWindow focused itself
        // regardless of it, which is what the handler existed to prevent.
        g_signal_connect(m_widget, "focus",
                            G_CALLBACK(wx_window_focus_callback), this);
#endif // !__WXGTK4__
    }

    // connect to the various key and mouse handlers

    GtkWidget *connect_widget = GetConnectWidget();

    ConnectWidget( connect_widget );

    // We cannot set colours, fonts and cursors before the widget has been
    // realized, so we do this directly after realization -- unless the widget
    // was in fact realized already.
    if ( gtk_widget_get_realized(connect_widget) )
    {
        GTKHandleRealized();
    }

    // Note that we connect to "realize" even if the widget is already realized
    // because we might be unrealized later and then realized again, and we
    // must be notified when the widget is re-realized again.
    g_signal_connect (connect_widget, "realize",
                      G_CALLBACK (gtk_window_realized_callback), this);
    g_signal_connect(connect_widget, "unrealize",
                      G_CALLBACK(gtk_window_unrealized_callback), this);

    if (!IsTopLevel())
    {
#ifdef __WXGTK4__
        // GtkWidget has no "size-allocate" signal any more -- only the vfunc,
        // which an outside observer cannot connect to. wxPizza emits one of
        // its own instead (see win_gtk.cpp), and everything wx puts on screen
        // is either a wxPizza or a child laid out by one, so connecting to
        // whichever applies covers every case the GTK3 signal did.
        //
        // For a child, the parent's signal fires whenever the child could have
        // been given a new allocation, which is what matters: a child's size
        // only changes when its parent lays it out, and the handler compares
        // against the cached size, so a spurious call costs nothing.
        GtkWidget* notifier = nullptr;
        if (m_wxwindow)
            notifier = m_wxwindow;
        else if (GtkWidget* const parent = gtk_widget_get_parent(m_widget))
        {
            if (WX_IS_PIZZA(parent))
                notifier = parent;
        }

        if (notifier)
        {
            g_signal_connect(notifier, wxPIZZA_SIGNAL_SIZE_ALLOCATED,
                G_CALLBACK(size_allocate), this);
        }
#else
        g_signal_connect(m_wxwindow ? m_wxwindow : m_widget, "size_allocate",
            G_CALLBACK(size_allocate), this);
#endif
    }

#if GTK_CHECK_VERSION(2, 8, 0)
    if ( wx_is_at_least_gtk2(8) )
    {
        // Make sure we can notify the app when mouse capture is lost
#ifndef __WXGTK4__
        if ( m_wxwindow )
        {
            g_signal_connect (m_wxwindow, "grab_broken_event",
                          G_CALLBACK (gtk_window_grab_broken), this);
        }

        if ( connect_widget != m_wxwindow )
        {
            g_signal_connect (connect_widget, "grab_broken_event",
                        G_CALLBACK (gtk_window_grab_broken), this);
        }
#endif // !__WXGTK4__
    }
#endif // GTK+ >= 2.8

    if (!WX_IS_PIZZA(gtk_widget_get_parent(m_widget)) && !GTK_IS_WINDOW(m_widget))
        gtk_widget_set_size_request(m_widget, m_width, m_height);

    // apply any font or color changes made before creation
    GTKApplyWidgetStyle();

    InheritAttributes();

    // if the window had been disabled before being created, it should be
    // created in the initially disabled state
    if ( !m_isEnabled )
        DoEnable(false);

    // unless the window was created initially hidden (i.e. Hide() had been
    // called before Create()), we should show it at GTK+ level as well
    if (m_isShown)
        gtk_widget_show( m_widget );
}

unsigned long
wxWindowGTK::GTKConnectWidget(const char *signal, wxGTKCallback callback)
{
    return g_signal_connect(m_widget, signal, callback, this);
}

#ifndef __WXGTK4__

// GSource callback functions for source used to detect new GDK events
extern "C" {
static gboolean source_prepare(GSource*, int*)
{
    return !gs_isNewEvent;
}

static gboolean source_check(GSource*)
{
    // 'check' will only be called if 'prepare' returned false
    return false;
}

static gboolean source_dispatch(GSource*, GSourceFunc, void*)
{
    gs_isNewEvent = true;
    // don't remove this source
    return true;
}
}

#endif // !__WXGTK4__

#ifdef wxGTK_HAS_GESTURES_SUPPORT

// Currently used for Press and Tap gesture only
enum GestureStates
{
    begin  = 1,
    update,
    end
};

enum TrackedGesture
{
    two_finger_tap = 0x0001,
    press_and_tap  = 0x0002,
    horizontal_pan = 0x0004,
    vertical_pan   = 0x0008,
    rotate         = 0x0010,
    zoom           = 0x0020,
};

extern "C" {
static void
horizontal_pan_gesture_end_callback(GtkGesture* gesture, GdkEventSequence* sequence, wxWindow* win)
{
    wxWindowGesturesData* const data = wxWindowGestures::FromObject(win);
    if ( !data )
        return;

    // Ignore the signal if the gesture isn't active
    if ( !(data->m_activeGestures & horizontal_pan) )
    {
        return;
    }

    gdouble x, y;

    if ( !gtk_gesture_get_point(gesture, sequence, &x, &y) )
    {
        return;
    }

    wxPanGestureEvent event(win->GetId());

    event.SetEventObject(win);
    event.SetPosition(wxPoint(wxRound(x), wxRound(y)));
    event.SetGestureEnd();

    data->m_activeGestures &= ~horizontal_pan;

    win->GTKProcessEvent(event);
}
}

extern "C" {
static void
vertical_pan_gesture_end_callback(GtkGesture* gesture, GdkEventSequence* sequence, wxWindow* win)
{
    wxWindowGesturesData* const data = wxWindowGestures::FromObject(win);
    if ( !data )
        return;

    // Ignore the signal if the gesture isn't active
    if ( !(data->m_activeGestures & vertical_pan) )
    {
        return;
    }

    gdouble x, y;

    if ( !gtk_gesture_get_point(gesture, sequence, &x, &y) )
    {
        return;
    }

    wxPanGestureEvent event(win->GetId());

    event.SetEventObject(win);
    event.SetPosition(wxPoint(wxRound(x), wxRound(y)));
    event.SetGestureEnd();

    data->m_activeGestures &= ~vertical_pan;

    win->GTKProcessEvent(event);
}
}

extern "C" {
static void
pan_gesture_callback(GtkGesture* gesture, GtkPanDirection direction, gdouble offset, wxWindow* win)
{
    wxWindowGesturesData* const data = wxWindowGestures::FromObject(win);
    if ( !data )
        return;

    // The function that retrieves the GdkEventSequence (which will further be used to get the gesture point)
    // should be called only when the gestrure is active
    if ( !gtk_gesture_is_active(gesture) )
    {
        return;
    }

    GdkEventSequence* sequence = gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(gesture));

    gdouble x, y;

    if ( !gtk_gesture_get_point(gesture, sequence, &x, &y) )
    {
        return;
    }

    wxPanGestureEvent event(win->GetId());

    event.SetEventObject(win);
    event.SetPosition(wxPoint(wxRound(x), wxRound(y)));

    TrackedGesture gesture_type = direction == GTK_PAN_DIRECTION_UP || direction == GTK_PAN_DIRECTION_DOWN
                                    ? vertical_pan : horizontal_pan;

    if ( !(data->m_activeGestures & (horizontal_pan | vertical_pan)) )
    {
        // For the pan gesture, unlike the others, we only consider the gesture started once we actually receive a pan signal
        data->m_activeGestures |= gesture_type;
        data->m_lastPanOffset = 0;
        event.SetGestureStart();
    }
    else if ( !(data->m_activeGestures & gesture_type) )
    {
        // We shouldn't receive horizontal pan events while a vertical pan is active and vice versa,
        // but let's ignore the event just in case
        return;
    }

    // This is the difference between this and the last pan gesture event in the current sequence
    int delta = wxRound(offset - data->m_lastPanOffset);

    switch ( direction )
    {
        case GTK_PAN_DIRECTION_UP:
            event.SetDelta(wxPoint(0, -delta));
            break;

        case GTK_PAN_DIRECTION_DOWN:
            event.SetDelta(wxPoint(0, delta));
            break;

        case GTK_PAN_DIRECTION_RIGHT:
            event.SetDelta(wxPoint(delta, 0));
            break;

        case GTK_PAN_DIRECTION_LEFT:
            event.SetDelta(wxPoint(-delta, 0));
            break;
    }

    // Update m_lastPanOffset
    data->m_lastPanOffset = offset;

    // Cancel press and tap gesture if it is not active during "pan" signal.
    if( !(data->m_activeGestures & press_and_tap) )
    {
        data->m_allowedGestures &= ~press_and_tap;
    }

    win->GTKProcessEvent(event);
}
}

extern "C" {
static void
zoom_gesture_callback(GtkGesture* gesture, gdouble scale, wxWindow* win)
{
    wxWindowGesturesData* const data = wxWindowGestures::FromObject(win);
    if ( !data )
        return;

    gdouble x, y;

    if ( !gtk_gesture_get_bounding_box_center(gesture, &x, &y) )
    {
        return;
    }

    wxZoomGestureEvent event(win->GetId());

    event.SetEventObject(win);
    event.SetPosition(wxPoint(wxRound(x), wxRound(y)));
    event.SetZoomFactor(scale);

    // Cancel "Two Finger Tap Event" if scale has changed
    if ( wxRound(scale * 1000) != wxRound(data->m_lastScale * 1000) )
    {
        data->m_allowedGestures &= ~two_finger_tap;
    }

    data->m_lastScale = scale;

    // Save this point because the point obtained through gtk_gesture_get_bounding_box_center()
    // in the "end" signal is not a zoom center
    data->m_lastGesturePoint = wxPoint(wxRound(x), wxRound(y));

    win->GTKProcessEvent(event);
}
}

extern "C" {
static void
zoom_gesture_begin_callback(GtkGesture* gesture, GdkEventSequence* WXUNUSED(sequence), wxWindowGTK* win)
{
    wxWindowGesturesData* const data = wxWindowGestures::FromObject(win);
    if ( !data )
        return;

    gdouble x, y;

    if ( !gtk_gesture_get_bounding_box_center(gesture, &x, &y) )
    {
        return;
    }

    wxZoomGestureEvent event(win->GetId());

    event.SetEventObject(win);
    event.SetPosition(wxPoint(wxRound(x), wxRound(y)));
    event.SetGestureStart();

    data->m_activeGestures |= zoom;

    data->m_lastScale = 1;

    // Save this point because the point obtained through gtk_gesture_get_bounding_box_center()
    // in the "end" signal is not a zoom center
    data->m_lastGesturePoint = wxPoint(wxRound(x), wxRound(y));

    win->GTKProcessEvent(event);
}
}

extern "C" {
static void
zoom_gesture_end_callback(GtkGesture* WXUNUSED(gesture), GdkEventSequence* WXUNUSED(sequence), wxWindowGTK* win)
{
    wxWindowGesturesData* const data = wxWindowGestures::FromObject(win);
    if ( !data )
        return;

    // Ignore the signal if the gesture isn't active
    if ( !(data->m_activeGestures & zoom) )
    {
        return;
    }

    wxZoomGestureEvent event(win->GetId());

    event.SetEventObject(win);
    event.SetPosition(data->m_lastGesturePoint);
    event.SetGestureEnd();
    event.SetZoomFactor(data->m_lastScale);

    data->m_activeGestures &= ~zoom;

    win->GTKProcessEvent(event);
}
}

extern "C" {
static void
rotate_gesture_begin_callback(GtkGesture* gesture, GdkEventSequence* WXUNUSED(sequence), wxWindowGTK* win)
{
    wxWindowGesturesData* const data = wxWindowGestures::FromObject(win);
    if ( !data )
        return;

    gdouble x, y;

    if ( !gtk_gesture_get_bounding_box_center(gesture, &x, &y) )
    {
        return;
    }

    wxRotateGestureEvent event(win->GetId());

    event.SetEventObject(win);
    event.SetPosition(wxPoint(wxRound(x), wxRound(y)));
    event.SetGestureStart();

    data->m_activeGestures |= rotate;

    data->m_lastAngleDelta = 0;

    // Save this point because the point obtained through gtk_gesture_get_bounding_box_center()
    // in the "end" signal is not a rotation center
    data->m_lastGesturePoint = wxPoint(wxRound(x), wxRound(y));

    win->GTKProcessEvent(event);
}
}

extern "C" {
static void
rotate_gesture_callback(GtkGesture* gesture, gdouble WXUNUSED(angle), gdouble angle_delta, wxWindowGTK* win)
{
    wxWindowGesturesData* const data = wxWindowGestures::FromObject(win);
    if ( !data )
        return;

    gdouble x, y;

    if ( !gtk_gesture_get_bounding_box_center(gesture, &x, &y) )
    {
        return;
    }

    wxRotateGestureEvent event(win->GetId());

    event.SetEventObject(win);
    event.SetPosition(wxPoint(wxRound(x), wxRound(y)));
    // angle is the absolute orientation of the two fingers, angle_delta is the angle relative to when the gesure started
    event.SetRotationAngle(angle_delta);

    // Save the angle to set it when the gesture ends.
    data->m_lastAngleDelta = angle_delta;

    // Save this point because the point obtained through gtk_gesture_get_bounding_box_center()
    // in the "end" signal is not a rotation center
    data->m_lastGesturePoint = wxPoint(wxRound(x), wxRound(y));

    win->GTKProcessEvent(event);
}
}

extern "C" {
static void
rotate_gesture_end_callback(GtkGesture* WXUNUSED(gesture), GdkEventSequence* WXUNUSED(sequence), wxWindowGTK* win)
{
    wxWindowGesturesData* const data = wxWindowGestures::FromObject(win);
    if ( !data )
        return;

    // Ignore the signal if the gesture isn't active
    if ( !(data->m_activeGestures & rotate) )
    {
        return;
    }

    wxRotateGestureEvent event(win->GetId());

    event.SetEventObject(win);
    event.SetPosition(data->m_lastGesturePoint);
    event.SetGestureEnd();
    event.SetRotationAngle(data->m_lastAngleDelta);

    data->m_activeGestures &= ~rotate;

    win->GTKProcessEvent(event);
}
}

extern "C" {
static void
long_press_gesture_callback(GtkGesture* WXUNUSED(gesture), gdouble x, gdouble y, wxWindowGTK* win)
{
    wxLongPressEvent event(win->GetId());

    event.SetEventObject(win);
    event.SetPosition(wxPoint(wxRound(x), wxRound(y)));
    event.SetGestureStart();
    event.SetGestureEnd();

    win->GTKProcessEvent(event);
}
}

// Raw touch events (GdkEventTouch and the "touch-event" signal) don't
// exist under GTK4; porting this needs the same event-controller redesign
// as the rest of the input pipeline -- see
// docs/gtk/gtk4-phase3-input-model-design.md, not yet implemented.
#ifndef __WXGTK4__

static void
wxEmitTwoFingerTapEvent(GdkEventTouch* gdk_event, wxWindow* win)
{
    wxTwoFingerTapEvent event(win->GetId());

    event.SetEventObject(win);

    wxWindowGesturesData* const data = wxWindowGestures::FromObject(win);
    if ( !data )
        return;

    double lastX = data->m_lastTouchPoint.x;
    double lastY = data->m_lastTouchPoint.y;

    // Calculate smaller of x coordinate between 2 touches
    double left = lastX <= gdk_event->x ? lastX : gdk_event->x;

    // Calculate smaller of y coordinate between 2 touches
    double up = lastY <= gdk_event->y ? lastY : gdk_event->y;

    // Calculate gesture point .i.e center of the box formed by two touches
    double x = left + abs(lastX - gdk_event->x)/2;
    double y = up + abs(lastY - gdk_event->y)/2;

    event.SetPosition(wxPoint(wxRound(x), wxRound(y)));
    event.SetGestureStart();
    event.SetGestureEnd();

    win->GTKProcessEvent(event);
}

static void
wxEmitPressAndTapEvent(GdkEventTouch* gdk_event, wxWindow* win)
{
    wxPressAndTapEvent event(win->GetId());

    event.SetEventObject(win);

    wxWindowGesturesData* const data = wxWindowGestures::FromObject(win);
    if ( !data )
        return;

    switch ( data->m_gestureState )
    {
        case begin:
            event.SetGestureStart();
            break;

        case update:
            // Update touch point as the touch corresponding to "press" is moving
            if ( data->m_touchSequence == gdk_event->sequence )
            {
                data->m_lastTouchPoint.x = int(gdk_event->x);
                data->m_lastTouchPoint.y = int(gdk_event->y);
            }
            break;

        case end:
            event.SetGestureEnd();
            break;
    }

    event.SetPosition(data->m_lastTouchPoint);

    win->GTKProcessEvent(event);
}

namespace
{

template <typename EventType>
void
wxEventMouseFromEventTouch(EventType* event,
                           const GdkEventTouch* gdk_event_touch)
{
    event->window = gdk_event_touch->window;
    event->send_event = gdk_event_touch->send_event;
    event->time = gdk_event_touch->time;
    event->x = gdk_event_touch->x;
    event->y = gdk_event_touch->y;
    event->axes = gdk_event_touch->axes;
    event->state = gdk_event_touch->state;
    event->device = gdk_event_touch->device;
    event->x_root = gdk_event_touch->x_root;
    event->y_root = gdk_event_touch->y_root;
}

void
wxEventButtonFromEventTouch(GdkEventButton* gdk_event_button,
                            const GdkEventTouch* gdk_event_touch)
{
    wxEventMouseFromEventTouch(gdk_event_button, gdk_event_touch);

    gdk_event_button->type = GDK_BUTTON_PRESS;
    gdk_event_button->button = 1; // left button
}

void
wxEventMotionFromEventTouch(GdkEventMotion* gdk_event_motion,
                            const GdkEventTouch* gdk_event_touch)
{
    wxEventMouseFromEventTouch(gdk_event_motion, gdk_event_touch);

    gdk_event_motion->type = GDK_MOTION_NOTIFY;
    gdk_event_motion->is_hint = true;
}

void
wxEmulateLeftDownEvent(GtkWidget* widget, GdkEventTouch* gdk_event, wxWindow* win)
{
    GdkEventButton gdk_event_button;

    if (!gdk_event->emulating_pointer)
        return;

    wxEventButtonFromEventTouch(&gdk_event_button, gdk_event);
    wxGTKImpl::WindowButtonPressCallback(widget,
                                         &gdk_event_button,
                                         win, true);
}

void
wxEmulateLeftUpEvent(GtkWidget* widget,GdkEventTouch* gdk_event, wxWindow* win)
{
    GdkEventButton gdk_event_button;

    if (!gdk_event->emulating_pointer)
        return;

    wxEventButtonFromEventTouch(&gdk_event_button, gdk_event);
    wxGTKImpl::WindowButtonReleaseCallback(widget,
                                           &gdk_event_button,
                                           win, true);
}

void
wxEmulateMotionEvent(GtkWidget* widget, GdkEventTouch* gdk_event, wxWindow* win)
{
    GdkEventMotion gdk_event_motion;

    if (!gdk_event->emulating_pointer)
        return;

    wxEventMotionFromEventTouch(&gdk_event_motion, gdk_event);
    wxGTKImpl::WindowMotionCallback(widget,
                                    &gdk_event_motion,
                                    win, true);
}

} // anonymous namespace

extern "C" {
static gboolean
touch_callback(GtkWidget* widget, GdkEventTouch* gdk_event, wxWindow* win)
{
    wxWindowGesturesData* const data = wxWindowGestures::FromObject(win);
    if ( !data )
        return false;

    if ( data->m_rawTouchEvents)
    {
        wxEventType type;

        switch(gdk_event->type)
        {
        case GDK_TOUCH_BEGIN:
            type = wxEVT_TOUCH_BEGIN;
            break;

        case GDK_TOUCH_UPDATE:
            type = wxEVT_TOUCH_MOVE;
            break;

        case GDK_TOUCH_END:
            type = wxEVT_TOUCH_END;
            break;

        case GDK_TOUCH_CANCEL:
            type = wxEVT_TOUCH_CANCEL;
            break;

        default:
            type = wxEVT_NULL;
        }
        if (type != wxEVT_NULL)
        {
            wxMultiTouchEvent event(win->GetId(), type);

            event.SetEventObject(win);
            event.SetPosition(wxPoint2DDouble(gdk_event->x, gdk_event->y));
            event.SetSequenceId(wxTouchSequenceId(gdk_event->sequence));
            event.SetPrimary(gdk_event->emulating_pointer);

            if (win->GTKProcessEvent(event))
                return true;
        }
    }

    switch ( gdk_event->type )
    {
        case GDK_TOUCH_BEGIN:
            wxEmulateLeftDownEvent(widget, gdk_event, win);
            data->m_touchCount++;

            data->m_allowedGestures &= ~two_finger_tap;

            if ( data->m_touchCount == 1 )
            {
                data->m_lastTouchTime = gdk_event->time;
                data->m_lastTouchPoint.x = int(gdk_event->x);
                data->m_lastTouchPoint.y = int(gdk_event->y);

                // Save the sequence which identifies touch corresponding to "press"
                data->m_touchSequence = gdk_event->sequence;

                // "Press and Tap Event" may occur in future
                data->m_allowedGestures |= press_and_tap;
            }

            // Check if two fingers are placed together .i.e difference between their time stamps is <= 200 milliseconds
            else if ( data->m_touchCount == 2 && gdk_event->time - data->m_lastTouchTime <= wxTwoFingerTimeInterval )
            {
                // "Two Finger Tap Event" may be possible in the future
                data->m_allowedGestures |= two_finger_tap;

                // Cancel "Press and Tap Event"
                data->m_allowedGestures &= ~press_and_tap;
            }
            break;

        case GDK_TOUCH_UPDATE:
            wxEmulateMotionEvent(widget, gdk_event, win);
            // If press and tap gesture is active and touch corresponding to that gesture is moving
            if ( (data->m_activeGestures & press_and_tap) && gdk_event->sequence == data->m_touchSequence )
            {
                data->m_gestureState = update;
                wxEmitPressAndTapEvent(gdk_event, win);
            }
            break;

        case GDK_TOUCH_END:
        case GDK_TOUCH_CANCEL:
            wxEmulateLeftUpEvent(widget, gdk_event, win);
            data->m_touchCount--;

            if ( data->m_touchCount == 1 )
            {
                data->m_lastTouchTime = gdk_event->time;

                // If the touch corresponding to "press" is present and "tap" is produced by some ather touch
                if ( (data->m_allowedGestures & press_and_tap) && gdk_event->sequence != data->m_touchSequence )
                {
                    // Press and Tap gesture becomes active now
                    if ( !(data->m_activeGestures & press_and_tap) )
                    {
                        data->m_gestureState = begin;
                        data->m_activeGestures |= press_and_tap;
                    }

                    else
                    {
                        data->m_gestureState = update;
                    }

                    wxEmitPressAndTapEvent(gdk_event, win);
                }
            }

            // Check if "Two Finger Tap Event" is possible and both the fingers have been lifted up together
            else if ( (data->m_allowedGestures & two_finger_tap) && !data->m_touchCount
                      && gdk_event->time - data->m_lastTouchTime <= wxTwoFingerTimeInterval )
            {
                // Process Two Finger Tap Event
                wxEmitTwoFingerTapEvent(gdk_event, win);
            }

            // If the gesture was active and the touch corresponding to "press" is no longer on the screen
            if ( (data->m_activeGestures & press_and_tap) && gdk_event->sequence == data->m_touchSequence )
            {
                data->m_gestureState = end;

                data->m_activeGestures &= ~press_and_tap;

                data->m_allowedGestures &= ~press_and_tap;

                wxEmitPressAndTapEvent(gdk_event, win);
            }
            break;

        default:
        break;
    }

    return true;
}
}
#endif // !__WXGTK4__

void wxWindowGesturesData::Reinit(wxWindowGTK* win,
                                  GtkWidget *widget,
                                  int eventsMask)
{
    m_touchCount = 0;
    m_lastTouchTime = 0;
    m_gestureState = 0;
    m_allowedGestures = 0;
    m_activeGestures = 0;
    m_touchSequence = nullptr;
    m_rawTouchEvents = false;
    m_lastPanOffset = 0;
    m_lastScale = 1;
    m_lastAngleDelta = 0;

    if ( eventsMask & wxTOUCH_VERTICAL_PAN_GESTURE )
    {
        eventsMask &= ~wxTOUCH_VERTICAL_PAN_GESTURE;

        #ifdef __WXGTK4__
        m_vertical_pan_gesture = gtk_gesture_pan_new(GTK_ORIENTATION_VERTICAL);
        gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(m_vertical_pan_gesture));
#else
        m_vertical_pan_gesture = gtk_gesture_pan_new(widget, GTK_ORIENTATION_VERTICAL);
#endif

        gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER(m_vertical_pan_gesture), GTK_PHASE_TARGET);

        g_signal_connect (m_vertical_pan_gesture, "pan",
                          G_CALLBACK(pan_gesture_callback), win);
        g_signal_connect (m_vertical_pan_gesture, "end",
                          G_CALLBACK(vertical_pan_gesture_end_callback), win);
        g_signal_connect (m_vertical_pan_gesture, "cancel",
                          G_CALLBACK(vertical_pan_gesture_end_callback), win);
    }
    else
    {
        m_vertical_pan_gesture = nullptr;
    }

    if ( eventsMask & wxTOUCH_HORIZONTAL_PAN_GESTURE )
    {
        eventsMask &= ~wxTOUCH_HORIZONTAL_PAN_GESTURE;

        #ifdef __WXGTK4__
        m_horizontal_pan_gesture = gtk_gesture_pan_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(m_horizontal_pan_gesture));
#else
        m_horizontal_pan_gesture = gtk_gesture_pan_new(widget, GTK_ORIENTATION_HORIZONTAL);
#endif

        // Pan signals are also generated in case of "left mouse down + mouse move". This can be disabled by
        // calling gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(m_horizontal_pan_gesture), TRUE) and
        // gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(verticaal_pan_gesture), TRUE) which will allow
        // pan signals only for Touch events.

        gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER(m_horizontal_pan_gesture), GTK_PHASE_TARGET);

        g_signal_connect (m_horizontal_pan_gesture, "pan",
                          G_CALLBACK(pan_gesture_callback), win);
        g_signal_connect (m_horizontal_pan_gesture, "end",
                          G_CALLBACK(horizontal_pan_gesture_end_callback), win);
        g_signal_connect (m_horizontal_pan_gesture, "cancel",
                          G_CALLBACK(horizontal_pan_gesture_end_callback), win);
    }
    else
    {
        m_horizontal_pan_gesture = nullptr;
    }

    if ( eventsMask & wxTOUCH_ZOOM_GESTURE )
    {
        eventsMask &= ~wxTOUCH_ZOOM_GESTURE;

        #ifdef __WXGTK4__
        m_zoom_gesture = gtk_gesture_zoom_new();
        gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(m_zoom_gesture));
#else
        m_zoom_gesture = gtk_gesture_zoom_new(widget);
#endif

        gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER(m_zoom_gesture), GTK_PHASE_TARGET);

        g_signal_connect (m_zoom_gesture, "begin",
                          G_CALLBACK(zoom_gesture_begin_callback), win);
        g_signal_connect (m_zoom_gesture, "scale-changed",
                          G_CALLBACK(zoom_gesture_callback), win);
        g_signal_connect (m_zoom_gesture, "end",
                          G_CALLBACK(zoom_gesture_end_callback), win);
        g_signal_connect (m_zoom_gesture, "cancel",
                          G_CALLBACK(zoom_gesture_end_callback), win);
    }
    else
    {
        m_zoom_gesture = nullptr;
    }

    if ( eventsMask & wxTOUCH_ROTATE_GESTURE )
    {
        eventsMask &= ~wxTOUCH_ROTATE_GESTURE;

        #ifdef __WXGTK4__
        m_rotate_gesture = gtk_gesture_rotate_new();
        gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(m_rotate_gesture));
#else
        m_rotate_gesture = gtk_gesture_rotate_new(widget);
#endif

        gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER(m_rotate_gesture), GTK_PHASE_TARGET);

        g_signal_connect (m_rotate_gesture, "begin",
                          G_CALLBACK(rotate_gesture_begin_callback), win);
        g_signal_connect (m_rotate_gesture, "angle-changed",
                          G_CALLBACK(rotate_gesture_callback), win);
        g_signal_connect (m_rotate_gesture, "end",
                          G_CALLBACK(rotate_gesture_end_callback), win);
        g_signal_connect (m_rotate_gesture, "cancel",
                          G_CALLBACK(rotate_gesture_end_callback), win);
    }
    else
    {
        m_rotate_gesture = nullptr;
    }

    if ( eventsMask & wxTOUCH_PRESS_GESTURES )
    {
        eventsMask &= ~wxTOUCH_PRESS_GESTURES;

        #ifdef __WXGTK4__
        m_long_press_gesture = gtk_gesture_long_press_new();
        gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(m_long_press_gesture));
#else
        m_long_press_gesture = gtk_gesture_long_press_new(widget);
#endif

        // "pressed" signal is also generated when left mouse is down for some minimum duration of time.
        // This can be disable by calling gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(m_long_press_gesture), TRUE)
        // which will allow "pressed" signal only for Touch events.

        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(m_long_press_gesture), GTK_PHASE_TARGET);

        g_signal_connect (m_long_press_gesture, "pressed",
                          G_CALLBACK(long_press_gesture_callback), win);
    }
    else
    {
        m_long_press_gesture = nullptr;
    }

    if ( eventsMask & wxTOUCH_RAW_EVENTS )
    {
#ifndef __WXGTK4__
        if ( gtk_check_version(3, 4, 0) == nullptr )
        {
            gtk_widget_add_events(widget, GDK_TOUCH_MASK);
        }
#endif // !__WXGTK4__
        // Raw touch events (GdkEventTouch, the "touch-event" signal) need
        // porting to GTK4's event-controller model along with the rest of
        // the input pipeline -- see
        // docs/gtk/gtk4-phase3-input-model-design.md, not yet implemented.

        eventsMask &= ~wxTOUCH_RAW_EVENTS;
        m_rawTouchEvents = true;
    }

#ifndef __WXGTK4__
    // GDK_TOUCHPAD_GESTURE_MASK was added in 3.18, but we can just define it
    // ourselves if we use an earlier version when compiling.
#if !GTK_CHECK_VERSION(3,18,0)
    #define GDK_TOUCHPAD_GESTURE_MASK (1 << 24)
#endif
    if ( gtk_check_version(3, 18, 0) == nullptr )
    {
        gtk_widget_add_events(widget, GDK_TOUCHPAD_GESTURE_MASK);
    }
#endif // !__WXGTK4__

    wxASSERT_MSG( eventsMask == 0, "Unknown touch event mask bit specified" );

#ifndef __WXGTK4__
    g_signal_connect (widget, "touch-event",
                      G_CALLBACK(touch_callback), win);
#endif // !__WXGTK4__
}

void wxWindowGesturesData::Free()
{
    g_clear_object(&m_vertical_pan_gesture);
    g_clear_object(&m_horizontal_pan_gesture);
    g_clear_object(&m_zoom_gesture);
    g_clear_object(&m_rotate_gesture);
    g_clear_object(&m_long_press_gesture);
    m_rawTouchEvents = false;

    // We don't current remove GDK_TOUCHPAD_GESTURE_MASK as this can't be done
    // for a window as long as it's realized, and this might still be the case
    // if we're called from EnableTouchEvents(wxTOUCH_NONE) and not from the
    // dtor, but it shouldn't really be a problem.
}

#endif // wxGTK_HAS_GESTURES_SUPPORT

// This method must be always defined for GTK+ 3 as it's declared in the
// header, where we can't (easily) test for wxGTK_HAS_GESTURES_SUPPORT.
#ifdef __WXGTK3__

bool wxWindowGTK::EnableTouchEvents(int eventsMask)
{
#ifdef wxGTK_HAS_GESTURES_SUPPORT
    // Check if gestures support is also available during run-time.
    if ( gtk_check_version(3, 14, 0) == nullptr )
    {
        wxWindowGesturesData* const dataOld = wxWindowGestures::FromObject(static_cast<wxWindow*>(this));

        if ( eventsMask == wxTOUCH_NONE )
        {
            // Reset the gestures data used by this object, but don't destroy
            // it, as we could be called from an event handler, in which case
            // this object could be still used after the event handler returns.
            if ( dataOld )
                dataOld->Free();
        }
        else
        {
            GtkWidget* const widget = GetConnectWidget();

            if ( dataOld )
            {
                dataOld->Reinit(this, widget, eventsMask);
            }
            else
            {
                wxWindowGesturesData* const
                    dataNew = new wxWindowGesturesData(this, widget, eventsMask);
                wxWindowGestures::StoreForObject(static_cast<wxWindow*>(this), dataNew);
            }
        }

        return true;
    }
#endif // wxGTK_HAS_GESTURES_SUPPORT

    return wxWindowBase::EnableTouchEvents(eventsMask);
}

#endif // __WXGTK3__

void wxWindowGTK::ConnectWidget( GtkWidget *widget )
{
#ifndef __WXGTK4__
    // This source only exists to tell EventAlreadyProcessed() when a new GDK
    // event has arrived, and that function is GTK3 only: the GTK4 input path
    // stops events propagating by claiming the gesture instead.
    static bool isSourceAttached;
    if (!isSourceAttached)
    {
        // attach GSource to detect new GDK events
        isSourceAttached = true;
        static GSourceFuncs funcs = {
            source_prepare, source_check, source_dispatch,
            nullptr, nullptr, nullptr
        };
        GSource* source = g_source_new(&funcs, sizeof(GSource));
        // priority slightly higher than GDK_PRIORITY_EVENTS
        g_source_set_priority(source, GDK_PRIORITY_EVENTS - 1);
        g_source_attach(source, nullptr);
        g_source_unref(source);
    }
#endif // !__WXGTK4__

    // When we're called for the main widget itself (but not when connecting
    // events for some other widget, such as individual radio buttons in
    // wxRadioBox::Create()), connect to m_focusWidget for the keyboard events
    // instead, as it should be used for everything keyboard input-related.
    GtkWidget* const focusWidget = widget == m_widget && m_focusWidget
                                    ? m_focusWidget
                                    : widget;
#ifdef __WXGTK4__
    // GTK4 delivers key input through a controller attached to the widget
    // rather than through per-widget signals carrying a GdkEventKey.
    {
        GtkEventController* const keyController = gtk_event_controller_key_new();

        // A native text-entry widget handles key presses itself and claims
        // them, and it does so on the widget the event is actually delivered
        // to -- the GtkText inside a GtkEntry, not the entry this controller
        // sits on. In the default bubble phase the controller would therefore
        // never fire for it, and wx would generate neither wxEVT_KEY_DOWN nor
        // wxEVT_CHAR while still seeing wxEVT_KEY_UP, which is not claimed.
        // The capture phase runs from the top level down to the target, so it
        // reaches wx first, which is what GTK3's g_signal_connect() on
        // "key_press_event" gave us: a look at the press before the widget's
        // own handling of it.
        //
        // Only for these widgets, deliberately. Using the capture phase for
        // every window would let the top level's controller claim every key
        // event before it ever reached the focused control.
        if ( GTK_IS_EDITABLE(focusWidget) || GTK_IS_TEXT_VIEW(focusWidget) )
        {
            gtk_event_controller_set_propagation_phase(keyController,
                                                       GTK_PHASE_CAPTURE);
        }

        g_signal_connect (keyController, "key-pressed",
                          G_CALLBACK (wx_gtk_key_pressed_callback), this);
        g_signal_connect (keyController, "key-released",
                          G_CALLBACK (wx_gtk_key_released_callback), this);
        // The widget takes ownership of the controller.
        gtk_widget_add_controller(focusWidget, keyController);
    }
#else
    g_signal_connect (focusWidget, "key_press_event",
                      G_CALLBACK (gtk_window_key_press_callback), this);
    g_signal_connect (focusWidget, "key_release_event",
                      G_CALLBACK (gtk_window_key_release_callback), this);
#endif

#ifndef __WXGTK4__
    g_signal_connect (widget, "button_press_event",
                      G_CALLBACK (gtk_window_button_press_callback), this);
    g_signal_connect (widget, "button_release_event",
                      G_CALLBACK (gtk_window_button_release_callback), this);
    g_signal_connect (widget, "motion_notify_event",
                      G_CALLBACK (gtk_window_motion_notify_callback), this);
#else
    // Motion and enter/leave all come from one GtkEventControllerMotion; the
    // coordinates it hands over are already widget-relative.
    {
        // Buttons: one GtkGestureClick, set to react to any button rather
        // than only the primary one, since wx reports middle/right/aux
        // clicks too. It stays in the default BUBBLE phase and claims the
        // sequence only when wx handles the press -- see the comment on
        // wx_gtk_button_pressed_callback() for why.
        GtkGesture* const clickGesture = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(clickGesture), 0);
        g_signal_connect (clickGesture, "pressed",
                          G_CALLBACK (wx_gtk_button_pressed_callback), this);
        g_signal_connect (clickGesture, "released",
                          G_CALLBACK (wx_gtk_button_released_callback), this);
        gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(clickGesture));

        GtkEventController* const motionController = gtk_event_controller_motion_new();
        g_signal_connect (motionController, "motion",
                          G_CALLBACK (wx_gtk_motion_callback), this);
        g_signal_connect (motionController, "enter",
                          G_CALLBACK (wx_gtk_enter_callback), this);
        g_signal_connect (motionController, "leave",
                          G_CALLBACK (wx_gtk_leave_callback), this);
        gtk_widget_add_controller(widget, motionController);
    }
#endif

#ifdef __WXGTK4__
    // One scroll controller per widget that used to carry a "scroll_event"
    // handler. BOTH_AXES because wx wants horizontal and vertical wheel
    // events alike, and the axis-swap logic for wheeling over an embedded
    // scrollbar needs to see whichever axis the wheel actually reported.
    {
        GtkWidget* const scrollWidgets[] = {
            widget,
            GTK_WIDGET(m_scrollBar[ScrollDir_Horz]),
            GTK_WIDGET(m_scrollBar[ScrollDir_Vert])
        };
        for ( size_t n = 0; n < WXSIZEOF(scrollWidgets); n++ )
        {
            if ( !scrollWidgets[n] )
                continue;

            GtkEventController* const scrollController =
                gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
            g_signal_connect(scrollController, "scroll",
                             G_CALLBACK(wx_gtk_scroll_callback), this);
            gtk_widget_add_controller(scrollWidgets[n], scrollController);
        }
    }
#else
    g_signal_connect(widget, "scroll_event", G_CALLBACK(scroll_event), this);
    wxGtkScrollbar* range = m_scrollBar[ScrollDir_Horz];
    if (range)
        g_signal_connect(range, "scroll_event", G_CALLBACK(scroll_event), this);
    range = m_scrollBar[ScrollDir_Vert];
    if (range)
        g_signal_connect(range, "scroll_event", G_CALLBACK(scroll_event), this);
#endif

#ifdef __WXGTK4__
    {
        GtkEventController* const key = gtk_event_controller_key_new();
        g_signal_connect (key, "key-pressed",
                          G_CALLBACK (wxgtk_window_context_menu_key), this);
        gtk_widget_add_controller(widget, key);
    }
#else
    g_signal_connect (widget, "popup_menu",
                     G_CALLBACK (wxgtk_window_popup_menu_callback), this);
#endif
#ifndef __WXGTK4__
    // Under GTK4 these are signals of the motion controller connected above.
    g_signal_connect (widget, "enter_notify_event",
                      G_CALLBACK (gtk_window_enter_callback), this);
    g_signal_connect (widget, "leave_notify_event",
                      G_CALLBACK (gtk_window_leave_callback), this);
#endif

#ifdef __WXGTK3__
    g_signal_connect (widget, "notify::scale-factor",
                      G_CALLBACK (gtk_window_scale_factor_notify), this);
#endif // __WXGTK3__
}

void wxWindowGTK::DoMoveWindow(int x, int y, int width, int height)
{
    GtkWidget* parent = gtk_widget_get_parent(m_widget);
    wxPizza* pizza = nullptr;
    if (WX_IS_PIZZA(parent))
    {
        pizza = WX_PIZZA(parent);
        pizza->move(m_widget, x, y, width, height);
    }

#ifdef __WXGTK3__
    // With GTK3, gtk_widget_queue_resize() is ignored while a size-allocate
    // is in progress. This situation is common in wxWidgets, since
    // size-allocate can generate wxSizeEvent and size event handlers often
    // call SetSize(), directly or indirectly. It should be fine to call
    // gtk_widget_size_allocate() immediately in this case.
    if (g_inSizeAllocate)
    {
        // obligatory size request before size allocate to avoid GTK3 warnings
        GtkRequisition req;
        gtk_widget_get_preferred_size(m_widget, &req, nullptr);

        if (pizza)
            pizza->size_allocate_child(m_widget, x, y, width, height);
        else
        {
            GtkAllocation a = { x, y, width, height };
            #ifdef __WXGTK4__
            gtk_widget_size_allocate(m_widget, &a, -1);
#else
            gtk_widget_size_allocate(m_widget, &a);
#endif
        }
#if GTK_CHECK_VERSION(3,8,0)
        if (wx_is_at_least_gtk3(8))
        {
            // Defer gtk_widget_set_size_request(), as doing it now
            // causes GTK's sizing state to become inconsistent
            gs_setSizeRequestList = g_slist_prepend(gs_setSizeRequestList, this);
            g_object_add_weak_pointer(G_OBJECT(m_widget), &gs_setSizeRequestList->data);
            return;
        }
#endif
    }
#endif // __WXGTK3__

    if (pizza)
        gtk_widget_queue_resize(m_widget);
    else
        gtk_widget_set_size_request(m_widget, width, height);
}

void wxWindowGTK::ConstrainSize()
{
    const wxSize minSize = GetMinSize();
    const wxSize maxSize = GetMaxSize();
    if (minSize.x > 0 && m_width  < minSize.x) m_width  = minSize.x;
    if (minSize.y > 0 && m_height < minSize.y) m_height = minSize.y;
    if (maxSize.x > 0 && m_width  > maxSize.x) m_width  = maxSize.x;
    if (maxSize.y > 0 && m_height > maxSize.y) m_height = maxSize.y;
}

void wxWindowGTK::DoSetSize( int x, int y, int width, int height, int sizeFlags )
{
    wxCHECK_RET(m_widget, "invalid window");

    int scrollX = 0, scrollY = 0;
    GtkWidget* parent = gtk_widget_get_parent(m_widget);
    if (WX_IS_PIZZA(parent))
    {
        wxPizza* pizza = WX_PIZZA(parent);
        scrollX = pizza->m_scroll_x;
        scrollY = pizza->m_scroll_y;
    }
    if (x != -1 || (sizeFlags & wxSIZE_ALLOW_MINUS_ONE))
        x += scrollX;
    else
        x = m_x;
    if (y != -1 || (sizeFlags & wxSIZE_ALLOW_MINUS_ONE))
        y += scrollY;
    else
        y = m_y;

    // calculate the best size if we should auto size the window
    if ( ((sizeFlags & wxSIZE_AUTO_WIDTH) && width == -1) ||
         ((sizeFlags & wxSIZE_AUTO_HEIGHT) && height == -1) )
    {
        const wxSize sizeBest = GetBestSize();
        if ( (sizeFlags & wxSIZE_AUTO_WIDTH) && width == -1 )
            width = sizeBest.x;
        if ( (sizeFlags & wxSIZE_AUTO_HEIGHT) && height == -1 )
            height = sizeBest.y;
    }

    if (width == -1)
        width = m_width;
    if (height == -1)
        height = m_height;

    const bool sizeChange = m_width != width || m_height != height;
    const bool positionChange = m_x != x || m_y != y;

    if (sizeChange)
        m_useCachedClientSize = false;
    if (positionChange)
        m_isGtkPositionValid = false;

    if (sizeChange || positionChange)
    {
        m_x = x;
        m_y = y;
        m_width = width;
        m_height = height;

        /* the default button has a border around it */
#ifdef __WXGTK4__
        // gtk_widget_get_can_default() and the "default_border" style property
        // are both gone: GTK4 draws the default-button indication from CSS on
        // the widget itself, within its own allocation, so there is no extra
        // border for wx to account for here.
        if (false)
        {
#else
        if (gtk_widget_get_can_default(m_widget))
        {
#endif
            GtkBorder *default_border = nullptr;
#ifndef __WXGTK4__
            gtk_widget_style_get( m_widget, "default_border", &default_border, nullptr );
#endif
            if (default_border)
            {
                x -= default_border->left;
                y -= default_border->top;
                width += default_border->left + default_border->right;
                height += default_border->top + default_border->bottom;
                gtk_border_free( default_border );
            }
        }

        DoMoveWindow(x, y, width, height);
    }

    if (((sizeChange
#ifdef __WXGTK3__
                     || m_needSizeEvent
#endif
                                       ) && !m_nativeSizeEvent) || (sizeFlags & wxSIZE_FORCE_EVENT))
    {
#ifdef __WXGTK3__
        m_needSizeEvent = false;
#endif
        // update these variables to keep size_allocate handler
        // from sending another size event for this change
        DoGetClientSize(&m_clientWidth, &m_clientHeight);

        wxSizeEvent event( wxSize(m_width,m_height), GetId() );
        event.SetEventObject( this );
        HandleWindowEvent( event );
    }
}

bool wxWindowGTK::GTKShowFromOnIdle()
{
    if (m_isShown && m_showOnIdle && !gtk_widget_get_visible(m_widget))
    {
        GtkAllocation alloc;
        alloc.x = m_x;
        alloc.y = m_y;
        alloc.width = m_width;
        alloc.height = m_height;
        #ifdef __WXGTK4__
            gtk_widget_size_allocate( m_widget, &alloc , -1);
#else
            gtk_widget_size_allocate( m_widget, &alloc );
#endif
        gtk_widget_show( m_widget );
        wxShowEvent eventShow(GetId(), true);
        eventShow.SetEventObject(this);
        HandleWindowEvent(eventShow);
        m_showOnIdle = false;
        return true;
    }

    return false;
}

#ifdef __WINDOWS__
WXHWND wxWindowGTK::GTKGetWin32Handle() const
{
    auto gtkWindow{gtk_widget_get_window(m_widget)};

    // If widget is not realized, there's no underlying handle to get.
    if (!gtkWindow)
        return nullptr;

    return reinterpret_cast<WXHWND>(gdk_win32_window_get_handle(gtkWindow));
}
#endif // __WINDOWS__

void wxWindowGTK::OnInternalIdle()
{
    if ( gs_deferredFocusOut )
        gs_deferredFocusOut->GTKHandleDeferredFocusOut();

    // Check if we have to show window now
    if (GTKShowFromOnIdle()) return;

    if ( m_dirtyTabOrder )
    {
        m_dirtyTabOrder = false;
        RealizeTabOrder();
    }

    wxWindowBase::OnInternalIdle();
}

void wxWindowGTK::DoGetSize( int *width, int *height ) const
{
    if (width) (*width) = m_width;
    if (height) (*height) = m_height;
}

void wxWindowGTK::DoSetClientSize( int width, int height )
{
    wxCHECK_RET( (m_widget != nullptr), wxT("invalid window") );

    const wxSize size = GetSize();
    const wxSize clientSize = GetClientSize();
    SetSize(width + (size.x - clientSize.x), height + (size.y - clientSize.y));
}

void wxWindowGTK::DoGetClientSize( int *width, int *height ) const
{
    wxCHECK_RET( (m_widget != nullptr), wxT("invalid window") );

    if (m_useCachedClientSize)
    {
        if (width)  *width  = m_clientWidth;
        if (height) *height = m_clientHeight;
        return;
    }

    int w = m_width;
    int h = m_height;

    if ( m_wxwindow )
    {
        // if window is scrollable, account for scrollbars
        if ( GTK_IS_SCROLLED_WINDOW(m_widget) )
        {
            GtkPolicyType policy[ScrollDir_Max];
            gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(m_widget),
                                           &policy[ScrollDir_Horz],
                                           &policy[ScrollDir_Vert]);

            // get scrollbar spacing the same way the GTK-private function
            // _gtk_scrolled_window_get_scrollbar_spacing() does it
#ifdef __WXGTK4__
            // Both the class field and the style property are gone under GTK4,
            // where a scrolled window overlays its scrollbars rather than
            // reserving space beside them, so there is no spacing to add.
            const int scrollbar_spacing = 0;
#else
            int scrollbar_spacing =
                GTK_SCROLLED_WINDOW_GET_CLASS(m_widget)->scrollbar_spacing;
            if (scrollbar_spacing < 0)
            {
                gtk_widget_style_get(
                    m_widget, "scrollbar-spacing", &scrollbar_spacing, nullptr);
            }
#endif

            for ( int i = 0; i < ScrollDir_Max; i++ )
            {
                // don't account for the scrollbars we don't have
                wxGtkScrollbar* const range = m_scrollBar[i];
                if ( !range )
                    continue;

                // nor for the ones we have but don't current show
                switch ( policy[i] )
                {
#if GTK_CHECK_VERSION(3,16,0)
                    case GTK_POLICY_EXTERNAL:
#endif
                    case GTK_POLICY_NEVER:
                        // never shown so doesn't take any place
                        continue;

                    case GTK_POLICY_ALWAYS:
                        // no checks necessary
                        break;

                    case GTK_POLICY_AUTOMATIC:
                        // may be shown or not, check
                        GtkAdjustment *adj = wxGtkScrollbarGetAdjustment(range);
                        if (gtk_adjustment_get_upper(adj) <= gtk_adjustment_get_page_size(adj))
                            continue;
                }

                GtkRequisition req;
#ifdef __WXGTK3__
                GtkWidget* widget = GTK_WIDGET(range);
                if (i == ScrollDir_Horz)
                {
                    if (height)
                    {
                        gtk_widget_get_preferred_height(widget, nullptr, &req.height);
                        h -= req.height + scrollbar_spacing;
                    }
                }
                else
                {
                    if (width)
                    {
                        gtk_widget_get_preferred_width(widget, nullptr, &req.width);
                        w -= req.width + scrollbar_spacing;
                    }
                }
#else // !__WXGTK3__
                gtk_widget_size_request(GTK_WIDGET(range), &req);
                if (i == ScrollDir_Horz)
                    h -= req.height + scrollbar_spacing;
                else
                    w -= req.width + scrollbar_spacing;
#endif // !__WXGTK3__
            }
        }

        const wxSize sizeBorders = GetWindowBorderSize();
        w -= sizeBorders.x;
        h -= sizeBorders.y;

        if (w < 0)
            w = 0;
        if (h < 0)
            h = 0;
    }

    if (width) *width = w;
    if (height) *height = h;
}

wxSize wxWindowGTK::GetWindowBorderSize() const
{
    if ( !m_wxwindow )
        return wxWindowBase::GetWindowBorderSize();

    GtkBorder border;
    WX_PIZZA(m_wxwindow)->get_border(border);
    return wxSize(border.left + border.right, border.top + border.bottom);
}

void wxWindowGTK::DoGetPosition( int *x, int *y ) const
{
    int dx = 0;
    int dy = 0;
    GtkWidget* parent = nullptr;
    if (m_widget)
        parent = gtk_widget_get_parent(m_widget);
    if (WX_IS_PIZZA(parent))
    {
        wxPizza* pizza = WX_PIZZA(parent);
        dx = pizza->m_scroll_x;
        dy = pizza->m_scroll_y;
    }
    if (x) (*x) = m_x - dx;
    if (y) (*y) = m_y - dy;
}

#ifdef __WXGTK4__

// GTK4 has one GdkSurface per toplevel: a child window no longer has an origin
// of its own to ask for, and gdk_window_get_origin() is gone with no
// replacement -- GTK4 declines to tell a client where its surface sits on
// screen at all. "Screen" coordinates below are therefore relative to the
// toplevel. That is self-consistent, which is what everything in wx that
// round-trips through ClientToScreen()/ScreenToClient() actually needs, and it
// is the best that can be done on Wayland in any case.
//
// Within the toplevel, gtk_widget_compute_point() maps a point through the
// widget tree, doing explicitly what the per-window GdkWindow origins used to
// do implicitly -- including every ancestor's offset, which the GTK3 code path
// below never had to add by hand and so does not.
static void wxGTKGetOriginInRoot(GtkWidget* widget, int* org_x, int* org_y)
{
    GtkRoot* const root = gtk_widget_get_root(widget);
    if ( !root )
        return;

    const graphene_point_t in = { 0, 0 };
    graphene_point_t out;
    if ( gtk_widget_compute_point(widget, GTK_WIDGET(root), &in, &out) )
    {
        *org_x = int(out.x);
        *org_y = int(out.y);
    }

    // The root widget does not start at the surface's origin when the window
    // is drawn with client-side decorations: the shadow is part of the surface
    // and lies outside the widget tree.
    double sx = 0,
           sy = 0;
    gtk_native_get_surface_transform(GTK_NATIVE(root), &sx, &sy);
    *org_x += int(sx);
    *org_y += int(sy);

#ifdef GDK_WINDOWING_X11
    // And finally the surface's own position, which turns all of the above
    // into real screen coordinates. GTK4 has no API for this -- gdk_window_get_origin()
    // was not replaced, deliberately, because Wayland clients are not told
    // where their windows are -- but under X11 the server still knows, so ask
    // it directly rather than reporting toplevel-relative coordinates and
    // leaving them to disagree with wxTopLevelWindow::GetPosition().
    GdkSurface* const surface = gtk_native_get_surface(GTK_NATIVE(root));

    // GDK_IS_X11_SURFACE() is a type check, not a liveness one: it stays true
    // for a surface whose X window has already been destroyed, and
    // GDK_SURFACE_XID() then hands the server a stale XID. The server answers
    // BadWindow, and GDK's error handler exits the process -- which is not a
    // theoretical risk, it killed test_gui halfway through the suite. See #85.
    if ( surface && GDK_IS_X11_SURFACE(surface) &&
            !gdk_surface_is_destroyed(surface) )
    {
        Display* const dpy = GDK_SURFACE_XDISPLAY(surface);
        int rx = 0,
            ry = 0;
        Window unused;

        // The check above still leaves a window: X is asynchronous, so the
        // surface can be destroyed between asking and the request reaching the
        // server. Trapping the error is what actually makes this safe -- the
        // check merely avoids the common case of trapping one every time.
        GdkDisplay* const display = gdk_surface_get_display(surface);
        gdk_x11_display_error_trap_push(display);

        const Bool ok = XTranslateCoordinates(dpy, GDK_SURFACE_XID(surface),
                                              DefaultRootWindow(dpy),
                                              0, 0, &rx, &ry, &unused);

        // Sync so that the error, if any, is caught by the pop below rather
        // than arriving later, outside the trap, where it would be fatal.
        if ( gdk_x11_display_error_trap_pop(display) == 0 && ok )
        {
            *org_x += rx;
            *org_y += ry;
        }
    }
#endif // GDK_WINDOWING_X11
}

#endif // __WXGTK4__

void wxWindowGTK::DoClientToScreen( int *x, int *y ) const
{
    wxCHECK_RET( (m_widget != nullptr), wxT("invalid window") );

#ifdef __WXGTK4__
    GdkSurface* const source = GTKGetMainWindow();
#else
    GdkWindow* const source = GTKGetMainWindow();
#endif

    bool fromParent = !m_isGtkPositionValid || source == nullptr;
#ifdef __WXGTK4__
    // Always, under GTK4. Layout there happens in the frame clock's layout
    // phase rather than synchronously, so a child's GTK allocation is stale
    // from the moment it is shown, hidden or moved until the next frame is
    // drawn -- and asking GTK where the widget is would answer out of date,
    // silently and only sometimes. wx knows where it put each of its children
    // regardless of whether GTK has caught up, so walk the parent chain using
    // that instead; only the toplevel's own client offset, which is GTK's
    // business and is settled once the window is mapped, still comes from GTK.
    fromParent = true;
#endif // __WXGTK4__

    if (fromParent && !IsTopLevel() && m_parent)
    {
        int xx, yy;
        DoGetPosition(&xx, &yy);
        if (m_wxwindow)
        {
            GtkBorder border;
            WX_PIZZA(m_wxwindow)->get_border(border);
            xx += border.left;
            yy += border.top;
        }

        // Ask the parent where its own client origin is rather than handing it
        // our coordinate: in RTL it would mirror that using *its* width, which
        // is not the basis this window's coordinates are expressed in.
        int ox = 0,
            oy = 0;
        m_parent->DoClientToScreen(&ox, &oy);

        if (y) *y += oy + yy;
        if (x)
        {
            if (GetLayoutDirection() != wxLayout_RightToLeft)
                *x += ox + xx;
            else
            {
                // In RTL the origin above is the parent client area's right
                // edge and this window's position is measured from it, so both
                // it and the coordinate within this window run leftwards.
                *x = ox - xx - *x;
            }
        }
        return;
    }

    if (source == nullptr)
    {
        wxLogDebug("ClientToScreen cannot work when toplevel window is not shown");
        return;
    }

    int org_x = 0;
    int org_y = 0;
#ifdef __WXGTK4__
    // See wxGTKGetOriginInRoot() above: this is relative to the toplevel, and
    // it already includes every ancestor's offset, so the allocation fixup the
    // GTK3 branch does for windowless widgets is not wanted here.
    wxUnusedVar(source);
    wxGTKGetOriginInRoot(m_wxwindow ? m_wxwindow : m_widget, &org_x, &org_y);

    if (m_wxwindow)
    {
        // The client area starts inside the border wxPizza draws itself.
        GtkBorder border;
        WX_PIZZA(m_wxwindow)->get_border(border);
        org_x += border.left;
        org_y += border.top;
    }
#else
    gdk_window_get_origin( source, &org_x, &org_y );

    if (!m_wxwindow)
    {
        if (!wx_gtk_widget_get_has_window(m_widget))
        {
            GtkAllocation a;
            gtk_widget_get_allocation(m_widget, &a);
            org_x += a.x;
            org_y += a.y;
        }
    }
#endif


    if (x)
    {
        if (GetLayoutDirection() == wxLayout_RightToLeft)
            *x = (GetClientSize().x - *x) + org_x;
        else
            *x += org_x;
    }

    if (y) *y += org_y;
}

void wxWindowGTK::DoScreenToClient( int *x, int *y ) const
{
    wxCHECK_RET( (m_widget != nullptr), wxT("invalid window") );

#ifdef __WXGTK4__
    GdkSurface* const source = GTKGetMainWindow();
#else
    GdkWindow* const source = GTKGetMainWindow();
#endif

    bool fromParent = !m_isGtkPositionValid || source == nullptr;
#ifdef __WXGTK4__
    fromParent = true;  // see DoClientToScreen()
#endif // __WXGTK4__

    if (fromParent && !IsTopLevel() && m_parent)
    {
        int xx, yy;
        DoGetPosition(&xx, &yy);
        if (m_wxwindow)
        {
            GtkBorder border;
            WX_PIZZA(m_wxwindow)->get_border(border);
            xx += border.left;
            yy += border.top;
        }

        // The exact inverse of DoClientToScreen() above, see the comments
        // there for why the parent is asked for its origin rather than for the
        // translation of this coordinate.
        int ox = 0,
            oy = 0;
        m_parent->DoClientToScreen(&ox, &oy);

        if (y) *y -= oy + yy;
        if (x)
        {
            if (GetLayoutDirection() != wxLayout_RightToLeft)
                *x -= ox + xx;
            else
                *x = ox - xx - *x;
        }
        return;
    }

    if (source == nullptr)
    {
        wxLogDebug("ScreenToClient cannot work when toplevel window is not shown");
        return;
    }

    int org_x = 0;
    int org_y = 0;
#ifdef __WXGTK4__
    // See wxGTKGetOriginInRoot() above: this is relative to the toplevel, and
    // it already includes every ancestor's offset, so the allocation fixup the
    // GTK3 branch does for windowless widgets is not wanted here.
    wxUnusedVar(source);
    wxGTKGetOriginInRoot(m_wxwindow ? m_wxwindow : m_widget, &org_x, &org_y);

    if (m_wxwindow)
    {
        // The client area starts inside the border wxPizza draws itself.
        GtkBorder border;
        WX_PIZZA(m_wxwindow)->get_border(border);
        org_x += border.left;
        org_y += border.top;
    }
#else
    gdk_window_get_origin( source, &org_x, &org_y );

    if (!m_wxwindow)
    {
        if (!wx_gtk_widget_get_has_window(m_widget))
        {
            GtkAllocation a;
            gtk_widget_get_allocation(m_widget, &a);
            org_x += a.x;
            org_y += a.y;
        }
    }
#endif

    if (x)
    {
        if (GetLayoutDirection() == wxLayout_RightToLeft)
            *x = (GetClientSize().x - *x) + org_x;
        else
            *x -= org_x;
    }
    if (y) *y -= org_y;
}

bool wxWindowGTK::Show( bool show )
{
    if ( !wxWindowBase::Show(show) )
    {
        // nothing to do
        return false;
    }

    // notice that we may call Hide() before the window is created and this is
    // actually useful to create it hidden initially -- but we can't call
    // Show() before it is created
    if ( !m_widget )
    {
        wxASSERT_MSG( !show, "can't show invalid window" );
        return true;
    }

    if ( show )
    {
        if ( m_showOnIdle )
        {
            // defer until later
            return true;
        }

        gtk_widget_show(m_widget);
    }
    else // hide
    {
        gtk_widget_hide(m_widget);
    }

    wxShowEvent eventShow(GetId(), show);
    eventShow.SetEventObject(this);
    HandleWindowEvent(eventShow);

    return true;
}

bool wxWindowGTK::IsShown() const
{
    // return false for non-selected wxNotebook pages
    return m_isShown && (m_widget == nullptr || gtk_widget_get_child_visible(m_widget));
}

void wxWindowGTK::DoEnable( bool enable )
{
    if ( !m_widget )
    {
        // The window can be disabled before being created, so just don't do
        // anything in this case and, in particular, don't assert.
        return;
    }

    gtk_widget_set_sensitive( m_widget, enable );
    if (m_wxwindow && (m_wxwindow != m_widget))
        gtk_widget_set_sensitive( m_wxwindow, enable );

    if (enable && AcceptsFocusFromKeyboard())
    {
        wxWindowGTK* parent = this;
        while ((parent = parent->GetParent()))
        {
            parent->m_dirtyTabOrder = true;
            if (parent->IsTopLevel())
                break;
        }
        wxTheApp->WakeUpIdle();
    }
}

int wxWindowGTK::GetCharHeight() const
{
    wxCHECK_MSG( (m_widget != nullptr), 12, wxT("invalid window") );

    wxFont font = GetFont();
    wxCHECK_MSG( font.IsOk(), 12, wxT("invalid font") );

    PangoContext* context = gtk_widget_get_pango_context(m_widget);

    if (!context)
        return 0;

    PangoFontDescription *desc = font.GetNativeFontInfo()->description;
    PangoLayout *layout = pango_layout_new(context);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, "H", 1);
    PangoLayoutLine* line;
#if PANGO_VERSION_CHECK(1,16,0)
    if ( wx_pango_version_check(1,16,0) == nullptr )
    {
        line = pango_layout_get_line_readonly(layout, 0);
    }
    else
#endif // Pango 1.16+
    {
        line = (PangoLayoutLine *)pango_layout_get_lines(layout)->data;
    }

    PangoRectangle rect;
    pango_layout_line_get_extents(line, nullptr, &rect);

    g_object_unref (layout);

    return (int) PANGO_PIXELS(rect.height);
}

int wxWindowGTK::GetCharWidth() const
{
    wxCHECK_MSG( (m_widget != nullptr), 8, wxT("invalid window") );

    wxFont font = GetFont();
    wxCHECK_MSG( font.IsOk(), 8, wxT("invalid font") );

    PangoContext* context = gtk_widget_get_pango_context(m_widget);

    if (!context)
        return 0;

    PangoFontDescription *desc = font.GetNativeFontInfo()->description;
    PangoLayout *layout = pango_layout_new(context);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_text(layout, "g", 1);
    PangoLayoutLine* line;
#if PANGO_VERSION_CHECK(1,16,0)
    if ( wx_pango_version_check(1,16,0) == nullptr )
    {
        line = pango_layout_get_line_readonly(layout, 0);
    }
    else
#endif // Pango 1.16+
    {
        line = (PangoLayoutLine *)pango_layout_get_lines(layout)->data;
    }

    PangoRectangle rect;
    pango_layout_line_get_extents(line, nullptr, &rect);

    g_object_unref (layout);

    return (int) PANGO_PIXELS(rect.width);
}

void wxWindowGTK::DoGetTextExtent( const wxString& string,
                                   int *x,
                                   int *y,
                                   int *descent,
                                   int *externalLeading,
                                   const wxFont *theFont ) const
{
    // ensure we work with a valid font
    wxFont fontToUse;
    if ( !theFont || !theFont->IsOk() )
        fontToUse = GetFont();
    else
        fontToUse = *theFont;

    wxCHECK_RET( fontToUse.IsOk(), wxT("invalid font") );

    const wxWindow* win = static_cast<const wxWindow*>(this);
    wxTextMeasure txm(win, &fontToUse);
    txm.GetTextExtent(string, x, y, descent, externalLeading);
}

double wxWindowGTK::GetContentScaleFactor() const
{
    double scaleFactor = 1;
#if GTK_CHECK_VERSION(3,10,0)
    if (m_widget && gtk_check_version(3,10,0) == nullptr)
    {
        scaleFactor = gtk_widget_get_scale_factor(m_widget);
    }
#endif
    return scaleFactor;
}

double wxWindowGTK::GetDPIScaleFactor() const
{
    // Under GTK 3 DPI scale factor is the same as content scale factor, while
    // under GTK 2 both are always 1, so they're still the same.
    return GetContentScaleFactor();
}

#ifdef __WXGTK4__

// The focus controller attached in ConnectWidget(), if any.
static gpointer wxGTKGetFocusController(GtkWidget* widget)
{
    return widget ? g_object_get_data(G_OBJECT(widget), "wx-focus-controller")
                  : nullptr;
}

void wxWindowGTK::GTKDisableFocusOutEvent()
{
    if ( gpointer const focus = wxGTKGetFocusController(m_focusWidget) )
        g_signal_handlers_block_by_func(focus, (gpointer) wx_window_focus_out, this);
}

void wxWindowGTK::GTKEnableFocusOutEvent()
{
    if ( gpointer const focus = wxGTKGetFocusController(m_focusWidget) )
        g_signal_handlers_unblock_by_func(focus, (gpointer) wx_window_focus_out, this);
}

#else

void wxWindowGTK::GTKDisableFocusOutEvent()
{
    g_signal_handlers_block_by_func( m_focusWidget,
                                (gpointer) gtk_window_focus_out_callback, this);
}

void wxWindowGTK::GTKEnableFocusOutEvent()
{
    g_signal_handlers_unblock_by_func( m_focusWidget,
                                (gpointer) gtk_window_focus_out_callback, this);
}

#endif // __WXGTK4__/!__WXGTK4__

bool wxWindowGTK::GTKHandleFocusIn()
{
    // Disable default focus handling for custom windows since the default GTK+
    // handler issues a repaint
    const bool retval = m_wxwindow ? true : false;

#ifdef __WXGTK4__
    // See gs_focusRestoreAfter: a focus wx did not ask for, arriving at a
    // window created after the focused one was destroyed, is GTK4 passing the
    // focus on rather than anything the user or the program did. Hand it back.
    if ( gs_focusRestoreAfter && gs_pendingFocus != this &&
            m_creationSerial > gs_focusRestoreAfter )
    {
        gs_focusRestoreAfter = 0;
        gs_focusDeclined = this;

        wxLogTrace(TRACE_FOCUS,
                   "declining focus restored by GTK to %s",
                   wxDumpWindow(this));

        return retval;
    }

    gs_focusRestoreAfter = 0;
#endif // __WXGTK4__


    // NB: if there's still unprocessed deferred focus-out event (see
    //     GTKHandleFocusOut() for explanation), we need to process it first so
    //     that the order of focus events -- focus-out first, then focus-in
    //     elsewhere -- is preserved
    if ( gs_deferredFocusOut )
    {
        if ( GTKNeedsToFilterSameWindowFocus() &&
             gs_deferredFocusOut == this )
        {
            // GTK+ focus changed from this wxWindow back to itself, so don't
            // emit any events at all
            wxLogTrace(TRACE_FOCUS,
                       "filtered out spurious focus change within %s",
                       wxDumpWindow(this));
            gs_deferredFocusOut = nullptr;
            return retval;
        }

        // otherwise we need to send focus-out first
        wxASSERT_MSG ( gs_deferredFocusOut != this,
                       "GTKHandleFocusIn(GTKFocus_Normal) called even though focus changed back to itself - derived class should handle this" );
        gs_deferredFocusOut->GTKHandleDeferredFocusOut();
    }


    wxLogTrace(TRACE_FOCUS,
               "handling focus_in event for %s",
               wxDumpWindow(this));

    if (m_imContext)
        gtk_im_context_focus_in(m_imContext);

    gs_currentFocus = this;

    if ( gs_pendingFocus )
    {
        if ( gs_pendingFocus != gs_currentFocus )
        {
            wxLogTrace(TRACE_FOCUS, "Resetting pending focus %s on focus set",
                       wxDumpWindow(gs_pendingFocus));
        }

        gs_pendingFocus = nullptr;
    }

#if wxUSE_CARET
    // caret needs to be informed about focus change
    wxCaret *caret = GetCaret();
    if ( caret )
    {
        caret->OnSetFocus();
    }
#endif // wxUSE_CARET

    // Notify the parent keeping track of focus for the kbd navigation
    // purposes that we got it.
    wxChildFocusEvent eventChildFocus(static_cast<wxWindow*>(this));
    GTKProcessEvent(eventChildFocus);

    wxFocusEvent eventFocus(wxEVT_SET_FOCUS, GetId());
    eventFocus.SetEventObject(this);
    eventFocus.SetWindow(static_cast<wxWindow*>(gs_lastFocus));
    gs_lastFocus = this;

    GTKProcessEvent(eventFocus);

    return retval;
}

bool wxWindowGTK::GTKHandleFocusOut()
{
    // Disable default focus handling for custom windows since the default GTK+
    // handler issues a repaint
    const bool retval = m_wxwindow ? true : false;

#ifdef __WXGTK4__
    // The focus-in for this window was declined, so wx never reported it as
    // focused and must not report it losing what it never had.
    if ( gs_focusDeclined == this )
    {
        gs_focusDeclined = nullptr;
        return retval;
    }
#endif // __WXGTK4__

    // If this window is still the pending focus one, reset that pointer as
    // we're not going to have focus any longer and DoFindFocus() must not
    // return this window.
    if ( gs_pendingFocus == this )
    {
        wxLogTrace(TRACE_FOCUS, "Resetting pending focus %s on focus loss",
                   wxDumpWindow(this));
        gs_pendingFocus = nullptr;
    }

    // NB: If a control is composed of several GtkWidgets and when focus
    //     changes from one of them to another within the same wxWindow, we get
    //     a focus-out event followed by focus-in for another GtkWidget owned
    //     by the same wx control. We don't want to generate two spurious
    //     wxEVT_SET_FOCUS events in this case, so we defer sending wx events
    //     from GTKHandleFocusOut() until we know for sure it's not coming back
    //     (i.e. in GTKHandleFocusIn() or at idle time).
    if ( GTKNeedsToFilterSameWindowFocus() )
    {
#ifdef __WXGTK4__
        // Both the focus controller and notify::focus-widget can report the
        // same focus loss. The first one is already waiting to be resolved by
        // GTKHandleFocusIn() or idle processing, so there is nothing to add.
        if ( gs_deferredFocusOut == this )
            return retval;
#endif // __WXGTK4__

        wxASSERT_MSG( gs_deferredFocusOut == nullptr,
                      "deferred focus out event already pending" );
        wxLogTrace(TRACE_FOCUS,
                   "deferring focus_out event for %s",
                   wxDumpWindow(this));
        gs_deferredFocusOut = this;
        return retval;
    }

    GTKHandleFocusOutNoDeferring();

    return retval;
}

void wxWindowGTK::GTKHandleFocusOutNoDeferring()
{
    wxLogTrace(TRACE_FOCUS,
               "handling focus_out event for %s",
               wxDumpWindow(this));

    gs_lastFocus = this;

    if (m_imContext)
        gtk_im_context_focus_out(m_imContext);

    if ( gs_currentFocus != this )
    {
        // Something is terribly wrong, gs_currentFocus is out of sync with the
        // real focus. We will reset it to nullptr anyway, because after this
        // focus-out event is handled, one of the following with happen:
        //
        // * either focus will go out of the app altogether, in which case
        //   gs_currentFocus _should_ be null
        //
        // * or it goes to another control, in which case focus-in event will
        //   follow immediately and it will set gs_currentFocus to the right
        //   value
        wxLogDebug("window %s lost focus even though it didn't have it",
                   wxDumpWindow(this));
    }
    gs_currentFocus = nullptr;

#if wxUSE_CARET
    // caret needs to be informed about focus change
    wxCaret *caret = GetCaret();
    if ( caret )
    {
        caret->OnKillFocus();
    }
#endif // wxUSE_CARET

    wxFocusEvent event( wxEVT_KILL_FOCUS, GetId() );
    event.SetEventObject( this );
    event.SetWindow( FindFocus() );
    GTKProcessEvent( event );
}

void wxWindowGTK::GTKHandleDeferredFocusOut()
{
    // NB: See GTKHandleFocusOut() for explanation. This function is called
    //     from either GTKHandleFocusIn() or OnInternalIdle() to process
    //     deferred event for this window.
    gs_deferredFocusOut = nullptr;

    wxLogTrace(TRACE_FOCUS,
               "processing deferred focus_out event for %s",
               wxDumpWindow(this));

    GTKHandleFocusOutNoDeferring();
}

void wxWindowGTK::SetFocus()
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid window") );

    // Setting "physical" focus is not immediate in GTK+ and while
    // gtk_widget_is_focus ("determines if the widget is the focus widget
    // within its toplevel", i.e. returns true for one widget per TLW, not
    // globally) returns true immediately after grabbing focus,
    // GTK_WIDGET_HAS_FOCUS (which returns true only for the one widget that
    // has focus at the moment) takes effect only after the window is shown
    // (if it was hidden at the moment of the call) or at the next event loop
    // iteration.
    //
    // Because we want to FindFocus() call immediately following
    // foo->SetFocus() to return foo, we have to keep track of "pending" focus
    // ourselves.
    gs_pendingFocus = nullptr;
    if (gs_currentFocus != this)
        gs_pendingFocus = this;

#ifdef __WXGTK4__
    // GTK already gave this window the focus and wx declined it (see
    // gs_focusDeclined). Now that wx does want it there is nothing left for
    // GTK to do -- grabbing a focus it already holds produces no further
    // focus-in -- so report the one that was suppressed instead.
    if ( gs_focusDeclined == this )
    {
        gs_focusDeclined = nullptr;
        GTKHandleFocusIn();
        return;
    }
#endif // __WXGTK4__

    // Toplevel must be active for child to actually receive focus.
    // But avoid activating if tlw is not yet shown, as that will
    // cause it to be immediately shown.
    GtkWidget* tlw = gtk_widget_get_ancestor(m_widget, GTK_TYPE_WINDOW);
    if (tlw && gtk_widget_get_visible(tlw) && !gtk_window_is_active(GTK_WINDOW(tlw)))
        gtk_window_present(GTK_WINDOW(tlw));

    GtkWidget *widget = m_wxwindow ? m_wxwindow : m_focusWidget;

    if ( wx_gtk_widget_is_container(widget) &&
         !wx_gtk_widget_get_focusable(widget) )
    {
        wxLogTrace(TRACE_FOCUS,
                   wxT("Setting focus to a child of %s"),
                   wxDumpWindow(this));
        gtk_widget_child_focus(widget, GTK_DIR_TAB_FORWARD);
    }
    else
    {
        wxLogTrace(TRACE_FOCUS,
                   wxT("Setting focus to %s"),
                   wxDumpWindow(this));
        gtk_widget_grab_focus(widget);
    }
}

void wxWindowGTK::SetCanFocus(bool canFocus)
{
    wxCHECK_RET(m_widget, "invalid window");

    wx_gtk_widget_set_focusable(m_widget, canFocus);

    if ( m_wxwindow && (m_widget != m_wxwindow) )
    {
        wx_gtk_widget_set_focusable(m_wxwindow, canFocus);
    }
}

bool wxWindowGTK::Reparent( wxWindowBase *newParentBase )
{
    wxCHECK_MSG( (m_widget != nullptr), false, wxT("invalid window") );

    wxWindowGTK * const newParent = (wxWindowGTK *)newParentBase;

    wxASSERT( GTK_IS_WIDGET(m_widget) );

    if ( !wxWindowBase::Reparent(newParent) )
        return false;

    wxASSERT( GTK_IS_WIDGET(m_widget) );

    // Notice that old m_parent pointer might be non-null here but the widget
    // still not have any parent at GTK level if it's a notebook page that had
    // been removed from the notebook so test this at GTK level and not wx one.
    if ( GtkWidget* const parentGTK = gtk_widget_get_parent(m_widget) )
    {
#ifdef __WXGTK4__
        GTKDetachFromParent();
#else
        // Not gtk_widget_unparent(): that detaches the widget but leaves it in
        // the container's own list of children, which the container then walks
        // when it is destroyed -- by which time the widget is gone. Only the
        // container's "remove" knows how to take it off that list, and under
        // GTK4 that is what GTKDetachFromParent() is for.
        gtk_container_remove(GTK_CONTAINER(parentGTK), m_widget);
#endif
    }

    wxASSERT( GTK_IS_WIDGET(m_widget) );

    if (newParent)
    {
        if (gtk_widget_get_visible (newParent->m_widget))
        {
            m_showOnIdle = true;
            gtk_widget_hide( m_widget );
        }
        /* insert GTK representation */
        newParent->AddChildGTK(this);
    }

    SetLayoutDirection(wxLayout_Default);

    return true;
}

void wxWindowGTK::GTKRemoveBorder()
{
}

void wxWindowGTK::DoAddChild(wxWindowGTK *child)
{
    wxASSERT_MSG( (m_widget != nullptr), wxT("invalid window") );
    wxASSERT_MSG( (child != nullptr), wxT("invalid child window") );

    // If parent is already showing, changing CSS after adding child
    // can cause transitory visual glitches, so change it here
    if (HasFlag(wxBORDER_NONE))
        GTKRemoveBorder();

    /* add to list */
    AddChild( child );

    /* insert GTK representation */
    AddChildGTK(child);
}

void wxWindowGTK::AddChild(wxWindowBase *child)
{
    wxWindowBase::AddChild(child);
    m_dirtyTabOrder = true;
    wxTheApp->WakeUpIdle();
}

void wxWindowGTK::RemoveChild(wxWindowBase *child)
{
    wxWindowBase::RemoveChild(child);
    m_dirtyTabOrder = true;
    wxTheApp->WakeUpIdle();
}

/* static */
wxLayoutDirection wxWindowGTK::GTKGetLayout(GtkWidget *widget)
{
    return gtk_widget_get_direction(widget) == GTK_TEXT_DIR_RTL
                ? wxLayout_RightToLeft
                : wxLayout_LeftToRight;
}

/* static */
void wxWindowGTK::GTKSetLayout(GtkWidget *widget, wxLayoutDirection dir)
{
    wxASSERT_MSG( dir != wxLayout_Default, wxT("invalid layout direction") );

    gtk_widget_set_direction(widget,
                             dir == wxLayout_RightToLeft ? GTK_TEXT_DIR_RTL
                                                         : GTK_TEXT_DIR_LTR);
}

wxLayoutDirection wxWindowGTK::GetLayoutDirection() const
{
    return GTKGetLayout(m_widget);
}

void wxWindowGTK::SetLayoutDirection(wxLayoutDirection dir)
{
    if ( dir == wxLayout_Default )
    {
        const wxWindow *const parent = GetParent();
        if ( parent )
        {
            // inherit layout from parent.
            dir = parent->GetLayoutDirection();
        }
        else // no parent, use global default layout
        {
            dir = wxTheApp->GetLayoutDirection();
        }
    }

    if ( dir == wxLayout_Default )
        return;

    GTKSetLayout(m_widget, dir);

    if (wxGtkScrollbar* range = m_scrollBar[ScrollDir_Horz])
        wxGtkScrollbarSetInverted(range, dir == wxLayout_RightToLeft);

    if (m_wxwindow && (m_wxwindow != m_widget))
        GTKSetLayout(m_wxwindow, dir);
}

wxCoord
wxWindowGTK::AdjustForLayoutDirection(wxCoord x,
                                      wxCoord WXUNUSED(width),
                                      wxCoord WXUNUSED(widthTotal)) const
{
    // We now mirror the coordinates of RTL windows in wxPizza
    return x;
}

void wxWindowGTK::DoMoveInTabOrder(wxWindow *win, WindowOrder move)
{
    wxWindowBase::DoMoveInTabOrder(win, move);

    // Update the TAB order at GTK+ level too, but do it slightly later in case
    // we're changing the TAB order of several controls at once, as is common.
    wxWindow* const parent = GetParent();
    if ( parent )
    {
        parent->m_dirtyTabOrder = true;
        wxTheApp->WakeUpIdle();
    }
}

bool wxWindowGTK::DoNavigateIn(int flags)
{
    wxWindow *parent = wxGetTopLevelParent((wxWindow *)this);
    wxCHECK_MSG( parent, false, wxT("every window must have a TLW parent") );

    GtkDirectionType dir;
    dir = flags & wxNavigationKeyEvent::IsForward ? GTK_DIR_TAB_FORWARD
                                                  : GTK_DIR_TAB_BACKWARD;

#ifdef __WXGTK4__
    // gtk_widget_child_focus() is the public entry point for what emitting
    // this signal did; the signal itself is now just a vfunc.
    return gtk_widget_child_focus(parent->m_widget, dir) != 0;
#else
    gboolean rc;
    g_signal_emit_by_name(parent->m_widget, "focus", dir, &rc);

    return rc != 0;
#endif
}

bool wxWindowGTK::GTKWidgetNeedsMnemonic() const
{
    // none needed by default
    return false;
}

void wxWindowGTK::GTKWidgetDoSetMnemonic(GtkWidget* WXUNUSED(w))
{
    // nothing to do by default since none is needed
}

void wxWindowGTK::RealizeTabOrder()
{
    if (m_wxwindow)
    {
        if ( !m_children.empty() )
        {
            // we don't only construct the correct focus chain but also use
            // this opportunity to update the mnemonic widgets for the widgets
            // that need them

            GList *chain = nullptr;
            wxWindowGTK* mnemonicWindow = nullptr;

            for ( wxWindowList::const_iterator i = m_children.begin();
                  i != m_children.end();
                  ++i )
            {
                wxWindowGTK *win = *i;

                bool focusableFromKeyboard = win->AcceptsFocusFromKeyboard();

                if ( mnemonicWindow )
                {
                    if ( focusableFromKeyboard )
                    {
                        // We may need to focus on the connect widget if the
                        // main one isn't focusable, but note that we still use
                        // the main widget if neither it nor connect widget is
                        // focusable, without this using a wxStaticText before
                        // wxChoice wouldn't work at all, for example.
                        GtkWidget* w = win->m_widget;
                        if ( !wx_gtk_widget_get_focusable(w) )
                        {
                            GtkWidget* const cw = win->GetConnectWidget();
                            if ( cw != w && wx_gtk_widget_get_focusable(cw) )
                                w = cw;
                        }

                        mnemonicWindow->GTKWidgetDoSetMnemonic(w);
                        mnemonicWindow = nullptr;
                    }
                }

                if ( win->GTKWidgetNeedsMnemonic() )
                {
                    mnemonicWindow = win;
                }

                if ( focusableFromKeyboard )
                    chain = g_list_prepend(chain, win->m_widget);
            }

            chain = g_list_reverse(chain);

#ifdef __WXGTK4__
            // GTK4 removed gtk_container_set_focus_chain() entirely --
            // there is no separate "focus chain" any more, Tab traversal
            // follows actual widget-tree sibling order. Reproduce the
            // desired order by physically reordering the children.
            // CAVEAT, not yet runtime-verified: GTK4 ties paint/Z-order to
            // the same widget-tree position, so this could visually
            // reorder overlapping children that used to have independent
            // paint and tab order -- see docs/gtk/gtk4-status.md.
            {
                GtkWidget* prev = nullptr;
                for (GList* p = chain; p; p = p->next)
                {
                    GtkWidget* const w = GTK_WIDGET(p->data);

                    // gtk_widget_insert_after() *parents* the widget, it does
                    // not merely reorder it, so it may only be used on
                    // widgets that already are our children. A wxTopLevelWindow
                    // with a wx parent is in GetChildren() but must never be a
                    // GTK-level child: parenting one here gave a GtkWindow a
                    // CSS parent, and GTK aborts the next time it validates
                    // that window as a root:
                    //   gtk_css_node_validate: assertion failed,
                    //   because it requires cssnode->parent to be null
                    //   for a root.
                    // See docs/gtk/probes/wx-stylecontext-abort.cpp.
                    if (gtk_widget_get_parent(w) != m_wxwindow)
                        continue;

                    gtk_widget_insert_after(w, m_wxwindow, prev);
                    prev = w;
                }
            }
#else
            wxGCC_WARNING_SUPPRESS(deprecated-declarations)
            gtk_container_set_focus_chain(GTK_CONTAINER(m_wxwindow), chain);
            wxGCC_WARNING_RESTORE(deprecated-declarations)
#endif // __WXGTK4__/!__WXGTK4__

            g_list_free(chain);
        }
        else // no children
        {
#ifndef __WXGTK4__
            wxGCC_WARNING_SUPPRESS(deprecated-declarations)
            gtk_container_unset_focus_chain(GTK_CONTAINER(m_wxwindow));
            wxGCC_WARNING_RESTORE(deprecated-declarations)
#endif // !__WXGTK4__
        }
    }
}

void wxWindowGTK::Raise()
{
    wxCHECK_RET( (m_widget != nullptr), wxT("invalid window") );

    if (!m_isShown)
        return;

#ifdef __WXGTK4__
    // gdk_window_raise() is gone and nothing replaces it: GTK4 has no child
    // windows to reorder, and the stacking of toplevels is the compositor's.
    // Presenting the toplevel is the closest thing available.
    if ( wxTopLevelWindow* const tlw =
            wxDynamicCast(wxGetTopLevelParent(this), wxTopLevelWindow) )
    {
        if ( tlw == this )
            gtk_window_present(GTK_WINDOW(m_widget));
    }
#else
    if (auto const window = GTKGetMainWindow())
    {
        gdk_window_raise(window);
    }
#endif
}

void wxWindowGTK::Lower()
{
    wxCHECK_RET( (m_widget != nullptr), wxT("invalid window") );

#ifdef __WXGTK4__
    // See Raise(): there is no lowering at all under GTK4.
#else
    if (auto const window = GTKGetMainWindow())
    {
        gdk_window_lower(window);
    }
#endif
}

// ----------------------------------------------------------------------------
// Cursor stuff
// ----------------------------------------------------------------------------

// Return non-null pointer if there some globally set cursor overriding all the
// other ones.
static GdkCursor* wxGetOverrideCursor(wxWindowGTK* w)
{
    if (g_globalCursor.IsOk())
        return g_globalCursor.GetCursor();

    if (wxIsBusy())
    {
        wxWindow* win = wxGetTopLevelParent(w);
        if (win && win->m_widget && !gtk_window_get_modal(GTK_WINDOW(win->m_widget)))
            return g_busyCursor.GetCursor();
    }

    return nullptr;
}

#ifndef __WXGTK4__
wxArrayGdkWindows wxWindowGTK::GTKSetCursorForAllWindows(GdkCursor* cursor)
{
    wxArrayGdkWindows changed;

    wxArrayGdkWindows windows;
    GdkWindow* window = GTKGetWindow(windows);
    if (window)
    {
        gdk_window_set_cursor(window, cursor);
        changed.push_back(window);
    }
    else
    {
        for (size_t i = windows.size(); i--;)
        {
            window = windows[i];
            if (window)
            {
                gdk_window_set_cursor(window, cursor);
                changed.push_back(window);
            }
        }
    }

    return changed;
}
#endif // !__WXGTK4__

#ifdef __WXGTK4__

// Under GTK4 widgets don't have windows, so there is nothing to enumerate:
// gtk_widget_set_cursor() sets the cursor for a widget and, by inheritance,
// all of its children, which is exactly what GTKSetCursorForAllWindows() went
// to the trouble of doing by hand. This is the same simplification already
// made for SetGlobalCursor() in src/gtk/cursor.cpp.
static void wxGTKSetWidgetCursor(GtkWidget* widget, GdkCursor* cursor)
{
    if (widget)
        gtk_widget_set_cursor(widget, cursor);
}

#endif // __WXGTK4__

void wxWindowGTK::GTKSetCursor(const wxCursor& cursor)
{
#ifdef __WXGTK4__
    // A null cursor means "inherit from the parent", which is how the global
    // override cursor set by SetGlobalCursor() becomes visible here.
    wxGTKSetWidgetCursor(m_wxwindow ? m_wxwindow : m_widget,
                         wxGetOverrideCursor(this) ? nullptr
                                                   : cursor.GetCursor());
#else
    if (wxGetOverrideCursor(this))
    {
        GTKSetCursorForAllWindows(nullptr);
        return;
    }

    GdkCursor* const gcursor = cursor.GetCursor();
    if (gcursor)
        GTKSetCursorForAllWindows(gcursor);
#endif // __WXGTK4__/!__WXGTK4__
}

void wxWindowGTK::GTKApplyCursor()
{
    m_needCursorReset = false;

    GTKSetCursor(GetCursor());
}

void wxWindowGTK::GTKUpdateCursor()
{
    GTKUpdateCursor(wxGetOverrideCursor(this));
}

void wxWindowGTK::GTKUpdateCursor(GdkCursor* overrideCursor)
{
    m_needCursorReset = false;

    if (m_widget == nullptr || !gtk_widget_get_realized(m_widget))
        return;

    // Globally set cursor overrides all the other ones, but we don't actually
    // even need to use it: as by default the cursors are inherited from the
    // (TLW) parent and because SetGlobalCursor() in src/gtk/cursor.cpp sets
    // the global cursor for them, it's enough to reset the cursor to show it.
    GdkCursor* const cursor = overrideCursor ? nullptr : m_cursor.GetCursor();

#ifdef __WXGTK4__
    // The loop below only exists to prod native widgets into restoring the
    // cursors they had set on their own GdkWindows, which they can't do under
    // GTK4 as they set them on themselves with gtk_widget_set_cursor() and a
    // cursor set on an ancestor doesn't override that in the first place.
    wxGTKSetWidgetCursor(m_wxwindow ? m_wxwindow : m_widget, cursor);
#else
    const wxArrayGdkWindows& windows = GTKSetCursorForAllWindows(cursor);

    // We don't need to do anything else if we set a valid cursor or if this is
    // not a native widget.
    if (cursor || m_wxwindow)
        return;

    for (auto* window : windows)
    {
        void* data;
        gdk_window_get_user_data(window, &data);
        if (data)
        {
#ifdef __WXGTK3__
            const char sig_name[] = "state-flags-changed";
            GtkStateFlags state = gtk_widget_get_state_flags(GTK_WIDGET(data));
#else
            const char sig_name[] = "state-changed";
            GtkStateType state = gtk_widget_get_state(GTK_WIDGET(data));
#endif
            static unsigned sig_id = g_signal_lookup(sig_name, GTK_TYPE_WIDGET);

            // encourage native widget to restore any non-default cursors
            g_signal_emit(data, sig_id, 0, state);
        }
    }
#endif // __WXGTK4__/!__WXGTK4__
}

void wxWindowGTK::WXUpdateCursor()
{
    // As GTKUpdateCursor() uses m_cursor, call the base class version to
    // update it first.
    wxWindowBase::WXUpdateCursor();

    GTKUpdateCursor();
}

#if defined(wxHAVE_WAYLAND_PROTOCOLS) && !defined(__WXGTK4__)

// Only reachable from WarpPointer() below, which is a no-op under GTK4, and
// built on GdkWindow besides.
namespace wxWayland
{

void WarpPointer(GdkWindow* window, int x, int y)
{
    if ( !WLGlobals.pointer_warp )
    {
        // This is not an error, many compositors don't support this protocol.
        return;
    }

    // We don't have any way to find the seat for which we want to warp the
    // pointer, so just use the first one which has a pointer to warp.
    for ( const auto& seat : WLGlobals.seats )
    {
        if ( seat.pointer && seat.lastEnterSerial )
        {
            wp_pointer_warp_v1_warp_pointer(WLGlobals.pointer_warp.get(),
                gdk_wayland_window_get_wl_surface(window),
                seat.pointer.get(),
                wl_fixed_from_int(x),
                wl_fixed_from_int(y),
                seat.lastEnterSerial
            );
        }
    }
}

} // namespace wxWayland

#endif // wxHAVE_WAYLAND_PROTOCOLS && !__WXGTK4__

void wxWindowGTK::WarpPointer( int x, int y )
{
    wxCHECK_RET( (m_widget != nullptr), wxT("invalid window") );

#if defined(wxHAVE_WAYLAND_PROTOCOLS) && !defined(__WXGTK4__)
    // Implement this ourselves as gdk_device_warp() doesn't do anything when
    // using Wayland backend in GTK3 and this function has been removed in GTK4.
    GdkWindow* const window = GTKGetMainWindow();
    if ( wxGTKImpl::IsWayland(window) )
    {
        int org_x = 0;
        int org_y = 0;
        gdk_window_get_origin(window, &org_x, &org_y);

        wxWayland::WarpPointer(window, org_x + x, org_y + y);
        return;
    }
#endif // wxHAVE_WAYLAND_PROTOCOLS && !__WXGTK4__

#ifdef __WXGTK4__
    // gdk_device_warp() was removed and nothing replaces it: moving the
    // pointer is not something a client may do under GTK4, and it already did
    // nothing under GTK3 with the Wayland backend. wxWindow::WarpPointer() is
    // therefore a no-op here.
    wxUnusedVar(x);
    wxUnusedVar(y);
#else
    ClientToScreen(&x, &y);
    GdkDisplay* display = gtk_widget_get_display(m_widget);
    GdkScreen* screen = gtk_widget_get_screen(m_widget);
#ifdef __WXGTK3__
    GdkDevice* const device = wx_get_gdk_device_from_display(display);
    gdk_device_warp(device, screen, x, y);
#elif defined(GDK_WINDOWING_X11)
    XWarpPointer(GDK_DISPLAY_XDISPLAY(display),
        None,
        GDK_WINDOW_XID(gdk_screen_get_root_window(screen)),
        0, 0, 0, 0, x, y);
#else
    wxUnusedVar(display);
    wxUnusedVar(screen);
#endif
#endif // __WXGTK4__/!__WXGTK4__
}

#ifdef __WXGTK4__

wxGtkScrollbar*
wxWindowGTK::GTKScrollbarFromAdjustment(GtkAdjustment* adj) const
{
    for (int dir = 0; dir < ScrollDir_Max; dir++)
    {
        wxGtkScrollbar* const sb = m_scrollBar[dir];
        if ( sb && gtk_scrollbar_get_adjustment(sb) == adj )
            return sb;
    }

    return nullptr;
}

#endif // __WXGTK4__

wxWindowGTK::ScrollDir wxWindowGTK::ScrollDirFromRange(wxGtkScrollbar *range) const
{
    // find the scrollbar which generated the event
    for ( int dir = 0; dir < ScrollDir_Max; dir++ )
    {
        if ( range == m_scrollBar[dir] )
            return (ScrollDir)dir;
    }

    wxFAIL_MSG( wxT("event from unknown scrollbar received") );

    return ScrollDir_Max;
}

bool wxWindowGTK::DoScrollByUnits(ScrollDir dir, ScrollUnit unit, int units)
{
    bool changed = false;
    wxGtkScrollbar* range = m_scrollBar[dir];
    if ( range && units )
    {
        GtkAdjustment* adj = wxGtkScrollbarGetAdjustment(range);
        double inc = unit == ScrollUnit_Line ? gtk_adjustment_get_step_increment(adj)
                                             : gtk_adjustment_get_page_increment(adj);

        const int posOld = wxRound(gtk_adjustment_get_value(adj));
        wxGtkScrollbarSetValue(range, posOld + units*inc);

        changed = wxRound(gtk_adjustment_get_value(adj)) != posOld;
    }

    return changed;
}

bool wxWindowGTK::ScrollLines(int lines)
{
    return DoScrollByUnits(ScrollDir_Vert, ScrollUnit_Line, lines);
}

bool wxWindowGTK::ScrollPages(int pages)
{
    return DoScrollByUnits(ScrollDir_Vert, ScrollUnit_Page, pages);
}

#ifdef __WXGTK4__

// GTK4 caches the render node each widget last produced, and invalidating one
// widget does not invalidate its children: they keep their cached nodes and are
// replayed unchanged. GTK3's gdk_window_invalidate_rect() took an
// "invalidate_children" flag which wx always passed TRUE, so wx's contract is
// that refreshing a region repaints everything inside it. Reproduce that by
// invalidating the children explicitly.
//
// Note this is *not* a workaround for GTK4 being lazy: caching the node tree
// and replaying it on the GPU is the whole point of the new rendering model,
// and re-running a widget's snapshot only when it is invalidated is correct.
// It just means "invalidate" now has to be said once per widget.
static void wxGTKRefreshChildren(wxWindowGTK* win, const wxRect* rect)
{
    for ( wxWindowList::compatibility_iterator node = win->GetChildren().GetFirst();
          node;
          node = node->GetNext() )
    {
        wxWindow* const child = node->GetData();

        // A child toplevel is not drawn by this window at all.
        if ( child->IsTopLevel() || !child->IsShown() )
            continue;

        if ( rect && !child->GetRect().Intersects(*rect) )
            continue;

        // No rectangle: a widget is always redrawn whole under GTK4, and this
        // recurses into the child's own children in turn, as invalidating a
        // GdkWindow tree used to.
        child->Refresh();
    }
}

#endif // __WXGTK4__

void wxWindowGTK::Refresh(bool WXUNUSED(eraseBackground),
                          const wxRect *rect)
{
    if (m_wxwindow)
    {
        if (gtk_widget_get_mapped(m_wxwindow))
        {
#ifdef __WXGTK4__
            // GTK4 has neither a GdkWindow to invalidate nor any partial
            // invalidation: gtk_widget_queue_draw_area() is gone too, and a
            // widget is always redrawn whole. The rectangle therefore does not
            // narrow what is repainted -- it cannot, since the snapshot vfunc
            // has to rebuild the widget's entire scene and anything wx left
            // out would simply be missing -- but it still decides which
            // children are in it, see below.
            //
            // This does cost something. Measured on a 300x200 window,
            // RefreshRect(20,30 100x40) leaves GetUpdateRegion() reporting
            // 20,30 100x40 under GTK+ 3 and the whole 0,0 300x200 under GTK4,
            // on both X11 and Wayland. An application repainting incrementally
            // therefore does full-window work per invalidation here. See
            // docs/gtk/gtk4-phase4-paint-model-design.md section 3.
            gtk_widget_queue_draw(m_wxwindow);
            wxGTKRefreshChildren(this, rect);
#else
            GdkWindow* window = gtk_widget_get_window(m_wxwindow);
            if (rect)
            {
                GdkRectangle r = { rect->x, rect->y, rect->width, rect->height };
                if (GetLayoutDirection() == wxLayout_RightToLeft)
                    r.x = gdk_window_get_width(window) - r.x - rect->width;
                gdk_window_invalidate_rect(window, &r, true);
            }
            else
                gdk_window_invalidate_rect(window, nullptr, true);
#endif
        }
    }
    else if (m_widget)
    {
        if (gtk_widget_get_mapped(m_widget))
        {
#ifdef __WXGTK4__
            gtk_widget_queue_draw(m_widget);
            wxGTKRefreshChildren(this, rect);
#else
            if (rect)
                gtk_widget_queue_draw_area(m_widget, rect->x, rect->y, rect->width, rect->height);
            else
                gtk_widget_queue_draw(m_widget);
#endif
        }
    }
}

void wxWindowGTK::Update()
{
    if (m_widget && gtk_widget_get_mapped(m_widget) && m_width > 0 && m_height > 0)
    {
#ifdef __WXGTK4__
        GdkSurface* const window = GTKGetMainWindow();
#else
        GdkWindow* const window = GTKGetMainWindow();
#endif

#ifdef GDK_WINDOWING_WAYLAND
        if (wxGTKImpl::IsWayland(window))
        {
            // Using the functions below with Wayland seems to break something
            // in the update logic, with future updates just getting lost, see
            // #25036, so don't use it in this case, especially as it doesn't
            // even seem to work anyhow.
            return;
        }
#endif // GDK_WINDOWING_WAYLAND

        GdkDisplay* display = gtk_widget_get_display(m_widget);
        // If window has just been shown, drawing may not work unless pending
        // requests queued for the windowing system are flushed first.
        gdk_display_flush(display);

#ifdef __WXGTK4__
        // gdk_window_process_updates() is gone. GTK4 does its layout and its
        // painting in the frame clock's phases rather than synchronously, so
        // merely queueing a redraw would leave Update() a no-op: the repaint,
        // and any re-layout still pending with it, would not happen until some
        // later frame, which is not what wxWindow::Update() promises. Running
        // the main loop is not enough either -- wxYield() returns long before
        // the clock next ticks.
        //
        // So ask for a frame and then pump the main loop until the clock says
        // it has produced one. The wait is bounded because a window that is
        // not being presented -- unmapped, occluded, or on a compositor which
        // has stopped sending frame events -- may never produce another frame,
        // and Update() must not hang in that case.
        gtk_widget_queue_draw(m_wxwindow ? m_wxwindow : m_widget);

        // GDK_IS_FRAME_CLOCK() rather than a null check: a widget whose
        // surface is going away can hand back something that is not one.
        GdkFrameClock* const clock = gtk_widget_get_frame_clock(m_widget);
        if ( GDK_IS_FRAME_CLOCK(clock) )
        {
            // Held for the duration: the loop below runs the main loop, and
            // the window may well be on its way out -- this is called from
            // teardown paths -- in which case the clock would be destroyed
            // under it.
            wxGtkObject<GdkFrameClock> keepAlive(GDK_FRAME_CLOCK(g_object_ref(clock)));

            const gint64 frame = gdk_frame_clock_get_frame_counter(clock);
            const gint64 deadline = g_get_monotonic_time() + 500000; // 0.5s

            gdk_frame_clock_request_phase(clock, GDK_FRAME_CLOCK_PHASE_PAINT);

            // The loop below dispatches whatever source is ready, and wx's
            // idle source is one of them: leaving it enabled lets a repaint
            // delete the windows queued with Destroy(), one of which can be
            // an ancestor -- or the owner -- of whoever called Update().
            wxGTKIdleSuppressor suppressIdle;

            while ( gdk_frame_clock_get_frame_counter(clock) == frame &&
                    g_get_monotonic_time() < deadline )
            {
                if ( !g_main_context_iteration(nullptr, FALSE) )
                    g_usleep(1000);
            }
        }
#else
        wxGCC_WARNING_SUPPRESS(deprecated-declarations)
        gdk_window_process_updates(window, true);
        wxGCC_WARNING_RESTORE(deprecated-declarations)
#endif

        gdk_display_flush(display);
    }
}

bool wxWindowGTK::DoIsExposed( int x, int y ) const
{
    return m_updateRegion.Contains(x, y) != wxOutRegion;
}

bool wxWindowGTK::DoIsExposed( int x, int y, int w, int h ) const
{
#ifndef __WXGTK3__
    if (GetLayoutDirection() == wxLayout_RightToLeft)
        return m_updateRegion.Contains(x-w, y, w, h) != wxOutRegion;
#endif

    return m_updateRegion.Contains(x, y, w, h) != wxOutRegion;
}

#if defined(__WXGTK4__) && !defined(__WXUNIVERSAL__)

void wxWindowGTK::GTKDrawBorder(cairo_t* cr)
{
    if ( !HasFlag(wxPizza::BORDER_STYLES) )
        return;

    const int w = gtk_widget_get_width(m_wxwindow);
    const int h = gtk_widget_get_height(m_wxwindow);
    if ( w <= 0 || h <= 0 )
        return;

    cairo_save(cr);

    if ( HasFlag(wxBORDER_SIMPLE) )
    {
        // The "border-color" property query went away with
        // gtk_style_context_get(); use the theme's conventional colour name
        // through the same shared helper as the rest of the port.
        wxColour colBorder;
        GtkStyleContext* const sc = gtk_widget_get_style_context(m_wxwindow);
        if ( !wxGTKLookupThemeColour(sc, "borders", colBorder) )
            colBorder = *wxBLACK;

        cairo_set_source_rgba(cr,
                              colBorder.Red() / 255.0,
                              colBorder.Green() / 255.0,
                              colBorder.Blue() / 255.0,
                              colBorder.Alpha() / 255.0);
        cairo_set_line_width(cr, 1);
        cairo_rectangle(cr, 0.5, 0.5, w - 1, h - 1);
        cairo_stroke(cr);
    }
    else if ( HasFlag(wxBORDER_RAISED | wxBORDER_SUNKEN | wxBORDER_THEME) )
    {
        //TODO: wxBORDER_RAISED and wxBORDER_SUNKEN are not distinguished,
        //      matching what the GTK3 code did.
        GtkStyleContext* const
            sc = gtk_widget_get_style_context(wxGTKPrivate::GetEntryWidget());

        gtk_render_frame(sc, cr, 0, 0, w, h);
    }

    cairo_restore(cr);
}

#endif // __WXGTK4__ && !__WXUNIVERSAL__

#ifdef __WXGTK3__
void wxWindowGTK::GTKSendPaintEvents(cairo_t* cr)
#else
void wxWindowGTK::GTKSendPaintEvents(const GdkRegion* region)
#endif
{
#ifdef __WXGTK3__
#ifdef __WXGTK4__
    // No clip to apply: there is no GdkWindow to take a clip region from, and
    // the cairo_t from gtk_snapshot_append_cairo() is already clipped to the
    // widget's bounds.
    //
    // Nor is there a damage region to narrow it to. GTK4 removed partial
    // invalidation entirely (gtk_widget_queue_draw_area() is gone) and tells a
    // widget nothing about what changed -- the renderer culls by diffing
    // render nodes instead. So m_updateRegion below ends up being the whole
    // client area every time, and wxWindow::GetUpdateRegion() reports that.
    // Repainting more than necessary is always safe, but applications using
    // the update region as an optimisation lose it. See
    // docs/gtk/gtk4-phase4-paint-model-design.md section 3.
    if (GetLayoutDirection() == wxLayout_RightToLeft)
    {
        // wxDC is mirrored for RTL
        const int w = gtk_widget_get_width(m_wxwindow);
        cairo_translate(cr, w, 0);
        cairo_scale(cr, -1, 1);
    }
#else
    {
        cairo_region_t* region = gdk_window_get_clip_region(gtk_widget_get_window(m_wxwindow));
        cairo_rectangle_int_t rect;
        cairo_region_get_extents(region, &rect);
        cairo_region_destroy(region);
        cairo_rectangle(cr, rect.x, rect.y, rect.width, rect.height);
        cairo_clip(cr);
    }
    if (GetLayoutDirection() == wxLayout_RightToLeft)
    {
        // wxDC is mirrored for RTL
        const int w = gdk_window_get_width(gtk_widget_get_window(m_wxwindow));
        cairo_translate(cr, w, 0);
        cairo_scale(cr, -1, 1);
    }
#endif
    double x1, y1, x2, y2;
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);

    if (x1 >= x2 || y1 >= y2)
        return;

    m_paintContext = cr;
    m_updateRegion = wxRegion(int(x1), int(y1), int(x2 - x1), int(y2 - y1));
#else // !__WXGTK3__
    m_updateRegion = wxRegion(region);
#if wxGTK_HAS_COMPOSITING_SUPPORT
    cairo_t* cr = nullptr;
#endif
#endif // !__WXGTK3__
    // Clip to paint region in wxClientDC
    m_clipPaintRegion = true;

    m_nativeUpdateRegion = m_updateRegion;

#ifndef __WXGTK3__
    if (GetLayoutDirection() == wxLayout_RightToLeft)
    {
        // Transform m_updateRegion under RTL
        m_updateRegion.Clear();

        const int width = gdk_window_get_width(GTKGetDrawingWindow());

        wxRegionIterator upd( m_nativeUpdateRegion );
        while (upd)
        {
            wxRect rect;
            rect.x = upd.GetX();
            rect.y = upd.GetY();
            rect.width = upd.GetWidth();
            rect.height = upd.GetHeight();

            rect.x = width - rect.x - rect.width;
            m_updateRegion.Union( rect );

            ++upd;
        }
    }
#endif

    switch ( GetBackgroundStyle() )
    {
        case wxBG_STYLE_TRANSPARENT:
#if wxGTK_HAS_COMPOSITING_SUPPORT
            if (IsTransparentBackgroundSupported())
            {
                // Set a transparent background, so that overlaying in parent
                // might indeed let see through where this child did not
                // explicitly paint.
                // NB: it works also for top level windows (but this is the
                // windows manager which then does the compositing job)
#ifndef __WXGTK3__
                cr = gdk_cairo_create(m_wxwindow->window);
                gdk_cairo_region(cr, m_nativeUpdateRegion.GetRegion());
                cairo_clip(cr);
#endif
                cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
                cairo_paint(cr);
                cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
#ifndef __WXGTK3__
                cairo_surface_flush(cairo_get_target(cr));
#endif
            }
#endif // wxGTK_HAS_COMPOSITING_SUPPORT
            break;

        case wxBG_STYLE_ERASE:
        case wxBG_STYLE_COLOUR:
            {
#ifdef __WXGTK3__
                wxGTKCairoDC dc(cr, static_cast<wxWindow*>(this), GetLayoutDirection());
#else
                wxWindowDC dc( (wxWindow*)this );
                dc.SetDeviceClippingRegion( m_updateRegion );

                // Work around gtk-qt <= 0.60 bug whereby the window colour
                // remains grey
                if ( UseBgCol() &&
                        wxSystemOptions::
                            GetOptionInt("gtk.window.force-background-colour") )
                {
                    dc.SetBackground(GetBackgroundColour());
                    dc.Clear();
                }
#endif // !__WXGTK3__
                wxEraseEvent erase_event( GetId(), &dc );
                erase_event.SetEventObject( this );

                if ( HandleWindowEvent(erase_event) )
                {
                    // background erased, don't do it again
                    break;
                }
            }
            wxFALLTHROUGH;

        case wxBG_STYLE_SYSTEM:
            if ( GetThemeEnabled() )
            {
#ifdef __WXGTK4__
                const int w = gtk_widget_get_width(m_wxwindow);
                const int h = gtk_widget_get_height(m_wxwindow);
#else
                GdkWindow* gdkWindow = GTKGetDrawingWindow();
                const int w = gdk_window_get_width(gdkWindow);
                const int h = gdk_window_get_height(gdkWindow);
#endif
#ifdef __WXGTK3__
                GtkStyleContext* sc = gtk_widget_get_style_context(m_wxwindow);
                gtk_render_background(sc, cr, 0, 0, w, h);
#else
                // find ancestor from which to steal background
                wxWindow *parent = wxGetTopLevelParent((wxWindow *)this);
                if (!parent)
                    parent = (wxWindow*)this;
                GdkRectangle rect;
                m_nativeUpdateRegion.GetBox(rect.x, rect.y, rect.width, rect.height);
                gtk_paint_flat_box(gtk_widget_get_style(parent->m_widget),
                                    gdkWindow,
                                    gtk_widget_get_state(m_wxwindow),
                                    GTK_SHADOW_NONE,
                                    &rect,
                                    parent->m_widget,
                                    const_cast<char*>("base"),
                                    0, 0, w, h);
#endif // !__WXGTK3__
            }
#ifdef __WXGTK3__
            else if (m_backgroundColour.IsOk() && gtk_check_version(3,20,0) == nullptr)
            {
                cairo_save(cr);
                gdk_cairo_set_source_rgba(cr, m_backgroundColour.GTKGetRGBA());
                cairo_paint(cr);
                cairo_restore(cr);
            }
#endif
            break;

        case wxBG_STYLE_PAINT:
            // nothing to do: window will be painted over in EVT_PAINT
            break;

        default:
            wxFAIL_MSG( "unsupported background style" );
    }

    wxNcPaintEvent nc_paint_event( this );
    HandleWindowEvent( nc_paint_event );

    wxPaintEvent paint_event( this );
    HandleWindowEvent( paint_event );

#if wxGTK_HAS_COMPOSITING_SUPPORT
    if (IsTransparentBackgroundSupported())
    { // now composite children which need it
        // Overlay all our composite children on top of the painted area
        wxWindowList::compatibility_iterator node;
        for ( node = m_children.GetFirst(); node ; node = node->GetNext() )
        {
            wxWindow *compositeChild = node->GetData();
            if (compositeChild->GetBackgroundStyle() == wxBG_STYLE_TRANSPARENT &&
                !compositeChild->IsTopLevel())
            {
#ifndef __WXGTK3__
                if (cr == nullptr)
                {
                    cr = gdk_cairo_create(m_wxwindow->window);
                    gdk_cairo_region(cr, m_nativeUpdateRegion.GetRegion());
                    cairo_clip(cr);
                }
#endif // !__WXGTK3__
                GtkWidget *child = compositeChild->m_wxwindow;
                GtkAllocation alloc;
                gtk_widget_get_allocation(child, &alloc);

                // The source data is the (composited) child
                gdk_cairo_set_source_window(
                    cr, gtk_widget_get_window(child), alloc.x, alloc.y);

                cairo_paint(cr);
            }
        }
#ifndef __WXGTK3__
        if (cr)
            cairo_destroy(cr);
#endif
    }
#endif // wxGTK_HAS_COMPOSITING_SUPPORT

#if defined(__WXGTK4__) && !defined(__WXUNIVERSAL__)
    // GTK3 painted the border from a handler on the *parent's* draw signal,
    // connected with connect_after so it overlaid the child's content. GTK4
    // has neither that signal nor a parent window to distinguish, so wx paints
    // it here: same "after the content" ordering, with coordinates becoming
    // child-relative, which works out because the stroke always fell just
    // inside the child's own bounds anyway.
    //
    // This also restores the BORDER_STYLES rendering that went missing when
    // wxPizza became windowless (status update 8).
    GTKDrawBorder(cr);
#endif

    m_clipPaintRegion = false;
#ifdef __WXGTK3__
    m_paintContext = nullptr;
#endif
    m_updateRegion.Clear();
    m_nativeUpdateRegion.Clear();
}

void wxWindowGTK::SetDoubleBuffered( bool on )
{
    wxCHECK_RET( (m_widget != nullptr), wxT("invalid window") );

#ifdef __WXGTK4__
    // GTK4 always double-buffers and removed the ability to turn it off.
    wxUnusedVar(on);
#else
    if ( m_wxwindow )
    {
        wxGCC_WARNING_SUPPRESS(deprecated-declarations)
        gtk_widget_set_double_buffered( m_wxwindow, on );
        wxGCC_WARNING_RESTORE(deprecated-declarations)
    }
#endif
}

bool wxWindowGTK::IsDoubleBuffered() const
{
#ifdef __WXGTK4__
    // Always true under GTK4, which has no way to disable it.
    return true;
#else
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    return gtk_widget_get_double_buffered( m_wxwindow ) != 0;
    wxGCC_WARNING_RESTORE(deprecated-declarations)
#endif
}

void wxWindowGTK::ClearBackground()
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid window") );
}

#if wxUSE_TOOLTIPS
void wxWindowGTK::DoSetToolTip( wxToolTip *tip )
{
    if (m_tooltip != tip)
    {
        wxWindowBase::DoSetToolTip(tip);

        if (m_tooltip)
            m_tooltip->GTKSetWindow(static_cast<wxWindow*>(this));
        else
            GTKApplyToolTip(nullptr);
    }
}

void wxWindowGTK::GTKApplyToolTip(const char* tip)
{
    wxToolTip::GTKApply(GetConnectWidget(), tip);
}
#endif // wxUSE_TOOLTIPS

bool wxWindowGTK::SetBackgroundColour( const wxColour &colour )
{
    if (!wxWindowBase::SetBackgroundColour(colour))
        return false;

    if (m_widget)
    {
#ifndef __WXGTK3__
        if (colour.IsOk())
        {
            // We need the pixel value e.g. for background clearing.
            m_backgroundColour.CalcPixel(gtk_widget_get_colormap(m_widget));
        }
#endif

        // apply style change (forceStyle=true so that new style is applied
        // even if the bg colour changed from valid to wxNullColour)
        GTKApplyWidgetStyle(true);
    }

    return true;
}

bool wxWindowGTK::SetForegroundColour( const wxColour &colour )
{
    if (!wxWindowBase::SetForegroundColour(colour))
        return false;

    if (m_widget)
    {
#ifndef __WXGTK3__
        if (colour.IsOk())
        {
            // We need the pixel value e.g. for background clearing.
            m_foregroundColour.CalcPixel(gtk_widget_get_colormap(m_widget));
        }
#endif

        // apply style change (forceStyle=true so that new style is applied
        // even if the bg colour changed from valid to wxNullColour):
        GTKApplyWidgetStyle(true);
    }

    return true;
}

PangoContext *wxWindowGTK::GTKGetPangoDefaultContext()
{
    return gtk_widget_get_pango_context( m_widget );
}

#ifdef __WXGTK3__

// GTK4's CSS parser insists that the last declaration in a block be terminated
// with a semicolon, which GTK3's did not -- and every stylesheet wx builds,
// here and in half a dozen controls, is written in the shorter form. Rather
// than fix each of them and trust nobody to write another, put the semicolon
// in where it is missing on the way through.
//
// See docs/gtk/probes/gtk4-css-parser.c; this is also checked for by
// build/tools/gtk4-invariants.c, as nothing documents it.
static void wxGTKLoadCssData(GtkCssProvider* provider, const char* style)
{
#ifdef __WXGTK4__
    wxCharBuffer fixed;
    if ( style )
    {
        wxString css;
        css.reserve(strlen(style) + 8);

        for ( const char* p = style; *p; p++ )
        {
            if ( *p == '}' )
            {
                // Look back past any whitespace: an empty block needs nothing
                // adding to it, and one already terminated must not get a
                // second semicolon.
                const char* q = p;
                while ( q > style && isspace(static_cast<unsigned char>(q[-1])) )
                    q--;

                if ( q > style && q[-1] != ';' && q[-1] != '{' )
                    css += ';';
            }

            css += *p;
        }

        fixed = css.utf8_str();
        style = fixed.data();
    }

    gtk_css_provider_load_from_data(provider, style, -1);
#else
    gtk_css_provider_load_from_data(provider, style, -1, nullptr);
#endif
}

void wxWindowGTK::GTKApplyCssStyle(GtkCssProvider* provider, const char* style)
{
    wxCHECK_RET(m_widget, "invalid window");

    gtk_style_context_remove_provider(gtk_widget_get_style_context(m_widget),
                                      GTK_STYLE_PROVIDER(provider));

    wxGTKLoadCssData(provider, style);

    gtk_style_context_add_provider(gtk_widget_get_style_context(m_widget),
                                   GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void wxWindowGTK::GTKApplyCssStyle(const char* style)
{
    GtkCssProvider* provider = gtk_css_provider_new();
    GTKApplyCssStyle(provider, style);
    g_object_unref(provider);
}
#else // GTK+ < 3
GtkRcStyle* wxWindowGTK::GTKCreateWidgetStyle()
{
    GtkRcStyle *style = gtk_rc_style_new();

    if ( m_font.IsOk() )
    {
        style->font_desc =
            pango_font_description_copy( m_font.GetNativeFontInfo()->description );
    }

    int flagsNormal = 0,
        flagsPrelight = 0,
        flagsActive = 0,
        flagsInsensitive = 0;

    if ( m_foregroundColour.IsOk() )
    {
        const GdkColor *fg = m_foregroundColour.GetColor();

        style->fg[GTK_STATE_NORMAL] =
        style->text[GTK_STATE_NORMAL] = *fg;
        flagsNormal |= GTK_RC_FG | GTK_RC_TEXT;

        style->fg[GTK_STATE_PRELIGHT] =
        style->text[GTK_STATE_PRELIGHT] = *fg;
        flagsPrelight |= GTK_RC_FG | GTK_RC_TEXT;

        style->fg[GTK_STATE_ACTIVE] =
        style->text[GTK_STATE_ACTIVE] = *fg;
        flagsActive |= GTK_RC_FG | GTK_RC_TEXT;
    }

    if ( m_backgroundColour.IsOk() )
    {
        const GdkColor *bg = m_backgroundColour.GetColor();

        style->bg[GTK_STATE_NORMAL] =
        style->base[GTK_STATE_NORMAL] = *bg;
        flagsNormal |= GTK_RC_BG | GTK_RC_BASE;

        style->bg[GTK_STATE_PRELIGHT] =
        style->base[GTK_STATE_PRELIGHT] = *bg;
        flagsPrelight |= GTK_RC_BG | GTK_RC_BASE;

        style->bg[GTK_STATE_ACTIVE] =
        style->base[GTK_STATE_ACTIVE] = *bg;
        flagsActive |= GTK_RC_BG | GTK_RC_BASE;

        style->bg[GTK_STATE_INSENSITIVE] =
        style->base[GTK_STATE_INSENSITIVE] = *bg;
        flagsInsensitive |= GTK_RC_BG | GTK_RC_BASE;
    }

    style->color_flags[GTK_STATE_NORMAL] = (GtkRcFlags)flagsNormal;
    style->color_flags[GTK_STATE_PRELIGHT] = (GtkRcFlags)flagsPrelight;
    style->color_flags[GTK_STATE_ACTIVE] = (GtkRcFlags)flagsActive;
    style->color_flags[GTK_STATE_INSENSITIVE] = (GtkRcFlags)flagsInsensitive;

    return style;
}
#endif // !__WXGTK3__

void wxWindowGTK::GTKApplyWidgetStyle(bool forceStyle)
{
    const wxColour& fg = m_foregroundColour;
    const wxColour& bg = m_backgroundColour;
    const bool isFg = fg.IsOk();
    const bool isBg = bg.IsOk();
    const bool isFont = m_font.IsOk();
    if (forceStyle || isFg || isBg || isFont)
    {
#ifdef __WXGTK3__
        GString* css = g_string_new("*{");
        if (isFg)
        {
            g_string_append_printf(css, "color:%s;",
                wxGtkString(gdk_rgba_to_string(fg.GTKGetRGBA())).c_str());
        }
        if (isBg)
        {
            g_string_append_printf(css, "background:%s;",
                wxGtkString(gdk_rgba_to_string(bg.GTKGetRGBA())).c_str());
        }
        if (isFont)
        {
#ifdef __WXGTK4__
            // Remembered so that the whole shorthand can be dropped again if
            // it turns out to have no size, see below.
            const gsize cssFontStart = css->len;
#endif // __WXGTK4__

            g_string_append(css, "font:");
            const PangoFontDescription* pfd = m_font.GetNativeFontInfo()->description;
            if (gtk_check_version(3,22,0))
                g_string_append(css, wxGtkString(pango_font_description_to_string(pfd)));
            else
            {
                const PangoFontMask pfm = pango_font_description_get_set_fields(pfd);
                if (pfm & PANGO_FONT_MASK_STYLE)
                {
                    const char* s = "";
                    switch (pango_font_description_get_style(pfd))
                    {
                    case PANGO_STYLE_NORMAL: break;
                    case PANGO_STYLE_OBLIQUE: s = "oblique "; break;
                    case PANGO_STYLE_ITALIC: s = "italic "; break;
                    }
                    g_string_append(css, s);
                }
                if (pfm & PANGO_FONT_MASK_VARIANT)
                {
                    switch (pango_font_description_get_variant(pfd))
                    {
                    case PANGO_VARIANT_NORMAL:
                        break;
                    case PANGO_VARIANT_SMALL_CAPS:
                        g_string_append(css, "small-caps ");
                        break;
#if PANGO_VERSION_CHECK(1,50,0)
                    case PANGO_VARIANT_ALL_SMALL_CAPS:
                        g_string_append(css, "all-small-caps ");
                        break;
                    case PANGO_VARIANT_PETITE_CAPS:
                        g_string_append(css, "petite-caps ");
                        break;
                    case PANGO_VARIANT_ALL_PETITE_CAPS:
                        g_string_append(css, "all-petite-caps ");
                        break;
                    case PANGO_VARIANT_UNICASE:
                        g_string_append(css, "unicase ");
                        break;
                    case PANGO_VARIANT_TITLE_CAPS:
                        g_string_append(css, "titling-caps ");
                        break;
#endif // Pango 1.50+
                    }
                }
                if (pfm & PANGO_FONT_MASK_WEIGHT)
                {
                    const int weight = pango_font_description_get_weight(pfd);
                    if (weight != PANGO_WEIGHT_NORMAL)
                        g_string_append_printf(css, "%d ", weight);
                }
                if (pfm & PANGO_FONT_MASK_STRETCH)
                {
                    const char* s = "";
                    switch (pango_font_description_get_stretch(pfd))
                    {
                    case PANGO_STRETCH_ULTRA_CONDENSED: s = "ultra-condensed "; break;
                    case PANGO_STRETCH_EXTRA_CONDENSED: s = "extra-condensed "; break;
                    case PANGO_STRETCH_CONDENSED: s = "condensed "; break;
                    case PANGO_STRETCH_SEMI_CONDENSED: s = "semi-condensed "; break;
                    case PANGO_STRETCH_NORMAL: break;
                    case PANGO_STRETCH_SEMI_EXPANDED: s = "semi-expanded "; break;
                    case PANGO_STRETCH_EXPANDED: s = "expanded "; break;
                    case PANGO_STRETCH_EXTRA_EXPANDED: s = "extra-expanded "; break;
                    case PANGO_STRETCH_ULTRA_EXPANDED: s = "ultra-expanded "; break;
                    }
                    g_string_append(css, s);
                }
                if (pfm & PANGO_FONT_MASK_SIZE)
                {
                    const int size = pango_font_description_get_size(pfd);
                    if (pango_font_description_get_size_is_absolute(pfd))
                        g_string_append_printf(css, "%dpx ", size);
                    else
                        g_string_append_printf(css, "%dpt ", size / PANGO_SCALE);
                }
                if (pfm & PANGO_FONT_MASK_FAMILY)
                {
                    g_string_append_printf(css, "\"%s\"",
                        pango_font_description_get_family(pfd));
                }
#ifdef __WXGTK4__
                // The CSS "font" shorthand requires a size, and GTK4's parser
                // enforces it: without one the whole declaration is rejected
                // and the font is not applied at all. Fall back to naming the
                // family alone in that case, which is the only part of the
                // description there is anything to say about.
                if ( !(pfm & PANGO_FONT_MASK_SIZE) )
                {
                    g_string_truncate(css, cssFontStart);

                    if (pfm & PANGO_FONT_MASK_FAMILY)
                    {
                        g_string_append_printf(css, "font-family:\"%s\";",
                            pango_font_description_get_family(pfd));
                    }
                }
#endif // __WXGTK4__
            }
        }
        g_string_append_c(css, '}');

        if (isFg || isBg)
        {
            // Selection may be invisible, so add textview selection colors.
            // This is specifically for wxTextCtrl, but may be useful for other
            // controls, and seems to do no harm to apply to all.
            const wxColour fg_sel(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
            const wxColour bg_sel(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT));
            wxGtkString fg_sel_string(gdk_rgba_to_string(fg_sel.GTKGetRGBA()));
            wxGtkString bg_sel_string(gdk_rgba_to_string(bg_sel.GTKGetRGBA()));
            g_string_append_printf(css,
                "selection{color:%s;background:%s}"
                "*:selected{color:%s;background:%s}",
                fg_sel_string.c_str(), bg_sel_string.c_str(),
                fg_sel_string.c_str(), bg_sel_string.c_str());

            if (isFg && wx_is_at_least_gtk3(20))
            {
                g_string_append_printf(css, "*{caret-color:%s}",
                    wxGtkString(gdk_rgba_to_string(fg.GTKGetRGBA())).c_str());
            }
            if (isBg)
            {
                // make "undershoot" node background transparent,
                // keeps expected look of GtkEntry with default theme
                g_string_append(css, "* undershoot{background:transparent}");
            }
        }

        if (m_styleProvider == nullptr && (isFg || isBg || isFont))
            m_styleProvider = GTK_STYLE_PROVIDER(gtk_css_provider_new());

        wxGtkString s(g_string_free(css, false));
        if (m_styleProvider)
        {
            wxGTKLoadCssData(GTK_CSS_PROVIDER(m_styleProvider), s);
            DoApplyWidgetStyle(nullptr);
        }
#else
        GtkRcStyle* style = GTKCreateWidgetStyle();
        DoApplyWidgetStyle(style);
        g_object_unref(style);
#endif
    }
}

void wxWindowGTK::DoApplyWidgetStyle(GtkRcStyle *style)
{
    GtkWidget* widget = m_wxwindow ? m_wxwindow : m_widget;
    GTKApplyStyle(widget, style);
}

void wxWindowGTK::GTKApplyStyle(GtkWidget* widget, GtkRcStyle* WXUNUSED_IN_GTK3(style))
{
#ifdef __WXGTK3__
    if (m_styleProvider)
    {
        GtkStyleContext* context = gtk_widget_get_style_context(widget);
        gtk_style_context_add_provider(context,
            m_styleProvider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
#else
    gtk_widget_modify_style(widget, style);
#endif
}

bool wxWindowGTK::SetBackgroundStyle(wxBackgroundStyle style)
{
    if (!wxWindowBase::SetBackgroundStyle(style))
        return false;

#ifndef __WXGTK3__
    GdkWindow *window;
    if ((style == wxBG_STYLE_PAINT || style == wxBG_STYLE_TRANSPARENT) &&
        (window = GTKGetDrawingWindow()))
    {
        gdk_window_set_back_pixmap(window, nullptr, false);
    }
#endif // !__WXGTK3__

    return true;
}

bool wxWindowGTK::IsTransparentBackgroundSupported(wxString* reason) const
{
#ifdef __WXGTK4__
    wxUnusedVar(reason);
    return true;
#elif wxGTK_HAS_COMPOSITING_SUPPORT
#ifndef __WXGTK3__
    if (!wx_is_at_least_gtk2(12))
    {
        if (reason)
        {
            *reason = _("GTK+ installed on this machine is too old to "
                        "support screen compositing, please install "
                        "GTK+ 2.12 or later.");
        }

        return false;
    }
#endif // !__WXGTK3__

    // NB: We don't check here if the particular kind of widget supports
    // transparency, we check only if it would be possible for a generic window

    wxCHECK_MSG ( m_widget, false, "Window must be created first" );

    if (!gdk_screen_is_composited(gtk_widget_get_screen(m_widget)))
    {
        if (reason)
        {
            *reason = _("Compositing not supported by this system, "
                        "please enable it in your Window Manager.");
        }

        return false;
    }

    return true;
#else
    if (reason)
    {
        *reason = _("This program was compiled with a too old version of GTK+, "
                    "please rebuild with GTK+ 2.12 or newer.");
    }

    return false;
#endif // wxGTK_HAS_COMPOSITING_SUPPORT/!wxGTK_HAS_COMPOSITING_SUPPORT
}

#ifdef __WXGTK3__
#ifndef __WXGTK4__
GdkWindow* wxWindowGTK::GTKFindWindow(GtkWidget* widget)
{
    GdkWindow* window = gtk_widget_get_window(widget);
    if (window == nullptr)
        return nullptr;
    for (const GList* p = gdk_window_peek_children(window); p; p = p->next)
    {
        window = GDK_WINDOW(p->data);
        void* data;
        gdk_window_get_user_data(window, &data);
        if (data == widget)
            return window;
    }
    return nullptr;
}

void wxWindowGTK::GTKFindWindow(GtkWidget* widget, wxArrayGdkWindows& windows)
{
    GdkWindow* window = gtk_widget_get_window(widget);
    if (window == nullptr)
        return;
    for (const GList* p = gdk_window_peek_children(window); p; p = p->next)
    {
        window = GDK_WINDOW(p->data);
        void* data;
        gdk_window_get_user_data(window, &data);
        if (data == widget)
            windows.push_back(window);
    }
}
#endif // !__WXGTK4__
#endif // __WXGTK3__

// ----------------------------------------------------------------------------
// Pop-up menu stuff
// ----------------------------------------------------------------------------

#if wxUSE_MENUS_NATIVE

struct wxPopupMenuPositionCallbackData
{
    wxPoint pos;
    wxMenu *menu;
};

extern "C" {
#ifndef __WXGTK4__
static
void wxPopupMenuPositionCallback( GtkMenu *menu,
                                  gint *x, gint *y,
                                  gboolean * WXUNUSED(whatever),
                                  gpointer user_data )
{
    // ensure that the menu appears entirely on the same display as the window
    GtkRequisition req;
#ifdef __WXGTK3__
    gtk_widget_get_preferred_size(GTK_WIDGET(menu), &req, nullptr);
#else
    gtk_widget_get_child_requisition(GTK_WIDGET(menu), &req);
#endif

    const wxPopupMenuPositionCallbackData&
        data = *static_cast<wxPopupMenuPositionCallbackData*>(user_data);

    const wxRect
        rect = wxDisplay(data.menu->GetInvokingWindow()).GetClientArea();

    wxPoint pos = data.pos;

    if ( wxWindowGTK::GTKGetLayout(GTK_WIDGET(menu)) == wxLayout_RightToLeft )
        pos.x -= req.width;

    if ( pos.x < rect.x )
        pos.x = rect.x;
    if ( pos.y < rect.y )
        pos.y = rect.y;
    if ( pos.x + req.width > rect.GetRight() )
        pos.x = rect.GetRight() - req.width;
    if ( pos.y + req.height > rect.GetBottom() )
        pos.y = rect.GetBottom() - req.height;

    *x = pos.x;
    *y = pos.y;
}
#endif // !__WXGTK4__
}

bool wxWindowGTK::DoPopupMenu( wxMenu *menu, int x, int y )
{
#ifdef __WXGTK4__
    // GtkMenu and the whole gtk_menu_popup*() family are gone under GTK4:
    // menus are GMenuModel + GtkPopoverMenu now, and the modal "spin the main
    // loop until the menu closes" idiom below has no equivalent either. All of
    // this is handled by the menu backend itself, see menu.cpp.
    wxCHECK_MSG( m_widget != nullptr, false, wxT("invalid window") );

    return menu->GTKShowPopup(this, x, y);
#else
    wxCHECK_MSG( m_widget != nullptr, false, wxT("invalid window") );

    GTKSetLayout(menu->m_menu, GetLayoutDirection());

    menu->SetupBitmaps(this);

    wxPopupMenuPositionCallbackData data;
    gpointer userdata;
    GtkMenuPositionFunc posfunc;
    if ( x == -1 && y == -1 )
    {
        // use GTK's default positioning algorithm
        userdata = nullptr;
        posfunc = nullptr;
    }
    else
    {
        data.pos = ClientToScreen(wxPoint(x, y));
        data.menu = menu;
        userdata = &data;
        posfunc = wxPopupMenuPositionCallback;
    }

    menu->m_popupShown = true;
#if GTK_CHECK_VERSION(3,22,0)
    GdkWindow* const window = GTKGetMainWindow();
    if (wxGTKImpl::IsWayland(window) && wx_is_at_least_gtk3(22))
    {
        GdkEvent* currentEvent = gtk_get_current_event();
        GdkEvent* event = currentEvent;
        GdkDevice* device = event ? gdk_event_get_device(event) : nullptr;
        if (device == nullptr)
        {
            GdkSeat* seat = gdk_display_get_default_seat(gdk_window_get_display(window));
            device = gdk_seat_get_pointer(seat);
        }
        GdkEventButton eventTmp = { };
        if (event == nullptr)
        {
            // An event is needed to avoid a Gtk-WARNING "no trigger event for menu popup".
            // If a real one is not available, use a temporary with the fields
            // set that GTK is going to use.
            eventTmp.type = GDK_BUTTON_RELEASE;
            eventTmp.time = GDK_CURRENT_TIME;
            eventTmp.device = device;
            event = (GdkEvent*)&eventTmp;
        }
        if (x == -1 && y == -1)
        {
            if (gdk_device_get_source(device) == GDK_SOURCE_KEYBOARD)
            {
                // We can't get the position from this device in this case, as
                // gdk_window_get_device_position() would just fail with a
                // "critical" error, so use the global mouse position instead:
                // it should be what we want anyhow.
                wxGetMousePosition(&x, &y);
            }
            else
            {
                gdk_window_get_device_position(window, device, &x, &y, nullptr);
            }
        }
        else if (GetLayoutDirection() == wxLayout_RightToLeft)
        {
            x = gdk_window_get_width(window) - x;
        }

        const GdkRectangle rect = { x, y, 1, 1 };
        gtk_menu_popup_at_rect(GTK_MENU(menu->m_menu),
            window, &rect, GDK_GRAVITY_NORTH_WEST, GDK_GRAVITY_NORTH_WEST, event);

        if (currentEvent)
            gdk_event_free(currentEvent);
    }
    else
#endif // GTK_CHECK_VERSION(3,22,0)
    {
        wxGCC_WARNING_SUPPRESS(deprecated-declarations)

        gtk_menu_popup(
                  GTK_MENU(menu->m_menu),
                  nullptr,           // parent menu shell
                  nullptr,           // parent menu item
                  posfunc,                      // function to position it
                  userdata,                     // client data
                  0,                            // button used to activate it
                  gtk_get_current_event_time()
                );

        wxGCC_WARNING_RESTORE(deprecated-declarations)
    }

    // it is possible for gtk_menu_popup() to fail
    if (!gtk_widget_get_visible(GTK_WIDGET(menu->m_menu)))
    {
        menu->m_popupShown = false;
        return false;
    }

    while (menu->m_popupShown)
    {
        gtk_main_iteration();
    }

    return true;
#endif // __WXGTK4__/!__WXGTK4__
}

#endif // wxUSE_MENUS_NATIVE

#if wxUSE_DRAG_AND_DROP

void wxWindowGTK::SetDropTarget( wxDropTarget *dropTarget )
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid window") );

    GtkWidget *dnd_widget = GetConnectWidget();

    if (m_dropTarget) m_dropTarget->GtkUnregisterWidget( dnd_widget );

    delete m_dropTarget;
    m_dropTarget = dropTarget;

    if (m_dropTarget) m_dropTarget->GtkRegisterWidget( dnd_widget );
}

#endif // wxUSE_DRAG_AND_DROP

GtkWidget* wxWindowGTK::GetConnectWidget() const
{
    return m_wxwindow ? m_wxwindow : m_widget;
}

#ifndef __WXGTK4__
bool wxWindowGTK::GTKIsOwnWindow(GdkWindow *window) const
{
    wxArrayGdkWindows windowsThis;
    GdkWindow * const winThis = GTKGetWindow(windowsThis);

    return winThis ? window == winThis
                   : windowsThis.Index(window) != wxNOT_FOUND;
}

GdkWindow *wxWindowGTK::GTKGetWindow(wxArrayGdkWindows& WXUNUSED(windows)) const
{
    return GTKGetMainWindow();
}
#endif // !__WXGTK4__

#ifdef __WXGTK4__

GdkSurface* wxWindowGTK::GTKGetMainWindow() const
{
    return wx_gtk_widget_get_surface(m_wxwindow ? m_wxwindow : m_widget);
}

GdkSurface* wxWindowGTK::GTKGetConnectWindow() const
{
    return wx_gtk_widget_get_surface(GetConnectWidget());
}

#else

GdkWindow* wxWindowGTK::GTKGetMainWindow() const
{
    return gtk_widget_get_window(m_wxwindow ? m_wxwindow : m_widget);
}

GdkWindow* wxWindowGTK::GTKGetConnectWindow() const
{
    return gtk_widget_get_window(GetConnectWidget());
}

#endif // __WXGTK4__/!__WXGTK4__

#ifdef __WXGTK3__
void wxWindowGTK::GTKSizeRevalidate()
{
    GList* next;
    for (GList* p = gs_sizeRevalidateList; p; p = next)
    {
        next = p->next;
        wxWindow* win = static_cast<wxWindow*>(p->data);
        wxWindow* w = win;
        while (w && w->IsShown() && !w->IsTopLevel())
            w = w->GetParent();
        // If win is a child of this
        if (w == this)
        {
            win->InvalidateBestSize();
            gs_sizeRevalidateList = g_list_delete_link(gs_sizeRevalidateList, p);
            // Mark parents as needing size event
            m_needSizeEvent = true;
            while (win != this)
            {
                win = win->m_parent;
                if (win->m_needSizeEvent)
                    break;
                win->m_needSizeEvent = true;
            }
        }
    }
}

void wxWindowGTK::GTKSendSizeEventIfNeeded()
{
    if (m_needSizeEvent)
    {
        m_needSizeEvent = false;
        SendSizeEvent();
    }
}

extern "C" {
static gboolean before_resize(void* data)
{
    wxWindow* win = static_cast<wxWindow*>(data);
    win->InvalidateBestSize();
    return false;
}
}
#endif // __WXGTK3__

bool wxWindowGTK::SetFont( const wxFont &font )
{
    if (!wxWindowBase::SetFont(font))
        return false;

    if (m_widget)
    {
        // apply style change (forceStyle=true so that new style is applied
        // even if the font changed from valid to wxNullFont):
        GTKApplyWidgetStyle(true);
        InvalidateBestSize();
    }

#ifdef __WXGTK3__
    // Starting with GTK 3.6, style information is cached, and the cache is only
    // updated before resizing, or when showing a TLW. If a different size font
    // is set, our best size calculation will be wrong. All we can do is
    // invalidate the best size right before the style cache is updated, so any
    // subsequent best size requests use the correct font.
    if (gtk_check_version(3,8,0) == nullptr)
        gs_sizeRevalidateList = g_list_prepend(gs_sizeRevalidateList, this);
    else if (gtk_check_version(3,6,0) == nullptr)
    {
        wxWindow* tlw = wxGetTopLevelParent(static_cast<wxWindow*>(this));
        if (tlw->m_widget && gtk_widget_get_visible(tlw->m_widget))
            g_idle_add_full(GTK_PRIORITY_RESIZE - 1, before_resize, this, nullptr);
        else
            gs_sizeRevalidateList = g_list_prepend(gs_sizeRevalidateList, this);
    }
#endif

    return true;
}

void wxWindowGTK::DoCaptureMouse()
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid window") );

#ifdef __WXGTK4__
    // GTK4 removed every explicit pointer-grab API (gdk_seat_grab(),
    // gdk_pointer_grab(), gtk_grab_add()) with no replacement, because it
    // grabs implicitly instead: a gesture that claims a pointer sequence keeps
    // receiving that sequence's events until it ends, wherever the pointer
    // goes.
    //
    // That covers what wxWindow::CaptureMouse() is overwhelmingly used for --
    // tracking a drag between button-down and button-up -- so the bookkeeping
    // below is enough for that case, and the motion handler's g_captureWindow
    // path continues to work as before.
    //
    // What it does NOT cover is capturing outside a pointer sequence (e.g.
    // from a hover or a timer): there is no sequence to be implicitly grabbed,
    // so events will not be redirected to this window. Known gap, not runtime-
    // verified; see docs/gtk/gtk4-status.md.
#else
    GdkWindow* const window = GTKGetConnectWindow();
    wxCHECK_RET( window, wxT("CaptureMouse() failed") );

#if GTK_CHECK_VERSION(3,20,0)
    if (gtk_check_version(3,20,0) == nullptr)
    {
        GdkDisplay* display = gdk_window_get_display(window);
        GdkSeat* seat = gdk_display_get_default_seat(display);
        gdk_seat_grab(seat, window, GDK_SEAT_CAPABILITY_ALL_POINTING, false,
            nullptr, nullptr, nullptr, nullptr);
    }
    else
#endif
    {
        const GdkEventMask mask = GdkEventMask(
            GDK_SCROLL_MASK |
            GDK_BUTTON_PRESS_MASK |
            GDK_BUTTON_RELEASE_MASK |
            GDK_POINTER_MOTION_HINT_MASK |
            GDK_POINTER_MOTION_MASK);
        wxGCC_WARNING_SUPPRESS(deprecated-declarations)
        gdk_pointer_grab( window, FALSE,
                          mask,
                          nullptr,
                          nullptr,
                          (guint32)GDK_CURRENT_TIME );
        wxGCC_WARNING_RESTORE()
    }
#endif // __WXGTK4__/!__WXGTK4__

    g_captureWindow = this;
    g_captureWindowHasMouse = true;
}

void wxWindowGTK::DoReleaseMouse()
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid window") );

    wxCHECK_RET( g_captureWindow, wxT("can't release mouse - not captured") );

    g_captureWindow = nullptr;

#ifndef __WXGTK4__
    GdkWindow* const window = GTKGetConnectWindow();

    if (!window)
        return;

    GdkDisplay* display = gdk_window_get_display(window);
#if GTK_CHECK_VERSION(3,20,0)
    if (gtk_check_version(3,20,0) == nullptr)
        gdk_seat_ungrab(gdk_display_get_default_seat(display));
    else
#endif
    {
        wxGCC_WARNING_SUPPRESS(deprecated-declarations)
        gdk_display_pointer_ungrab(display, unsigned(GDK_CURRENT_TIME));
        wxGCC_WARNING_RESTORE()
    }
#else
    // Nothing to ungrab: GTK4 has no explicit grabs, and the implicit one a
    // gesture holds is released when its pointer sequence ends. See
    // DoCaptureMouse() above.
#endif
}

void wxWindowGTK::GTKReleaseMouseAndNotify()
{
#ifndef __WXGTK4__
    GdkDisplay* display = gtk_widget_get_display(m_widget);
#if GTK_CHECK_VERSION(3,20,0)
    if (gtk_check_version(3,20,0) == nullptr)
        gdk_seat_ungrab(gdk_display_get_default_seat(display));
    else
#endif
    {
        wxGCC_WARNING_SUPPRESS(deprecated-declarations)
        gdk_display_pointer_ungrab(display, unsigned(GDK_CURRENT_TIME));
        wxGCC_WARNING_RESTORE()
    }
#endif // !__WXGTK4__
    g_captureWindow = nullptr;
    NotifyCaptureLost();
}

void wxWindowGTK::GTKHandleCaptureLost()
{
    g_captureWindow = nullptr;
    NotifyCaptureLost();
}

/* static */
wxWindow *wxWindowBase::GetCapture()
{
    return (wxWindow *)g_captureWindow;
}

bool wxWindowGTK::IsRetained() const
{
    return false;
}

void wxWindowGTK::SetScrollbar(int orient,
                               int pos,
                               int thumbVisible,
                               int range,
                               bool WXUNUSED(update))
{
    const int dir = ScrollDirFromOrient(orient);
    wxGtkScrollbar* const sb = m_scrollBar[dir];
    wxCHECK_RET( sb, wxT("this window is not scrollable") );

    if (range <= 0)
    {
        // GtkRange requires upper > lower
        range =
        thumbVisible = 1;
    }
    else if (thumbVisible <= 0)
        thumbVisible = 1;

    g_signal_handlers_block_by_func(
        wxGtkScrollbarValueNotifier(sb), (void*)gtk_scrollbar_value_changed, this);

    GtkAdjustment* adj = wxGtkScrollbarGetAdjustment(sb);
    const bool wasVisible = gtk_adjustment_get_upper(adj) > gtk_adjustment_get_page_size(adj);

    g_object_freeze_notify(G_OBJECT(adj));
    wxGtkScrollbarSetIncrements(sb, 1, thumbVisible);
    gtk_adjustment_set_page_size(adj, thumbVisible);
    wxGtkScrollbarSetRange(sb, 0, range);
    g_object_thaw_notify(G_OBJECT(adj));

    wxGtkScrollbarSetValue(sb, pos);
    m_scrollPos[dir] = wxGtkScrollbarGetValue(sb);

    const bool isVisible = gtk_adjustment_get_upper(adj) > gtk_adjustment_get_page_size(adj);
    if (isVisible != wasVisible)
        m_useCachedClientSize = false;

    g_signal_handlers_unblock_by_func(
        wxGtkScrollbarValueNotifier(sb), (void*)gtk_scrollbar_value_changed, this);
}

void wxWindowGTK::SetScrollPos(int orient, int pos, bool WXUNUSED(refresh))
{
    const int dir = ScrollDirFromOrient(orient);
    wxGtkScrollbar * const sb = m_scrollBar[dir];
    wxCHECK_RET( sb, wxT("this window is not scrollable") );

    // This check is more than an optimization. Without it, the slider
    //   will not move smoothly while tracking when using wxScrollHelper.
    if (GetScrollPos(orient) != pos)
    {
        g_signal_handlers_block_by_func(
            wxGtkScrollbarValueNotifier(sb), (void*)gtk_scrollbar_value_changed, this);

        wxGtkScrollbarSetValue(sb, pos);
        m_scrollPos[dir] = wxGtkScrollbarGetValue(sb);

        g_signal_handlers_unblock_by_func(
            wxGtkScrollbarValueNotifier(sb), (void*)gtk_scrollbar_value_changed, this);
    }
}

int wxWindowGTK::GetScrollThumb(int orient) const
{
    wxGtkScrollbar * const sb = m_scrollBar[ScrollDirFromOrient(orient)];
    wxCHECK_MSG( sb, 0, wxT("this window is not scrollable") );

    return wxRound(gtk_adjustment_get_page_size(wxGtkScrollbarGetAdjustment(sb)));
}

int wxWindowGTK::GetScrollPos( int orient ) const
{
    wxGtkScrollbar * const sb = m_scrollBar[ScrollDirFromOrient(orient)];
    wxCHECK_MSG( sb, 0, wxT("this window is not scrollable") );

    return wxRound(wxGtkScrollbarGetValue(sb));
}

int wxWindowGTK::GetScrollRange( int orient ) const
{
    wxGtkScrollbar * const sb = m_scrollBar[ScrollDirFromOrient(orient)];
    wxCHECK_MSG( sb, 0, wxT("this window is not scrollable") );

    return wxRound(gtk_adjustment_get_upper(wxGtkScrollbarGetAdjustment(sb)));
}

// Determine if increment is the same as +/-x, allowing for some small
//   difference due to possible inexactness in floating point arithmetic
static inline bool IsScrollIncrement(double increment, double x)
{
    wxASSERT(increment > 0);
    const double tolerance = 1.0 / 1024;
    return fabs(increment - fabs(x)) < tolerance;
}

wxEventType wxWindowGTK::GTKGetScrollEventType(wxGtkScrollbar* range)
{
    wxASSERT(range == m_scrollBar[0] || range == m_scrollBar[1]);

    const int barIndex = range == m_scrollBar[1];

    GtkAdjustment* adj = wxGtkScrollbarGetAdjustment(range);
    const double value = gtk_adjustment_get_value(adj);

    // save previous position
    const double oldPos = m_scrollPos[barIndex];
    // update current position
    m_scrollPos[barIndex] = value;
    // If event should be ignored, or integral position has not changed
    // or scrollbar is disabled (webkitgtk is known to cause a "value-changed"
    // by setting the GtkAdjustment to all zeros)
    if (g_blockEventsOnDrag || wxRound(value) == wxRound(oldPos) ||
        gtk_adjustment_get_upper(adj) <= gtk_adjustment_get_page_size(adj))
    {
        return wxEVT_NULL;
    }

    wxEventType eventType = wxEVT_SCROLL_THUMBTRACK;
    if (!m_isScrolling)
    {
        // Difference from last change event
        const double diff = value - oldPos;
        const bool isDown = diff > 0;

        if (IsScrollIncrement(gtk_adjustment_get_step_increment(adj), diff))
        {
            eventType = isDown ? wxEVT_SCROLL_LINEDOWN : wxEVT_SCROLL_LINEUP;
        }
        else if (IsScrollIncrement(gtk_adjustment_get_page_increment(adj), diff))
        {
            eventType = isDown ? wxEVT_SCROLL_PAGEDOWN : wxEVT_SCROLL_PAGEUP;
        }
        else if (m_mouseButtonDown)
        {
            // Assume track event
            m_isScrolling = true;
        }
    }
    return eventType;
}

void wxWindowGTK::ScrollWindow( int dx, int dy, const wxRect* WXUNUSED(rect) )
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid window") );

    wxCHECK_RET( m_wxwindow != nullptr, wxT("window needs client area for scrolling") );

    // No scrolling requested.
    if ((dx == 0) && (dy == 0)) return;

    m_clipPaintRegion = true;

    WX_PIZZA(m_wxwindow)->scroll(dx, dy);

    m_clipPaintRegion = false;

#if wxUSE_CARET
    bool restoreCaret = (GetCaret() != nullptr && GetCaret()->IsVisible());
    if (restoreCaret)
    {
        wxRect caretRect(GetCaret()->GetPosition(), GetCaret()->GetSize());
        if (dx > 0)
            caretRect.width += dx;
        else
        {
            caretRect.x += dx; caretRect.width -= dx;
        }
        if (dy > 0)
            caretRect.height += dy;
        else
        {
            caretRect.y += dy; caretRect.height -= dy;
        }

        RefreshRect(caretRect);
    }
#endif // wxUSE_CARET
}

void wxWindowGTK::GTKScrolledWindowSetBorder(GtkWidget* w, int wxstyle)
{
    //RN: Note that static controls usually have no border on gtk, so maybe
    //it makes sense to treat that as simply no border at the wx level
    //as well...
    if (!(wxstyle & wxNO_BORDER) && !(wxstyle & wxBORDER_STATIC))
    {
#ifdef __WXGTK4__
        // GtkShadowType is gone: GTK4 reduced the scrolled window's border to
        // a boolean, so the in/out distinction (wxBORDER_RAISED vs the rest)
        // can no longer be expressed -- any wx border becomes a plain frame.
        gtk_scrolled_window_set_has_frame( GTK_SCROLLED_WINDOW(w), TRUE );
#else
        GtkShadowType gtkstyle = GTK_SHADOW_IN;

        if(wxstyle & wxBORDER_RAISED)
            gtkstyle = GTK_SHADOW_OUT;

        gtk_scrolled_window_set_shadow_type( GTK_SCROLLED_WINDOW(w),
                                             gtkstyle );
#endif
    }
}

// Get the current mouse position.
void wxGetMousePosition(int* x, int* y)
{
    GdkDisplay* display = wxGetTopLevelGdkDisplay();
#ifdef __WXGTK4__
    if ( !wxGTKQueryPointerX11(display, x, y, nullptr) )
    {
        // See the identical comment in wxGetMouseState() above: this is only
        // meaningful while the pointer is over one of this application's own
        // windows, not truly global screen coordinates.
        GdkSeat* seat = gdk_display_get_default_seat(display);
        GdkDevice* device = gdk_seat_get_pointer(seat);
        double dx = 0, dy = 0;
        gdk_device_get_surface_at_position(device, &dx, &dy);
        if (x) *x = gint(dx);
        if (y) *y = gint(dy);
    }
#elif defined(__WXGTK3__)
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    GdkDeviceManager* manager = gdk_display_get_device_manager(display);
    GdkDevice* device = gdk_device_manager_get_client_pointer(manager);
    wxGCC_WARNING_RESTORE()
    gdk_device_get_position(device, nullptr, x, y);
#else
    gdk_display_get_pointer(display, nullptr, x, y, nullptr);
#endif
}

#ifdef __WXGTK4__
GdkSurface* wxWindowGTK::GTKGetDrawingWindow() const
{
    return m_wxwindow ? wx_gtk_widget_get_surface(m_wxwindow) : nullptr;
}
#else
GdkWindow* wxWindowGTK::GTKGetDrawingWindow() const
{
    return m_wxwindow ? gtk_widget_get_window(m_wxwindow) : nullptr;
}
#endif

// ----------------------------------------------------------------------------
// freeze/thaw
// ----------------------------------------------------------------------------

#ifdef __WXGTK4__

// Freezing worked by connecting a draw handler that returned TRUE to swallow
// the event, and blocking/unblocking it. GTK4 has no draw signal to intercept,
// so the frozen state becomes a flag on the widget which wxPizza's snapshot
// vfunc checks -- see pizza_snapshot() in win_gtk.cpp, which skips both its
// own painting and its children's while set.
//
// Note this only suppresses painting for wxPizza widgets. GTK3 could freeze
// any widget, including native controls, because it intercepted the signal
// before the widget's own handler; under GTK4 a native control's snapshot
// vfunc cannot be intercepted from outside, so freezing one has no effect.
// Known gap.

void wxWindowGTK::GTKConnectFreezeWidget(GtkWidget* WXUNUSED(widget))
{
    // Nothing to connect: the flag below is checked directly.
}

void wxWindowGTK::GTKFreezeWidget(GtkWidget* widget)
{
    g_object_set_data(G_OBJECT(widget), "wx-frozen", GINT_TO_POINTER(1));
}

void wxWindowGTK::GTKThawWidget(GtkWidget* widget)
{
    g_object_set_data(G_OBJECT(widget), "wx-frozen", nullptr);
    gtk_widget_queue_draw(widget);
}

#else // !__WXGTK4__

extern "C" {
static gboolean draw_freeze(GtkWidget*, void*, wxWindow*)
{
    // stop other handlers from being invoked
    return true;
}
}

void wxWindowGTK::GTKConnectFreezeWidget(GtkWidget* widget)
{
#ifdef __WXGTK3__
    gulong id = g_signal_connect(widget, "draw", G_CALLBACK(draw_freeze), this);
#else
    gulong id = g_signal_connect(widget, "expose-event", G_CALLBACK(draw_freeze), this);
#endif
    g_signal_handler_block(widget, id);
}

void wxWindowGTK::GTKFreezeWidget(GtkWidget* widget)
{
    g_signal_handlers_unblock_by_func(widget, (void*)draw_freeze, this);
}

void wxWindowGTK::GTKThawWidget(GtkWidget* widget)
{
    g_signal_handlers_block_by_func(widget, (void*)draw_freeze, this);
    gtk_widget_queue_draw(widget);
}

#endif // __WXGTK4__/!__WXGTK4__

void wxWindowGTK::DoFreeze()
{
    wxCHECK_RET(m_widget, "invalid window");

    GTKFreezeWidget(m_widget);
    if (m_wxwindow && m_wxwindow != m_widget)
        GTKFreezeWidget(m_wxwindow);
}

void wxWindowGTK::DoThaw()
{
    wxCHECK_RET(m_widget, "invalid window");

    GTKThawWidget(m_widget);
    if (m_wxwindow && m_wxwindow != m_widget)
        GTKThawWidget(m_wxwindow);
}
