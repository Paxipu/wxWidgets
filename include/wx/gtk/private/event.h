///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/event.h
// Purpose:     Helper functions for working with GDK and wx events
// Author:      Vaclav Slavik
// Created:     2011-10-14
// Copyright:   (c) 2011 Vaclav Slavik
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _GTK_PRIVATE_EVENT_H_
#define _GTK_PRIVATE_EVENT_H_

#if !GTK_CHECK_VERSION(2,10,0)
    // GTK+ can reliably detect Meta key state only since 2.10 when
    // GDK_META_MASK was introduced -- there wasn't any way to detect it
    // in older versions. wxGTK used GDK_MOD2_MASK for this purpose, but
    // GDK_MOD2_MASK is documented as:
    //
    //     the fifth modifier key (it depends on the modifier mapping of the X
    //     server which key is interpreted as this modifier)
    //
    // In other words, it isn't guaranteed to map to Meta. This is a real
    // problem: it is common to map NumLock to it (in fact, it's an exception
    // if the X server _doesn't_ use it for NumLock).  So the old code caused
    // wxKeyEvent::MetaDown() to always return true as long as NumLock was
    // on many systems, which broke all applications using
    // wxKeyEvent::GetModifiers() to check modifiers state (see e.g.  here:
    // http://tinyurl.com/56lsk2).
    //
    // Because of this, it's better to not detect Meta key state at all than
    // to detect it incorrectly. Hence the following #define, which causes
    // m_metaDown to be always set to false.
    #define GDK_META_MASK 0
#endif

namespace wxGTKImpl
{

#ifdef __WXGTK4__

// Return where the pointer is, relative to the surface the widget is on.
//
// Defined in src/gtk/window.cpp rather than here because asking for this
// safely means guarding against a surface whose underlying window is already
// gone, and that guard is windowing-system specific.
bool GetPointerPosition(GtkWidget* widget, double* x, double* y);

// Return the position, relative to the surface, at which the given event
// happened.
//
// Not every GTK4 event carries one: gdk_event_get_position() returns false for
// those that don't -- and, importantly, sets both coordinates to NaN rather
// than leaving them alone, so its result can't just be ignored. Scroll events
// are the case that matters, as GdkScrollEvent stores only the deltas, so this
// covers the whole of the mouse wheel path; passing NaN on to InitMouseEvent()
// below reaches wxRound() and asserts.
//
// The position such an event happened at is where the pointer is, so ask the
// seat for it. Only if that fails as well -- the pointer having left the
// surface, say, which is possible for a leave event -- is the origin used.
inline bool GetEventPosition(GdkEvent* gdk_event,
                             GtkWidget* widget,
                             double* x,
                             double* y)
{
    if ( gdk_event && gdk_event_get_position(gdk_event, x, y) )
        return true;

    if ( widget && GetPointerPosition(widget, x, y) )
        return true;

    *x =
    *y = 0;
    return false;
}

// Init wxMouseEvent from a GdkEvent.
//
// Unlike GTK+ 3, where the event structs carried their own coordinates
// relative to the GdkWindow the event arrived on, GTK4 events are opaque and
// their position is relative to the surface, i.e. the toplevel. The event
// controllers which deliver these events do however hand out coordinates
// already relative to the widget they're attached to, so those are passed in
// here rather than extracted from the event: that is both more accurate and
// avoids the surface-to-widget translation entirely.
inline void InitMouseEvent(wxWindowGTK *win,
                           wxMouseEvent& event,
                           GdkEvent *gdk_event,
                           double x,
                           double y)
{
    // There may be no event at all: GtkEventControllerMotion::leave is emitted
    // without one when the pointer is taken away by something other than the
    // pointer moving, e.g. a grab. Everything read from the event keeps its
    // default in that case rather than provoking GTK warnings.
    const GdkModifierType state = gdk_event
                                    ? gdk_event_get_modifier_state(gdk_event)
                                    : GdkModifierType(0);

    event.m_shiftDown = (state & GDK_SHIFT_MASK) != 0;
    event.m_controlDown = (state & GDK_CONTROL_MASK) != 0;
    event.m_altDown = (state & GDK_ALT_MASK) != 0;
    event.m_metaDown = (state & GDK_META_MASK) != 0;
    event.m_leftDown = (state & GDK_BUTTON1_MASK) != 0;
    event.m_middleDown = (state & GDK_BUTTON2_MASK) != 0;
    event.m_rightDown = (state & GDK_BUTTON3_MASK) != 0;
    event.m_aux1Down = (state & GDK_BUTTON4_MASK) != 0;
    event.m_aux2Down = (state & GDK_BUTTON5_MASK) != 0;

    const wxPoint pt = win->GetClientAreaOrigin();
    event.m_x = wxRound(x) - pt.x;
    event.m_y = wxRound(y) - pt.y;

    // Note that the GTK+ 3 version has an extra correction here for no-window
    // widgets owning a GdkWindow covering part of their area. That can't
    // happen under GTK4, where no widget has a window of its own.

    if (win->GetLayoutDirection() == wxLayout_RightToLeft)
    {
        // origin in the upper right corner
        GtkAllocation a;
        gtk_widget_get_allocation(win->m_wxwindow ? win->m_wxwindow : win->m_widget, &a);
        int window_width = a.width;
        event.m_x = window_width - event.m_x;
    }

    event.SetEventObject( win );
    event.SetId( win->GetId() );
    if ( gdk_event )
        event.SetTimestamp( gdk_event_get_time(gdk_event) );
}

#else // !__WXGTK4__

// init wxMouseEvent with the info from GdkEventXXX struct
template<typename T> void InitMouseEvent(wxWindowGTK *win,
                                         wxMouseEvent& event,
                                         T *gdk_event)
{
    event.m_shiftDown = (gdk_event->state & GDK_SHIFT_MASK) != 0;
    event.m_controlDown = (gdk_event->state & GDK_CONTROL_MASK) != 0;
    event.m_altDown = (gdk_event->state & GDK_MOD1_MASK) != 0;
    event.m_metaDown = (gdk_event->state & GDK_META_MASK) != 0;
    event.m_leftDown = (gdk_event->state & GDK_BUTTON1_MASK) != 0;
    event.m_middleDown = (gdk_event->state & GDK_BUTTON2_MASK) != 0;
    event.m_rightDown = (gdk_event->state & GDK_BUTTON3_MASK) != 0;

    // In gdk/win32 VK_XBUTTON1 is translated to GDK_BUTTON4_MASK
    // and VK_XBUTTON2 to GDK_BUTTON5_MASK. In x11/gdk buttons 4/5
    // are wheel rotation and buttons 8/9 don't change the state.
    event.m_aux1Down = (gdk_event->state & GDK_BUTTON4_MASK) != 0;
    event.m_aux2Down = (gdk_event->state & GDK_BUTTON5_MASK) != 0;

    wxPoint pt = win->GetClientAreaOrigin();
    event.m_x = (wxCoord)gdk_event->x - pt.x;
    event.m_y = (wxCoord)gdk_event->y - pt.y;

    // Some no-window widgets, notably GtkEntry on GTK3, have a GdkWindow
    // covering part of their area. Event coordinates from that window are
    // not relative to the widget, so do the conversion here.
    if (win->m_wxwindow == nullptr && !gtk_widget_get_has_window(win->m_widget) &&
        gtk_widget_get_window(win->m_widget) == gdk_window_get_parent(gdk_event->window))
    {
        GtkAllocation a;
        gtk_widget_get_allocation(win->m_widget, &a);
        int posX, posY;
        gdk_window_get_position(gdk_event->window, &posX, &posY);

        event.m_x += posX - a.x;
        event.m_y += posY - a.y;
    }

    if (win->GetLayoutDirection() == wxLayout_RightToLeft)
    {
        // origin in the upper right corner
        GtkAllocation a;
        gtk_widget_get_allocation(win->m_wxwindow ? win->m_wxwindow : win->m_widget, &a);
        int window_width = a.width;
        event.m_x = window_width - event.m_x;
    }

    event.SetEventObject( win );
    event.SetId( win->GetId() );
    event.SetTimestamp( gdk_event->time );
}

#endif // __WXGTK4__/!__WXGTK4__

// Update the window currently known to be under the mouse pointer.
//
// Returns true if it was updated, false if this window was already known to
// contain the mouse pointer.
bool SetWindowUnderMouse(wxWindowGTK* win);

#ifdef __WXGTK4__

// Under GTK4 these aren't signal handlers for the widget any more: pointer
// events are delivered by GtkEventController objects attached to it, which
// pass the event along with widget-relative coordinates. The controllers
// themselves are set up by wxWindowGTK, these just turn one GTK4 event into
// the corresponding wx one.
//
// They return true if the event was handled and shouldn't be propagated
// further, matching what the GTK+ 3 signal handlers returned.

bool
WindowEnterCallback(wxWindowGTK* win, GdkEvent* event, double x, double y);

bool
WindowLeaveCallback(wxWindowGTK* win, GdkEvent* event);

bool
WindowMotionCallback(wxWindowGTK* win, GdkEvent* event, double x, double y,
                     bool synthesized = false);

bool
WindowButtonPressCallback(wxWindowGTK* win, GdkEvent* event,
                          int button, int nPress, double x, double y,
                          bool synthesized = false);

bool
WindowButtonReleaseCallback(wxWindowGTK* win, GdkEvent* event,
                            int button, double x, double y,
                            bool synthesized = false);

#else // !__WXGTK4__

// Implementation of enter/leave window callbacks.
gboolean
WindowEnterCallback(GtkWidget* widget,
                    GdkEventCrossing* event,
                    wxWindowGTK* win);

gboolean
WindowLeaveCallback(GtkWidget* widget,
                    GdkEventCrossing* event,
                    wxWindowGTK* win);

gboolean
WindowMotionCallback(GtkWidget* widget,
                     GdkEventMotion* event,
                     wxWindowGTK* win,
                     bool synthesized = false);

gboolean
WindowButtonPressCallback(GtkWidget* widget,
                          GdkEventButton* event,
                          wxWindowGTK* win,
                          bool synthesized = false);

gboolean
WindowButtonReleaseCallback(GtkWidget* widget,
                            GdkEventButton* event,
                            wxWindowGTK* win,
                            bool synthesized = false);

#endif // __WXGTK4__/!__WXGTK4__

} // namespace wxGTKImpl

#endif // _GTK_PRIVATE_EVENT_H_

