/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/slider.cpp
// Purpose:
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_SLIDER

#include "wx/slider.h"

#ifndef WX_PRECOMP
    #include "wx/utils.h"
    #include "wx/math.h"
#endif

#include "wx/gtk/private/wrapgtk.h"
#include "wx/gtk/private/eventsdisabler.h"
#include "wx/gtk/private/gtk3-compat.h"

//-----------------------------------------------------------------------------
// data
//-----------------------------------------------------------------------------

extern bool g_blockEventsOnDrag;

// ----------------------------------------------------------------------------
// helper functions
// ----------------------------------------------------------------------------

// process a scroll event
static void
ProcessScrollEvent(wxSlider *win, wxEventType evtType)
{
    const int orient = win->HasFlag(wxSL_VERTICAL) ? wxVERTICAL
                                                   : wxHORIZONTAL;

    const int value = win->GetValue();

    // if we have any "special" event (i.e. the value changed by a line or a
    // page), send this specific event first
    if ( evtType != wxEVT_NULL )
    {
        wxScrollEvent event( evtType, win->GetId(), value, orient );
        event.SetEventObject( win );
        win->HandleWindowEvent( event );
    }

    // but, in any case, except if we're dragging the slider (and so the change
    // is not definitive), send a generic "changed" event
    if ( evtType != wxEVT_SCROLL_THUMBTRACK )
    {
        wxScrollEvent event(wxEVT_SCROLL_CHANGED, win->GetId(), value, orient);
        event.SetEventObject( win );
        win->HandleWindowEvent( event );
    }

    // and also generate a command event for compatibility
    wxCommandEvent event( wxEVT_SLIDER, win->GetId() );
    event.SetEventObject( win );
    event.SetInt( value );
    win->HandleWindowEvent( event );
}

static inline wxEventType GtkScrollTypeToWx(int scrollType)
{
    wxEventType eventType;
    switch (scrollType)
    {
    case GTK_SCROLL_STEP_BACKWARD:
    case GTK_SCROLL_STEP_LEFT:
    case GTK_SCROLL_STEP_UP:
        eventType = wxEVT_SCROLL_LINEUP;
        break;
    case GTK_SCROLL_STEP_DOWN:
    case GTK_SCROLL_STEP_FORWARD:
    case GTK_SCROLL_STEP_RIGHT:
        eventType = wxEVT_SCROLL_LINEDOWN;
        break;
    case GTK_SCROLL_PAGE_BACKWARD:
    case GTK_SCROLL_PAGE_LEFT:
    case GTK_SCROLL_PAGE_UP:
        eventType = wxEVT_SCROLL_PAGEUP;
        break;
    case GTK_SCROLL_PAGE_DOWN:
    case GTK_SCROLL_PAGE_FORWARD:
    case GTK_SCROLL_PAGE_RIGHT:
        eventType = wxEVT_SCROLL_PAGEDOWN;
        break;
    case GTK_SCROLL_START:
        eventType = wxEVT_SCROLL_TOP;
        break;
    case GTK_SCROLL_END:
        eventType = wxEVT_SCROLL_BOTTOM;
        break;
    case GTK_SCROLL_JUMP:
        eventType = wxEVT_SCROLL_THUMBTRACK;
        break;
    default:
        wxFAIL_MSG(wxT("Unknown GtkScrollType"));
        eventType = wxEVT_NULL;
        break;
    }
    return eventType;
}

// Determine if increment is the same as +/-x, allowing for some small
//   difference due to possible inexactness in floating point arithmetic
static inline bool IsScrollIncrement(double increment, double x)
{
    wxASSERT(increment > 0);
    const double tolerance = 1.0 / 1024;
    return fabs(increment - fabs(x)) < tolerance;
}

//-----------------------------------------------------------------------------
// "value_changed"
//-----------------------------------------------------------------------------

extern "C" {
static void
gtk_value_changed(GtkRange* range, wxSlider* win)
{
    const double value = gtk_range_get_value(range);
    const double oldPos = win->m_pos;
    win->m_pos = value;

    if (g_blockEventsOnDrag)
        return;

    if (win->GTKEventsDisabled())
    {
        win->m_scrollEventType = GTK_SCROLL_NONE;
        return;
    }

    wxEventType eventType = wxEVT_NULL;
    if (win->m_isScrolling)
    {
        eventType = wxEVT_SCROLL_THUMBTRACK;
    }
    else if (win->m_scrollEventType != GTK_SCROLL_NONE)
    {
        // Scroll event from "move-slider" (keyboard)
        eventType = GtkScrollTypeToWx(win->m_scrollEventType);
    }
    else if (win->m_mouseButtonDown)
    {
        // Difference from last change event
        const double diff = value - oldPos;
        const bool isDown = diff > 0;

        GtkAdjustment* adj = gtk_range_get_adjustment(range);
        if (IsScrollIncrement(gtk_adjustment_get_page_increment(adj), diff))
        {
            eventType = isDown ? wxEVT_SCROLL_PAGEDOWN : wxEVT_SCROLL_PAGEUP;
        }
        else if (wxIsSameDouble(value, 0))
        {
            eventType = wxEVT_SCROLL_PAGEUP;
        }
        else if (wxIsSameDouble(value, gtk_adjustment_get_upper(adj)))
        {
            eventType = wxEVT_SCROLL_PAGEDOWN;
        }
        else
        {
            // Assume track event
            eventType = wxEVT_SCROLL_THUMBTRACK;
            // Remember that we're tracking
            win->m_isScrolling = true;
        }
    }

    win->m_scrollEventType = GTK_SCROLL_NONE;

    // If integral position has changed
    if (wxRound(oldPos) != wxRound(value))
    {
        ProcessScrollEvent(win, eventType);
        win->m_needThumbRelease = eventType == wxEVT_SCROLL_THUMBTRACK;
    }
}
}

//-----------------------------------------------------------------------------
// "move_slider" (keyboard event)
//-----------------------------------------------------------------------------

extern "C" {
static void
gtk_move_slider(GtkRange*, GtkScrollType scrollType, wxSlider* win)
{
    // Save keyboard scroll type for "value_changed" handler
    win->m_scrollEventType = scrollType;
}
}

#ifdef __WXGTK4__

// GTK4 rebound the arrow and page keys of a GtkRange: they now change the
// value regardless of how the range is laid out, where GTK+ 3 moved the slider
// in the direction the key points at. For a plain horizontal wxSlider that
// reverses all four of them -- measured with one starting at 50, page 20,
// line 2:
//
//              GTK+ 3   GTK4
//   Page Up      30       70
//   Page Down    70       30
//   Up           48       52
//   Down         52       48
//
// which is a visible change for users and what makes
// SliderTestCase::LinePageSize fail. wxSL_INVERSE shows which of the two is
// which: with it, GTK+ 3 gives 70 as well, so its binding follows the layout,
// while GTK4 gives 70 either way.
//
// Take the keys over and ask the range for the movement GTK+ 3 would have
// made. Going through GTK's own "move-slider" keeps its clamping and leaves
// gtk_move_slider() above to record the scroll type, so the wxEVT_SCROLL_*
// events come out as before.
extern "C" {
static gboolean
wx_slider_key_pressed(GtkEventControllerKey*, guint keyval, guint,
                      GdkModifierType state, wxSlider* win)
{
    // Anything with a modifier is not ours to interpret.
    if ( state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK) )
        return FALSE;

    GtkScrollType scroll;
    switch ( keyval )
    {
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            scroll = GTK_SCROLL_STEP_BACKWARD;
            break;

        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            scroll = GTK_SCROLL_STEP_FORWARD;
            break;

        case GDK_KEY_Page_Up:
        case GDK_KEY_KP_Page_Up:
            scroll = GTK_SCROLL_PAGE_BACKWARD;
            break;

        case GDK_KEY_Page_Down:
        case GDK_KEY_KP_Page_Down:
            scroll = GTK_SCROLL_PAGE_FORWARD;
            break;

        default:
            return FALSE;
    }

    // BACKWARD and FORWARD are about the value, not the layout, so an
    // inverted scale -- where GTK+ 3 moved the other way, as measured above --
    // needs them the other way round.
    if ( gtk_range_get_inverted(GTK_RANGE(win->m_scale)) )
    {
        switch ( scroll )
        {
            case GTK_SCROLL_STEP_BACKWARD: scroll = GTK_SCROLL_STEP_FORWARD;  break;
            case GTK_SCROLL_STEP_FORWARD:  scroll = GTK_SCROLL_STEP_BACKWARD; break;
            case GTK_SCROLL_PAGE_BACKWARD: scroll = GTK_SCROLL_PAGE_FORWARD;  break;
            case GTK_SCROLL_PAGE_FORWARD:  scroll = GTK_SCROLL_PAGE_BACKWARD; break;
            default: break;
        }
    }

    g_signal_emit_by_name(win->m_scale, "move-slider", scroll);

    return TRUE;
}
}

#endif // __WXGTK4__

//-----------------------------------------------------------------------------
// mouse button tracking, and the "after the release was handled" hook
//-----------------------------------------------------------------------------

// Both of these do the same thing under GTK3 and GTK4, but they have to hook
// into completely different places: GTK3 uses the GdkEvent-level signals of
// the scale widget, which GTK4 removed, so there we use a click gesture and
// an idle source instead.
//
// The thumb release processing deliberately does not happen directly in the
// button release handler: it has to run *after* GtkRange itself has seen the
// release and settled on its final value, as otherwise the value we force to
// an integral position below is immediately overwritten again by the range.
// GTK3 offered "event-after" for exactly this; GTK4 has no equivalent signal,
// so we defer to an idle source, which likewise runs once the current event
// has been fully delivered.

#ifdef __WXGTK4__

extern "C" {
static void
wx_slider_pressed(GtkGestureClick*, int, double, double, wxSlider* win)
{
    win->m_mouseButtonDown = true;
}

static gboolean
wx_slider_after_release(void* data)
{
    wxSlider* const win = static_cast<wxSlider*>(data);
    win->m_afterReleaseIdle = 0;

    if (win->m_needThumbRelease)
    {
        win->m_needThumbRelease = false;
        ProcessScrollEvent(win, wxEVT_SCROLL_THUMBRELEASE);
    }
    // Keep slider at an integral position
    wxGtkEventsDisabler<wxSlider> noEvents(win);
    gtk_range_set_value(GTK_RANGE (win->m_scale), win->GetValue());

    return G_SOURCE_REMOVE;
}

static void
wx_slider_finish_drag(wxSlider* win)
{
    win->m_mouseButtonDown = false;
    if (win->m_isScrolling)
    {
        win->m_isScrolling = false;
        if (win->m_afterReleaseIdle == 0)
            win->m_afterReleaseIdle = g_idle_add(wx_slider_after_release, win);
    }
}

static void
wx_slider_released(GtkGestureClick*, int, double, double, wxSlider* win)
{
    wx_slider_finish_drag(win);
}

// GtkRange claims the sequence for itself the moment the thumb is grabbed, and
// a gesture whose sequence was claimed elsewhere is never told about the
// release: dragging the thumb therefore ended without wxEVT_SCROLL_THUMBRELEASE
// ever being sent, which is what SliderTestCase::Thumb catches. ("end" does
// arrive eventually, but only when the widget goes away, which is far too
// late.) A legacy controller sees every event whatever the gestures decide, so
// the release is picked up there; both paths funnel into the same place, which
// does nothing twice thanks to the m_afterReleaseIdle check.
static gboolean
wx_slider_legacy_event(GtkEventControllerLegacy*, GdkEvent* event,
                       wxSlider* win)
{
    if ( gdk_event_get_event_type(event) == GDK_BUTTON_RELEASE &&
            gdk_button_event_get_button(event) == GDK_BUTTON_PRIMARY )
    {
        wx_slider_finish_drag(win);
    }

    return GDK_EVENT_PROPAGATE;
}
}

#else // !__WXGTK4__

extern "C" {
static gboolean
gtk_button_press_event(GtkWidget*, GdkEventButton*, wxSlider* win)
{
    win->m_mouseButtonDown = true;

    return false;
}

static void
gtk_event_after(GtkRange* range, GdkEvent* event, wxSlider* win)
{
    if (event->type == GDK_BUTTON_RELEASE)
    {
        g_signal_handlers_block_by_func(range, (gpointer) gtk_event_after, win);

        if (win->m_needThumbRelease)
        {
            win->m_needThumbRelease = false;
            ProcessScrollEvent(win, wxEVT_SCROLL_THUMBRELEASE);
        }
        // Keep slider at an integral position
        wxGtkEventsDisabler<wxSlider> noEvents(win);
        gtk_range_set_value(GTK_RANGE (win->m_scale), win->GetValue());
    }
}

static gboolean
gtk_button_release_event(GtkRange* range, GdkEventButton*, wxSlider* win)
{
    win->m_mouseButtonDown = false;
    if (win->m_isScrolling)
    {
        win->m_isScrolling = false;
        g_signal_handlers_unblock_by_func(range, (gpointer) gtk_event_after, win);
    }
    return false;
}
}

#endif // __WXGTK4__/!__WXGTK4__

//-----------------------------------------------------------------------------
// "format_value"
//-----------------------------------------------------------------------------

extern "C" {
static gchar* gtk_format_value(GtkScale*, double value, void*)
{
    // Format value as nearest integer
    return g_strdup_printf("%d", wxRound(value));
}
}

//-----------------------------------------------------------------------------
// wxSlider
//-----------------------------------------------------------------------------

wxSlider::wxSlider()
{
    Init();
}

wxSlider::~wxSlider()
{
    if (m_scale && m_scale != m_widget)
        GTKDisconnect(m_scale);

#ifdef __WXGTK4__
    if (m_afterReleaseIdle)
        g_source_remove(m_afterReleaseIdle);
#endif // __WXGTK4__
}

void wxSlider::Init()
{
    m_scrollEventType = GTK_SCROLL_NONE;
    m_needThumbRelease = false;
    m_blockScrollEvent = false;
    m_tickFreq = 0;
    m_scale = nullptr;
#ifdef __WXGTK4__
    m_afterReleaseIdle = 0;
#endif // __WXGTK4__
}

bool wxSlider::Create(wxWindow *parent,
                      wxWindowID id,
                      int value,
                      int minValue,
                      int maxValue,
                      const wxPoint& pos,
                      const wxSize& size,
                      long style,
                      const wxValidator& validator,
                      const wxString& name)
{
    Init();
    m_pos = value;

    if (!PreCreation( parent, pos, size ) ||
        !CreateBase( parent, id, pos, size, style, validator, name ))
    {
        wxFAIL_MSG( wxT("wxSlider creation failed") );
        return false;
    }

    // Note that wxSL_LEFT or wxSL_RIGHT imply vertical layout too, as in wxMSW.
    const bool isVertical = (style & (wxSL_LEFT | wxSL_RIGHT | wxSL_VERTICAL)) != 0;
    m_widget =
    m_scale = gtk_scale_new(GtkOrientation(isVertical), nullptr);

    const bool showMinMaxLabels = (style & wxSL_MIN_MAX_LABELS) != 0;
#ifndef __WXGTK3__
    if (showMinMaxLabels)
#endif
    {
        gtk_widget_show( m_scale );

        m_widget = gtk_box_new(GtkOrientation(isVertical), 0);
    }
    m_minLabel = nullptr;
    m_maxLabel = nullptr;
    if (showMinMaxLabels)
    {
        m_minLabel = gtk_label_new(nullptr);
        gtk_widget_show( m_minLabel );

        m_maxLabel = gtk_label_new(nullptr);
        gtk_widget_show( m_maxLabel );

        gtk_box_pack_start(GTK_BOX(m_widget), m_minLabel, false, false, 0);
        gtk_box_pack_start(GTK_BOX(m_widget), m_scale, true, true, 0);
        gtk_box_pack_start(GTK_BOX(m_widget), m_maxLabel, false, false, 0);
    }
#ifdef __WXGTK3__
    else
    {
        gtk_box_pack_start(GTK_BOX(m_widget), m_scale, true, true, 0);
    }
#endif
    g_object_ref(m_widget);

    const bool showValueLabel = (style & wxSL_VALUE_LABEL) != 0;
    gtk_scale_set_draw_value(GTK_SCALE (m_scale), showValueLabel );
    if ( showValueLabel )
    {
        float xAlign = 0.5f;
        float yAlign = 0.5f;

        // Position the label appropriately: notice that wxSL_DIRECTION flags
        // specify the position of the ticks, not label, and so the
        // label is on the opposite side.
        GtkPositionType posLabel;
        if (isVertical)
        {
            if ( style & wxSL_LEFT )
            {
                posLabel = GTK_POS_RIGHT;
                xAlign = 0.25f;
            }
            else // if ( style & wxSL_RIGHT ) -- this is also the default
            {
                posLabel = GTK_POS_LEFT;
                xAlign = 0.75f;
            }
        }
        else // horizontal slider
        {
            if ( style & wxSL_TOP )
            {
                posLabel = GTK_POS_BOTTOM;
                yAlign = 0.25f;
            }
            else // if ( style & wxSL_BOTTOM) -- this is again the default
            {
                posLabel = GTK_POS_TOP;
                yAlign = 0.75f;
            }
        }

        gtk_scale_set_value_pos( GTK_SCALE(m_scale), posLabel );

        if (m_minLabel)
        {
            // The value label causes the slider to be somewhat off-center,
            // try to keep the labels approximately aligned with it.
#ifdef __WXGTK4__
            // GtkMisc is gone; for labels its alignment was always just the
            // label's own xalign/yalign properties anyhow.
            gtk_label_set_xalign(GTK_LABEL(m_minLabel), xAlign);
            gtk_label_set_yalign(GTK_LABEL(m_minLabel), yAlign);
            gtk_label_set_xalign(GTK_LABEL(m_maxLabel), xAlign);
            gtk_label_set_yalign(GTK_LABEL(m_maxLabel), yAlign);
#else // !__WXGTK4__
            wxGCC_WARNING_SUPPRESS(deprecated-declarations)
            gtk_misc_set_alignment(GTK_MISC(m_minLabel), xAlign, yAlign);
            gtk_misc_set_alignment(GTK_MISC(m_maxLabel), xAlign, yAlign);
            wxGCC_WARNING_RESTORE()
#endif // __WXGTK4__/!__WXGTK4__
        }
    }
#ifdef __WXGTK3__
    if (showValueLabel || !showMinMaxLabels)
    {
        // Some themes draw the slider partially outside the GtkScale's allocation.
        // This is known to occur with Mint-Y, and even slightly with Adwaita.
        // To avoid clipping, add some extra space.

        GtkBorder margin = { };

        if (!showMinMaxLabels)
        {
            const int extraEnd = 1;

            if (isVertical)
                margin.top = margin.bottom = extraEnd;
            else
                margin.left = margin.right = extraEnd;
        }
        if (showValueLabel)
        {
            const int extraSide = 5;

            if (isVertical)
                (style & wxSL_LEFT ? margin.left : margin.right) = extraSide;
            else
                (style & wxSL_TOP ? margin.top : margin.bottom) = extraSide;
        }
        wxGCC_WARNING_SUPPRESS(deprecated-declarations)
        gtk_widget_set_margin_left(m_scale, margin.left);
        gtk_widget_set_margin_right(m_scale, margin.right);
        wxGCC_WARNING_RESTORE()
        gtk_widget_set_margin_top(m_scale, margin.top);
        gtk_widget_set_margin_bottom(m_scale, margin.bottom);
    }
#endif

    // Keep full precision in position value
    gtk_scale_set_digits(GTK_SCALE (m_scale), -1);

    if (style & wxSL_INVERSE)
        gtk_range_set_inverted( GTK_RANGE(m_scale), TRUE );

#ifdef __WXGTK4__
    {
        // The gesture has to run in the capture phase: GtkRange installs its
        // own click gesture which claims the sequence, so a bubble phase one
        // of ours would simply never see the press.
        GtkGesture* const click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click),
                                                   GTK_PHASE_CAPTURE);
        g_signal_connect(click, "pressed", G_CALLBACK(wx_slider_pressed), this);
        g_signal_connect(click, "released", G_CALLBACK(wx_slider_released), this);

        GtkEventController* const legacy = gtk_event_controller_legacy_new();
        g_signal_connect(legacy, "event",
                         G_CALLBACK(wx_slider_legacy_event), this);
        gtk_widget_add_controller(m_scale, legacy);
        gtk_widget_add_controller(m_scale, GTK_EVENT_CONTROLLER(click));
    }
#else // !__WXGTK4__
    g_signal_connect(m_scale, "button_press_event", G_CALLBACK(gtk_button_press_event), this);
    g_signal_connect(m_scale, "button_release_event", G_CALLBACK(gtk_button_release_event), this);
#endif // __WXGTK4__/!__WXGTK4__
    g_signal_connect(m_scale, "move_slider", G_CALLBACK(gtk_move_slider), this);
#ifdef __WXGTK4__
    // Before the range acts on the key itself; see wx_slider_key_pressed().
    GtkEventController* const keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed",
                     G_CALLBACK(wx_slider_key_pressed), this);
    gtk_widget_add_controller(m_scale, keys);
#endif // __WXGTK4__
#ifdef __WXGTK4__
    // The "format-value" signal is gone; a plain callback replaces it. The
    // callback has the same signature the signal handler had, so the same
    // function serves for both.
    gtk_scale_set_format_value_func(GTK_SCALE(m_scale), gtk_format_value,
                                    nullptr, nullptr);
#else
    g_signal_connect(m_scale, "format_value", G_CALLBACK(gtk_format_value), nullptr);
#endif // __WXGTK4__/!__WXGTK4__
    g_signal_connect(m_scale, "value_changed", G_CALLBACK(gtk_value_changed), this);
#ifndef __WXGTK4__
    gulong handler_id = g_signal_connect(m_scale, "event_after", G_CALLBACK(gtk_event_after), this);
    g_signal_handler_block(m_scale, handler_id);
#endif // !__WXGTK4__

    SetRange( minValue, maxValue );

    // don't call the public SetValue() as it won't do anything unless the
    // value really changed
    GTKSetValue( value );

    m_parent->DoAddChild( this );

    PostCreation(size);

    return true;
}

void wxSlider::GTKDisableEvents()
{
    m_blockScrollEvent = true;
}

void wxSlider::GTKEnableEvents()
{
    m_blockScrollEvent = false;
}

bool wxSlider::GTKEventsDisabled() const
{
   return m_blockScrollEvent;
}

int wxSlider::GetValue() const
{
    return wxRound(m_pos);
}

void wxSlider::SetValue( int value )
{
    if (GetValue() != value)
        GTKSetValue(value);
}

void wxSlider::GTKSetValue(int value)
{
    wxGtkEventsDisabler<wxSlider> noEvents(this);

    gtk_range_set_value(GTK_RANGE (m_scale), value);
    // GTK only updates value label if handle moves at least 1 pixel
    gtk_widget_queue_draw(m_scale);
}

void wxSlider::SetRange( int minValue, int maxValue )
{
    wxGtkEventsDisabler<wxSlider> noEvents(this);
    if (minValue == maxValue)
       maxValue++;
    gtk_range_set_range(GTK_RANGE (m_scale), minValue, maxValue);
    gtk_range_set_increments(GTK_RANGE (m_scale), 1, (maxValue - minValue + 9) / 10);

    if (HasFlag(wxSL_MIN_MAX_LABELS))
    {
        wxString str;

        str.Printf( "%d", minValue );
        if (HasFlag(wxSL_INVERSE))
            gtk_label_set_text( GTK_LABEL(m_maxLabel), str.utf8_str() );
        else
            gtk_label_set_text( GTK_LABEL(m_minLabel), str.utf8_str() );

        str.Printf( "%d", maxValue );
        if (HasFlag(wxSL_INVERSE))
            gtk_label_set_text( GTK_LABEL(m_minLabel), str.utf8_str() );
        else
            gtk_label_set_text( GTK_LABEL(m_maxLabel), str.utf8_str() );

    }
}

int wxSlider::GetMin() const
{
    GtkAdjustment* adj = gtk_range_get_adjustment(GTK_RANGE(m_scale));
    return int(gtk_adjustment_get_lower(adj));
}

int wxSlider::GetMax() const
{
    GtkAdjustment* adj = gtk_range_get_adjustment(GTK_RANGE(m_scale));
    return int(gtk_adjustment_get_upper(adj));
}

void wxSlider::SetPageSize( int pageSize )
{
    wxGtkEventsDisabler<wxSlider> noEvents(this);
    gtk_range_set_increments(GTK_RANGE (m_scale), GetLineSize(), pageSize);
}

int wxSlider::GetPageSize() const
{
    GtkAdjustment* adj = gtk_range_get_adjustment(GTK_RANGE(m_scale));
    return int(gtk_adjustment_get_page_increment(adj));
}

// GTK does not support changing the size of the slider
void wxSlider::SetThumbLength(int)
{
}

int wxSlider::GetThumbLength() const
{
    return 0;
}

void wxSlider::SetLineSize( int lineSize )
{
    wxGtkEventsDisabler<wxSlider> noEvents(this);
    gtk_range_set_increments(GTK_RANGE (m_scale), lineSize, GetPageSize());
}

int wxSlider::GetLineSize() const
{
    GtkAdjustment* adj = gtk_range_get_adjustment(GTK_RANGE(m_scale));
    return int(gtk_adjustment_get_step_increment(adj));
}

void wxSlider::ClearTicks()
{
#if GTK_CHECK_VERSION(2,16,0)
    if (wx_is_at_least_gtk2(16))
        gtk_scale_clear_marks(GTK_SCALE (m_scale));
#endif
}

void wxSlider::SetTick(int tickPos)
{
#if GTK_CHECK_VERSION(2,16,0)
    if ( wx_is_at_least_gtk2(16) )
    {
        GtkPositionType posTicks;
        long style = GetWindowStyle();

        if ( style & wxSL_VERTICAL )
        {
            if ( style & wxSL_LEFT )
                posTicks = GTK_POS_LEFT;
            else
                posTicks = GTK_POS_RIGHT;
        }
        else // horizontal slider
        {
            if ( style & wxSL_TOP )
                posTicks = GTK_POS_TOP;
            else
                posTicks = GTK_POS_BOTTOM;
        }

        gtk_scale_add_mark(GTK_SCALE (m_scale), (double)tickPos, posTicks, nullptr);
    }
#else
    wxUnusedVar(tickPos);
#endif
}

void wxSlider::DoSetTickFreq(int freq)
{
#if GTK_CHECK_VERSION(2,16,0)
    if ( wx_is_at_least_gtk2(16) )
    {
        m_tickFreq = freq;
        gtk_scale_clear_marks(GTK_SCALE (m_scale));

        for (int i = GetMin() + freq; i < GetMax(); i += freq)
            SetTick(i);
    }
#else
    wxUnusedVar(freq);
#endif
}

int wxSlider::GetTickFreq() const
{
#if GTK_CHECK_VERSION(2,16,0)
    return wx_is_at_least_gtk2(16) ? m_tickFreq : -1;
#else
    return -1;
#endif
}

wxSize wxSlider::DoGetBestSize() const
{
    // We need to get the size in the transverse direction from GTK, but we use
    // hard-coded default in the other direction, as otherwise the slider would
    // have the smallest possible size and not have any extent at all.
    wxSize size = GTKGetPreferredSize(m_widget);
    (HasFlag(wxSL_VERTICAL) ? size.y : size.x) = 100;
    return size;
}

#ifndef __WXGTK4__
GdkWindow *wxSlider::GTKGetWindow(wxArrayGdkWindows& WXUNUSED(windows)) const
{
#ifdef __WXGTK3__
    return GTKFindWindow(m_scale);
#else
    return GTK_RANGE(m_scale)->event_window;
#endif
}
#endif // !__WXGTK4__

// static
wxVisualAttributes
wxSlider::GetClassDefaultAttributes(wxWindowVariant WXUNUSED(variant))
{
    return GetDefaultAttributesFromGTKWidget(gtk_scale_new(GTK_ORIENTATION_VERTICAL, nullptr));
}

#endif // wxUSE_SLIDER
