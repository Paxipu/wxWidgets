///////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/calctrl.cpp
// Purpose:     implementation of the wxGtkCalendarCtrl
// Author:      Marcin Wojdyr
// Copyright:   (c) 2008 Marcin Wojdyr
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"


#if wxUSE_CALENDARCTRL

#ifndef WX_PRECOMP
#endif //WX_PRECOMP

#include "wx/calctrl.h"

#include "wx/gtk/private/wrapgtk.h"

extern "C" {

static void gtk_day_selected_callback(GtkWidget *WXUNUSED(widget),
                                      wxGtkCalendarCtrl *cal)
{
    cal->GTKGenerateEvent(wxEVT_CALENDAR_SEL_CHANGED);
}

#ifdef __WXGTK4__
// GtkCalendar has no "day-selected-double-click" signal any more, so the
// double click is detected directly. A gesture reports the press count, so
// there is nothing to keep track of.
static void gtk_calendar_double_click_callback(GtkGestureClick* WXUNUSED(gesture),
                                               int nPress,
                                               double WXUNUSED(x),
                                               double WXUNUSED(y),
                                               wxGtkCalendarCtrl *cal)
{
    if ( nPress == 2 )
        cal->GTKGenerateEvent(wxEVT_CALENDAR_DOUBLECLICKED);
}
#else
static void gtk_day_selected_double_click_callback(GtkWidget *WXUNUSED(widget),
                                                   wxGtkCalendarCtrl *cal)
{
    cal->GTKGenerateEvent(wxEVT_CALENDAR_DOUBLECLICKED);
}
#endif // __WXGTK4__/!__WXGTK4__

static void gtk_month_changed_callback(GtkWidget *WXUNUSED(widget),
                                       wxGtkCalendarCtrl *cal)
{
    cal->GTKGenerateEvent(wxEVT_CALENDAR_PAGE_CHANGED);
}

// callbacks that send deprecated events

static void gtk_prev_month_callback(GtkWidget *WXUNUSED(widget),
                                    wxGtkCalendarCtrl *cal)
{
    cal->GTKGenerateEvent(wxEVT_CALENDAR_MONTH_CHANGED);
}

static void gtk_prev_year_callback(GtkWidget *WXUNUSED(widget),
                                    wxGtkCalendarCtrl *cal)
{
    cal->GTKGenerateEvent(wxEVT_CALENDAR_YEAR_CHANGED);
}

}

// ----------------------------------------------------------------------------
// wxGtkCalendarCtrl
// ----------------------------------------------------------------------------


bool wxGtkCalendarCtrl::Create(wxWindow *parent,
                               wxWindowID id,
                               const wxDateTime& date,
                               const wxPoint& pos,
                               const wxSize& size,
                               long style,
                               const wxString& name)
{
    if (!PreCreation(parent, pos, size) ||
          !CreateBase(parent, id, pos, size, style, wxDefaultValidator, name))
    {
        wxFAIL_MSG(wxT("wxGtkCalendarCtrl creation failed"));
        return false;
    }

    m_widget = gtk_calendar_new();
    g_object_ref(m_widget);
    SetDate(date.IsValid() ? date : wxDateTime::Today());

#ifndef __WXGTK4__
    if (style & wxCAL_NO_MONTH_CHANGE)
        g_object_set (G_OBJECT (m_widget), "no-month-change", true, nullptr);
#else
    // GtkCalendar's "no-month-change" property is gone under GTK4 and has no
    // replacement, so wxCAL_NO_MONTH_CHANGE cannot be honoured: the month and
    // year arrows stay usable. Setting it by name would not have worked
    // either -- g_object_set() on a property that does not exist is a runtime
    // warning, not an error, and does nothing. See docs/gtk/gtk4-status.md.
#endif // !__WXGTK4__/__WXGTK4__
    if (style & wxCAL_SHOW_WEEK_NUMBERS)
        g_object_set (G_OBJECT (m_widget), "show-week-numbers", true, nullptr);

    g_signal_connect_after(m_widget, "day-selected",
                           G_CALLBACK (gtk_day_selected_callback),
                           this);
#ifdef __WXGTK4__
    // GtkCalendar lost "day-selected-double-click" -- activating a day is
    // reported by the double click itself now, so that is what is watched for.
    {
        GtkGesture* const click = gtk_gesture_click_new();
        g_signal_connect(click, "pressed",
                         G_CALLBACK (gtk_calendar_double_click_callback), this);
        gtk_widget_add_controller(m_widget, GTK_EVENT_CONTROLLER(click));
    }

    // It also lost "month-changed", but the four signals below, which say
    // exactly how the month changed, are all still there and between them
    // cover every way it can.
    g_signal_connect_after(m_widget, "prev-month",
                           G_CALLBACK (gtk_month_changed_callback), this);
    g_signal_connect_after(m_widget, "next-month",
                           G_CALLBACK (gtk_month_changed_callback), this);
    g_signal_connect_after(m_widget, "prev-year",
                           G_CALLBACK (gtk_month_changed_callback), this);
    g_signal_connect_after(m_widget, "next-year",
                           G_CALLBACK (gtk_month_changed_callback), this);
#else // !__WXGTK4__
    g_signal_connect_after(m_widget, "day-selected-double-click",
                           G_CALLBACK (gtk_day_selected_double_click_callback),
                           this);
    g_signal_connect_after(m_widget, "month-changed",
                           G_CALLBACK (gtk_month_changed_callback),
                           this);
#endif // __WXGTK4__/!__WXGTK4__

    // connect callbacks that send deprecated events
    g_signal_connect_after(m_widget, "prev-month",
                           G_CALLBACK (gtk_prev_month_callback),
                           this);
    g_signal_connect_after(m_widget, "next-month",
                           G_CALLBACK (gtk_prev_month_callback),
                           this);
    g_signal_connect_after(m_widget, "prev-year",
                           G_CALLBACK (gtk_prev_year_callback),
                           this);
    g_signal_connect_after(m_widget, "next-year",
                           G_CALLBACK (gtk_prev_year_callback),
                           this);

    m_parent->DoAddChild(this);

    PostCreation(size);

    return true;
}

void wxGtkCalendarCtrl::GTKGenerateEvent(wxEventType type)
{
    // First check if the new date is in the specified range.
    wxDateTime dt = GetDate();
    if ( !IsInValidRange(dt) )
    {
        if ( m_validStart.IsValid() && dt < m_validStart )
            dt = m_validStart;
        else
            dt = m_validEnd;

        SetDate(dt);

        return;
    }

    if ( type == wxEVT_CALENDAR_SEL_CHANGED )
    {
        // Don't generate this event if the new date is the same as the old
        // one.
        if ( m_selectedDate == dt )
            return;

        m_selectedDate = dt;

        GenerateEvent(type);

        // Also send the deprecated event together with the new one.
        GenerateEvent(wxEVT_CALENDAR_DAY_CHANGED);
    }
    else
    {
        GenerateEvent(type);
    }
}

bool wxGtkCalendarCtrl::IsInValidRange(const wxDateTime& dt) const
{
    return (!m_validStart.IsValid() || m_validStart <= dt) &&
                (!m_validEnd.IsValid() || dt <= m_validEnd);
}

bool
wxGtkCalendarCtrl::SetDateRange(const wxDateTime& lowerdate,
                                const wxDateTime& upperdate)
{
    if ( lowerdate.IsValid() && upperdate.IsValid() && lowerdate >= upperdate )
        return false;

    m_validStart = lowerdate;
    m_validEnd = upperdate;

    return true;
}

bool
wxGtkCalendarCtrl::GetDateRange(wxDateTime *lowerdate,
                                wxDateTime *upperdate) const
{
    if ( lowerdate )
        *lowerdate = m_validStart;
    if ( upperdate )
        *upperdate = m_validEnd;

    return m_validStart.IsValid() || m_validEnd.IsValid();
}


bool wxGtkCalendarCtrl::EnableMonthChange(bool enable)
{
#ifdef __WXGTK4__
    // Not supported: see the comment in Create(). Say so rather than claiming
    // success, so that callers can fall back to the generic implementation.
    wxUnusedVar(enable);
    return false;
#else
    if ( !wxCalendarCtrlBase::EnableMonthChange(enable) )
        return false;

    g_object_set (G_OBJECT (m_widget), "no-month-change", !enable, nullptr);

    return true;
#endif // __WXGTK4__/!__WXGTK4__
}


bool wxGtkCalendarCtrl::SetDate(const wxDateTime& date)
{
    wxCHECK_MSG( date.IsValid(), false, "invalid date" );

    if ( !IsInValidRange(date) )
        return false;

    g_signal_handlers_block_by_func(m_widget,
        (gpointer) gtk_day_selected_callback, this);
    g_signal_handlers_block_by_func(m_widget,
        (gpointer) gtk_month_changed_callback, this);

    m_selectedDate = date;
    int year = date.GetYear();
    int month = date.GetMonth();
    int day = date.GetDay();
#ifdef __WXGTK4__
    // gtk_calendar_select_month() is gone under GTK4 -- gtk_calendar_
    // select_day() now takes a full GDateTime (GLib months are 1-based,
    // unlike wxDateTime::Month/GTK3's 0-based gtk_calendar_select_month()).
    GDateTime* const dt = g_date_time_new_local(year, month + 1, day, 0, 0, 0);
    gtk_calendar_select_day(GTK_CALENDAR(m_widget), dt);
    g_date_time_unref(dt);
#else
    gtk_calendar_select_month(GTK_CALENDAR(m_widget), month, year);
    gtk_calendar_select_day(GTK_CALENDAR(m_widget), day);
#endif

    g_signal_handlers_unblock_by_func( m_widget,
        (gpointer) gtk_month_changed_callback, this);
    g_signal_handlers_unblock_by_func( m_widget,
        (gpointer) gtk_day_selected_callback, this);

    return true;
}

wxDateTime wxGtkCalendarCtrl::GetDate() const
{
    guint year, monthGTK, day;
#ifdef __WXGTK4__
    // gtk_calendar_get_date() returns a GDateTime under GTK4 instead of
    // filling in separate out-params; GLib months are 1-based, unlike
    // wxDateTime::Month/GTK3's 0-based out-param.
    GDateTime* const dt = gtk_calendar_get_date(GTK_CALENDAR(m_widget));
    year = guint(g_date_time_get_year(dt));
    monthGTK = guint(g_date_time_get_month(dt) - 1);
    day = guint(g_date_time_get_day_of_month(dt));
    g_date_time_unref(dt);
#else
    gtk_calendar_get_date(GTK_CALENDAR(m_widget), &year, &monthGTK, &day);
#endif

    // GTK may return an invalid date, this happens at least when switching the
    // month (or the year in case of February in a leap year) and the new month
    // has fewer days than the currently selected one making the currently
    // selected day invalid, e.g. just choosing May 31 and going back a month
    // results in the date being (non existent) April 31 when we're called from
    // gtk_prev_month_callback(). We need to manually work around this to avoid
    // asserts from wxDateTime ctor.
    const wxDateTime::Month month = static_cast<wxDateTime::Month>(monthGTK);
    const guint dayMax = wxDateTime::GetNumberOfDays(month, year);
    if ( day > dayMax )
        day = dayMax;

    return wxDateTime(day, month, year);
}

void wxGtkCalendarCtrl::Mark(size_t day, bool mark)
{
    if (mark)
        gtk_calendar_mark_day(GTK_CALENDAR(m_widget), day);
    else
        gtk_calendar_unmark_day(GTK_CALENDAR(m_widget), day);
}

#endif // wxUSE_CALENDARCTRL
