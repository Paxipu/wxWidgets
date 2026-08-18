///////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/evtloop.cpp
// Purpose:     implements wxEventLoop for GTK+
// Author:      Vadim Zeitlin
// Created:     10.07.01
// Copyright:   (c) 2001 Vadim Zeitlin <zeitlin@dptmaths.ens-cachan.fr>
//              (c) 2013 Rob Bresalier, Vadim Zeitlin
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#include "wx/evtloop.h"
#include "wx/evtloopsrc.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/log.h"
#endif // WX_PRECOMP

#include "wx/window.h"
#include "wx/toplevel.h"

#include "wx/private/eventloopsourcesmanager.h"
#include "wx/apptrait.h"

#include "wx/gtk/private/wrapgtk.h"

#ifndef __WXGTK4__
GdkWindow* wxGetTopLevelGDK();
#endif
GdkDisplay* wxGetTopLevelGdkDisplay();

// ============================================================================
// wxEventLoop implementation
// ============================================================================

// ----------------------------------------------------------------------------
// wxEventLoop running and exiting
// ----------------------------------------------------------------------------

#ifdef __WXGTK4__

// GTK4 removed gtk_main() and with it gtk_main_level(), so the nesting depth
// that DoRun() below needs is tracked here. The stack also gives DoRun() the
// enclosing loop to quit, which gtk_main_quit() used to find for it.
static wxVector<GMainLoop*> gs_mainLoops;

#endif // __WXGTK4__

wxGUIEventLoop::wxGUIEventLoop()
{
    m_exitcode = 0;
#ifdef __WXGTK4__
    m_mainLoop = nullptr;
#else
    m_lastEvent = new GdkEvent;
    memset(m_lastEvent, 0, sizeof(GdkEvent));
#endif
}

wxGUIEventLoop::~wxGUIEventLoop()
{
#ifndef __WXGTK4__
    delete m_lastEvent;
#endif
}

int wxGUIEventLoop::DoRun()
{
#ifdef __WXGTK4__
    m_mainLoop = g_main_loop_new(nullptr, FALSE);
    gs_mainLoops.push_back(m_mainLoop);

    // Same shape as the GTK3 code below: keep re-entering because an Exit()
    // for an outer loop quits this one too, and then quit the enclosing loop
    // so that it gets a chance to notice it should exit as well.
    while ( !m_shouldExit )
        g_main_loop_run(m_mainLoop);

    gs_mainLoops.pop_back();

    if ( !gs_mainLoops.empty() )
        g_main_loop_quit(gs_mainLoops.back());

    g_main_loop_unref(m_mainLoop);
    m_mainLoop = nullptr;
#else
    guint loopLevel = gtk_main_level();

    // This is placed inside of a loop to take into account nested
    // event loops.  For example, inside this event loop, we may receive
    // Exit() for a different event loop (which we are currently inside of)
    // That Exit() will cause this gtk_main() to exit so we need to re-enter it.
    while ( !m_shouldExit )
    {
        gtk_main();
    }

    // Force the enclosing event loop to also exit to see if it is done in case
    // that event loop had Exit() called inside of the just ended loop. If it
    // is not time yet for that event loop to exit, it will be executed again
    // due to the while() loop on m_shouldExit().
    //
    // This is unnecessary if we are the top level loop, i.e. loop of level 0.
    if ( loopLevel )
    {
        gtk_main_quit();
    }
#endif // __WXGTK4__/!__WXGTK4__

#if wxUSE_EXCEPTIONS
    // Rethrow any exceptions which could have been produced by the handlers
    // ran by the event loop.
    if ( wxTheApp )
        wxTheApp->RethrowStoredException();
#endif // wxUSE_EXCEPTIONS

    return m_exitcode;
}

void wxGUIEventLoop::DoStop(int rc)
{
    m_exitcode = rc;

#ifdef __WXGTK4__
    // Quit the innermost running loop, which is what gtk_main_quit() did.
    if ( !gs_mainLoops.empty() )
        g_main_loop_quit(gs_mainLoops.back());
#else
    gtk_main_quit();
#endif
}

void wxGUIEventLoop::WakeUp()
{
    // TODO: idle events handling should really be done by wxEventLoop itself
    //       but for now it's completely in gtk/app.cpp so just call there when
    //       we have wxTheApp and hope that it doesn't matter that we do
    //       nothing when we don't...
    if ( wxTheApp )
        wxTheApp->WakeUpIdle();
}

#ifndef __WXGTK4__

bool wxGUIEventLoop::GTKIsSameAsLastEvent(const GdkEvent* ev, size_t size)
{
    if ( memcmp(m_lastEvent, ev, size) == 0 )
        return true;

    memcpy(m_lastEvent, ev, size);
    return false;
}

#endif // !__WXGTK4__

// ----------------------------------------------------------------------------
// wxEventLoop adding & removing sources
// ----------------------------------------------------------------------------

#if wxUSE_EVENTLOOP_SOURCE

extern "C"
{
static gboolean wx_on_channel_event(GIOChannel *channel,
                                    GIOCondition condition,
                                    gpointer data)
{
    wxUnusedVar(channel); // Unused if !wxUSE_LOG || !wxDEBUG_LEVEL

    wxLogTrace(wxTRACE_EVT_SOURCE,
               "wx_on_channel_event, fd=%d, condition=%08x",
               g_io_channel_unix_get_fd(channel), condition);

    wxEventLoopSourceHandler * const
        handler = static_cast<wxEventLoopSourceHandler *>(data);

    if ( (condition & G_IO_IN) || (condition & G_IO_PRI) || (condition & G_IO_HUP) )
        handler->OnReadWaiting();

    if (condition & G_IO_OUT)
        handler->OnWriteWaiting();

    if ( (condition & G_IO_ERR) || (condition & G_IO_NVAL) )
        handler->OnExceptionWaiting();

    // we never want to remove source here, so always return true
    //
    // The source may have been removed by the handler, so it may be
    // a good idea to return FALSE when the source has already been
    // removed.  However, that would involve somehow informing this function
    // that the source was removed, which is not trivial to implement
    // and handle all cases.  It has been found through testing
    // that if the source was removed by the handler, that even if we
    // return TRUE here, the source/callback will not get called again.
    return TRUE;
}
}

class wxGUIEventLoopSourcesManager : public wxEventLoopSourcesManagerBase
{
public:
    virtual wxEventLoopSource*
    AddSourceForFD(int fd, wxEventLoopSourceHandler *handler, int flags) override
    {
        wxCHECK_MSG( fd != -1, nullptr, "can't monitor invalid fd" );

        int condition = 0;
        if ( flags & wxEVENT_SOURCE_INPUT )
            condition |= G_IO_IN | G_IO_PRI | G_IO_HUP;
        if ( flags & wxEVENT_SOURCE_OUTPUT )
            condition |= G_IO_OUT;
        if ( flags & wxEVENT_SOURCE_EXCEPTION )
            condition |= G_IO_ERR | G_IO_NVAL;

        GIOChannel* channel = g_io_channel_unix_new(fd);
        const unsigned sourceId  = g_io_add_watch
                                   (
                                    channel,
                                    (GIOCondition)condition,
                                    &wx_on_channel_event,
                                    handler
                                   );
        // it was ref'd by g_io_add_watch() so we can unref it here
        g_io_channel_unref(channel);

        if ( !sourceId )
            return nullptr;

        wxLogTrace(wxTRACE_EVT_SOURCE,
                   "Adding event loop source for fd=%d with GTK id=%u",
                   fd, sourceId);


        return new wxGTKEventLoopSource(sourceId, handler, flags);
    }
};

wxEventLoopSourcesManagerBase* wxGUIAppTraits::GetEventLoopSourcesManager()
{
    static wxGUIEventLoopSourcesManager s_eventLoopSourcesManager;

    return &s_eventLoopSourcesManager;
}

wxGTKEventLoopSource::~wxGTKEventLoopSource()
{
    wxLogTrace(wxTRACE_EVT_SOURCE,
               "Removing event loop source with GTK id=%u", m_sourceId);

    g_source_remove(m_sourceId);
}

#endif // wxUSE_EVENTLOOP_SOURCE

// ----------------------------------------------------------------------------
// wxEventLoop message processing dispatching
// ----------------------------------------------------------------------------

bool wxGUIEventLoop::Pending() const
{
    if ( wxTheApp )
    {
        // this avoids false positives from our idle source
        return wxTheApp->EventsPending();
    }

#ifdef __WXGTK4__
    return g_main_context_pending(nullptr);
#else
    return gtk_events_pending() != 0;
#endif
}

bool wxGUIEventLoop::Dispatch()
{
    wxCHECK_MSG( IsRunning(), false, wxT("can't call Dispatch() if not running") );

#ifdef __WXGTK4__
    g_main_context_iteration(nullptr, TRUE);

    // gtk_main_iteration() reported whether the loop had been asked to quit;
    // g_main_context_iteration() reports whether it dispatched anything, which
    // is a different question, so answer the original one directly.
    return !m_shouldExit;
#else
    // gtk_main_iteration() returns TRUE only if gtk_main_quit() was called
    return !gtk_main_iteration();
#endif
}

extern "C" {
static gboolean wx_event_loop_timeout(void* data)
{
    bool* expired = static_cast<bool*>(data);
    *expired = true;

    // return FALSE to remove this timeout
    return FALSE;
}
}

int wxGUIEventLoop::DispatchTimeout(unsigned long timeout)
{
    bool expired = false;
    const unsigned id = g_timeout_add(timeout, wx_event_loop_timeout, &expired);
#ifdef __WXGTK4__
    g_main_context_iteration(nullptr, TRUE);
    const bool quit = m_shouldExit;
#else
    bool quit = gtk_main_iteration() != 0;
#endif

    if ( expired )
        return -1;

    g_source_remove(id);

    return !quit;
}

//-----------------------------------------------------------------------------
// YieldFor
//-----------------------------------------------------------------------------

#ifndef __WXGTK4__

extern "C" {
static void wxgtk_main_do_event(GdkEvent* event, void* data)
{
    // categorize the GDK event according to wxEventCategory.
    // See http://library.gnome.org/devel/gdk/unstable/gdk-Events.html#GdkEventType
    // for more info.

    // NOTE: GDK_* constants which were not present in the GDK2.0 can be tested for
    //       only at compile-time; when running the program (compiled with a recent GDK)
    //       on a system with an older GDK lib we can be sure there won't be problems
    //       because event->type will never assume those values corresponding to
    //       new event types (since new event types are always added in GDK with non
    //       conflicting values for ABI compatibility).

    // Some events (currently only a single one) may be used for more than one
    // category, so we need 2 variables. The second one will remain "unknown"
    // in most cases.
    wxEventCategory cat = wxEVT_CATEGORY_UNKNOWN,
                    cat2 = wxEVT_CATEGORY_UNKNOWN;
    switch (event->type)
    {
    case GDK_SELECTION_REQUEST:
    case GDK_SELECTION_NOTIFY:
    case GDK_SELECTION_CLEAR:
    case GDK_OWNER_CHANGE:
        cat = wxEVT_CATEGORY_CLIPBOARD;
        break;

    case GDK_KEY_PRESS:
    case GDK_KEY_RELEASE:
    case GDK_BUTTON_PRESS:
    case GDK_2BUTTON_PRESS:
    case GDK_3BUTTON_PRESS:
    case GDK_BUTTON_RELEASE:
    case GDK_SCROLL:        // generated from mouse buttons
    case GDK_CLIENT_EVENT:
        cat = wxEVT_CATEGORY_USER_INPUT;
        break;

    case GDK_PROPERTY_NOTIFY:
        // This one is special: it can be used for UI purposes but also for
        // clipboard operations, so allow it in both cases (we probably could
        // examine the event itself to distinguish between the two cases but
        // this would be unnecessarily complicated).
        cat2 = wxEVT_CATEGORY_CLIPBOARD;
        wxFALLTHROUGH;

    case GDK_PROXIMITY_IN:
    case GDK_PROXIMITY_OUT:

    case GDK_MOTION_NOTIFY:
    case GDK_ENTER_NOTIFY:
    case GDK_LEAVE_NOTIFY:
    case GDK_VISIBILITY_NOTIFY:

    case GDK_FOCUS_CHANGE:
    case GDK_CONFIGURE:
    case GDK_WINDOW_STATE:
    case GDK_SETTING:
    case GDK_DELETE:
    case GDK_DESTROY:

    case GDK_EXPOSE:
#ifndef __WXGTK3__
    case GDK_NO_EXPOSE:
#endif
    case GDK_MAP:
    case GDK_UNMAP:

    case GDK_DRAG_ENTER:
    case GDK_DRAG_LEAVE:
    case GDK_DRAG_MOTION:
    case GDK_DRAG_STATUS:
    case GDK_DROP_START:
    case GDK_DROP_FINISHED:
#if GTK_CHECK_VERSION(2,8,0)
    case GDK_GRAB_BROKEN:
#endif
#if GTK_CHECK_VERSION(2,14,0)
    case GDK_DAMAGE:
#endif
        cat = wxEVT_CATEGORY_UI;
        break;

    default:
        cat = wxEVT_CATEGORY_UNKNOWN;
        break;
    }

    wxGUIEventLoop* evtloop = static_cast<wxGUIEventLoop*>(data);

    // is this event allowed now?
    if (evtloop->IsEventAllowedInsideYield(cat) ||
            (cat2 != wxEVT_CATEGORY_UNKNOWN &&
                evtloop->IsEventAllowedInsideYield(cat2)))
    {
        // process it now
        gtk_main_do_event(event);
    }
    else if (event->type != GDK_NOTHING)
    {
        // process it later (but make a copy; the caller will free the event
        // pointer)
        evtloop->StoreGdkEventForLaterProcessing(gdk_event_copy(event));
    }
}
}

#endif // !__WXGTK4__

#ifdef __WXGTK4__

// Let GTK catch up with the layout it has scheduled.
//
// GTK3 laid out from idle handlers, which running the main loop dispatched, so
// a yield settled the interface: that is what wx callers have always assumed,
// and what "wxYield(); // let GTK layout the control" comments throughout the
// test suite are asking for. GTK4 lays out in the frame clock's phases
// instead, which running the main loop does not advance. A window shown,
// hidden, resized or re-filled just before a yield is therefore still stale
// when it returns, and every query that depends on the layout --
// wxTextCtrl::PositionToCoords(), HitTest(), the scrollbar ranges -- answers
// out of date without anything indicating it.
//
// So ask each mapped toplevel's clock for a frame and pump until it has
// produced one. The wait is bounded because a window that is not being
// presented may never produce another frame, and a yield must not hang.
static void wxGTKSettleFrames()
{
    for ( wxWindowList::const_iterator i = wxTopLevelWindows.begin();
          i != wxTopLevelWindows.end();
          ++i )
    {
        GtkWidget* const widget = (*i)->GetHandle();
        if ( !widget || !gtk_widget_get_mapped(widget) )
            continue;

        GdkFrameClock* const clock = gtk_widget_get_frame_clock(widget);
        if ( !clock )
            continue;

        const gint64 frame = gdk_frame_clock_get_frame_counter(clock);
        const gint64 deadline = g_get_monotonic_time() + 100000; // 0.1s

        gdk_frame_clock_request_phase(clock, GDK_FRAME_CLOCK_PHASE_LAYOUT);

        while ( gdk_frame_clock_get_frame_counter(clock) == frame &&
                g_get_monotonic_time() < deadline )
        {
            if ( !g_main_context_iteration(nullptr, FALSE) )
                g_usleep(1000);
        }
    }
}

#endif // __WXGTK4__

void wxGUIEventLoop::DoYieldFor(long eventsToProcess)
{
#ifdef __WXGTK4__
    // GTK4 removed gdk_event_handler_set(), which is what the GTK3 code below
    // uses to take over the global event stream, sort each native event into a
    // wxEventCategory and put back the ones the caller didn't ask for. There is
    // no replacement: GTK4 delivers events to surfaces internally and offers no
    // interception point, and GdkEvent is opaque so they could not be examined
    // or copied anyway.
    //
    // So a GTK4 yield processes every pending native event rather than only
    // those in the requested categories. Filtering by category still works for
    // wx events, which the base class handles below; what is lost is deferring
    // *native* ones, so e.g. YieldFor(wxEVT_CATEGORY_UI) no longer keeps user
    // input from reaching a window during the yield.
    //
    // See docs/gtk/gtk4-status.md for this as a recorded fidelity gap.
    while ( Pending() )
        g_main_context_iteration(nullptr, FALSE);

    wxGTKSettleFrames();

    // The frame above is where GTK4's deferred focus move would have happened,
    // so whatever wxWindowGTK is watching for it need not watch any longer.
    extern void wxGTKClearFocusRestoreMark();
    wxGTKClearFocusRestoreMark();

    wxEventLoopBase::DoYieldFor(eventsToProcess);
#else
    // temporarily replace the global GDK event handler with our function, which
    // categorizes the events and using m_eventsToProcessInsideYield decides
    // if an event should be processed immediately or not
    // NOTE: this approach is better than using gdk_display_get_event() because
    //       gtk_main_iteration() does more than just calling gdk_display_get_event()
    //       and then call gtk_main_do_event()!
    //       In particular in this way we also process input from sources like
    //       GIOChannels (this is needed for e.g. wxGUIAppTraits::WaitForChild).
    gdk_event_handler_set(wxgtk_main_do_event, this, nullptr);
    while (Pending())   // avoid false positives from our idle source
        gtk_main_iteration();

    wxGCC_WARNING_SUPPRESS_CAST_FUNCTION_TYPE()
    gdk_event_handler_set ((GdkEventFunc)gtk_main_do_event, nullptr, nullptr);
    wxGCC_WARNING_RESTORE_CAST_FUNCTION_TYPE()

    wxEventLoopBase::DoYieldFor(eventsToProcess);

    // put any unprocessed GDK events back in the queue
    if ( !m_queuedGdkEvents.empty() )
    {
        GdkDisplay* disp = wxGetTopLevelGdkDisplay();
        for ( GdkEvent* ev : m_queuedGdkEvents )
        {
            // NOTE: gdk_display_put_event makes a copy of the event passed to it
            gdk_display_put_event(disp, ev);
            gdk_event_free(ev);
        }

        m_queuedGdkEvents.clear();
    }
#endif // __WXGTK4__/!__WXGTK4__
}
