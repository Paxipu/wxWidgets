/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/dirdlg.cpp
// Purpose:     native implementation of wxDirDialog
// Author:      Robert Roebling, Zbigniew Zagorski, Mart Raudsepp, Francesco Montorsi
// Copyright:   (c) 1998 Robert Roebling, 2004 Zbigniew Zagorski, 2005 Mart Raudsepp
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"



/*
  NOTE: the GtkFileChooser interface can be used both for wxFileDialog and for wxDirDialog.
        Thus following code is very similar (even if not identic) to src/gtk/filedlg.cpp
        If you find a problem in this code, remember to check also that file !
*/



// Nothing here under GTK4: wxDirDialog is the generic one there, because
// GTK4's GtkFileDialog does not reliably open at all -- see wx/dirdlg.h and
// docs/gtk/probes/gtk4-filedialog-portal-hang.c. The file still builds the
// native dialog for GTK+ 2 and GTK+ 3.
#if wxUSE_DIRDLG && !defined(__WXGTK4__)

#include "wx/dirdlg.h"

#ifndef WX_PRECOMP
    #include "wx/intl.h"
    #include "wx/filedlg.h"
#endif

#include "wx/modalhook.h"
#include "wx/gtk/private.h"
#include "wx/gtk/private/mnemonics.h"
#include "wx/gtk/private/object.h"
#include "wx/gtk/private/string.h"
#include "wx/gtk/private/gtk3-compat.h"
#include "wx/stockitem.h"

#ifdef __WXGTK4__
    #include "wx/gtk/private/dialogasync.h"
#endif

#ifdef __WXGTK4__

namespace
{

// Lives on wxDirDialog::ShowModal()'s stack, which outlives the asynchronous
// call because that function blocks until the callback has finished with it.
struct wxDirChooseContext
{
    wxDirDialog* dialog;
    wxGTKDialogAsyncResult* result;
    bool multiple;
};

// One selected folder, as a path, or an empty string if it has none -- a
// GFile can name something that is not on this machine.
wxString wxGTKPathFromFile(GFile* file)
{
    wxGtkString path(g_file_get_path(file));
    return path ? wxString::FromUTF8(path) : wxString();
}

} // anonymous namespace

extern "C" {

static void
wxgtk_dir_chosen(GObject* source, GAsyncResult* res, gpointer user_data)
{
    wxDirChooseContext* const ctx = static_cast<wxDirChooseContext*>(user_data);
    GtkFileDialog* const dialog = GTK_FILE_DIALOG(source);

    GError* error = nullptr;
    wxArrayString paths;

    if ( ctx->multiple )
    {
        wxGtkObject<GListModel> files(
            gtk_file_dialog_select_multiple_folders_finish(dialog, res, &error));
        if ( files )
        {
            const guint n = g_list_model_get_n_items(files);
            for ( guint i = 0; i < n; i++ )
            {
                wxGtkObject<GFile> file(
                    static_cast<GFile*>(g_list_model_get_item(files, i)));
                const wxString path = wxGTKPathFromFile(file);
                if ( !path.empty() )
                    paths.Add(path);
            }
        }
    }
    else
    {
        wxGtkObject<GFile> file(
            gtk_file_dialog_select_folder_finish(dialog, res, &error));
        if ( file )
        {
            const wxString path = wxGTKPathFromFile(file);
            if ( !path.empty() )
                paths.Add(path);
        }
    }

    int rc;
    if ( !paths.empty() )
    {
        ctx->dialog->GTKSetChosenPaths(paths);
        rc = wxID_OK;
    }
    else
    {
        rc = wxGTKDialogAsyncResult::GetCodeForError(error, "Choosing a folder");
    }

    g_clear_error(&error);

    ctx->result->Finish(rc);
}
}

#else // !__WXGTK4__

extern "C" {
static void gtk_dirdialog_response_callback(GtkWidget * WXUNUSED(w),
                                             gint response,
                                             wxDirDialog *dialog)
{
    if (response == GTK_RESPONSE_ACCEPT)
        dialog->GTKOnAccept();
    else // GTK_RESPONSE_CANCEL or GTK_RESPONSE_NONE
        dialog->GTKOnCancel();
}

#if GTK_CHECK_VERSION(3,20,0)
static void wx_dirdialog_show(GtkWidget*, wxDirDialog* win)
{
    // If m_widget is shown, then GtkFileChooserNative is not being used.
    // This happens when using wxDirPickerCtrl, for example.
    win->GTKDropNative();
}
#endif
}

#endif // __WXGTK4__/!__WXGTK4__

//-----------------------------------------------------------------------------
// wxDirDialog
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxDirDialog, wxDialog);

wxDirDialog::wxDirDialog(wxWindow* parent,
                         const wxString& title,
                         const wxString& defaultPath,
                         long style,
                         const wxPoint& pos,
                         const wxSize& WXUNUSED(sz),
                         const wxString& WXUNUSED(name))
{
    Create(parent, title, defaultPath, style, pos);
}

bool wxDirDialog::Create(wxWindow* parent,
                         const wxString& title,
                         const wxString& defaultPath,
                         long style,
                         const wxPoint& pos,
                         const wxSize& WXUNUSED(sz),
                         const wxString& WXUNUSED(name))
{
    m_message = title;

    wxASSERT_MSG( !( (style & wxDD_MULTIPLE) && (style & wxDD_CHANGE_DIR) ),
                  "wxDD_CHANGE_DIR can't be used together with wxDD_MULTIPLE" );

    parent = GetParentForModalDialog(parent, style);

    if (!PreCreation(parent, pos, wxDefaultSize) ||
        !CreateBase(parent, wxID_ANY, pos, wxDefaultSize, style,
                wxDefaultValidator, wxT("dirdialog")))
    {
        wxFAIL_MSG( wxT("wxDirDialog creation failed") );
        return false;
    }

    GtkWindow* gtk_parent = nullptr;
    if (parent)
        gtk_parent = GTK_WINDOW( gtk_widget_get_toplevel(parent->m_widget) );

#ifdef __WXGTK4__
    // GtkFileChooserDialog is deprecated since GTK 4.10 and its replacement,
    // GtkFileDialog, is a controller object rather than a widget: there is
    // nothing here that could be m_widget, and nothing exists between
    // ShowModal() calls. A plain window is created instead so that the
    // inherited wxWindow calls keep working on something; it is never shown.
    wxUnusedVar(gtk_parent);
    m_widget = gtk_window_new();
    g_object_ref_sink(m_widget);
    gtk_window_set_title(GTK_WINDOW(m_widget), m_message.utf8_str());

    if ( !defaultPath.empty() )
        SetPath(defaultPath);

    return true;
#else // !__WXGTK4__

    m_widget = gtk_file_chooser_dialog_new(
                   m_message.utf8_str(),
                   gtk_parent,
                   GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
#ifdef __WXGTK4__
                   static_cast<const char*>(wxConvertMnemonicsToGTK(wxGetStockLabel(wxID_CANCEL)).utf8_str()),
#else
                   "gtk-cancel",
#endif
                   GTK_RESPONSE_CANCEL,
#ifdef __WXGTK4__
                   static_cast<const char*>(wxConvertMnemonicsToGTK(wxGetStockLabel(wxID_OPEN)).utf8_str()),
#else
                   "gtk-open",
#endif
                   GTK_RESPONSE_ACCEPT,
                   nullptr);

    g_object_ref(m_widget);

#if GTK_CHECK_VERSION(3,20,0)
    if (wx_is_at_least_gtk3(20))
    {
        m_fileChooser = GTK_FILE_CHOOSER(gtk_file_chooser_native_new(
            m_message.utf8_str(),
            gtk_parent,
            GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            nullptr, nullptr));
        g_signal_connect(m_widget, "show", G_CALLBACK(wx_dirdialog_show), this);
    }
    else
#endif
    {
        m_fileChooser = GTK_FILE_CHOOSER(m_widget);
        g_object_ref(m_fileChooser);
    }

    gtk_dialog_set_default_response(GTK_DIALOG(m_widget), GTK_RESPONSE_ACCEPT);
#if GTK_CHECK_VERSION(2,18,0)
    if (wx_is_at_least_gtk2(18))
    {
        gtk_file_chooser_set_create_folders(
            m_fileChooser, !HasFlag(wxDD_DIR_MUST_EXIST));
        gtk_file_chooser_set_create_folders(
            (GtkFileChooser*)m_widget, !HasFlag(wxDD_DIR_MUST_EXIST));
    }
#endif

    // Enable multiple selection if desired
    gtk_file_chooser_set_select_multiple(m_fileChooser, HasFlag(wxDD_MULTIPLE));
    gtk_file_chooser_set_select_multiple((GtkFileChooser*)m_widget, HasFlag(wxDD_MULTIPLE));

#ifndef __WXGTK4__
    // Enable show hidden folders if desired.
    //
    // GTK4 removed gtk_file_chooser_set_show_hidden(): whether hidden files
    // are listed is the user's choice there (Ctrl+H, or the chooser's own
    // menu) and an application can no longer force it, so wxDD_SHOW_HIDDEN
    // has no effect.
    gtk_file_chooser_set_show_hidden(m_fileChooser, HasFlag(wxDD_SHOW_HIDDEN));
    gtk_file_chooser_set_show_hidden((GtkFileChooser*)m_widget, HasFlag(wxDD_SHOW_HIDDEN));
#endif

    // local-only property could be set to false to allow non-local files to be loaded.
    // In that case get/set_uri(s) should be used instead of get/set_filename(s) everywhere
    // and the GtkFileChooserDialog should probably also be created with a backend,
    // e.g. "gnome-vfs", "default", ... (gtk_file_chooser_dialog_new_with_backend).
    // Currently local-only is kept as the default - true:
    // gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(m_widget), true);

    g_signal_connect (m_widget, "response",
        G_CALLBACK (gtk_dirdialog_response_callback), this);

    if ( !defaultPath.empty() )
        SetPath(defaultPath);

    return true;
#endif // __WXGTK4__/!__WXGTK4__
}

wxDirDialog::~wxDirDialog()
{
#ifndef __WXGTK4__
    g_object_unref(m_fileChooser);
#endif
}

#ifdef __WXGTK4__

void wxDirDialog::GTKSetChosenPaths(const wxArrayString& paths)
{
    m_paths = paths;
    GTKFinishAccept();
}

#endif // __WXGTK4__

// The part of accepting that is not about getting the paths out of GTK: it is
// the same whichever way they arrived.
void wxDirDialog::GTKFinishAccept()
{
    if ( m_paths.empty() )
        return;

    // change to the directory where the user went if asked
    if (HasFlag(wxDD_CHANGE_DIR))
    {
        wxSetWorkingDirectory(m_paths.Last());
    }

    if (!HasFlag(wxDD_MULTIPLE))
    {
        m_path = m_paths.Last();
    }
}

#ifndef __WXGTK4__

void wxDirDialog::GTKAccept()
{
    GSList *fnamesi = gtk_file_chooser_get_filenames(m_fileChooser);
    GSList *fnames = fnamesi;

    while ( fnamesi )
    {
        wxString dir(wxString::FromUTF8(static_cast<gchar *>(fnamesi->data)));
        m_paths.Add(dir);

        g_free(fnamesi->data);
        fnamesi = fnamesi->next;
    }

    g_slist_free(fnames);

    GTKFinishAccept();
}

void wxDirDialog::GTKOnAccept()
{
    GTKAccept();
    EndDialog(wxID_OK);
}

void wxDirDialog::GTKOnCancel()
{
    EndDialog(wxID_CANCEL);
}

void wxDirDialog::GTKDropNative()
{
    if (m_fileChooser != (GtkFileChooser*)m_widget)
    {
        g_object_unref(m_fileChooser);
        m_fileChooser = (GtkFileChooser*)m_widget;
        g_object_ref(m_fileChooser);
    }
}

#endif // !__WXGTK4__

int wxDirDialog::ShowModal()
{
#ifdef __WXGTK4__
    // Not the base class version, so the hook it would have called has to be
    // called here: wxExpectModal() and the rest of the modal dialog testing
    // machinery hang off it.
    WX_HOOK_MODAL_DIALOG();

    m_paths.Clear();
    m_path.clear();

    GtkFileDialog* const dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_modal(dialog, TRUE);
    gtk_file_dialog_set_title(dialog, m_message.utf8_str());

    if ( !m_initialPath.empty() )
    {
        wxGtkObject<GFile> folder(
            g_file_new_for_path(wxGTK_CONV_FN(m_initialPath)));
        gtk_file_dialog_set_initial_folder(dialog, folder);
    }

    GtkWindow* const parentGTK = m_parent && m_parent->m_widget
                                    ? GTK_WINDOW(m_parent->m_widget)
                                    : nullptr;

    const bool multiple = HasFlag(wxDD_MULTIPLE);

    wxGTKDialogAsyncResult result;
    wxDirChooseContext ctx = { this, &result, multiple };

    if ( multiple )
    {
        gtk_file_dialog_select_multiple_folders(dialog, parentGTK,
                                                nullptr /* GCancellable */,
                                                wxgtk_dir_chosen, &ctx);
    }
    else
    {
        gtk_file_dialog_select_folder(dialog, parentGTK,
                                      nullptr /* GCancellable */,
                                      wxgtk_dir_chosen, &ctx);
    }

    const int rc = result.Run();

    g_object_unref(dialog);

    m_returnCode = rc;
    return rc;
#else // !__WXGTK4__
    WX_HOOK_MODAL_DIALOG();

#if GTK_CHECK_VERSION(3,20,0)
    if (m_fileChooser != (GtkFileChooser*)m_widget)
    {
        m_returnCode = 0;
        int res = gtk_native_dialog_run(GTK_NATIVE_DIALOG(m_fileChooser));
        if (res == GTK_RESPONSE_ACCEPT)
        {
            GTKAccept();
            m_returnCode = wxID_OK;
        }
        else if (m_returnCode == 0)
            m_returnCode = wxID_CANCEL;

        return m_returnCode;
    }
#endif
    return BaseType::ShowModal();
#endif // __WXGTK4__/!__WXGTK4__
}

void wxDirDialog::EndModal(int retCode)
{
#ifdef __WXGTK4__
    // There is no dialog to hide: ShowModal() returns when the controller's
    // callback ends the nested loop, and nothing else can end it.
    BaseType::EndModal(retCode);
#else
#if GTK_CHECK_VERSION(3,20,0)
    if (m_fileChooser != (GtkFileChooser*)m_widget)
    {
        m_returnCode = retCode;
        gtk_native_dialog_hide(GTK_NATIVE_DIALOG(m_fileChooser));
    }
    else
#endif
    {
        BaseType::EndModal(retCode);
    }
#endif // __WXGTK4__/!__WXGTK4__
}

void wxDirDialog::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    if (!m_wxwindow)
        return;

    BaseType::DoSetSize(x, y, width, height, sizeFlags);
}

void wxDirDialog::SetPath(const wxString& dir)
{
    if (wxDirExists(dir))
    {
#ifdef __WXGTK4__
        // Remembered rather than applied: the controller that would receive it
        // does not exist until ShowModal() builds one.
        m_initialPath = dir;
#else
        gtk_file_chooser_set_current_folder(m_fileChooser, wxGTK_CONV_FN(dir));
        if (m_fileChooser != (GtkFileChooser*)m_widget)
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(m_widget), wxGTK_CONV_FN(dir));
#endif
    }
}

#endif // wxUSE_DIRDLG && !__WXGTK4__
