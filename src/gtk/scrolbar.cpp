/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/scrolbar.cpp
// Purpose:
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_SCROLLBAR

#include "wx/scrolbar.h"

#ifndef WX_PRECOMP
    #include "wx/utils.h"
#endif

#include "wx/gtk/private.h"

//-----------------------------------------------------------------------------
// "value_changed" from scrollbar
//-----------------------------------------------------------------------------

extern "C" {
static void
#ifdef __WXGTK4__
// A GtkScrollbar is not a GtkRange under GTK4 and has no "value_changed" of
// its own: the value belongs to its adjustment, which is what this is
// connected to. See wx/gtk/private.h.
gtk_value_changed(GtkAdjustment*, wxScrollBar* win)
{
    wxGtkScrollbar* const range = GTK_SCROLLBAR(win->m_widget);
#else
gtk_value_changed(GtkRange* range, wxScrollBar* win)
{
#endif
    wxEventType eventType = win->GTKGetScrollEventType(range);
    if (eventType != wxEVT_NULL)
    {
        const int orient = win->HasFlag(wxSB_VERTICAL) ? wxVERTICAL : wxHORIZONTAL;
        const int value = win->GetThumbPosition();
        const int id = win->GetId();

        // first send the specific event for the user action
        wxScrollEvent evtSpec(eventType, id, value, orient);
        evtSpec.SetEventObject(win);
        win->HandleWindowEvent(evtSpec);

        if (!win->m_isScrolling)
        {
            // and if it's over also send a general "changed" event
            wxScrollEvent evtChanged(wxEVT_SCROLL_CHANGED, id, value, orient);
            evtChanged.SetEventObject(win);
            win->HandleWindowEvent(evtChanged);
        }
    }
}
}

//-----------------------------------------------------------------------------
// "button_press_event" from scrollbar
//-----------------------------------------------------------------------------

#ifdef __WXGTK4__

extern "C" {

// Deferred emission of the thumb-release events.
//
// GTK3 achieved this with the "event_after" signal, which runs once GTK has
// finished handling the event, so a handler is free to set the scroll position
// from it. GTK4 has no such signal; an idle callback lands at the equivalent
// point, after the current event's handling completes.
static gboolean wx_gtk_send_thumb_release(void* data)
{
    wxScrollBar* const win = static_cast<wxScrollBar*>(data);

    const int value = win->GetThumbPosition();
    const int orient = win->HasFlag(wxSB_VERTICAL) ? wxVERTICAL : wxHORIZONTAL;
    const int id = win->GetId();

    wxScrollEvent evtRel(wxEVT_SCROLL_THUMBRELEASE, id, value, orient);
    evtRel.SetEventObject(win);
    win->HandleWindowEvent(evtRel);

    wxScrollEvent evtChanged(wxEVT_SCROLL_CHANGED, id, value, orient);
    evtChanged.SetEventObject(win);
    win->HandleWindowEvent(evtChanged);

    return G_SOURCE_REMOVE;
}

static void
wx_gtk_scrollbar_pressed(GtkGestureClick*, int, double, double, wxScrollBar* win)
{
    win->m_mouseButtonDown = true;
}

static void
wx_gtk_scrollbar_released(GtkGestureClick*, int, double, double, wxScrollBar* win)
{
    win->m_mouseButtonDown = false;

    if (win->m_isScrolling)
    {
        win->m_isScrolling = false;
        g_idle_add(wx_gtk_send_thumb_release, win);
    }
}

} // extern "C"

#else // !__WXGTK4__

extern "C" {
static gboolean
gtk_button_press_event(GtkRange*, GdkEventButton*, wxScrollBar* win)
{
    win->m_mouseButtonDown = true;
    return false;
}
}

//-----------------------------------------------------------------------------
// "event_after" from scrollbar
//-----------------------------------------------------------------------------

extern "C" {
static void
gtk_event_after(GtkRange* range, GdkEvent* event, wxScrollBar* win)
{
    if (event->type == GDK_BUTTON_RELEASE)
    {
        g_signal_handlers_block_by_func(range, (void*)gtk_event_after, win);

        const int value = win->GetThumbPosition();
        const int orient = win->HasFlag(wxSB_VERTICAL) ? wxVERTICAL : wxHORIZONTAL;
        const int id = win->GetId();

        wxScrollEvent evtRel(wxEVT_SCROLL_THUMBRELEASE, id, value, orient);
        evtRel.SetEventObject(win);
        win->HandleWindowEvent(evtRel);

        wxScrollEvent evtChanged(wxEVT_SCROLL_CHANGED, id, value, orient);
        evtChanged.SetEventObject(win);
        win->HandleWindowEvent(evtChanged);
    }
}
}

//-----------------------------------------------------------------------------
// "button_release_event" from scrollbar
//-----------------------------------------------------------------------------

extern "C" {
static gboolean
gtk_button_release_event(GtkRange* range, GdkEventButton*, wxScrollBar* win)
{
    win->m_mouseButtonDown = false;
    // If thumb tracking
    if (win->m_isScrolling)
    {
        win->m_isScrolling = false;
        // Hook up handler to send thumb release event after this emission is finished.
        // To allow setting scroll position from event handler, sending event must
        // be deferred until after the GtkRange handler for this signal has run
        g_signal_handlers_unblock_by_func(range, (void*)gtk_event_after, win);
    }

    return false;
}
}

#endif // __WXGTK4__/!__WXGTK4__

//-----------------------------------------------------------------------------
// wxScrollBar
//-----------------------------------------------------------------------------

wxScrollBar::wxScrollBar()
{
}

wxScrollBar::~wxScrollBar()
{
}

bool wxScrollBar::Create(wxWindow *parent, wxWindowID id,
           const wxPoint& pos, const wxSize& size,
           long style, const wxValidator& validator, const wxString& name )
{
    if (!PreCreation( parent, pos, size ) ||
        !CreateBase( parent, id, pos, size, style, validator, name ))
    {
        wxFAIL_MSG( wxT("wxScrollBar creation failed") );
        return false;
    }

    const bool isVertical = (style & wxSB_VERTICAL) != 0;
    m_widget = gtk_scrollbar_new(GtkOrientation(isVertical), nullptr);
    g_object_ref(m_widget);

#ifdef __WXGTK4__
    m_scrollBar[0] = GTK_SCROLLBAR(m_widget);
#else
    m_scrollBar[0] = (GtkRange*)m_widget;
#endif

    g_signal_connect_after(wxGtkScrollbarValueNotifier(m_scrollBar[0]),
                     "value_changed",
                     G_CALLBACK(gtk_value_changed), this);
#ifdef __WXGTK4__
    {
        // CAPTURE phase deliberately. GtkRange has its own click gesture for
        // dragging the thumb, and a bubble-phase gesture that doesn't claim
        // the sequence gets the press but never the release once GtkRange
        // claims it -- measured, see docs/gtk/probes/gtk4-gesture-semantics.c.
        // Capturing sees both without claiming, so GtkRange still drags
        // normally; the wx events are deferred to an idle callback anyway, so
        // running ahead of GtkRange here doesn't reorder them.
        GtkGesture* const gesture = gtk_gesture_click_new();
        gtk_event_controller_set_propagation_phase(
            GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_CAPTURE);
        g_signal_connect(gesture, "pressed",
                         G_CALLBACK(wx_gtk_scrollbar_pressed), this);
        g_signal_connect(gesture, "released",
                         G_CALLBACK(wx_gtk_scrollbar_released), this);
        gtk_widget_add_controller(m_widget, GTK_EVENT_CONTROLLER(gesture));
    }
#else
    g_signal_connect(m_widget, "button_press_event",
                     G_CALLBACK(gtk_button_press_event), this);
    g_signal_connect(m_widget, "button_release_event",
                     G_CALLBACK(gtk_button_release_event), this);

    gulong handler_id;
    handler_id = g_signal_connect(
        m_widget, "event_after", G_CALLBACK(gtk_event_after), this);
    g_signal_handler_block(m_widget, handler_id);
#endif

    m_parent->DoAddChild( this );

    PostCreation(size);

    return true;
}

int wxScrollBar::GetThumbPosition() const
{
    return wxRound(wxGtkScrollbarGetValue(m_scrollBar[0]));
}

int wxScrollBar::GetThumbSize() const
{
    GtkAdjustment* adj = wxGtkScrollbarGetAdjustment(m_scrollBar[0]);
    return int(gtk_adjustment_get_page_size(adj));
}

int wxScrollBar::GetPageSize() const
{
    GtkAdjustment* adj = wxGtkScrollbarGetAdjustment(m_scrollBar[0]);
    return int(gtk_adjustment_get_page_increment(adj));
}

int wxScrollBar::GetRange() const
{
    GtkAdjustment* adj = wxGtkScrollbarGetAdjustment(m_scrollBar[0]);
    return int(gtk_adjustment_get_upper(adj));
}

void wxScrollBar::SetThumbPosition( int viewStart )
{
    if (GetThumbPosition() != viewStart)
    {
        g_signal_handlers_block_by_func(wxGtkScrollbarValueNotifier(m_scrollBar[0]),
            (gpointer)gtk_value_changed, this);

        wxGtkScrollbarSetValue(m_scrollBar[0], viewStart);
        m_scrollPos[0] = wxGtkScrollbarGetValue(m_scrollBar[0]);

        g_signal_handlers_unblock_by_func(wxGtkScrollbarValueNotifier(m_scrollBar[0]),
            (gpointer)gtk_value_changed, this);
    }
}

void wxScrollBar::SetScrollbar(int position, int thumbSize, int range, int pageSize, bool)
{
    if (range <= 0)
    {
        // GtkRange requires upper > lower
        range =
        pageSize =
        thumbSize = 1;
    }
    else if (pageSize <= 0)
        pageSize = 1;
    g_signal_handlers_block_by_func(m_widget, (void*)gtk_value_changed, this);
    wxGtkScrollbar* widget = m_scrollBar[0];
    GtkAdjustment* adj = wxGtkScrollbarGetAdjustment(widget);

    g_object_freeze_notify(G_OBJECT(adj));
    wxGtkScrollbarSetIncrements(widget, 1, pageSize);
    gtk_adjustment_set_page_size(adj, thumbSize);
    wxGtkScrollbarSetRange(widget, 0, range);
    g_object_thaw_notify(G_OBJECT(adj));

    wxGtkScrollbarSetValue(widget, position);
    m_scrollPos[0] = wxGtkScrollbarGetValue(widget);
    g_signal_handlers_unblock_by_func(m_widget, (void*)gtk_value_changed, this);
}

void wxScrollBar::SetThumbSize(int thumbSize)
{
    SetScrollbar(GetThumbPosition(), thumbSize, GetRange(), GetPageSize());
}

void wxScrollBar::SetPageSize( int pageLength )
{
    SetScrollbar(GetThumbPosition(), GetThumbSize(), GetRange(), pageLength);
}

void wxScrollBar::SetRange(int range)
{
    SetScrollbar(GetThumbPosition(), GetThumbSize(), range, GetPageSize());
}

// static
wxVisualAttributes
wxScrollBar::GetClassDefaultAttributes(wxWindowVariant WXUNUSED(variant))
{
    return GetDefaultAttributesFromGTKWidget(gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, nullptr));
}

#endif // wxUSE_SCROLLBAR
