/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/popupwin.cpp
// Purpose:
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_POPUPWIN

#include "wx/popupwin.h"

#ifndef WX_PRECOMP
#endif // WX_PRECOMP

#include "wx/gtk/private/wrapgtk.h"
#include "wx/gtk/private/gtk3-compat.h"

#include "wx/gtk/private/win_gtk.h"

//-----------------------------------------------------------------------------
// "button_press"
//-----------------------------------------------------------------------------

// GTK4 has no way to look up which widget an event was aimed at
// (gtk_get_event_widget() went with the rest of the GdkEvent API) and no
// GTK_WINDOW_POPUP to grab for in the first place, so this dismissal
// short-cut is GTK3-only. Under GTK4 a wxPopupTransientWindow is dismissed by
// the generic code in src/common/popupcmn.cpp instead, which does the same job
// through wx's own mouse capture rather than through a GTK grab.
#ifndef __WXGTK4__

extern "C" {
static gint gtk_popup_button_press (GtkWidget *widget, GdkEvent *gdk_event, wxPopupWindow* win )
{
    GtkWidget *child = gtk_get_event_widget (gdk_event);

    /* Ignore events sent out before we connected to the signal */
    if (win->m_time >= ((GdkEventButton*)gdk_event)->time)
        return FALSE;

    /*  We don't ask for button press events on the grab widget, so
     *  if an event is reported directly to the grab widget, it must
     *  be on a window outside the application (and thus we remove
     *  the popup window). Otherwise, we check if the widget is a child
     *  of the grab widget, and only remove the popup window if it
     *  is not. */
    if (child != widget)
    {
        while (child)
        {
            if (child == widget)
                return FALSE;
            child = gtk_widget_get_parent(child);
        }
    }

    wxFocusEvent event( wxEVT_KILL_FOCUS, win->GetId() );
    event.SetEventObject( win );

    (void)win->HandleWindowEvent( event );

    return TRUE;
}
}

//-----------------------------------------------------------------------------
// "delete_event"
//-----------------------------------------------------------------------------

extern "C" {
static
bool gtk_dialog_delete_callback( GtkWidget *WXUNUSED(widget), GdkEvent *WXUNUSED(event), wxPopupWindow *win )
{
    if (win->IsEnabled())
        win->Close();

    return TRUE;
}
}

#endif // !__WXGTK4__

#ifdef __WXGTK4__

extern "C" {

static gboolean wx_popup_place_again(void* data)
{
    wxPopupWindow* const win = static_cast<wxPopupWindow*>(data);

    win->m_placeAgainIdle = 0;
    win->GTKUpdatePointingTo();

    return G_SOURCE_REMOVE;
}

static void wx_popup_mapped(GtkWidget*, wxPopupWindow* win)
{
    // The inset is only measurable once the popover has been laid out, which
    // has not happened yet even here -- measured, "map" still reports 0 and
    // the following idle reports 11. So place it again from there.
    if ( !win->m_placeAgainIdle )
    {
        // Ahead of the redraw, or the popover would be painted where it was
        // first put and then visibly hop into place.
        win->m_placeAgainIdle = g_idle_add_full(GDK_PRIORITY_REDRAW - 1,
                                                wx_popup_place_again,
                                                win, nullptr);
    }
}

}

#endif // __WXGTK4__

//-----------------------------------------------------------------------------
// wxPopupWindow
//-----------------------------------------------------------------------------

#ifdef __WXUNIVERSAL__
wxBEGIN_EVENT_TABLE(wxPopupWindow,wxPopupWindowBase)
    EVT_SIZE(wxPopupWindow::OnSize)
wxEND_EVENT_TABLE()
#endif

wxPopupWindow::~wxPopupWindow()
{
#ifdef __WXGTK4__
    if ( m_placeAgainIdle )
    {
        // Don't let it fire with a pointer which is about to dangle.
        g_source_remove(m_placeAgainIdle);
    }
#endif // __WXGTK4__

}

bool wxPopupWindow::Create( wxWindow *parent, int style )
{
    if (!PreCreation( parent, wxDefaultPosition, wxDefaultSize ) ||
        !CreateBase( parent, -1, wxDefaultPosition, wxDefaultSize, style, wxDefaultValidator, wxT("popup") ))
    {
        wxFAIL_MSG( wxT("wxPopupWindow creation failed") );
        return false;
    }

    // Unlike windows, top level windows are created hidden by default.
    m_isShown = false;

    // All dialogs should really have this style
    m_windowStyle |= wxTAB_TRAVERSAL;

#ifdef __WXGTK4__
    // GTK4 removed GTK_WINDOW_POPUP, and with it every way of creating a
    // toplevel that the application, rather than the window manager, decides
    // the position of: gtk_window_move() is gone too. The one widget which
    // still gets a surface of its own that may extend past the edges of its
    // toplevel is GtkPopover, so that is what a wxPopupWindow is made of now.
    //
    // A popover is placed by "pointing at" a rectangle in its parent's
    // coordinate space, which DoSetSize() below converts wx's screen
    // coordinates into.
    //
    // The consequence is that, unlike under GTK3, a wxPopupWindow must have a
    // parent: there is nothing to attach a parentless popover to.
    wxCHECK_MSG( parent, false,
                 "wxPopupWindow must have a parent when using GTK4" );

    m_widget = gtk_popover_new();
    g_object_ref( m_widget );

    gtk_widget_set_name( m_widget, "wxPopupWindow" );

    gtk_popover_set_has_arrow( GTK_POPOVER(m_widget), FALSE );

    // wx popups are placed and dismissed by wx itself; an autohiding popover
    // would take a grab and close itself behind wx's back.
    gtk_popover_set_autohide( GTK_POPOVER(m_widget), FALSE );

    // With a zero-height pointing rectangle this puts the popover's top edge
    // at the requested y, see DoSetSize().
    gtk_popover_set_position( GTK_POPOVER(m_widget), GTK_POS_BOTTOM );

    gtk_widget_set_parent( m_widget,
                           parent->m_wxwindow ? parent->m_wxwindow
                                              : parent->m_widget );

    m_wxwindow = wxPizza::New();
    gtk_popover_set_child( GTK_POPOVER(m_widget), m_wxwindow );

    g_signal_connect( m_widget, "map", G_CALLBACK(wx_popup_mapped), this );
#else // !__WXGTK4__
    m_widget = gtk_window_new( GTK_WINDOW_POPUP );
    g_object_ref( m_widget );

    gtk_widget_set_name( m_widget, "wxPopupWindow" );

    // While wxPopupWindow is used for different windows as well, we don't
    // really know how is it going to be used but we do know that without the
    // hint at all, it doesn't work correctly, at least under Wayland, where
    // GTK only maps COMBO and {DROPDOWN,POPUP}_MENU to popups, so do set it.
    gtk_window_set_type_hint( GTK_WINDOW(m_widget), GDK_WINDOW_TYPE_HINT_COMBO );

    // Popup windows can be created without parent, so handle this correctly.
    if (parent)
    {
        GtkWidget *toplevel = gtk_widget_get_toplevel( parent->m_widget );
        if (GTK_IS_WINDOW (toplevel))
            gtk_window_set_transient_for (GTK_WINDOW (m_widget), GTK_WINDOW (toplevel));
    }

    gtk_window_set_resizable (GTK_WINDOW (m_widget), FALSE);

    g_signal_connect (m_widget, "delete_event",
                      G_CALLBACK (gtk_dialog_delete_callback), this);

    m_wxwindow = wxPizza::New();
    gtk_widget_show( m_wxwindow );

    gtk_container_add( GTK_CONTAINER(m_widget), m_wxwindow );
#endif // __WXGTK4__/!__WXGTK4__

    if (m_parent) m_parent->AddChild( this );

    PostCreation();

#ifndef __WXGTK4__
    m_time = gtk_get_current_event_time();

    g_signal_connect (m_widget, "button_press_event",
                      G_CALLBACK (gtk_popup_button_press), this);
#endif // !__WXGTK4__

    return true;
}

void wxPopupWindow::DoMoveWindow(int WXUNUSED(x), int WXUNUSED(y), int WXUNUSED(width), int WXUNUSED(height) )
{
    wxFAIL_MSG( wxT("DoMoveWindow called for wxPopupWindow") );
}

void wxPopupWindow::DoSetSize( int x, int y, int width, int height, int sizeFlags )
{
    wxASSERT_MSG( (m_widget != nullptr), wxT("invalid dialog") );
    wxASSERT_MSG( (m_wxwindow != nullptr), wxT("invalid dialog") );

    int old_x = m_x;
    int old_y = m_y;

    int old_width = m_width;
    int old_height = m_height;

    if (x != -1 || (sizeFlags & wxSIZE_ALLOW_MINUS_ONE))
        m_x = x;
    if (y != -1 || (sizeFlags & wxSIZE_ALLOW_MINUS_ONE))
        m_y = y;
    if (width != -1)
        m_width = width;
    if (height != -1)
        m_height = height;

    ConstrainSize();

#ifdef __WXGTK4__
    // A popover has no position of its own: it is placed relative to a
    // rectangle given in its parent's coordinates, so both a move and a resize
    // have to go through the same call.
    if (m_x != old_x || m_y != old_y ||
            m_width != old_width || m_height != old_height)
    {
        // A GtkPopover adds its own padding around its child, so requesting
        // the popup size on the popover itself would make the wx client area
        // smaller than requested and clip controls at its bottom/right edges.
        gtk_widget_set_size_request( m_wxwindow, m_width, m_height );
        GTKUpdatePointingTo();
    }

    if (m_x != old_x || m_y != old_y)
    {
        wxMoveEvent event(wxPoint(m_x, m_y), GetId());
        event.SetEventObject(this);
        HandleWindowEvent(event);
    }

    if ((m_width != old_width) || (m_height != old_height))
    {
        wxSizeEvent event(GetSize(), GetId());
        event.SetEventObject(this);
        HandleWindowEvent(event);
    }
#else // !__WXGTK4__
    if (m_x != old_x || m_y != old_y)
    {
        gtk_window_move(GTK_WINDOW(m_widget), m_x, m_y);
        wxMoveEvent event(wxPoint(m_x, m_y), GetId());
        event.SetEventObject(this);
        HandleWindowEvent(event);
    }

    if ((m_width != old_width) || (m_height != old_height))
    {
        // gtk_window_resize does not work for GTK_WINDOW_POPUP
        gtk_widget_set_size_request( m_widget, m_width, m_height );
        wxSizeEvent event(GetSize(), GetId());
        event.SetEventObject(this);
        HandleWindowEvent(event);
    }
#endif // __WXGTK4__/!__WXGTK4__
}

#ifdef __WXGTK4__

int wxPopupWindow::GTKGetContentTopInset()
{
    // Once there is an allocation the exact figure can be read off it.
    graphene_rect_t bounds;
    if ( m_wxwindow &&
            gtk_widget_compute_bounds(m_wxwindow, m_widget, &bounds) &&
            bounds.origin.y > 0 )
    {
        m_contentTopInset = int(bounds.origin.y);
        return m_contentTopInset;
    }

    if ( m_contentTopInset )
        return m_contentTopInset;

    // There is none yet, and waiting for one is not good enough: a popup shown
    // and clicked within the same burst of events would be placed wrongly for
    // exactly as long as it mattered. What can be measured without an
    // allocation is how much taller the popover is than its child, i.e. both
    // insets together; splitting that evenly is off by a pixel or so against
    // the measured 11 above and 13 below, which is close enough to be clicked,
    // and the exact figure replaces it as soon as there is an allocation.
    int popoverHeight = 0, childHeight = 0;
    gtk_widget_measure(m_widget, GTK_ORIENTATION_VERTICAL, m_width,
                       &popoverHeight, nullptr, nullptr, nullptr);
    gtk_widget_measure(m_wxwindow, GTK_ORIENTATION_VERTICAL, m_width,
                       &childHeight, nullptr, nullptr, nullptr);

    const int both = popoverHeight - childHeight;

    return both > 0 ? both / 2 : 0;
}

void wxPopupWindow::GTKUpdatePointingTo()
{
    wxWindow* const parent = GetParent();
    if ( !parent )
        return;

    // m_x/m_y are in screen coordinates, the pointing rectangle is in the
    // parent's client coordinates.
    const wxPoint pos = parent->ScreenToClient(wxPoint(m_x, m_y));

    // The popover centers itself horizontally on this rectangle and, with
    // GTK_POS_BOTTOM, puts its top edge at the rectangle's bottom. Making the
    // rectangle exactly as wide as the popup and giving it no height therefore
    // lands the popup's top-left corner where it was asked to go -- and, since
    // the popover is wider than its content by its own padding on each side,
    // using the content width here cancels that padding out rather than
    // needing it to be measured. See docs/gtk/probes/gtk4-popover-placement.c.
    // Vertically nothing cancels out: the top edge which lands on the
    // rectangle is the popover's own, and its content starts below that by
    // however much the popover keeps for itself. Measured on plain GTK, a
    // 120x60 child gives a 144x84 popover with the child at (12, 11) -- so
    // without this the content appears eleven pixels below where it was asked
    // for, which is enough for a click aimed at the first row of an
    // autocompletion list to land above it and select nothing.
    GdkRectangle rect;
    rect.x = pos.x;
    rect.y = pos.y - GTKGetContentTopInset();
    rect.width = m_width;
    rect.height = 0;

    gtk_popover_set_pointing_to( GTK_POPOVER(m_widget), &rect );
}

#endif // __WXGTK4__

void wxPopupWindow::SetFocus()
{
    // set the focus to the first child who wants it
    wxWindowList::compatibility_iterator node = GetChildren().GetFirst();
    while ( node )
    {
        wxWindow *child = node->GetData();
        node = node->GetNext();

        if ( child->CanAcceptFocus() && !child->IsTopLevel() )
        {
            child->SetFocus();
            return;
        }
    }

    wxPopupWindowBase::SetFocus();
}

bool wxPopupWindow::Show( bool show )
{
    if (show && !IsShown())
    {
        wxSizeEvent event(GetSize(), GetId());
        event.SetEventObject(this);
        HandleWindowEvent(event);
    }

    bool ret = wxWindow::Show( show );

    return ret;
}

#endif // wxUSE_POPUPWIN
