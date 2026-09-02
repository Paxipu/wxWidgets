///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/dialogasync.h
// Purpose:     Run a GTK4 dialog controller from wxDialog::ShowModal()
// Author:      wxWidgets team
// Copyright:   (c) 2026 wxWidgets team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_PRIVATE_DIALOGASYNC_H_
#define _WX_GTK_PRIVATE_DIALOGASYNC_H_

#ifdef __WXGTK4__

#include "wx/evtloop.h"
#include "wx/log.h"

// GTK 4.10 deprecated the dialog *widgets* and replaced them with dialog
// *controllers*: GtkColorDialog, GtkFontDialog, GtkAlertDialog and
// GtkFileDialog are not widgets, cannot be shown, and only offer to choose
// something -- asynchronously, through a GAsyncReadyCallback.
//
// wxDialog::ShowModal() has to block and return a code instead, so the caller
// starts the asynchronous choose and then calls Run(), which spins a nested
// event loop until the callback calls Finish().
//
// That the callback still reaches a loop started *after* the asynchronous call
// was made is not obvious and is not assumed: it is measured in
// docs/gtk/probes/gtk4-dialog-controller-async.c.
class wxGTKDialogAsyncResult
{
public:
    wxGTKDialogAsyncResult() = default;

    // Wait for the callback, and return the code it passed to Finish().
    int Run()
    {
        // Finish() before Run() should not happen, since the controllers are
        // documented to call back from the main loop, but a controller that
        // failed its arguments outright could report so at once.
        if ( !m_done )
        {
            wxGUIEventLoop loop;
            m_loop = &loop;
            loop.Run();
            m_loop = nullptr;
        }

        return m_rc;
    }

    // Called from the GAsyncReadyCallback with the wx return code.
    void Finish(int rc)
    {
        m_rc = rc;
        m_done = true;

        if ( m_loop )
            m_loop->ScheduleExit();
    }

    // Turn the GError a gtk_*_dialog_*_finish() reported into a wx code.
    //
    // Only GTK_DIALOG_ERROR_FAILED is a failure worth mentioning:
    // GTK_DIALOG_ERROR_DISMISSED means the user closed the dialog and
    // GTK_DIALOG_ERROR_CANCELLED means the program cancelled the call, and
    // both of those are an ordinary wxID_CANCEL.
    //
    // Note that the obvious guess, G_IO_ERROR_CANCELLED, matches none of them:
    // GTK4 reports these in its own GTK_DIALOG_ERROR domain, so checking for
    // the G_IO_ERROR one would make every dismissal look like a failure.
    static int GetCodeForError(GError* error, const char* what)
    {
        if ( error &&
                !g_error_matches(error, GTK_DIALOG_ERROR,
                                 GTK_DIALOG_ERROR_DISMISSED) &&
                !g_error_matches(error, GTK_DIALOG_ERROR,
                                 GTK_DIALOG_ERROR_CANCELLED) )
        {
            wxLogDebug("%s failed: %s", what, error->message);
        }

        return wxID_CANCEL;
    }

private:
    wxGUIEventLoop* m_loop = nullptr;
    int m_rc = wxID_CANCEL;
    bool m_done = false;

    wxDECLARE_NO_COPY_CLASS(wxGTKDialogAsyncResult);
};

#endif // __WXGTK4__

#endif // _WX_GTK_PRIVATE_DIALOGASYNC_H_
