/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/dialog.cpp
// Purpose:
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/dialog.h"

#ifndef WX_PRECOMP
#endif // WX_PRECOMP

#include "wx/evtloop.h"

#include "wx/scopedptr.h"
#include "wx/modalhook.h"

#include "wx/gtk/private/wrapgtk.h"
#include "wx/gtk/private/gtk3-compat.h"

wxDEFINE_TIED_SCOPED_PTR_TYPE(wxGUIEventLoop)


//-----------------------------------------------------------------------------
// wxDialog
//-----------------------------------------------------------------------------

void wxDialog::Init()
{
    m_modalLoop = nullptr;
    m_modalShowing = false;
}

wxDialog::wxDialog( wxWindow *parent,
                    wxWindowID id, const wxString &title,
                    const wxPoint &pos, const wxSize &size,
                    long style, const wxString &name )
{
    Init();

    (void)Create( parent, id, title, pos, size, style, name );
}

bool wxDialog::Create( wxWindow *parent,
                       wxWindowID id, const wxString &title,
                       const wxPoint &pos, const wxSize &size,
                       long style, const wxString &name )
{
    SetExtraStyle(GetExtraStyle() | wxTOPLEVEL_EX_DIALOG);

    // all dialogs should have tab traversal enabled
    style |= wxTAB_TRAVERSAL;

    return wxTopLevelWindow::Create(parent, id, title, pos, size, style, name);
}

bool wxDialog::Show( bool show )
{
    if (!show && IsModal())
    {
        EndModal( wxID_CANCEL );
    }

    if (show && CanDoLayoutAdaptation())
        DoLayoutAdaptation();

    bool ret = wxDialogBase::Show(show);

    if (show)
        InitDialog();

    return ret;
}

wxDialog::~wxDialog()
{
    // if the dialog is modal, this will end its event loop
    if ( IsModal() )
        EndModal(wxID_CANCEL);
}

bool wxDialog::IsModal() const
{
    return m_modalShowing;
}

// Workaround for Ubuntu overlay scrollbar, which adds our GtkWindow to a
// private window group in a GtkScrollbar realize handler. This breaks the grab
// done by gtk_window_set_modal(), and allows menus and toolbars in the parent
// frame to remain active. So, we install an emission hook on the "realize"
// signal while showing a modal dialog. For any realize on a GtkScrollbar,
// we check the top level parent to see if it has an explicitly set window
// group that is not the same as its transient parent. If we find this, we
// put the top level back in the same window group as its transient parent, and
// re-add the grab.
// Ubuntu 12.04 and 12.10 are known to have this problem.

// need 2.10 for gtk_window_get_group()
#if GTK_CHECK_VERSION(2,10,0)
extern "C" {
static gboolean
realize_hook(GSignalInvocationHint*, unsigned, const GValue* param_values, void*)
{
    void* p = g_value_peek_pointer(param_values);
    if (GTK_IS_SCROLLBAR(p))
    {
        GtkWindow* toplevel = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(p)));
        GtkWindow* transient_parent = gtk_window_get_transient_for(toplevel);
        if (transient_parent && gtk_window_has_group(toplevel))
        {
            GtkWindowGroup* group = gtk_window_get_group(toplevel);
            GtkWindowGroup* group_parent = gtk_window_get_group(transient_parent);
            if (group != group_parent)
            {
                gtk_window_group_add_window(group_parent, toplevel);
#ifndef __WXGTK4__
                // gtk_grab_add() is gone with the rest of GTK4's explicit
                // grabs. This whole hook works around a GTK bug in which a
                // scrollbar's grab escaped its window group (seen on Ubuntu
                // 12.04/12.10), so it is very unlikely to be needed against
                // GTK4 at all -- and there is no way to express it there.
                gtk_grab_add(GTK_WIDGET(toplevel));
#endif
            }
        }
    }
    return true;
}
}
#endif // GTK 2.10

#ifdef __WXGTK4__

extern "C" {

// GTK4 offers one of two things for "close-request", never both: return TRUE
// and the window survives, but GtkDialog does not emit "response"; return
// FALSE and it responds, but the window is destroyed. GTK3's "delete-event"
// did both, and wx wanted both -- the dialog is a wxWindow its owner still
// holds, so it must not be destroyed, yet something has to end the modal loop.
//
// Ending it here restores that. Close() is what wxTopLevelWindowGTK's own
// close-request handler does, and for a modal dialog it comes down to
// EndModal(wxID_CANCEL). That handler is however not connected on the native
// dialogs -- colour, font, file, directory -- which build their m_widget
// themselves instead of going through wxTopLevelWindow::Create(), and those
// are exactly the ones that reach this handler: for any other dialog the
// wxTLW one runs first and stops the emission.
static gboolean wx_gtk_dialog_close_request(GtkWindow*, wxDialog* dialog)
{
    dialog->Close();

    return TRUE;
}

}

#endif // __WXGTK4__

int wxDialog::ShowModal()
{
    WX_HOOK_MODAL_DIALOG();

    wxASSERT_MSG( !IsModal(), "ShowModal() can't be called twice" );

    // release the mouse if it's currently captured as the window having it
    // will be disabled when this dialog is shown -- but will still keep the
    // capture making it impossible to do anything in the modal dialog itself
    GTKReleaseMouseAndNotify();

    wxWindow * const parent = GetParentForModalDialog();
    if ( parent )
    {
        gtk_window_set_transient_for( GTK_WINDOW(m_widget),
                                      GTK_WINDOW(parent->m_widget) );
    }

#if GTK_CHECK_VERSION(2,10,0)
    unsigned sigId = 0;
    gulong hookId = 0;
    // Ubuntu overlay scrollbar uses at least GTK 2.24
    if (wx_is_at_least_gtk2(24))
    {
        sigId = g_signal_lookup("realize", GTK_TYPE_WIDGET);
        hookId = g_signal_add_emission_hook(sigId, 0, realize_hook, nullptr, nullptr);
    }
#endif

    // NOTE: this will cause a gtk_grab_add() during Show()
    gtk_window_set_modal(GTK_WINDOW(m_widget), true);

    m_modalShowing = true;

    Show( true );

    // Prevent the widget from being destroyed if the user closes the window.
    // Needed for derived classes which bypass wxTLW::Create(), and therefore
    // the wxTLW "delete-event" handler is not connected
#ifdef __WXGTK4__
    // "delete-event" became "close-request", which also has to end the modal
    // loop here: see the comment on the handler above.
    gulong handler_id = g_signal_connect(
        m_widget, "close-request", G_CALLBACK(wx_gtk_dialog_close_request), this);
#else
    gulong handler_id = g_signal_connect(
        m_widget, "delete-event", G_CALLBACK(gtk_true), this);
#endif

    // Run modal dialog event loop.
    {
        wxGUIEventLoopTiedPtr modal(&m_modalLoop, new wxGUIEventLoop());
        m_modalLoop->Run();
    }

    g_signal_handler_disconnect(m_widget, handler_id);
#if GTK_CHECK_VERSION(2,10,0)
    if (sigId)
        g_signal_remove_emission_hook(sigId, hookId);
#endif

    gtk_window_set_modal(GTK_WINDOW(m_widget), FALSE);

    return GetReturnCode();
}

void wxDialog::EndModal( int retCode )
{
    SetReturnCode( retCode );

    if (!IsModal())
    {
        wxFAIL_MSG( "either wxDialog:EndModal called twice or ShowModal wasn't called" );
        return;
    }

    m_modalShowing = false;

    // Ensure Exit() is only called once. The dialog's event loop may be terminated
    // externally due to an uncaught exception.
    if (m_modalLoop && m_modalLoop->IsRunning())
        m_modalLoop->Exit();

    Show( false );
}
