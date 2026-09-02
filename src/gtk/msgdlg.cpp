/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/msgdlg.cpp
// Purpose:     wxMessageDialog for GTK+2
// Author:      Vaclav Slavik
// Created:     2003/02/28
// Copyright:   (c) Vaclav Slavik, 2003
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#if wxUSE_MSGDLG

#include "wx/msgdlg.h"

#ifndef WX_PRECOMP
    #include "wx/intl.h"
#endif

#include "wx/modalhook.h"

#include "wx/gtk/private.h"
#include "wx/gtk/private/gtk3-compat.h"
#include "wx/gtk/private/list.h"
#include "wx/gtk/private/messagetype.h"
#include "wx/gtk/private/mnemonics.h"
#include "wx/gtk/private/object.h"

#include <algorithm>

#ifdef __WXGTK4__
    #include "wx/gtk/private/dialogasync.h"
#endif

wxIMPLEMENT_CLASS(wxMessageDialog, wxDialog);

wxMessageDialog::wxMessageDialog(wxWindow *parent,
                                 const wxString& message,
                                 const wxString& caption,
                                 long style,
                                 const wxPoint& WXUNUSED(pos))
               : wxMessageDialogBase
                 (
                    parent,
                    message,
                    caption,
                    style
                 )
{
}

wxString wxMessageDialog::GetDefaultYesLabel() const
{
#ifdef __WXGTK4__
    return wxConvertMnemonicsToGTK(wxGetStockLabel(wxID_YES));
#else
    return "gtk-yes";
#endif
}

wxString wxMessageDialog::GetDefaultNoLabel() const
{
#ifdef __WXGTK4__
    return wxConvertMnemonicsToGTK(wxGetStockLabel(wxID_NO));
#else
    return "gtk-no";
#endif
}

wxString wxMessageDialog::GetDefaultOKLabel() const
{
#ifdef __WXGTK4__
    return wxConvertMnemonicsToGTK(wxGetStockLabel(wxID_OK));
#else
    return "gtk-ok";
#endif
}

wxString wxMessageDialog::GetDefaultCancelLabel() const
{
#ifdef __WXGTK4__
    return wxConvertMnemonicsToGTK(wxGetStockLabel(wxID_CANCEL));
#else
    return "gtk-cancel";
#endif
}

wxString wxMessageDialog::GetDefaultHelpLabel() const
{
#ifdef __WXGTK4__
    return wxConvertMnemonicsToGTK(wxGetStockLabel(wxID_HELP));
#else
    return "gtk-help";
#endif
}

void wxMessageDialog::DoSetCustomLabel(wxString& var, const ButtonLabel& label)
{
    int stockId = label.GetStockId();
    if ( stockId == wxID_NONE )
    {
        wxMessageDialogBase::DoSetCustomLabel(var, label);
        var = wxConvertMnemonicsToGTK(var);
    }
    else // stock label
    {
#ifdef __WXGTK4__
        var = wxConvertMnemonicsToGTK(wxGetStockLabel(stockId));
#else
        var = wxGetStockGtkID(stockId);
#endif
    }
}

#ifndef __WXGTK4__

// Only the GtkDialog path needs a dialog widget: under GTK4 a GtkAlertDialog
// is a controller, built and thrown away inside ShowModal().
void wxMessageDialog::GTKCreateMsgDialog()
{
    // Avoid crash if wxMessageBox() is called before GTK is initialized
    if (g_type_class_peek(GDK_TYPE_DISPLAY) == nullptr)
        return;

    GtkWindow * const parent = m_parent ? GTK_WINDOW(m_parent->m_widget) : nullptr;

    GtkMessageType type = GTK_MESSAGE_ERROR;
    GtkButtonsType buttons = GTK_BUTTONS_NONE;

    // when using custom labels, we have to add all the buttons ourselves
    if ( !HasCustomLabels() )
    {
        // "Help" button is not supported by predefined combinations so we
        // always need to create the buttons manually when it's used.
        if ( !(m_dialogStyle & wxHELP) )
        {
            if ( m_dialogStyle & wxYES_NO )
            {
                if ( !(m_dialogStyle & wxCANCEL) )
                    buttons = GTK_BUTTONS_YES_NO;
                //else: no standard GTK_BUTTONS_YES_NO_CANCEL so leave as NONE
            }
            else if ( m_dialogStyle & wxOK )
            {
                buttons = m_dialogStyle & wxCANCEL ? GTK_BUTTONS_OK_CANCEL
                                                   : GTK_BUTTONS_OK;
            }
        }
    }

    if ( !wxGTKImpl::ConvertMessageTypeFromWX(GetEffectiveIcon(), &type) )
    {
        // if no style is explicitly specified, detect the suitable icon
        // ourselves (this can be disabled by using wxICON_NONE)
        type = m_dialogStyle & wxYES ? GTK_MESSAGE_QUESTION : GTK_MESSAGE_INFO;
    }

    wxString message;
    bool needsExtMessage = false;
    if (!m_extendedMessage.empty())
    {
        message = m_message;
        needsExtMessage = true;
    }
    else // extended message not needed
    {
        message = GetFullMessage();
    }

    m_widget = gtk_message_dialog_new(parent,
                                      GTK_DIALOG_MODAL,
                                      type,
                                      buttons,
                                      "%s",
                                      (const char*)message.utf8_str());

    if ( needsExtMessage )
    {
        gtk_message_dialog_format_secondary_text
        (
            (GtkMessageDialog *)m_widget,
            "%s",
            (const char *)m_extendedMessage.utf8_str()
        );
    }

    g_object_ref(m_widget);

    if (m_caption != wxMessageBoxCaptionStr)
        gtk_window_set_title(GTK_WINDOW(m_widget), m_caption.utf8_str());

    GtkDialog * const dlg = GTK_DIALOG(m_widget);

#ifndef __WXGTK4__
    // GTK4 removed gtk_window_set_keep_above(): window stacking is the
    // compositor's business there and applications can no longer request it,
    // so wxSTAY_ON_TOP has no effect on this dialog.
    if ( m_dialogStyle & wxSTAY_ON_TOP )
    {
        gtk_window_set_keep_above(GTK_WINDOW(m_widget), TRUE);
    }
#endif

#if GTK_CHECK_VERSION(2,22,0)
    // A GTKMessageDialog usually displays its labels without selection enabled,
    // so we enable selection to allow the user to select+copy the text out of
    // the dialog.
    if (wx_is_at_least_gtk2(22))
    {
        GtkMessageDialog * const msgdlg = GTK_MESSAGE_DIALOG(m_widget);

        GtkWidget* const area = gtk_message_dialog_get_message_area(msgdlg);
#ifdef __WXGTK4__
        for ( GtkWidget* widget = gtk_widget_get_first_child(area);
              widget != nullptr;
              widget = gtk_widget_get_next_sibling(widget) )
        {
            if ( GTK_IS_LABEL(widget) )
#else
        wxGtkList labels(gtk_container_get_children(GTK_CONTAINER(area)));

        for ( GList* elem = labels; elem; elem = elem->next )
        {
            GtkWidget* const widget = GTK_WIDGET( elem->data );

            if ( GTK_IS_LABEL(widget) )
#endif
            {
                gtk_label_set_selectable(GTK_LABEL(widget), TRUE);
            }
        }
    }
#endif // GTK_CHECK_VERSION(2,22,0)

    // we need to add buttons manually if we use custom labels or always for
    // Yes/No/Cancel dialog as GTK+ doesn't support it natively
    const bool addButtons = buttons == GTK_BUTTONS_NONE;


    if ( addButtons )
    {
        if ( m_dialogStyle & wxHELP )
        {
            gtk_dialog_add_button(dlg, GetHelpLabel().utf8_str(),
                                  GTK_RESPONSE_HELP);
        }

        if ( m_dialogStyle & wxYES_NO ) // Yes/No or Yes/No/Cancel dialog
        {
            // Add the buttons in the correct order which is, according to
            // http://library.gnome.org/devel/hig-book/stable/windows-alert.html.en
            // the following one:
            //
            // [Help]                  [Alternative] [Cancel] [Affirmative]

            gtk_dialog_add_button(dlg, GetNoLabel().utf8_str(),
                                  GTK_RESPONSE_NO);

            if ( m_dialogStyle & wxCANCEL )
            {
                gtk_dialog_add_button(dlg, GetCancelLabel().utf8_str(),
                                      GTK_RESPONSE_CANCEL);
            }

            gtk_dialog_add_button(dlg, GetYesLabel().utf8_str(),
                                  GTK_RESPONSE_YES);
        }
        else // Ok or Ok/Cancel dialog
        {
            gtk_dialog_add_button(dlg, GetOKLabel().utf8_str(), GTK_RESPONSE_OK);
            if ( m_dialogStyle & wxCANCEL )
            {
                gtk_dialog_add_button(dlg, GetCancelLabel().utf8_str(),
                                      GTK_RESPONSE_CANCEL);
            }
        }
    }

    gint defaultButton;
    if ( m_dialogStyle & wxCANCEL_DEFAULT )
        defaultButton = GTK_RESPONSE_CANCEL;
    else if ( m_dialogStyle & wxNO_DEFAULT )
        defaultButton = GTK_RESPONSE_NO;
    else if ( m_dialogStyle & wxYES_NO )
        defaultButton = GTK_RESPONSE_YES;
    else if ( m_dialogStyle & wxOK )
        defaultButton = GTK_RESPONSE_OK;
    else // No need to change the default value, whatever it is.
        defaultButton = GTK_RESPONSE_NONE;

    if ( defaultButton != GTK_RESPONSE_NONE )
        gtk_dialog_set_default_response(dlg, defaultButton);
}

#endif // !__WXGTK4__

#ifdef __WXGTK4__

namespace
{

// What the asynchronous callback needs. It lives on ShowModal()'s stack,
// which outlives the call because that function blocks until the callback has
// finished with it.
struct wxAlertChooseContext
{
    // wx id for each button, in the order they were given to GTK.
    const wxVector<int>* ids;
    wxGTKDialogAsyncResult* result;
};

} // anonymous namespace

extern "C" {
static void
wxgtk_alert_chosen(GObject* source, GAsyncResult* res, gpointer user_data)
{
    wxAlertChooseContext* const
        ctx = static_cast<wxAlertChooseContext*>(user_data);

    GError* error = nullptr;
    const int button = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source),
                                                      res, &error);

    int rc;
    if ( !error && button >= 0 && size_t(button) < ctx->ids->size() )
    {
        rc = (*ctx->ids)[button];
    }
    else
    {
        // Dismissing the dialog is reported as an error here, so this is the
        // ordinary Escape/close path as well as the failure one; see
        // GetCodeForError() for which is which.
        rc = wxGTKDialogAsyncResult::GetCodeForError(error, "Showing a message");
    }

    g_clear_error(&error);

    ctx->result->Finish(rc);
}
}

int wxMessageDialog::ShowModal()
{
    // Not wxDialog::ShowModal(), so the hook it would have called has to be
    // called here: wxExpectModal() and the rest of the modal dialog testing
    // machinery hang off it.
    WX_HOOK_MODAL_DIALOG();

    // Avoid crashing if wxMessageBox() is called before GTK is initialized.
    if (g_type_class_peek(GDK_TYPE_DISPLAY) == nullptr)
        return wxID_CANCEL;

    wxString message;
    wxString detail;
    if ( !m_extendedMessage.empty() )
    {
        message = m_message;
        detail = m_extendedMessage;
    }
    else
    {
        message = GetFullMessage();
    }

    wxGtkObject<GtkAlertDialog>
        dialog(gtk_alert_dialog_new("%s", (const char*)message.utf8_str()));
    gtk_alert_dialog_set_modal(dialog, TRUE);

    if ( !detail.empty() )
        gtk_alert_dialog_set_detail(dialog, detail.utf8_str());

    // The same order the GtkDialog path used, which is the GNOME HIG one:
    //
    //   [Help]                  [Alternative] [Cancel] [Affirmative]
    //
    // GtkAlertDialog lays the buttons out in the order it is given them, so
    // the order is the only thing that has to be right; which one Escape and
    // Return pick is said separately below.
    wxVector<wxString> labels;
    wxVector<int> ids;

    auto addButton = [&labels, &ids](const wxString& label, int id)
    {
        labels.push_back(label);
        ids.push_back(id);
    };

    if ( m_dialogStyle & wxHELP )
        addButton(GetHelpLabel(), wxID_HELP);

    int idCancel = wxNOT_FOUND;
    if ( m_dialogStyle & wxYES_NO )
    {
        addButton(GetNoLabel(), wxID_NO);

        if ( m_dialogStyle & wxCANCEL )
        {
            idCancel = int(labels.size());
            addButton(GetCancelLabel(), wxID_CANCEL);
        }

        addButton(GetYesLabel(), wxID_YES);
    }
    else
    {
        addButton(GetOKLabel(), wxID_OK);

        if ( m_dialogStyle & wxCANCEL )
        {
            idCancel = int(labels.size());
            addButton(GetCancelLabel(), wxID_CANCEL);
        }
    }

    wxVector<const char*> labelsUTF8;
    wxVector<wxScopedCharBuffer> labelBuffers;
    labelBuffers.reserve(labels.size());
    for ( size_t i = 0; i < labels.size(); i++ )
    {
        labelBuffers.push_back(labels[i].utf8_str());
        labelsUTF8.push_back(labelBuffers[i].data());
    }
    labelsUTF8.push_back(nullptr);

    gtk_alert_dialog_set_buttons(dialog, &labelsUTF8[0]);

    // Which button Escape and the window close button pick. Without this GTK
    // has no way to know, and dismissing the dialog would report an error
    // rather than the answer the user gave by closing it.
    if ( idCancel != wxNOT_FOUND )
        gtk_alert_dialog_set_cancel_button(dialog, idCancel);

    int idDefault = wxNOT_FOUND;
    if ( m_dialogStyle & wxCANCEL_DEFAULT )
        idDefault = idCancel;
    else if ( m_dialogStyle & wxNO_DEFAULT )
        idDefault = int(std::find(ids.begin(), ids.end(), wxID_NO) - ids.begin());
    else if ( m_dialogStyle & wxYES_NO )
        idDefault = int(std::find(ids.begin(), ids.end(), wxID_YES) - ids.begin());
    else if ( m_dialogStyle & wxOK )
        idDefault = int(std::find(ids.begin(), ids.end(), wxID_OK) - ids.begin());

    if ( idDefault != wxNOT_FOUND && size_t(idDefault) < ids.size() )
        gtk_alert_dialog_set_default_button(dialog, idDefault);

    GtkWindow* const parentGTK = m_parent && m_parent->m_widget
                                    ? GTK_WINDOW(m_parent->m_widget)
                                    : nullptr;

    wxGTKDialogAsyncResult result;
    wxAlertChooseContext ctx = { &ids, &result };

    gtk_alert_dialog_choose(dialog, parentGTK, nullptr /* GCancellable */,
                            wxgtk_alert_chosen, &ctx);

    return result.Run();
}

#else // !__WXGTK4__

int wxMessageDialog::ShowModal()
{
    WX_HOOK_MODAL_DIALOG();

    if ( !m_widget )
    {
        GTKCreateMsgDialog();
        wxCHECK_MSG( m_widget, wxID_CANCEL,
                     wxT("failed to create GtkMessageDialog") );
    }

    // break the mouse capture as it would interfere with modal dialog (see
    // wxDialog::ShowModal)
    GTKReleaseMouseAndNotify();

    // This should be necessary, but otherwise the
    // parent TLW will disappear..
    if (m_parent)
        gtk_window_present( GTK_WINDOW(m_parent->m_widget) );

    gint result = gtk_dialog_run(GTK_DIALOG(m_widget));
    GTKDisconnect(m_widget);
#ifdef __WXGTK4__
    gtk_window_destroy(GTK_WINDOW(m_widget));
#else
    gtk_widget_destroy(m_widget);
#endif
    g_object_unref(m_widget);
    m_widget = nullptr;

    switch (result)
    {
        default:
            wxFAIL_MSG(wxT("unexpected GtkMessageDialog return code"));
            wxFALLTHROUGH;

        case GTK_RESPONSE_CANCEL:
        case GTK_RESPONSE_DELETE_EVENT:
        case GTK_RESPONSE_CLOSE:
            return wxID_CANCEL;
        case GTK_RESPONSE_OK:
            return wxID_OK;
        case GTK_RESPONSE_YES:
            return wxID_YES;
        case GTK_RESPONSE_NO:
            return wxID_NO;
        case GTK_RESPONSE_HELP:
            return wxID_HELP;
    }
}

#endif // __WXGTK4__/!__WXGTK4__


#endif // wxUSE_MSGDLG
