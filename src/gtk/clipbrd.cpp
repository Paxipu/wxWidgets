/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/clipbrd.cpp
// Purpose:     wxClipboard implementation for wxGTK
// Author:      Robert Roebling, Vadim Zeitlin
// Copyright:   (c) 1998 Robert Roebling
//              (c) 2007 Vadim Zeitlin
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_CLIPBOARD

#include "wx/clipbrd.h"

#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/log.h"
    #include "wx/utils.h"
    #include "wx/dataobj.h"
#endif

#include "wx/scopedarray.h"
#include "wx/scopeguard.h"
#include "wx/evtloop.h"

#include "wx/gtk/private.h"
#include "wx/gtk/private/backend.h"

typedef wxScopedArray<wxDataFormat> wxDataFormatArray;

#ifdef __WXGTK4__

// ============================================================================
// GTK4: GdkClipboard
// ============================================================================
//
// GTK4 removed the X11 selection protocol wholesale -- gtk_selection_*, the
// invisible owner widget, "selection_get"/"selection_received" and GdkAtom
// targets are all gone. A clipboard is a GdkClipboard which owns a
// GdkContentProvider, and reading it is asynchronous.
//
// wxClipboard's API is synchronous, so GetData() bridges the two with a nested
// main loop, the same way modal dialogs and popup menus do elsewhere in this
// port. The critical detail -- see docs/gtk/probes/gtk4-clipboard.c -- is that
// the whole read, including draining the GInputStream, must stay asynchronous
// and be pumped by that one loop. When the clipboard is locally owned the
// stream's writer is our own content provider, running on the same main
// context, so a blocking g_output_stream_splice() deadlocks against it and no
// watchdog can recover: the loop is stuck inside the read callback.

static const wxString TRACE_CLIPBOARD = wxT("clipboard");

// Defined in dataobj.cpp: the alternative spelling of a text format, as the
// two in use ("UTF8_STRING"/"text/plain;charset=utf-8" and "STRING"/
// "text/plain") are interchangeable and which one is offered is up to the
// other side.
extern wxDataFormat::NativeFormat
wxGTKGetAltWaylandFormat(wxDataFormat::NativeFormat format);

namespace
{

// Collect the MIME types a data object handles in the given direction: Get
// for the formats it can provide when we put it on the clipboard, Set for the
// ones it can accept when we fill it from the clipboard. These are not the
// same list and using the wrong one silently finds nothing.
void GetDataObjectFormats(wxDataObject* data,
                          wxDataObject::Direction dir,
                          wxVector<wxDataFormat>& formats)
{
    const size_t count = data->GetFormatCount(dir);
    if ( !count )
        return;

    wxDataFormatArray all(new wxDataFormat[count]);
    data->GetAllFormats(all.get(), dir);

    for ( size_t i = 0; i < count; i++ )
        formats.push_back(all[i]);
}

// State shared with the async callbacks for the duration of one read.
struct ClipboardRead
{
    GMainLoop* loop = nullptr;
    GOutputStream* out = nullptr;
    GBytes* bytes = nullptr;
    bool finished = false;
};

extern "C" {

void wx_clipboard_spliced(GObject* src, GAsyncResult* res, gpointer user_data)
{
    ClipboardRead* const read = static_cast<ClipboardRead*>(user_data);

    if ( g_output_stream_splice_finish(G_OUTPUT_STREAM(src), res, nullptr) >= 0 )
    {
        read->bytes = g_memory_output_stream_steal_as_bytes(
                        G_MEMORY_OUTPUT_STREAM(src));
    }

    read->finished = true;
    g_main_loop_quit(read->loop);
}

void wx_clipboard_read_done(GObject* src, GAsyncResult* res, gpointer user_data)
{
    ClipboardRead* const read = static_cast<ClipboardRead*>(user_data);

    GError* error = nullptr;
    GInputStream* const stream =
        gdk_clipboard_read_finish(GDK_CLIPBOARD(src), res, nullptr, &error);

    if ( !stream )
    {
        if ( error )
        {
            wxLogTrace(TRACE_CLIPBOARD, "clipboard read failed: %s",
                       error->message);
            g_error_free(error);
        }

        read->finished = true;
        g_main_loop_quit(read->loop);
        return;
    }

    // Asynchronously, deliberately: see the comment at the top of this block.
    read->out = g_memory_output_stream_new_resizable();
    g_output_stream_splice_async(read->out, stream,
                                 GOutputStreamSpliceFlags(
                                    G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
                                    G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET),
                                 G_PRIORITY_DEFAULT, nullptr,
                                 wx_clipboard_spliced, read);
    g_object_unref(stream);
}

} // extern "C"

// Read one format off the clipboard, blocking until it is available. Returns
// null if the clipboard doesn't have it, or if reading it failed.
GBytes* ReadFormatSync(GdkClipboard* clipboard, const char* mime)
{
    const char* mimes[2] = { mime, nullptr };

    ClipboardRead read;
    read.loop = g_main_loop_new(nullptr, FALSE);

    gdk_clipboard_read_async(clipboard, mimes, G_PRIORITY_DEFAULT, nullptr,
                             wx_clipboard_read_done, &read);

    // The callbacks always quit the loop, on the failure path too, so this
    // can't be entered without a way out.
    g_main_loop_run(read.loop);
    g_main_loop_unref(read.loop);

    if ( read.out )
        g_object_unref(read.out);

    return read.bytes;
}

} // anonymous namespace

wxClipboard::wxClipboard()
{
    m_open = false;
    m_receivedData = nullptr;
    m_formatSupported = false;
}

wxClipboard::~wxClipboard()
{
    Clear();
}

// ----------------------------------------------------------------------------
// wxClipboard helper functions
// ----------------------------------------------------------------------------

GdkClipboard* wxClipboard::GTKGetClipboard() const
{
    GdkDisplay* const display = gdk_display_get_default();
    if ( !display )
        return nullptr;

    return m_usePrimary ? gdk_display_get_primary_clipboard(display)
                        : gdk_display_get_clipboard(display);
}

void wxClipboard::GTKClearData(Kind kind)
{
    Data(kind).reset();
}

GdkContentProvider* wxClipboard::GTKMakeContentProvider()
{
    wxDataObject* const data = Data().get();
    if ( !data )
        return nullptr;

    wxVector<wxDataFormat> formats;
    GetDataObjectFormats(data, wxDataObject::Get, formats);
    if ( formats.empty() )
        return nullptr;

    // Note that the data is serialised here rather than when the clipboard is
    // actually read. GTK3 offered it lazily, through a "selection_get"
    // callback; GTK4's equivalent would mean subclassing GdkContentProvider,
    // and this way the bytes cannot go stale under a data object the
    // application mutates or destroys after copying.
    wxVector<GdkContentProvider*> providers;
    for ( size_t i = 0; i < formats.size(); i++ )
    {
        const wxDataFormat& format = formats[i];

        const size_t size = data->GetDataSize(format);
        if ( !size )
            continue;

        wxCharBuffer buf(size);
        if ( !data->GetDataHere(format, buf.data()) )
            continue;

        GBytes* const bytes = g_bytes_new(buf.data(), size);
        providers.push_back(
            gdk_content_provider_new_for_bytes(format.GetFormatId(), bytes));
        g_bytes_unref(bytes);
    }

    if ( providers.empty() )
        return nullptr;

    if ( providers.size() == 1 )
        return providers[0];

    // gdk_content_provider_new_union() takes ownership of the providers it is
    // given, so they must not be unreffed here.
    return gdk_content_provider_new_union(&providers[0], providers.size());
}

bool wxClipboard::DoIsSupported(const wxDataFormat& format)
{
    wxCHECK_MSG( format, false, wxT("invalid clipboard format") );

    GdkClipboard* const clipboard = GTKGetClipboard();
    if ( !clipboard )
        return false;

    GdkContentFormats* const formats = gdk_clipboard_get_formats(clipboard);
    if ( !formats )
        return false;

    if ( gdk_content_formats_contain_mime_type(formats, format.GetFormatId()) )
        return true;

    // Text has two spellings and which one is offered depends on the other
    // side, so accept either, as the GTK3 code did.
    const wxDataFormat::NativeFormat alt =
        wxGTKGetAltWaylandFormat(format.GetFormatId());

    return alt && gdk_content_formats_contain_mime_type(formats, alt);
}

// ----------------------------------------------------------------------------
// wxClipboard public API implementation
// ----------------------------------------------------------------------------

void wxClipboard::Clear()
{
    if ( GdkClipboard* const clipboard = GTKGetClipboard() )
        gdk_clipboard_set_content(clipboard, nullptr);

    Data().reset();

    m_formatSupported = false;
}

bool wxClipboard::Flush()
{
    // There is no equivalent under GTK4: gtk_clipboard_store() is gone and
    // whether the contents outlive the application is entirely up to the
    // clipboard manager, which an application cannot ask to persist them.
    return false;
}

bool wxClipboard::Open()
{
    wxCHECK_MSG( !m_open, false, wxT("clipboard already open") );

    m_open = true;

    return true;
}

bool wxClipboard::SetData( wxDataObject *data )
{
    wxCHECK_MSG( m_open, false, wxT("clipboard not open") );
    wxCHECK_MSG( data, false, wxT("data is invalid") );

    return AddData( data );
}

bool wxClipboard::AddData( wxDataObject *data )
{
    wxCHECK_MSG( m_open, false, wxT("clipboard not open") );
    wxCHECK_MSG( data, false, wxT("data is invalid") );

    // we can only store one wxDataObject so clear the old one
    Data().reset(data);

    GdkClipboard* const clipboard = GTKGetClipboard();
    if ( !clipboard )
        return false;

    GdkContentProvider* const provider = GTKMakeContentProvider();
    if ( !provider )
        return false;

    const bool ok = gdk_clipboard_set_content(clipboard, provider) != 0;
    g_object_unref(provider);

    return ok;
}

void wxClipboard::Close()
{
    wxCHECK_RET( m_open, wxT("clipboard not open") );

    m_open = false;
}

bool wxClipboard::IsOpened() const
{
    return m_open;
}

bool wxClipboard::IsSupported( const wxDataFormat& format )
{
    return DoIsSupported(format);
}

bool wxClipboard::IsSupportedAsync( wxEvtHandler *sink )
{
    if ( m_sink )
        return false;  // currently busy, come back later

    wxCHECK_MSG( sink, false, wxT("no sink given") );

    GdkClipboard* const clipboard = GTKGetClipboard();
    if ( !clipboard )
        return false;

    // Unlike GTK3, where this meant a round trip through the X server for the
    // TARGETS atom, GTK4 already knows what the clipboard holds, so the answer
    // can be built right away. The event is still queued rather than sent
    // directly to keep the asynchronous contract this function documents.
    wxClipboardEvent* const event = new wxClipboardEvent(wxEVT_CLIPBOARD_CHANGED);
    event->SetEventObject( this );

    if ( GdkContentFormats* const formats = gdk_clipboard_get_formats(clipboard) )
    {
        gsize n = 0;
        const char* const* const mimes =
            gdk_content_formats_get_mime_types(formats, &n);

        for ( gsize i = 0; i < n; i++ )
        {
            const wxDataFormat format(mimes[i]);

            wxLogTrace(TRACE_CLIPBOARD, wxT("\t%s"), format.GetId());

            event->AddFormat( format );
        }
    }

    sink->QueueEvent( event );

    return true;
}

bool wxClipboard::GetData( wxDataObject& data )
{
    wxCHECK_MSG( m_open, false, wxT("clipboard not open") );

    GdkClipboard* const clipboard = GTKGetClipboard();
    if ( !clipboard )
        return false;

    wxVector<wxDataFormat> formats;
    GetDataObjectFormats(&data, wxDataObject::Set, formats);

    for ( size_t i = 0; i < formats.size(); i++ )
    {
        const wxDataFormat& format = formats[i];

        if ( !DoIsSupported(format) )
            continue;

        // The other side may be offering the alternative spelling of a text
        // format rather than the one we asked for.
        wxDataFormat::NativeFormat mime = format.GetFormatId();
        GdkContentFormats* const avail = gdk_clipboard_get_formats(clipboard);
        if ( !gdk_content_formats_contain_mime_type(avail, mime) )
        {
            const wxDataFormat::NativeFormat alt =
                wxGTKGetAltWaylandFormat(mime);
            if ( alt && gdk_content_formats_contain_mime_type(avail, alt) )
                mime = alt;
        }

        wxLogTrace(TRACE_CLIPBOARD, wxT("Requesting format %s"),
                   format.GetId());

        GBytes* const bytes = ReadFormatSync(clipboard, mime);
        if ( !bytes )
            continue;

        gsize size = 0;
        gconstpointer const ptr = g_bytes_get_data(bytes, &size);

        const bool ok = data.SetData(format, size, ptr);

        g_bytes_unref(bytes);

        if ( ok )
            return true;

        wxLogTrace(TRACE_CLIPBOARD,
                   wxT("Failed to set data in the requested format"));
    }

    wxLogTrace(TRACE_CLIPBOARD, wxT("GetData(): format not found"));

    return false;
}

#else // !__WXGTK4__

// ----------------------------------------------------------------------------
// data
// ----------------------------------------------------------------------------

static GdkAtom  g_targetsAtom     = nullptr;
static GdkAtom  g_timestampAtom   = nullptr;

// This is defined in src/gtk/dataobj.cpp.
extern bool wxGTKIsSameFormat(GdkAtom atom1, GdkAtom atom2);

// Returns alternative format used under Wayland for the given format or 0.
extern GdkAtom wxGTKGetAltWaylandFormat(GdkAtom atom);

static wxString wxAtomName(GdkAtom atom)
{
    return wxString::FromAscii(wxGtkString(gdk_atom_name(atom)));
}

// the trace mask we use with wxLogTrace() - call
// wxLog::AddTraceMask(TRACE_CLIPBOARD) to enable the trace messages from here
// (there will be a *lot* of them!)
#define TRACE_CLIPBOARD wxT("clipboard")

// ----------------------------------------------------------------------------
// wxClipboardSync: used to perform clipboard operations synchronously
// ----------------------------------------------------------------------------

// constructing this object on stack will wait until the latest clipboard
// operation is finished on block exit
//
// notice that there can be no more than one such object alive at any moment,
// i.e. reentrancies are not allowed
class wxClipboardSync
{
public:
    wxClipboardSync(wxClipboard& clipboard)
    {
        wxASSERT_MSG( !ms_clipboard, wxT("reentrancy in clipboard code") );
        ms_clipboard = &clipboard;
    }

    ~wxClipboardSync()
    {
#if wxUSE_CONSOLE_EVENTLOOP
        // ensure that there is a running event loop: this might not be the
        // case if we're called before the main event loop startup
        wxEventLoopGuarantor ensureEventLoop;
#endif
        while (ms_clipboard)
            wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_CLIPBOARD);
    }

    // this method must be called by GTK+ callbacks to indicate that we got the
    // result for our clipboard operation
    static void OnDone(wxClipboard * WXUNUSED_UNLESS_DEBUG(clipboard))
    {
        wxASSERT_MSG( clipboard == ms_clipboard,
                        wxT("got notification for alien clipboard") );

        ms_clipboard = nullptr;
    }

    // this method should be called if it's possible that no async clipboard
    // operation is currently in progress (like it can be the case when the
    // clipboard is cleared but not because we asked about it), it should only
    // be called if such situation is expected -- otherwise call OnDone() which
    // would assert in this case
    static void OnDoneIfInProgress(wxClipboard *clipboard)
    {
        if ( ms_clipboard )
            OnDone(clipboard);
    }

private:
    static wxClipboard *ms_clipboard;

    wxDECLARE_NO_COPY_CLASS(wxClipboardSync);
};

wxClipboard *wxClipboardSync::ms_clipboard = nullptr;

// ============================================================================
// clipboard callbacks implementation
// ============================================================================

//-----------------------------------------------------------------------------
// "selection_received" for targets
//-----------------------------------------------------------------------------

extern "C" {
static void
targets_selection_received( GtkWidget *WXUNUSED(widget),
                            GtkSelectionData *selection_data,
                            guint32 WXUNUSED(time),
                            wxClipboard *clipboard )
{
    if ( !clipboard )
        return;

    wxON_BLOCK_EXIT1(wxClipboardSync::OnDone, clipboard);

    if (!selection_data)
        return;

    const int selection_data_length = gtk_selection_data_get_length(selection_data);
    if (selection_data_length <= 0)
        return;

    // make sure we got the data in the correct form
    GdkAtom type = gtk_selection_data_get_data_type(selection_data);
    if ( type != GDK_SELECTION_TYPE_ATOM )
    {
        if ( strcmp(wxGtkString(gdk_atom_name(type)), "TARGETS") != 0 )
        {
            wxLogTrace( TRACE_CLIPBOARD,
                        wxT("got unsupported clipboard target") );

            return;
        }
    }

    // it's not really a format, of course, but we can reuse its GetId() method
    // to format this atom as string
    wxDataFormat clip(gtk_selection_data_get_selection(selection_data));
    wxLogTrace( TRACE_CLIPBOARD,
                wxT("Received available formats for clipboard %s"),
                clip.GetId() );

    // the atoms we received, holding a list of targets (= formats)
    const GdkAtom* const atoms = reinterpret_cast<const GdkAtom*>(gtk_selection_data_get_data(selection_data));
    for (size_t i = 0; i < selection_data_length / sizeof(GdkAtom); i++)
    {
        const wxDataFormat format(atoms[i]);

        wxLogTrace(TRACE_CLIPBOARD, wxT("\t%s"), format.GetId());

        if ( clipboard->GTKOnTargetReceived(format) )
            return;
    }
}
}

bool wxClipboard::GTKOnTargetReceived(const wxDataFormat& format)
{
    if ( !wxGTKIsSameFormat(m_targetRequested, format) )
        return false;

    m_targetRequested = format;
    m_formatSupported = true;
    return true;
}

//-----------------------------------------------------------------------------
// "selection_received" for the actual data
//-----------------------------------------------------------------------------

extern "C" {
static void
selection_received( GtkWidget *WXUNUSED(widget),
                    GtkSelectionData *selection_data,
                    guint32 WXUNUSED(time),
                    wxClipboard *clipboard )
{
    if ( !clipboard )
        return;

    wxON_BLOCK_EXIT1(wxClipboardSync::OnDone, clipboard);

    if (!selection_data || gtk_selection_data_get_length(selection_data) <= 0)
        return;

    clipboard->GTKOnSelectionReceived(*selection_data);
}
}

//-----------------------------------------------------------------------------
// "selection_clear"
//-----------------------------------------------------------------------------

extern "C" {
static gint
selection_clear_clip( GtkWidget *WXUNUSED(widget), GdkEventSelection *event )
{
    wxClipboard * const clipboard = wxTheClipboard;
    if ( !clipboard )
        return TRUE;

    // notice the use of OnDoneIfInProgress() here instead of just OnDone():
    // it's perfectly possible that we're receiving this notification from GTK+
    // even though we hadn't cleared the clipboard ourselves but because
    // another application (or even another window in the same program)
    // acquired it
    wxON_BLOCK_EXIT1(wxClipboardSync::OnDoneIfInProgress, clipboard);

    wxClipboard::Kind kind;
    if (event->selection == GDK_SELECTION_PRIMARY)
    {
        wxLogTrace(TRACE_CLIPBOARD, wxT("Lost primary selection" ));

        kind = wxClipboard::Primary;
    }
    else if ( event->selection == GDK_SELECTION_CLIPBOARD )
    {
        wxLogTrace(TRACE_CLIPBOARD, wxT("Lost clipboard" ));

        kind = wxClipboard::Clipboard;
    }
    else // some other selection, we're not concerned
    {
        return FALSE;
    }

    // the clipboard is no longer in our hands, we don't need data any more
    clipboard->GTKClearData(kind);

    return TRUE;
}
}

//-----------------------------------------------------------------------------
// selection handler for supplying data
//-----------------------------------------------------------------------------

extern "C" {
static void
selection_handler( GtkWidget *WXUNUSED(widget),
                   GtkSelectionData *selection_data,
                   guint WXUNUSED(info),
                   guint WXUNUSED(time),
                   gpointer signal_data )
{
    wxClipboard * const clipboard = wxTheClipboard;
    if ( !clipboard )
        return;

    wxDataObject * const data = clipboard->GTKGetDataObject(
        gtk_selection_data_get_selection(selection_data));
    if ( !data )
        return;

    unsigned timestamp = unsigned(wxUIntPtr(signal_data));

    // ICCCM says that TIMESTAMP is a required atom.
    // In particular, it satisfies Klipper, which polls
    // TIMESTAMP to see if the clipboards content has changed.
    // It shall return the time which was used to set the data.
    if (gtk_selection_data_get_target(selection_data) == g_timestampAtom)
    {
        gtk_selection_data_set(selection_data,
                               GDK_SELECTION_TYPE_INTEGER,
                               32,
                               (guchar*)&(timestamp),
                               sizeof(timestamp));
        wxLogTrace(TRACE_CLIPBOARD,
                   wxT("Clipboard TIMESTAMP requested, returning timestamp=%u"),
                   timestamp);
        return;
    }

    wxDataFormat format(gtk_selection_data_get_target(selection_data));

    wxLogTrace(TRACE_CLIPBOARD,
               wxT("clipboard data in format %s, GtkSelectionData is target=%s type=%s selection=%s timestamp=%u"),
               format.GetId(),
               wxAtomName(gtk_selection_data_get_target(selection_data)),
               wxAtomName(gtk_selection_data_get_data_type(selection_data)),
               wxAtomName(gtk_selection_data_get_selection(selection_data)),
               timestamp
               );

    if ( !data->IsSupportedFormat( format ) )
        return;

    int size = data->GetDataSize( format );
    if ( !size )
        return;

    wxLogTrace(TRACE_CLIPBOARD, "Valid clipboard data of size %d found", size);

    wxCharBuffer buf(size - 1); // it adds 1 internally (for NUL)

    // text data must be returned in UTF8 if format is wxDF_UNICODETEXT
    if ( !data->GetDataHere(format, buf.data()) )
        return;

    // use UTF8_STRING format if requested in Unicode build but just plain
    // STRING one in ANSI or if explicitly asked in Unicode
    if (format == wxDF_UNICODETEXT)
    {
        gtk_selection_data_set_text(
            selection_data,
            (const gchar*)buf.data(),
            size );
    }
    else
    {
        gtk_selection_data_set(
            selection_data,
            format.GetFormatId(),
            8*sizeof(gchar),
            (const guchar*)buf.data(),
            size );
    }
}
}

void wxClipboard::GTKOnSelectionReceived(const GtkSelectionData& sel)
{
    wxCHECK_RET( m_receivedData, wxT("should be inside GetData()") );

    GtkSelectionData* const gsel = const_cast<GtkSelectionData*>(&sel);

    const wxDataFormat format(gtk_selection_data_get_target(gsel));
    wxLogTrace(TRACE_CLIPBOARD, wxT("Received selection %s, len=%d"),
               format.GetId(), gtk_selection_data_get_length(gsel));

    if ( !m_receivedData->IsSupportedFormat(format, wxDataObject::Set) )
        return;

    m_receivedData->SetData(format,
        gtk_selection_data_get_length(gsel),
        gtk_selection_data_get_data(gsel));
    m_formatSupported = true;
}

//-----------------------------------------------------------------------------
// asynchronous "selection_received" for targets
//-----------------------------------------------------------------------------

extern "C" {
static void
async_targets_selection_received( GtkWidget *WXUNUSED(widget),
                            GtkSelectionData *selection_data,
                            guint32 WXUNUSED(time),
                            wxClipboard *clipboard )
{
    if ( !clipboard ) // Assert?
        return;

    if (!clipboard->m_sink)
        return;

    wxClipboardEvent *event = new wxClipboardEvent(wxEVT_CLIPBOARD_CHANGED);
    event->SetEventObject( clipboard );

    int selection_data_length = 0;
    if (selection_data)
        selection_data_length = gtk_selection_data_get_length(selection_data);

    if (selection_data_length <= 0)
    {
        clipboard->m_sink->QueueEvent( event );
        clipboard->m_sink.Release();
        return;
    }

    // make sure we got the data in the correct form
    GdkAtom type = gtk_selection_data_get_data_type(selection_data);
    if ( type != GDK_SELECTION_TYPE_ATOM )
    {
        if ( strcmp(wxGtkString(gdk_atom_name(type)), "TARGETS") != 0 )
        {
            wxLogTrace( TRACE_CLIPBOARD,
                        wxT("got unsupported clipboard target") );

            clipboard->m_sink->QueueEvent( event );
            clipboard->m_sink.Release();
            return;
        }
    }

    // it's not really a format, of course, but we can reuse its GetId() method
    // to format this atom as string
    wxDataFormat clip(gtk_selection_data_get_selection(selection_data));
    wxLogTrace( TRACE_CLIPBOARD,
                wxT("Received available formats for clipboard %s"),
                clip.GetId() );

    // the atoms we received, holding a list of targets (= formats)
    const GdkAtom* const atoms = reinterpret_cast<const GdkAtom*>(gtk_selection_data_get_data(selection_data));
    for (size_t i = 0; i < selection_data_length / sizeof(GdkAtom); i++)
    {
        const wxDataFormat format(atoms[i]);

        wxLogTrace(TRACE_CLIPBOARD, wxT("\t%s"), format.GetId());

        event->AddFormat( format );
    }

    clipboard->m_sink->QueueEvent( event );
    clipboard->m_sink.Release();
}
}

// ============================================================================
// wxClipboard implementation
// ============================================================================

// ----------------------------------------------------------------------------
// wxClipboard ctor/dtor
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxClipboard,wxObject);

wxClipboard::wxClipboard()
{
    m_idSelectionGetHandler = 0;

    m_open = false;

    m_receivedData = nullptr;

    m_formatSupported = false;
    m_targetRequested = nullptr;

    // we use m_targetsWidget to query what formats are available
    m_targetsWidget = gtk_window_new( GTK_WINDOW_POPUP );
    gtk_widget_realize( m_targetsWidget );

    g_signal_connect (m_targetsWidget, "selection_received",
                      G_CALLBACK (targets_selection_received), this);

    // we use m_targetsWidgetAsync to query what formats are available asynchronously
    m_targetsWidgetAsync = gtk_window_new( GTK_WINDOW_POPUP );
    gtk_widget_realize( m_targetsWidgetAsync );

    g_signal_connect (m_targetsWidgetAsync, "selection_received",
                      G_CALLBACK (async_targets_selection_received), this);

    // we use m_clipboardWidget to get and to offer data
    m_clipboardWidget = gtk_window_new( GTK_WINDOW_POPUP );
    gtk_widget_realize( m_clipboardWidget );

    g_signal_connect (m_clipboardWidget, "selection_received",
                      G_CALLBACK (selection_received), this);

    g_signal_connect (m_clipboardWidget, "selection_clear_event",
                      G_CALLBACK (selection_clear_clip), nullptr);

    // initialize atoms we use if not done yet
    if ( !g_targetsAtom )
        g_targetsAtom = gdk_atom_intern ("TARGETS", FALSE);
    if ( !g_timestampAtom )
        g_timestampAtom = gdk_atom_intern ("TIMESTAMP", FALSE);
}

wxClipboard::~wxClipboard()
{
    Clear();

    gtk_widget_destroy( m_clipboardWidget );
    gtk_widget_destroy( m_targetsWidget );
}

// ----------------------------------------------------------------------------
// wxClipboard helper functions
// ----------------------------------------------------------------------------

GdkAtom wxClipboard::GTKGetClipboardAtom() const
{
    return m_usePrimary ? (GdkAtom)GDK_SELECTION_PRIMARY
                        : (GdkAtom)GDK_SELECTION_CLIPBOARD;
}

void wxClipboard::GTKClearData(Kind kind)
{
    Data(kind).reset();
}

bool wxClipboard::SetSelectionOwner(bool set)
{
    bool rc = gtk_selection_owner_set
              (
                set ? m_clipboardWidget : nullptr,
                GTKGetClipboardAtom(),
                (guint32)GDK_CURRENT_TIME
              ) != 0;

    if ( !rc )
    {
        wxLogTrace(TRACE_CLIPBOARD, wxT("Failed to %sset selection owner"),
                   set ? wxT("") : wxT("un"));
    }

    return rc;
}

bool wxClipboard::IsSupportedAsync(wxEvtHandler *sink)
{
    if (m_sink.get())
        return false;  // currently busy, come back later

    wxCHECK_MSG( sink, false, wxT("no sink given") );

    m_sink = sink;
    gtk_selection_convert( m_targetsWidgetAsync,
                           GTKGetClipboardAtom(),
                           g_targetsAtom,
                           (guint32) GDK_CURRENT_TIME );

    return true;
}

bool wxClipboard::DoIsSupported(const wxDataFormat& format)
{
    wxCHECK_MSG( format, false, wxT("invalid clipboard format") );

    wxLogTrace(TRACE_CLIPBOARD, wxT("Checking if format %s is available"),
               format.GetId());

    return DoGetTarget(format) != nullptr;
}

GdkAtom wxClipboard::DoGetTarget(const wxDataFormat& format)
{
    // these variables will be used by our GTKOnTargetReceived()
    m_targetRequested = format;
    m_formatSupported = false;

    // block until m_formatSupported is set from targets_selection_received
    // callback
    {
        wxClipboardSync sync(*this);

        gtk_selection_convert( m_targetsWidget,
                               GTKGetClipboardAtom(),
                               g_targetsAtom,
                               (guint32) GDK_CURRENT_TIME );
    }

    if ( !m_formatSupported )
        return nullptr;

    // This could have been changed by GTKOnTargetReceived().
    return m_targetRequested;
}

// ----------------------------------------------------------------------------
// wxClipboard public API implementation
// ----------------------------------------------------------------------------

void wxClipboard::Clear()
{
    gtk_selection_clear_targets( m_clipboardWidget, GTKGetClipboardAtom() );

    if ( gdk_selection_owner_get(GTKGetClipboardAtom()) ==
            gtk_widget_get_window(m_clipboardWidget) )
    {
        wxClipboardSync sync(*this);

        // this will result in selection_clear_clip callback being called and
        // it will free our data
        SetSelectionOwner(false);
    }
    else
    {
        // We need to free our data directly to avoid leaking memory.
        Data().reset();
    }

    m_targetRequested = nullptr;
    m_formatSupported = false;
}

bool wxClipboard::Flush()
{
    // Only store the non-primary clipboard when flushing. The primary clipboard is a scratch-space
    // formed using the currently selected text.
    if ( !m_usePrimary )
    {
        GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

        gtk_clipboard_set_can_store(clipboard, nullptr, 0);
        gtk_clipboard_store(clipboard);

        return true;
    }

    return false;
}

bool wxClipboard::Open()
{
    wxCHECK_MSG( !m_open, false, wxT("clipboard already open") );

    m_open = true;

    return true;
}

bool wxClipboard::SetData( wxDataObject *data )
{
    wxCHECK_MSG( m_open, false, wxT("clipboard not open") );

    wxCHECK_MSG( data, false, wxT("data is invalid") );

    return AddData( data );
}

bool wxClipboard::AddData( wxDataObject *data )
{
    wxCHECK_MSG( m_open, false, wxT("clipboard not open") );

    wxCHECK_MSG( data, false, wxT("data is invalid") );

    // we can only store one wxDataObject so clear the old one
    Data().reset(data);

    // get formats from wxDataObjects
    const size_t count = data->GetFormatCount();
    wxDataFormatArray formats(count);
    data->GetAllFormats(formats.get());

#ifdef GDK_WINDOWING_WAYLAND
    const bool isWayland =
        wxGTKImpl::IsWayland(gtk_widget_get_window(m_clipboardWidget));
#else // !GDK_WINDOWING_WAYLAND
    constexpr bool isWayland = false;
#endif // GDK_WINDOWING_WAYLAND/!GDK_WINDOWING_WAYLAND

    std::vector<wxString> atomNames, atomX11Names;

    // under X11, always provide TIMESTAMP as a target, see comments in
    // selection_handler for explanation
    if ( !isWayland )
        atomNames.push_back("TIMESTAMP");

    for ( size_t i = 0; i < count; i++ )
    {
        const wxDataFormat format(formats[i]);

        // Put Wayland native format first, if any.
        //
        // Note that since v3.24.25, GTK adds Wayland formats automatically
        // when putting the traditional X11 atoms corresponding text formats
        // available, so this could be skipped for the high enough versions,
        // but it seems simpler to just always do this than test GTK version
        // and this ensures consistent behaviour on all distributions.
        if ( isWayland )
        {
            if ( GdkAtom altFormat = wxGTKGetAltWaylandFormat(format) )
            {
                atomNames.push_back(wxAtomName(altFormat));

                // Still use the traditional X11 one too for compatibility but
                // put it at the end of the formats list.
                atomX11Names.push_back(wxAtomName(format));

                continue;
            }
        }

        // When not using Wayland or when the same format is used under Wayland
        // and X11, just add it to the list directly.
        atomNames.push_back(wxAtomName(format));
    }

    // Add the X11 formats at the end, if any.
    atomNames.insert(atomNames.end(), atomX11Names.begin(), atomX11Names.end());

    std::vector<GtkTargetEntry> targets(atomNames.size());
    auto target = targets.begin();
    for ( const auto& name: atomNames )
    {
        wxLogTrace(TRACE_CLIPBOARD, wxT("Adding support for %s"), name);

        target->target = const_cast<char*>((const char*)name.utf8_str());
        target->flags = 0;
        target->info = 0;
        ++target;
    }

    gtk_selection_add_targets
    (
        m_clipboardWidget,
        GTKGetClipboardAtom(),
        targets.data(),
        targets.size()
    );

    if ( !m_idSelectionGetHandler )
    {
        m_idSelectionGetHandler = g_signal_connect (
                      m_clipboardWidget, "selection_get",
                      G_CALLBACK (selection_handler),
                      GUINT_TO_POINTER (gtk_get_current_event_time()) );
    }

    // tell the world we offer clipboard data
    return SetSelectionOwner();
}

void wxClipboard::Close()
{
    wxCHECK_RET( m_open, wxT("clipboard not open") );

    m_open = false;
}

bool wxClipboard::IsOpened() const
{
    return m_open;
}

bool wxClipboard::IsSupported( const wxDataFormat& format )
{
    if ( DoIsSupported(format) )
        return true;

    // Check also plain STRING format for compatibility.
    if ( format == wxDF_UNICODETEXT && DoIsSupported(wxDF_TEXT) )
        return true;

    return false;
}

bool wxClipboard::GetData( wxDataObject& data )
{
    wxCHECK_MSG( m_open, false, wxT("clipboard not open") );

    // get all supported formats from wxDataObjects: notice that we are setting
    // the object data, so we need them in "Set" direction
    const size_t count = data.GetFormatCount(wxDataObject::Set);
    wxDataFormatArray formats(count);
    data.GetAllFormats(formats.get(), wxDataObject::Set);

    for ( size_t i = 0; i < count; i++ )
    {
        const wxDataFormat format(formats[i]);

        // Get the atom corresponding to this format.
        const GdkAtom target = DoGetTarget(format);
        if ( !target )
            continue;

        wxLogTrace(TRACE_CLIPBOARD, wxT("Requesting format %s"),
                   wxDataFormat(target).GetId());

        // these variables will be used by our GTKOnSelectionReceived()
        m_receivedData = &data;
        m_formatSupported = false;

        {
            wxClipboardSync sync(*this);

            gtk_selection_convert(m_clipboardWidget,
                                  GTKGetClipboardAtom(),
                                  target,
                                  (guint32) GDK_CURRENT_TIME );
        } // wait until we get the results

        /*
           Normally this is a true error as we checked for the presence of such
           data before, but there are applications that may return an empty
           string (e.g. Gnumeric-1.6.1 on Linux if an empty cell is copied)
           which would produce a false error message here, so we check for the
           size of the string first. With ANSI, GetDataSize returns an extra
           value (for the closing null?), with unicode, the exact number of
           tokens is given (that is more than 1 for non-ASCII characters)
           (tested with Gnumeric-1.6.1 and OpenOffice.org-2.0.2)
         */
        if ( format != wxDF_UNICODETEXT || data.GetDataSize(format) > 0 )
        {
            wxCHECK_MSG( m_formatSupported, false,
                         wxT("error retrieving data from clipboard") );
        }

        return true;
    }

    wxLogTrace(TRACE_CLIPBOARD, wxT("GetData(): format not found"));

    return false;
}

wxDataObject* wxClipboard::GTKGetDataObject( GdkAtom atom )
{
    if ( atom == GDK_NONE )
        return Data().get();

    if ( atom == GDK_SELECTION_PRIMARY )
    {
        wxLogTrace(TRACE_CLIPBOARD, wxT("Primary selection requested" ));

        return Data(wxClipboard::Primary).get();
    }
    else if ( atom == GDK_SELECTION_CLIPBOARD )
    {
        wxLogTrace(TRACE_CLIPBOARD, wxT("Clipboard data requested" ));

        return Data(wxClipboard::Clipboard).get();
    }
    else // some other selection, we're not concerned
    {
        return nullptr;
    }
}

#endif // __WXGTK4__/!__WXGTK4__

#endif // wxUSE_CLIPBOARD
