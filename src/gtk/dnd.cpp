///////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/dnd.cpp
// Purpose:     wxDropTarget class
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_DRAG_AND_DROP

#include "wx/dnd.h"

#ifndef WX_PRECOMP
    #include "wx/intl.h"
    #include "wx/log.h"
    #include "wx/app.h"
    #include "wx/utils.h"
    #include "wx/window.h"
    #include "wx/gdicmn.h"
#endif

#include "wx/scopeguard.h"

#include "wx/gtk/private/wrapgtk.h"
#include "wx/gtk/private/gtk3-compat.h"
#include "wx/gtk/private/backend.h"

//----------------------------------------------------------------------------
// global data
//----------------------------------------------------------------------------

extern bool g_blockEventsOnDrag;

// the flags used for the last DoDragDrop()
static long gs_flagsForDrag = 0;

// the trace mask we use with wxLogTrace() - call
// wxLog::AddTraceMask(TRACE_DND) to enable the trace messages from here
// (there are quite a few of them, so don't enable this by default)
#define TRACE_DND "dnd"

// global variables because GTK+ DnD want to have the
// mouse event that caused it
extern GdkEvent *g_lastMouseEvent;
extern int       g_lastButtonNumber;

//----------------------------------------------------------------------------
// standard icons
//----------------------------------------------------------------------------

/* Copyright (c) Julian Smart */
static const char * page_xpm[] = {
/* columns rows colors chars-per-pixel */
"32 32 37 1",
"5 c #7198D9",
", c #769CDA",
"2 c #DCE6F6",
"i c #FFFFFF",
"e c #779DDB",
": c #9AB6E4",
"9 c #EAF0FA",
"- c #B1C7EB",
"$ c #6992D7",
"y c #F7F9FD",
"= c #BED0EE",
"q c #F0F5FC",
"; c #A8C0E8",
"@ c #366BC2",
"  c None",
"u c #FDFEFF",
"8 c #5987D3",
"* c #C4D5F0",
"7 c #7CA0DC",
"O c #487BCE",
"< c #6B94D7",
"& c #CCDAF2",
"> c #89A9DF",
"3 c #5584D1",
"w c #82A5DE",
"1 c #3F74CB",
"+ c #3A70CA",
". c #3569BF",
"% c #D2DFF4",
"# c #3366BB",
"r c #F5F8FD",
"0 c #FAFCFE",
"4 c #DFE8F7",
"X c #5E8AD4",
"o c #5282D0",
"t c #B8CCEC",
"6 c #E5EDF9",
/* pixels */
"                                ",
"                                ",
"                                ",
"                                ",
"                                ",
"       .XXXooOO++@#             ",
"       $%&*=-;::>,<1            ",
"       $2%&*=-;::><:3           ",
"       $42%&*=-;::<&:3          ",
"       56477<<<<8<<9&:X         ",
"       59642%&*=-;<09&:5        ",
"       5q9642%&*=-<<<<<#        ",
"       5qqw777<<<<<88:>+        ",
"       erqq9642%&*=t;::+        ",
"       eyrqq9642%&*=t;:O        ",
"       eyywwww777<<<<t;O        ",
"       e0yyrqq9642%&*=to        ",
"       e00yyrqq9642%&*=o        ",
"       eu0wwwwwww777<&*X        ",
"       euu00yyrqq9642%&X        ",
"       eiuu00yyrqq9642%X        ",
"       eiiwwwwwwwwww742$        ",
"       eiiiuu00yyrqq964$        ",
"       eiiiiuu00yyrqq96$        ",
"       eiiiiiuu00yyrqq95        ",
"       eiiiiiiuu00yyrqq5        ",
"       eeeeeeeeeeeeee55e        ",
"                                ",
"                                ",
"                                ",
"                                ",
"                                "
};


// ============================================================================
// private functions
// ============================================================================

// ----------------------------------------------------------------------------
// convert between GTK+ and wxWidgets DND constants
// ----------------------------------------------------------------------------

static wxDragResult ConvertFromGTK(long action)
{
    switch ( action )
    {
        case GDK_ACTION_COPY:
            return wxDragCopy;

        case GDK_ACTION_LINK:
            return wxDragLink;

        case GDK_ACTION_MOVE:
            return wxDragMove;
    }

    return wxDragNone;
}

#ifndef __WXGTK4__

// ----------------------------------------------------------------------------
// "drag_leave"
// ----------------------------------------------------------------------------

extern "C" {
static void target_drag_leave( GtkWidget *WXUNUSED(widget),
                               GdkDragContext *context,
                               guint WXUNUSED(time),
                               wxDropTarget *drop_target )
{
    /* inform the wxDropTarget about the current GdkDragContext.
       this is only valid for the duration of this call */
    drop_target->GTKSetDragContext( context );

    /* we don't need return values. this event is just for
       information */
    drop_target->OnLeave();

    /* this has to be done because GDK has no "drag_enter" event */
    drop_target->m_firstMotion = true;

    /* after this, invalidate the drop_target's GdkDragContext */
    drop_target->GTKSetDragContext( nullptr );
}
}

// ----------------------------------------------------------------------------
// "drag_motion"
// ----------------------------------------------------------------------------

extern "C" {
static gboolean target_drag_motion( GtkWidget *WXUNUSED(widget),
                                    GdkDragContext *context,
                                    gint x,
                                    gint y,
                                    guint time,
                                    wxDropTarget *drop_target )
{
    /* Owen Taylor: "if the coordinates not in a drop zone,
       return FALSE, otherwise call gtk_drag_status() and
       return TRUE" */

#if 0
    wxPrintf( "motion\n" );
    GList *tmp_list;
    for (tmp_list = context->targets; tmp_list; tmp_list = tmp_list->next)
    {
        wxString atom = wxString::FromAscii( gdk_atom_name (GDK_POINTER_TO_ATOM (tmp_list->data)) );
        wxPrintf( "Atom: %s\n", atom );
    }
#endif

    // Inform the wxDropTarget about the current GdkDragContext.
    // This is only valid for the duration of this call.
    drop_target->GTKSetDragContext( context );

    // Does the source actually accept the data type?
    if (drop_target->GTKGetMatchingPair() == (GdkAtom) nullptr)
    {
        drop_target->GTKSetDragContext( nullptr );
        return FALSE;
    }

    wxDragResult suggested_action = drop_target->GTKFigureOutSuggestedAction();

    wxDragResult result = wxDragNone;

    if (drop_target->m_firstMotion)
    {
        // the first "drag_motion" event substitutes a "drag_enter" event
        result = drop_target->OnEnter( x, y, suggested_action );
    }
    else
    {
        // give program a chance to react (i.e. to say no by returning FALSE)
        result = drop_target->OnDragOver( x, y, suggested_action );
    }

    GdkDragAction result_action = GDK_ACTION_DEFAULT;
    if (result == wxDragCopy)
        result_action = GDK_ACTION_COPY;
    else if (result == wxDragLink)
        result_action = GDK_ACTION_LINK;
    else
        result_action = GDK_ACTION_MOVE;

    // is result action actually supported
    bool ret = (result_action != GDK_ACTION_DEFAULT) &&
               (gdk_drag_context_get_actions(context) & result_action);

    if (ret)
        gdk_drag_status( context, result_action, time );

    // after this, invalidate the drop_target's GdkDragContext
    drop_target->GTKSetDragContext( nullptr );

    // this has to be done because GDK has no "drag_enter" event
    drop_target->m_firstMotion = false;

    return ret;
}
}

// ----------------------------------------------------------------------------
// "drag_drop"
// ----------------------------------------------------------------------------

extern "C" {
static gboolean target_drag_drop( GtkWidget *widget,
                                  GdkDragContext *context,
                                  gint x,
                                  gint y,
                                  guint time,
                                  wxDropTarget *drop_target )
{
    /* Owen Taylor: "if the drop is not in a drop zone,
       return FALSE, otherwise, if you aren't accepting
       the drop, call gtk_drag_finish() with success == FALSE
       otherwise call gtk_drag_data_get()" */

    /* inform the wxDropTarget about the current GdkDragContext.
       this is only valid for the duration of this call */
    drop_target->GTKSetDragContext( context );

    // Does the source actually accept the data type?
    if (drop_target->GTKGetMatchingPair() == (GdkAtom) nullptr)
    {
        // cancel the whole thing
        gtk_drag_finish( context,
                          FALSE,        // no success
                          FALSE,        // don't delete data on dropping side
                          time );

        drop_target->GTKSetDragContext( nullptr );

        drop_target->m_firstMotion = true;

        return FALSE;
    }

    /* inform the wxDropTarget about the current drag widget.
       this is only valid for the duration of this call */
    drop_target->GTKSetDragWidget( widget );

    /* inform the wxDropTarget about the current drag time.
       this is only valid for the duration of this call */
    drop_target->GTKSetDragTime( time );

    /* reset the block here as someone might very well
       show a dialog as a reaction to a drop and this
       wouldn't work without events */
    g_blockEventsOnDrag = false;

    bool ret = drop_target->OnDrop( x, y );

    if (!ret)
    {
        wxLogTrace(TRACE_DND, wxT( "Drop target: OnDrop returned FALSE") );

        /* cancel the whole thing */
        gtk_drag_finish( context,
                          FALSE,        /* no success */
                          FALSE,        /* don't delete data on dropping side */
                          time );
    }
    else
    {
        wxLogTrace(TRACE_DND, wxT( "Drop target: OnDrop returned true") );

        GdkAtom format = drop_target->GTKGetMatchingPair();

        // this does happen somehow, see bug 555111
        wxCHECK_MSG( format, FALSE, wxT("no matching GdkAtom for format?") );

        /* this should trigger an "drag_data_received" event */
        gtk_drag_get_data( widget,
                           context,
                           format,
                           time );
    }

    /* after this, invalidate the drop_target's GdkDragContext */
    drop_target->GTKSetDragContext( nullptr );

    /* after this, invalidate the drop_target's drag widget */
    drop_target->GTKSetDragWidget( nullptr );

    /* this has to be done because GDK has no "drag_enter" event */
    drop_target->m_firstMotion = true;

    return ret;
}
}

// ----------------------------------------------------------------------------
// "drag_data_received"
// ----------------------------------------------------------------------------

extern "C" {
static void target_drag_data_received( GtkWidget *WXUNUSED(widget),
                                       GdkDragContext *context,
                                       gint x,
                                       gint y,
                                       GtkSelectionData *data,
                                       guint WXUNUSED(info),
                                       guint time,
                                       wxDropTarget *drop_target )
{
    /* Owen Taylor: "call gtk_drag_finish() with
       success == TRUE" */

    if (gtk_selection_data_get_length(data) <= 0 || gtk_selection_data_get_format(data) != 8)
    {
        /* negative data length and non 8-bit data format
           qualifies for junk */
        gtk_drag_finish (context, FALSE, FALSE, time);

        return;
    }

    wxLogTrace(TRACE_DND, wxT( "Drop target: data received event") );

    /* Inform the wxDropTarget about the current GtkSelectionData and GdkDragContext.
       This is only valid for the duration of this call. */
    drop_target->GTKSetDragContext( context );
    drop_target->GTKSetDragData( data );

    wxDragResult result = ConvertFromGTK(gdk_drag_context_get_selected_action(context));

    if ( wxIsDragResultOk( drop_target->OnData( x, y, result ) ) )
    {
        wxLogTrace(TRACE_DND, wxT( "Drop target: OnData returned true") );

        /* tell GTK that data transfer was successful */
        gtk_drag_finish( context, TRUE, FALSE, time );
    }
    else
    {
        wxLogTrace(TRACE_DND, wxT( "Drop target: OnData returned FALSE") );

        /* tell GTK that data transfer was not successful */
        gtk_drag_finish( context, FALSE, FALSE, time );
    }

    /* after this, invalidate the drop_target's GtkSelectionData and GdkDragContext */
    drop_target->GTKSetDragData( nullptr );
    drop_target->GTKSetDragContext( nullptr );
}
}

#endif // !__WXGTK4__

//----------------------------------------------------------------------------
// wxDropTarget
//----------------------------------------------------------------------------

wxDropTarget::~wxDropTarget()
{
#ifdef __WXGTK4__
    if ( m_dropController )
    {
        // The controller can outlive us while an asynchronous transfer holds
        // a reference to it. Never leave its non-owning C++ pointer dangling.
        g_object_set_data(G_OBJECT(m_dropController), "wx-drop-target", nullptr);
        g_object_remove_weak_pointer(
            G_OBJECT(m_dropController),
            reinterpret_cast<gpointer*>(&m_dropController));
    }
#endif // __WXGTK4__
}

#ifdef __WXGTK4__

// ============================================================================
// GTK4: GtkDropTargetAsync and gdk_drag_begin()
// ============================================================================
//
// GTK4 replaced the whole drag and drop API. gtk_drag_dest_set() and the
// drag_motion/drag_drop/drag_data_received signals are gone, as are
// GdkDragContext, GtkTargetList and GtkSelectionData. A drop target is an
// event controller now, the drag itself is a GdkDrop on the receiving side and
// a GdkDrag on the sending side, and the data has to be read asynchronously
// rather than arriving attached to the drop.
//
// That asynchrony is the interesting part: the "drop" handler must start the
// read and return before GTK can deliver its result. The stream is drained
// with g_output_stream_splice_async(), and only then is the synchronous wx
// OnData() callback invoked with the received bytes made available to
// GetData().

extern wxDataFormat::NativeFormat
wxGTKGetAltWaylandFormat(wxDataFormat::NativeFormat format);

// The counterpart of ConvertFromGTK() above. GTK3 never needed it because the
// drag_motion handler answered with gdk_drag_status(); a GTK4 drop target
// returns the action from the handler itself.
static GdkDragAction ConvertToGTK(wxDragResult result)
{
    switch ( result )
    {
        case wxDragCopy:
            return GDK_ACTION_COPY;

        case wxDragMove:
            return GDK_ACTION_MOVE;

        case wxDragLink:
            return GDK_ACTION_LINK;

        case wxDragNone:
        case wxDragCancel:
        case wxDragError:
            break;
    }

    return GdkDragAction(0);
}

namespace
{

// The drop target a controller belongs to.
wxDropTarget* TargetFromController(GtkEventController* controller)
{
    return static_cast<wxDropTarget*>(
        g_object_get_data(G_OBJECT(controller), "wx-drop-target"));
}

const char wxDropDataKey[] = "wx-drop-data";

// Keep everything needed by the GDK read callbacks alive independently of
// both the widget and its wxDropTarget. The controller's non-owning pointer is
// cleared when the target is unregistered, so it is safe to check from the
// completion callback even if application code destroyed the target while a
// transfer was pending.
struct PendingDrop
{
    PendingDrop(GtkDropTargetAsync* target, GdkDrop* drop,
                const char* mime, int x, int y, wxDragResult suggested)
        : controller(GTK_EVENT_CONTROLLER(g_object_ref(target))),
          drop(GDK_DROP(g_object_ref(drop))),
          mime(g_strdup(mime)),
          x(x),
          y(y),
          suggested(suggested)
    {
        mimes[0] = this->mime;
        mimes[1] = nullptr;
    }

    ~PendingDrop()
    {
        if ( out )
            g_object_unref(out);

        g_free(mime);
        g_object_unref(controller);
        g_object_unref(drop);
    }

    GtkEventController* const controller;
    GdkDrop* const drop;
    char* const mime;
    const char* mimes[2];
    const int x;
    const int y;
    const wxDragResult suggested;
    GOutputStream* out = nullptr;
};

void CompletePendingDrop(PendingDrop* pending, GBytes* bytes)
{
    wxDragResult result = wxDragNone;
    if ( bytes )
    {
        if ( wxDropTarget* const dt =
                TargetFromController(pending->controller) )
        {
            // GBytes remains owned by this function for the entire OnData()
            // call; GetData() only copies from it.
            g_object_set_data(G_OBJECT(pending->drop), wxDropDataKey, bytes);
            dt->GTKSetDrop(pending->drop);
            result = dt->OnData(pending->x, pending->y, pending->suggested);

            g_object_set_data(G_OBJECT(pending->drop), wxDropDataKey, nullptr);

            // OnData() is application code and may destroy the target.
            if ( TargetFromController(pending->controller) == dt )
                dt->GTKSetDrop(nullptr);
        }
    }

    gdk_drop_finish(pending->drop,
                    result != wxDragNone
                        ? ConvertToGTK(pending->suggested)
                        : GdkDragAction(0));

    if ( bytes )
        g_bytes_unref(bytes);

    delete pending;
}

extern "C" {

void wx_drop_spliced(GObject* src, GAsyncResult* res, gpointer user_data)
{
    PendingDrop* const pending = static_cast<PendingDrop*>(user_data);

    GError* error = nullptr;
    GBytes* bytes = nullptr;
    if ( g_output_stream_splice_finish(G_OUTPUT_STREAM(src), res, &error) >= 0 )
    {
        bytes = g_memory_output_stream_steal_as_bytes(
                    G_MEMORY_OUTPUT_STREAM(src));
    }
    else if ( error )
    {
        wxLogTrace(TRACE_DND, "drop stream read failed: %s", error->message);
        g_error_free(error);
    }

    CompletePendingDrop(pending, bytes);
}

void wx_drop_read_done(GObject* src, GAsyncResult* res, gpointer user_data)
{
    PendingDrop* const pending = static_cast<PendingDrop*>(user_data);

    GError* error = nullptr;
    GInputStream* const stream =
        gdk_drop_read_finish(GDK_DROP(src), res, nullptr, &error);

    if ( !stream )
    {
        if ( error )
        {
            wxLogTrace(TRACE_DND, "drop read failed: %s", error->message);
            g_error_free(error);
        }

        CompletePendingDrop(pending, nullptr);
        return;
    }

    pending->out = g_memory_output_stream_new_resizable();
    g_output_stream_splice_async(pending->out, stream,
                                 GOutputStreamSpliceFlags(
                                    G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
                                    G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET),
                                 G_PRIORITY_DEFAULT, nullptr,
                                 wx_drop_spliced, pending);
    g_object_unref(stream);
}

} // extern "C"

} // anonymous namespace

extern "C" {

static gboolean
wx_drop_accept(GtkDropTargetAsync* target, GdkDrop* drop, gpointer)
{
    wxDropTarget* const dt = TargetFromController(GTK_EVENT_CONTROLLER(target));
    if ( !dt )
        return FALSE;

    dt->GTKSetDrop(drop);
    const bool ok = dt->GTKGetMatchingPair(true) != nullptr;
    dt->GTKSetDrop(nullptr);

    return ok;
}

static GdkDragAction
wx_drop_enter(GtkDropTargetAsync* target, GdkDrop* drop,
              double x, double y, gpointer)
{
    wxDropTarget* const dt = TargetFromController(GTK_EVENT_CONTROLLER(target));
    if ( !dt )
        return GdkDragAction(0);

    dt->GTKSetDrop(drop);

    if ( !dt->GTKGetMatchingPair() )
        return GdkDragAction(0);

    const wxDragResult result =
        dt->OnEnter(int(x), int(y), dt->GTKFigureOutSuggestedAction());

    return ConvertToGTK(result);
}

static GdkDragAction
wx_drop_motion(GtkDropTargetAsync* target, GdkDrop* drop,
               double x, double y, gpointer)
{
    wxDropTarget* const dt = TargetFromController(GTK_EVENT_CONTROLLER(target));
    if ( !dt )
        return GdkDragAction(0);

    dt->GTKSetDrop(drop);

    if ( !dt->GTKGetMatchingPair(true) )
        return GdkDragAction(0);

    const wxDragResult result =
        dt->OnDragOver(int(x), int(y), dt->GTKFigureOutSuggestedAction());

    return ConvertToGTK(result);
}

static void
wx_drop_leave(GtkDropTargetAsync* target, GdkDrop*, gpointer)
{
    if ( wxDropTarget* const dt =
            TargetFromController(GTK_EVENT_CONTROLLER(target)) )
    {
        dt->OnLeave();
        dt->GTKSetDrop(nullptr);
    }
}

static gboolean
wx_drop_perform(GtkDropTargetAsync* target, GdkDrop* drop,
                double x, double y, gpointer)
{
    wxDropTarget* const dt = TargetFromController(GTK_EVENT_CONTROLLER(target));
    if ( !dt )
        return FALSE;

    dt->GTKSetDrop(drop);

    const wxDragResult suggested = dt->GTKFigureOutSuggestedAction();

    const wxDataFormat::NativeFormat mime = dt->GTKGetMatchingPair();
    bool ok = false;
    if ( mime && dt->OnDrop(int(x), int(y)) )
    {
        // GtkDropTargetAsync requires the handler to return before completing
        // the data transfer. Keep the MIME array in PendingDrop too because
        // gdk_drop_read_async() uses it after this function has returned.
        PendingDrop* const pending =
            new PendingDrop(target, drop, mime, int(x), int(y), suggested);
        gdk_drop_read_async(drop, pending->mimes, G_PRIORITY_DEFAULT, nullptr,
                            wx_drop_read_done, pending);
        ok = true;
    }

    if ( !ok )
    {
        gdk_drop_finish(drop, GdkDragAction(0));
        dt->GTKSetDrop(nullptr);
    }

    return ok;
}

} // extern "C"

wxDropTarget::wxDropTarget( wxDataObject *data )
            : wxDropTargetBase( data )
{
    m_firstMotion = true;
    m_drop = nullptr;
    m_dropController = nullptr;
    m_dragWidget = nullptr;
}

wxDragResult wxDropTarget::OnDragOver( wxCoord WXUNUSED(x),
                                       wxCoord WXUNUSED(y),
                                       wxDragResult def )
{
    return def;
}

bool wxDropTarget::OnDrop( wxCoord WXUNUSED(x), wxCoord WXUNUSED(y) )
{
    return true;
}

wxDragResult wxDropTarget::OnData( wxCoord WXUNUSED(x), wxCoord WXUNUSED(y),
                                   wxDragResult def )
{
    return GetData() ? def : wxDragNone;
}

wxDragResult wxDropTarget::GTKFigureOutSuggestedAction()
{
    if (!m_drop)
        return wxDragError;

    // GTK4 has no separate "suggested" action: GdkDrop reports the set of
    // actions the source offers, and choosing between them is entirely ours.
    // That removes the GTK3 complication of second-guessing a suggestion which
    // was always wxDragCopy even when a move was wanted.
    const GdkDragAction actions = gdk_drop_get_actions(m_drop);

    if (GetDefaultAction() == wxDragNone)
    {
        if ( (gs_flagsForDrag & wxDrag_DefaultMove) == wxDrag_DefaultMove &&
            (actions & GDK_ACTION_MOVE))
        {
            return wxDragMove;
        }
    }
    else if (GetDefaultAction() == wxDragMove && (actions & GDK_ACTION_MOVE))
    {
        return wxDragMove;
    }

    if (actions & GDK_ACTION_COPY)
        return wxDragCopy;
    if (actions & GDK_ACTION_MOVE)
        return wxDragMove;
    if (actions & GDK_ACTION_LINK)
        return wxDragLink;

    return wxDragNone;
}

wxDataFormat wxDropTarget::GetMatchingPair()
{
    return wxDataFormat( GTKGetMatchingPair() );
}

wxDataFormat::NativeFormat wxDropTarget::GTKGetMatchingPair(bool quiet)
{
    if (!m_dataObject || !m_drop)
        return nullptr;

    GdkContentFormats* const formats = gdk_drop_get_formats(m_drop);
    if (!formats)
        return nullptr;

    gsize n = 0;
    const char* const* const mimes =
        gdk_content_formats_get_mime_types(formats, &n);

    for (gsize i = 0; i < n; i++)
    {
        const wxDataFormat format(mimes[i]);

        if ( !quiet )
        {
            wxLogTrace(TRACE_DND, wxT("Drop target: drag has format: %s"),
                       format.GetId().c_str());
        }

        if (m_dataObject->IsSupportedFormat( format ))
            return format.GetFormatId();
    }

    return nullptr;
}

bool wxDropTarget::GetData()
{
    if (!m_drop || !m_dataObject)
        return false;

    const wxDataFormat::NativeFormat mime = GTKGetMatchingPair(true);
    if (!mime)
        return false;

    GBytes* const bytes = static_cast<GBytes*>(
        g_object_get_data(G_OBJECT(m_drop), wxDropDataKey));
    if (!bytes)
        return false;

    gsize size = 0;
    gconstpointer const data = g_bytes_get_data(bytes, &size);

    return m_dataObject->SetData(wxDataFormat(mime), size, data);
}

void wxDropTarget::GtkUnregisterWidget( GtkWidget *widget )
{
    wxCHECK_RET( widget != nullptr, wxT("unregister widget is null") );

    if ( GtkEventController* const controller =
            static_cast<GtkEventController*>(
                g_object_get_data(G_OBJECT(widget), "wx-drop-controller")) )
    {
        // A pending asynchronous drop keeps the controller alive, so clear
        // its non-owning C++ pointer before detaching it from the widget.
        g_object_set_data(G_OBJECT(controller), "wx-drop-target", nullptr);

        if ( m_dropController == controller )
        {
            g_object_remove_weak_pointer(
                G_OBJECT(controller),
                reinterpret_cast<gpointer*>(&m_dropController));
            m_dropController = nullptr;
        }

        gtk_widget_remove_controller(widget, controller);
        g_object_set_data(G_OBJECT(widget), "wx-drop-controller", nullptr);
    }
}

void wxDropTarget::GtkRegisterWidget( GtkWidget *widget )
{
    wxCHECK_RET( widget != nullptr, wxT("register widget is null") );

    // Unlike gtk_drag_dest_set(), which could be told to supply no formats and
    // no actions so that wx could decide everything itself in the signal
    // handlers, a GtkDropTargetAsync is created with both up front. The
    // formats are those the data object accepts and the actions everything wx
    // might return; the handlers still decide per position, by returning the
    // action to use or none at all.
    GdkContentFormats* formats = nullptr;

    if ( m_dataObject )
    {
        const size_t count = m_dataObject->GetFormatCount(wxDataObject::Set);
        if ( count )
        {
            wxDataFormat* const array = new wxDataFormat[count];
            m_dataObject->GetAllFormats(array, wxDataObject::Set);

            GdkContentFormatsBuilder* const builder =
                gdk_content_formats_builder_new();
            for ( size_t i = 0; i < count; i++ )
                gdk_content_formats_builder_add_mime_type(builder,
                                                          array[i].GetFormatId());

            delete[] array;

            formats = gdk_content_formats_builder_free_to_formats(builder);
        }
    }

    // Note that gtk_drop_target_async_new() takes ownership of the formats
    // (they are annotated "transfer full"), so they must not be unreffed
    // here: doing that frees them while the drop target is still holding
    // them, and the widget's eventual destruction then unrefs freed memory.
    GtkDropTargetAsync* const target = gtk_drop_target_async_new(
        formats, GdkDragAction(GDK_ACTION_COPY | GDK_ACTION_MOVE |
                               GDK_ACTION_LINK));

    wxASSERT_MSG(!m_dropController, "drop target is already registered");
    m_dropController = GTK_EVENT_CONTROLLER(target);
    g_object_add_weak_pointer(
        G_OBJECT(target), reinterpret_cast<gpointer*>(&m_dropController));

    g_object_set_data(G_OBJECT(target), "wx-drop-target", this);

    g_signal_connect(target, "accept", G_CALLBACK(wx_drop_accept), nullptr);
    g_signal_connect(target, "drag-enter", G_CALLBACK(wx_drop_enter), nullptr);
    g_signal_connect(target, "drag-motion", G_CALLBACK(wx_drop_motion), nullptr);
    g_signal_connect(target, "drag-leave", G_CALLBACK(wx_drop_leave), nullptr);
    g_signal_connect(target, "drop", G_CALLBACK(wx_drop_perform), nullptr);

    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(target));

    g_object_set_data(G_OBJECT(widget), "wx-drop-controller", target);
}

//----------------------------------------------------------------------------
// wxDropSource
//----------------------------------------------------------------------------

extern "C" {

static void wx_drag_finished(GdkDrag*, wxDropSource* source)
{
    source->m_waiting = false;
}

static void wx_drag_cancelled(GdkDrag*, GdkDragCancelReason, wxDropSource* source)
{
    source->m_retValue = wxDragCancel;
    source->m_waiting = false;
}

} // extern "C"

wxDragResult wxDropSource::DoDragDrop(int flags)
{
    wxCHECK_MSG( m_data && m_data->GetFormatCount(), wxDragNone,
                 wxT("Drop source: no data") );

    // still in drag
    if (g_blockEventsOnDrag)
        return wxDragNone;

    // don't start dragging if no button is down
    if (g_lastButtonNumber == 0)
        return wxDragNone;

    // we can only start a drag after a mouse event
    if (g_lastMouseEvent == nullptr)
        return wxDragNone;

    GdkSurface* const surface = wx_gtk_widget_get_surface(m_widget);
    if ( !surface )
        return wxDragError;

    GdkDevice* const device =
        wx_get_gdk_device_from_display(gtk_widget_get_display(m_widget));
    if ( !device )
        return wxDragError;

    // The data is serialised here rather than being supplied lazily on
    // request, for the same reason as in wxClipboard::AddData(): GTK4's lazy
    // form means subclassing GdkContentProvider.
    const size_t count = m_data->GetFormatCount(wxDataObject::Get);
    wxDataFormat* const array = new wxDataFormat[count];
    m_data->GetAllFormats(array, wxDataObject::Get);

    wxVector<GdkContentProvider*> providers;
    for ( size_t i = 0; i < count; i++ )
    {
        const size_t size = m_data->GetDataSize(array[i]);
        if ( !size )
            continue;

        wxCharBuffer buf(size);
        if ( !m_data->GetDataHere(array[i], buf.data()) )
            continue;

        GBytes* const bytes = g_bytes_new(buf.data(), size);
        providers.push_back(
            gdk_content_provider_new_for_bytes(array[i].GetFormatId(), bytes));
        g_bytes_unref(bytes);
    }

    delete[] array;

    if ( providers.empty() )
        return wxDragNone;

    // new_union() takes ownership of the providers given to it.
    GdkContentProvider* const content = providers.size() == 1
        ? providers[0]
        : gdk_content_provider_new_union(&providers[0], providers.size());

    int allowed_actions = GDK_ACTION_COPY;
    if ( flags & wxDrag_AllowMove )
        allowed_actions |= GDK_ACTION_MOVE;

    // VZ: as we already use g_blockEventsOnDrag it shouldn't be that bad
    //     to use a global to pass the flags to the drop target but I'd
    //     surely prefer a better way to do it
    gs_flagsForDrag = flags;

    m_retValue = wxDragCopy;
    m_waiting = true;

    GdkDrag* const drag = gdk_drag_begin(surface, device, content,
                                         GdkDragAction(allowed_actions),
                                         0, 0);
    g_object_unref(content);

    if ( !drag )
        return wxDragError;

    m_dragContext = drag;

    // There is no drag icon set here: GTK4 draws one from the content provider
    // by default, and the GTK3 code's approach -- an override-redirect window
    // whose shape is combined from a bitmap mask -- has no GTK4 form at all.
    g_signal_connect(drag, "dnd-finished", G_CALLBACK(wx_drag_finished), this);
    g_signal_connect(drag, "cancel", G_CALLBACK(wx_drag_cancelled), this);

    // wxDropSource::DoDragDrop() is documented to return only once the drag
    // has finished, so spin a nested loop, as everything else in this port
    // that has to be synchronous over an asynchronous API does.
    while ( m_waiting )
    {
        g_main_context_iteration(nullptr, TRUE);

        GiveFeedback(ConvertFromGTK(gdk_drag_get_selected_action(drag)));
    }

    if ( m_retValue != wxDragCancel )
        m_retValue = ConvertFromGTK(gdk_drag_get_selected_action(drag));

    g_signal_handlers_disconnect_by_data(drag, this);
    m_dragContext = nullptr;

    return m_retValue;
}

wxDropSource::wxDropSource(wxWindow *win,
                           const wxIcon &iconCopy,
                           const wxIcon &iconMove,
                           const wxIcon &iconNone)
    : m_iconCopy(iconCopy)
    , m_iconMove(iconMove)
    , m_iconNone(iconNone)
{
    Init(win);
}

wxDropSource::wxDropSource(wxDataObject& data,
                           wxWindow *win,
                           const wxIcon &iconCopy,
                           const wxIcon &iconMove,
                           const wxIcon &iconNone)
    : wxDropSource(win, iconCopy, iconMove, iconNone)
{
    SetData( data );
}

void wxDropSource::Init(wxWindow* win)
{
    m_waiting = true;
    m_retValue = wxDragNone;
    m_dragContext = nullptr;
    m_widget = win->m_wxwindow ? win->m_wxwindow : win->m_widget;

    // The icons are stored but never used: see the note about drag icons
    // above. They are kept so that SetIcon() remains a no-op rather than an
    // error, and so that this can start working again without an API change
    // if GTK4 ever grows a way to supply one.
}

wxDropSource::~wxDropSource()
{
}

#else // !__WXGTK4__

wxDropTarget::wxDropTarget( wxDataObject *data )
            : wxDropTargetBase( data )
{
    m_firstMotion = true;
    m_dragContext = nullptr;
    m_dragWidget = nullptr;
    m_dragData = nullptr;
    m_dragTime = 0;
}

wxDragResult wxDropTarget::OnDragOver( wxCoord WXUNUSED(x),
                                       wxCoord WXUNUSED(y),
                                       wxDragResult def )
{
    return def;
}

bool wxDropTarget::OnDrop( wxCoord WXUNUSED(x), wxCoord WXUNUSED(y) )
{
    return true;
}

wxDragResult wxDropTarget::OnData( wxCoord WXUNUSED(x), wxCoord WXUNUSED(y),
                                   wxDragResult def )
{
    return GetData() ? def : wxDragNone;
}

wxDragResult wxDropTarget::GTKFigureOutSuggestedAction()
{
    if (!m_dragContext)
        return wxDragError;

    // GTK+ always supposes that we want to copy the data by default while we
    // might want to move it, so examine not only suggested_action - which is
    // only good if we don't have our own preferences - but also the actions
    // field
    wxDragResult suggested_action = wxDragNone;
    const GdkDragAction actions = gdk_drag_context_get_actions(m_dragContext);
    if (GetDefaultAction() == wxDragNone)
    {
        // use default action set by wxDropSource::DoDragDrop()
        if ( (gs_flagsForDrag & wxDrag_DefaultMove) == wxDrag_DefaultMove &&
            (actions & GDK_ACTION_MOVE))
        {
            // move is requested by the program and allowed by GTK+ - do it, even
            // though suggested_action may be currently wxDragCopy
            suggested_action = wxDragMove;
        }
        else // use whatever GTK+ says we should
        {
            suggested_action = ConvertFromGTK(gdk_drag_context_get_suggested_action(m_dragContext));

#if 0
            // RR: I don't understand the code below: if the drag comes from
            //     a different app, the gs_flagsForDrag is invalid; if it
            //     comes from the same wx app, then GTK+ hopefully won't
            //     suggest something we didn't allow in the frist place
            //     in DoDrop()
            if ( (suggested_action == wxDragMove) && !(gs_flagsForDrag & wxDrag_AllowMove) )
            {
                // we're requested to move but we can't
                suggested_action = wxDragCopy;
            }
#endif
        }
    }
    else if (GetDefaultAction() == wxDragMove &&
            (actions & GDK_ACTION_MOVE))
    {

        suggested_action = wxDragMove;
    }
    else
    {
        if (actions & GDK_ACTION_COPY)
            suggested_action = wxDragCopy;
        else if (actions & GDK_ACTION_MOVE)
            suggested_action = wxDragMove;
        else if (actions & GDK_ACTION_LINK)
            suggested_action = wxDragLink;
        else
            suggested_action = wxDragNone;
    }

    return suggested_action;
}

wxDataFormat wxDropTarget::GetMatchingPair()
{
    return wxDataFormat( GTKGetMatchingPair() );
}

GdkAtom wxDropTarget::GTKGetMatchingPair(bool quiet)
{
    if (!m_dataObject)
        return (GdkAtom) nullptr;

    if (!m_dragContext)
        return (GdkAtom) nullptr;

    const GList* child = gdk_drag_context_list_targets(m_dragContext);
    while (child)
    {
        GdkAtom formatAtom = (GdkAtom)(child->data);
        wxDataFormat format( formatAtom );

        if ( !quiet )
        {
            wxLogTrace(TRACE_DND, wxT("Drop target: drag has format: %s"),
                       format.GetId().c_str());
        }

        if (m_dataObject->IsSupportedFormat( format ))
            return formatAtom;

        child = child->next;
    }

    return (GdkAtom) nullptr;
}

bool wxDropTarget::GetData()
{
    if (!m_dragData)
        return false;

    if (!m_dataObject)
        return false;

    wxDataFormat dragFormat(gtk_selection_data_get_target(m_dragData));

    if (!m_dataObject->IsSupportedFormat( dragFormat ))
        return false;

    m_dataObject->SetData(dragFormat,
        (size_t)gtk_selection_data_get_length(m_dragData),
        (const void*)gtk_selection_data_get_data(m_dragData));

    return true;
}

void wxDropTarget::GtkUnregisterWidget( GtkWidget *widget )
{
    wxCHECK_RET( widget != nullptr, wxT("unregister widget is null") );

    gtk_drag_dest_unset( widget );

    g_signal_handlers_disconnect_by_func (widget,
                                          (gpointer) target_drag_leave, this);
    g_signal_handlers_disconnect_by_func (widget,
                                          (gpointer) target_drag_motion, this);
    g_signal_handlers_disconnect_by_func (widget,
                                          (gpointer) target_drag_drop, this);
    g_signal_handlers_disconnect_by_func (widget,
                                          (gpointer) target_drag_data_received, this);
}

void wxDropTarget::GtkRegisterWidget( GtkWidget *widget )
{
    wxCHECK_RET( widget != nullptr, wxT("register widget is null") );

    /* gtk_drag_dest_set() determines what default behaviour we'd like
       GTK to supply. we don't want to specify out targets (=formats)
       or actions in advance (i.e. not GTK_DEST_DEFAULT_MOTION and
       not GTK_DEST_DEFAULT_DROP). instead we react individually to
       "drag_motion" and "drag_drop" events. this makes it possible
       to allow dropping on only a small area. we should set
       GTK_DEST_DEFAULT_HIGHLIGHT as this will switch on the nice
       highlighting if dragging over standard controls, but this
       seems to be broken without the other two. */

    gtk_drag_dest_set( widget,
                       (GtkDestDefaults) 0,         /* no default behaviour */
                       nullptr,      /* we don't supply any formats here */
                       0,                           /* number of targets = 0 */
                       (GdkDragAction) 0 );         /* we don't supply any actions here */

    g_signal_connect (widget, "drag_leave",
                      G_CALLBACK (target_drag_leave), this);

    g_signal_connect (widget, "drag_motion",
                      G_CALLBACK (target_drag_motion), this);

    g_signal_connect (widget, "drag_drop",
                      G_CALLBACK (target_drag_drop), this);

    g_signal_connect (widget, "drag_data_received",
                      G_CALLBACK (target_drag_data_received), this);
}

//----------------------------------------------------------------------------
// "drag_data_get"
//----------------------------------------------------------------------------

extern "C" {
static void
source_drag_data_get  (GtkWidget          *WXUNUSED(widget),
                       GdkDragContext     *context,
                       GtkSelectionData   *selection_data,
                       guint               WXUNUSED(info),
                       guint               WXUNUSED(time),
                       wxDropSource       *drop_source )
{
    wxDataFormat format(gtk_selection_data_get_target(selection_data));

    wxLogTrace(TRACE_DND, wxT("Drop source: format requested: %s"),
               format.GetId().c_str());

    drop_source->m_retValue = wxDragError;

    wxDataObject *data = drop_source->GetDataObject();

    if (!data)
    {
        wxLogTrace(TRACE_DND, wxT("Drop source: no data object") );
        return;
    }

    if (!data->IsSupportedFormat(format))
    {
        wxLogTrace(TRACE_DND, wxT("Drop source: unsupported format") );
        return;
    }

    if (data->GetDataSize(format) == 0)
    {
        wxLogTrace(TRACE_DND, wxT("Drop source: empty data") );
        return;
    }

    size_t size = data->GetDataSize(format);

//  printf( "data size: %d.\n", (int)data_size );

    guchar *d = new guchar[size];

    if (!data->GetDataHere( format, (void*)d ))
    {
        delete[] d;
        return;
    }

    drop_source->m_retValue = ConvertFromGTK(gdk_drag_context_get_selected_action(context));

    gtk_selection_data_set( selection_data,
                            gtk_selection_data_get_target(selection_data),
                            8,   // 8-bit
                            d,
                            size );

    delete[] d;
}
}

//----------------------------------------------------------------------------
// "drag_end"
//----------------------------------------------------------------------------

extern "C" {
static void source_drag_end( GtkWidget          *WXUNUSED(widget),
                             GdkDragContext     *WXUNUSED(context),
                             wxDropSource       *drop_source )
{
    drop_source->m_waiting = false;
}
}

//-----------------------------------------------------------------------------
// "configure_event" from m_iconWindow
//-----------------------------------------------------------------------------

extern "C" {
static gboolean
gtk_dnd_window_configure_callback( GtkWidget *WXUNUSED(widget), GdkEventConfigure *WXUNUSED(event), wxDropSource *source )
{
    source->GiveFeedback(ConvertFromGTK(gdk_drag_context_get_selected_action(source->m_dragContext)));

    return false;
}
}

//-----------------------------------------------------------------------------
// "draw" from m_iconWindow
//-----------------------------------------------------------------------------

#ifdef __WXGTK3__
extern "C" {
static gboolean draw_icon(GtkWidget*, cairo_t* cr, wxIcon* icon)
{
    icon->Draw(cr, 0, 0);
    return true;
}
}

extern "C" {
static gboolean wx_gtk_mouse_event(GtkWidget*, GdkEvent*, wxDropSource* source)
{
    source->m_waiting = false;
    return false;
}
}
#endif // __WXGTK3__

//---------------------------------------------------------------------------
// wxDropSource
//---------------------------------------------------------------------------

wxDropSource::wxDropSource(wxWindow *win,
                           const wxIcon &iconCopy,
                           const wxIcon &iconMove,
                           const wxIcon &iconNone)
    : m_iconCopy(iconCopy)
    , m_iconMove(iconMove)
    , m_iconNone(iconNone)
{
    Init(win);
}

wxDropSource::wxDropSource(wxDataObject& data,
                           wxWindow *win,
                           const wxIcon &iconCopy,
                           const wxIcon &iconMove,
                           const wxIcon &iconNone)
    : wxDropSource(win, iconCopy, iconMove, iconNone)
{
    SetData( data );
}

void wxDropSource::Init(wxWindow* win)
{
    m_waiting = true;
    m_iconWindow = nullptr;
    m_retValue = wxDragNone;
    m_widget = win->m_wxwindow ? win->m_wxwindow : win->m_widget;

    if ( !m_iconCopy.IsOk() )
        m_iconCopy = wxIcon(page_xpm);
    if ( !m_iconMove.IsOk() )
        m_iconMove = m_iconCopy;
    if ( !m_iconNone.IsOk() )
        m_iconNone = m_iconCopy;
}

wxDropSource::~wxDropSource()
{
}

void wxDropSource::PrepareIcon( int action, GdkDragContext *context )
{
    // get the right icon to display
    wxIcon *icon = nullptr;
    if ( action & GDK_ACTION_MOVE )
        icon = &m_iconMove;
    else if ( action & GDK_ACTION_COPY )
        icon = &m_iconCopy;
    else
        icon = &m_iconNone;

#ifdef __WXGTK3__
    GtkWidget* widget;
    if (gtk_check_version(3,20,0) == nullptr)
    {
        widget = gtk_drawing_area_new();
        gtk_widget_set_size_request(widget, icon->GetWidth(), icon->GetHeight());
        gtk_widget_show(widget);
        gtk_drag_set_icon_widget(context, widget, 0, 0);
        // GTK >= 3.20 puts the icon widget in a GTK_WINDOW_POPUP,
        // we need to connect to that widget to get "configure-event"
        m_iconWindow = gtk_widget_get_parent(widget);
    }
    else
    {
        widget = gtk_window_new(GTK_WINDOW_POPUP);
        m_iconWindow = widget;
        gtk_widget_set_size_request(widget, icon->GetWidth(), icon->GetHeight());
        gtk_widget_set_app_paintable(widget, true);
        gtk_drag_set_icon_widget(context, m_iconWindow, 0, 0);
    }

    wxMask* wxmask = icon->GetMask();
    cairo_surface_t* mask;
    if (wxmask && (mask = *wxmask))
    {
        cairo_region_t* region = gdk_cairo_region_create_from_surface(mask);
        gtk_widget_shape_combine_region(m_iconWindow, region);
        cairo_region_destroy(region);
    }

    g_signal_connect(widget, "draw", G_CALLBACK(draw_icon), icon);

#else // !__WXGTK3__

    GdkBitmap *mask;
    if ( icon->GetMask() )
        mask = *icon->GetMask();
    else
        mask = nullptr;

    GdkPixmap *pixmap = icon->GetPixmap();

    GdkColormap *colormap = gtk_widget_get_colormap( m_widget );
    gtk_widget_push_colormap (colormap);

    m_iconWindow = gtk_window_new (GTK_WINDOW_POPUP);
    gtk_widget_set_events (m_iconWindow, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    gtk_widget_set_app_paintable (m_iconWindow, TRUE);

    gtk_widget_pop_colormap ();

    gtk_widget_set_size_request (m_iconWindow, icon->GetWidth(), icon->GetHeight());
    gtk_widget_realize (m_iconWindow);

    gdk_window_set_back_pixmap(gtk_widget_get_window(m_iconWindow), pixmap, false);

    if (mask)
        gtk_widget_shape_combine_mask (m_iconWindow, mask, 0, 0);

    gtk_drag_set_icon_widget( context, m_iconWindow, 0, 0 );
#endif // !__WXGTK3__

    g_object_ref(m_iconWindow);
    g_signal_connect(m_iconWindow, "configure-event",
        G_CALLBACK(gtk_dnd_window_configure_callback), this);
}

wxDragResult wxDropSource::DoDragDrop(int flags)
{
    wxCHECK_MSG( m_data && m_data->GetFormatCount(), wxDragNone,
                 wxT("Drop source: no data") );

    // still in drag
    if (g_blockEventsOnDrag)
        return wxDragNone;

    // don't start dragging if no button is down
    if (g_lastButtonNumber == 0)
        return wxDragNone;

    // we can only start a drag after a mouse event
    if (g_lastMouseEvent == nullptr)
        return wxDragNone;

    GTKConnectDragSignals();
    wxON_BLOCK_EXIT_OBJ0(*this, wxDropSource::GTKDisconnectDragSignals);

    m_waiting = true;

    GtkTargetList *target_list = gtk_target_list_new( nullptr, 0 );

    wxDataFormat *array = new wxDataFormat[ m_data->GetFormatCount() ];
    m_data->GetAllFormats( array );
    size_t count = m_data->GetFormatCount();
    for (size_t i = 0; i < count; i++)
    {
        GdkAtom atom = array[i];
        gtk_target_list_add( target_list, atom, 0, 0 );
    }
    delete[] array;

    int allowed_actions = GDK_ACTION_COPY;
    if ( flags & wxDrag_AllowMove )
        allowed_actions |= GDK_ACTION_MOVE;

    // VZ: as we already use g_blockEventsOnDrag it shouldn't be that bad
    //     to use a global to pass the flags to the drop target but I'd
    //     surely prefer a better way to do it
    gs_flagsForDrag = flags;

    m_retValue = wxDragCancel;

    // gtk_drag_begin() is deprecated and gtk_drag_begin_with_coordinates()
    // should be used instead, but the former is exactly the same as calling
    // the latter with (-1, -1) coordinates, meaning to use the current pointer
    // position, and as this is exactly what we want to do here, just keep
    // using the old function and suppress the warnings about doing it.
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)

    GdkDragContext *context = gtk_drag_begin( m_widget,
                target_list,
                (GdkDragAction)allowed_actions,
                g_lastButtonNumber,  // number of mouse button which started drag
                g_lastMouseEvent );

    wxGCC_WARNING_RESTORE(deprecated-declarations)

    gtk_target_list_unref(target_list);

    if ( !context )
    {
        // this can happen e.g. if gdk_pointer_grab() failed
        return wxDragError;
    }

    m_dragContext = context;

    PrepareIcon( allowed_actions, context );

    for (;;)
    {
        gtk_main_iteration();
        if (!m_waiting)
            break;
#ifdef __WXGTK3__
        if (wxGTKImpl::IsWayland(nullptr))
            GiveFeedback(ConvertFromGTK(gdk_drag_context_get_selected_action(context)));
#endif
    }

    g_signal_handlers_disconnect_by_func (m_iconWindow,
                                          (gpointer) gtk_dnd_window_configure_callback, this);
#ifdef __WXGTK3__
    GtkWidget* drawWidget = m_iconWindow;
    if (gtk_check_version(3,20,0) == nullptr)
        drawWidget = gtk_bin_get_child(GTK_BIN(drawWidget));
    if (drawWidget)
    {
        g_signal_handlers_disconnect_matched(
            drawWidget, G_SIGNAL_MATCH_FUNC, 0, 0, nullptr, (void*)draw_icon, nullptr);
    }
#endif
    g_object_unref(m_iconWindow);
    m_iconWindow = nullptr;

    return m_retValue;
}

#endif // __WXGTK4__/!__WXGTK4__

#ifndef __WXGTK4__

void wxDropSource::GTKConnectDragSignals()
{
    if (!m_widget)
        return;

    g_blockEventsOnDrag = true;

    g_signal_connect (m_widget, "drag_data_get",
                      G_CALLBACK (source_drag_data_get), this);
    g_signal_connect (m_widget, "drag_end",
                      G_CALLBACK (source_drag_end), this);
#ifdef __WXGTK3__
    if (wxGTKImpl::IsWayland(gtk_widget_get_display(m_widget)))
    {
        // Something can apparently go wrong under Wayland, and the
        // "drag-end" event never happens. This is a work-around to
        // at least allow DoDragDrop() to finish.
        g_signal_connect(m_widget, "button-press-event", G_CALLBACK(wx_gtk_mouse_event), this);
        g_signal_connect(m_widget, "button-release-event", G_CALLBACK(wx_gtk_mouse_event), this);
        g_signal_connect(m_widget, "motion-notify-event", G_CALLBACK(wx_gtk_mouse_event), this);
    }
#endif
}

void wxDropSource::GTKDisconnectDragSignals()
{
    if (!m_widget)
        return;

    g_blockEventsOnDrag = false;

    g_signal_handlers_disconnect_by_func (m_widget,
                                          (gpointer) source_drag_data_get,
                                          this);
    g_signal_handlers_disconnect_by_func (m_widget,
                                          (gpointer) source_drag_end,
                                          this);
#ifdef __WXGTK3__
    g_signal_handlers_disconnect_by_func(m_widget, (void*)wx_gtk_mouse_event, this);
#endif
}

#endif // !__WXGTK4__

#endif
      // wxUSE_DRAG_AND_DROP
