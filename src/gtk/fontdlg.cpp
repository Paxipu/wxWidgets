/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/fontdlg.cpp
// Purpose:     wxFontDialog
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_FONTDLG

#include "wx/fontdlg.h"

#ifndef WX_PRECOMP
    #include "wx/intl.h"
#endif

#include "wx/fontutil.h"
#include "wx/gtk/private.h"
#include "wx/modalhook.h"

#ifdef __WXGTK4__
    #include "wx/gtk/private/dialogasync.h"
#endif

//-----------------------------------------------------------------------------
// "response"
//-----------------------------------------------------------------------------

#ifdef __WXGTK4__

namespace
{

// Lives on wxFontDialog::ShowModal()'s stack, which outlives the asynchronous
// call because that function blocks until the callback has finished with it.
struct wxFontChooseContext
{
    wxFontData* data;
    wxGTKDialogAsyncResult* result;
};

} // anonymous namespace

extern "C" {
static void
wxgtk_font_chosen(GObject* source, GAsyncResult* res, gpointer user_data)
{
    wxFontChooseContext* const
        ctx = static_cast<wxFontChooseContext*>(user_data);

    GError* error = nullptr;
    PangoFontDescription* const desc = gtk_font_dialog_choose_font_finish(
                                        GTK_FONT_DIALOG(source), res, &error);

    int rc;
    if ( desc )
    {
        // wxNativeFontInfo owns whatever its description points at, and
        // choose_font_finish() hands us a new one, so this transfers rather
        // than copies.
        wxNativeFontInfo info;
        info.description = desc;
        ctx->data->SetChosenFont(wxFont(info));
        rc = wxID_OK;
    }
    else
    {
        rc = wxGTKDialogAsyncResult::GetCodeForError(error, "Choosing a font");
        g_clear_error(&error);
    }

    ctx->result->Finish(rc);
}
}

#else // !__WXGTK4__

extern "C" {
static void response(GtkDialog* dialog, int response_id, wxFontDialog* win)
{
    int rc = wxID_CANCEL;
    if (response_id == GTK_RESPONSE_OK)
    {
        rc = wxID_OK;
#if GTK_CHECK_VERSION(3,2,0)
        if (gtk_check_version(3,2,0) == nullptr)
        {
            wxNativeFontInfo info;
            info.description = gtk_font_chooser_get_font_desc(GTK_FONT_CHOOSER(dialog));
            win->GetFontData().SetChosenFont(wxFont(info));
        }
#ifndef __WXGTK4__
        else
#endif
#endif
#ifndef __WXGTK4__
        {
            wxGCC_WARNING_SUPPRESS(deprecated-declarations)
            GtkFontSelectionDialog* sel = GTK_FONT_SELECTION_DIALOG(dialog);
            wxGtkString name(gtk_font_selection_dialog_get_font_name(sel));
            win->GetFontData().SetChosenFont(wxFont(wxString::FromUTF8(name)));
            wxGCC_WARNING_RESTORE()
        }
#endif
    }

    if (win->IsModal())
        win->EndModal(rc);
    else
        win->Show(false);
}
}

#endif // __WXGTK4__/!__WXGTK4__

//-----------------------------------------------------------------------------
// wxFontDialog
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxFontDialog, wxDialog);

bool wxFontDialog::DoCreate(wxWindow *parent)
{
    parent = GetParentForModalDialog(parent, 0);

    if (!PreCreation( parent, wxDefaultPosition, wxDefaultSize ) ||
        !CreateBase( parent, -1, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE,
                     wxDefaultValidator, wxT("fontdialog") ))
    {
        wxFAIL_MSG( wxT("wxFontDialog creation failed") );
        return false;
    }

    const wxString message(_("Choose font"));
    GtkWindow* gtk_parent = nullptr;
    if (parent)
        gtk_parent = GTK_WINDOW(parent->m_widget);

#ifdef __WXGTK4__
    // Nothing here can be m_widget: GtkFontDialog is a controller object, not
    // a widget, and there is no dialog at all between ShowModal() calls. A
    // plain window is created instead so that the inherited wxWindow calls --
    // GetTitle(), IsShown(), the destructor -- keep working on something.
    // It is never shown.
    wxUnusedVar(gtk_parent);
    m_widget = gtk_window_new();
    g_object_ref_sink(m_widget);
    gtk_window_set_title(GTK_WINDOW(m_widget), message.utf8_str());

    return true;
#else // !__WXGTK4__

#if GTK_CHECK_VERSION(3,2,0)
#if GLIB_CHECK_VERSION(2, 34, 0)
    g_type_ensure(PANGO_TYPE_FONT_FACE);
#endif
    if (gtk_check_version(3,2,0) == nullptr)
        m_widget = gtk_font_chooser_dialog_new(message.utf8_str(), gtk_parent);
#ifndef __WXGTK4__
    else
#endif
#endif
#ifndef __WXGTK4__
    {
        wxGCC_WARNING_SUPPRESS(deprecated-declarations)
        m_widget = gtk_font_selection_dialog_new(message.utf8_str());
        if (gtk_parent)
            gtk_window_set_transient_for(GTK_WINDOW(m_widget), gtk_parent);
        wxGCC_WARNING_RESTORE()
    }
#endif
    g_object_ref(m_widget);

    g_signal_connect(m_widget, "response", G_CALLBACK(response), this);

    wxFont font = m_fontData.GetInitialFont();
    if( font.IsOk() )
    {
        const wxNativeFontInfo *info = font.GetNativeFontInfo();

        if ( info )
        {
#if GTK_CHECK_VERSION(3,2,0)
            if (gtk_check_version(3,2,0) == nullptr)
                gtk_font_chooser_set_font_desc(GTK_FONT_CHOOSER(m_widget), info->description);
#ifndef __WXGTK4__
            else
#endif
#endif
#ifndef __WXGTK4__
            {
                wxGCC_WARNING_SUPPRESS(deprecated-declarations)
                const wxString& fontname = info->ToString();
                GtkFontSelectionDialog* sel = GTK_FONT_SELECTION_DIALOG(m_widget);
                gtk_font_selection_dialog_set_font_name(sel, fontname.utf8_str());
                wxGCC_WARNING_RESTORE()
            }
#endif
        }
        else
        {
            // this is not supposed to happen!
            wxFAIL_MSG(wxT("font is ok but no native font info?"));
        }
    }

    return true;
#endif // __WXGTK4__/!__WXGTK4__
}

#ifdef __WXGTK4__

int wxFontDialog::ShowModal()
{
    // Not wxDialog::ShowModal(), so the hook it would have called has to be
    // called here: wxExpectModal() and the rest of the modal dialog testing
    // machinery hang off it.
    WX_HOOK_MODAL_DIALOG();

    GtkFontDialog* const dialog = gtk_font_dialog_new();
    gtk_font_dialog_set_modal(dialog, TRUE);

    // The title lives on the placeholder window so that SetTitle() and
    // GetTitle() keep working; hand it to the controller here.
    if ( const char* const title = gtk_window_get_title(GTK_WINDOW(m_widget)) )
        gtk_font_dialog_set_title(dialog, title);

    GtkWindow* const parentGTK = m_parent && m_parent->m_widget
                                    ? GTK_WINDOW(m_parent->m_widget)
                                    : nullptr;

    // A copy, not the font's own description: choose_font() takes this as a
    // non-const pointer and there is nothing in the header that says it will
    // neither keep nor modify it. Handing it our wxFont's description would
    // make that GTK's business as well as ours, and the failure mode of
    // guessing wrong is a double free.
    PangoFontDescription* initial = nullptr;
    const wxFont& font = m_fontData.GetInitialFont();
    if ( font.IsOk() )
    {
        if ( const wxNativeFontInfo* const info = font.GetNativeFontInfo() )
            initial = pango_font_description_copy(info->description);
    }

    wxGTKDialogAsyncResult result;
    wxFontChooseContext ctx = { &m_fontData, &result };

    gtk_font_dialog_choose_font(dialog, parentGTK, initial,
                                nullptr /* GCancellable */,
                                wxgtk_font_chosen, &ctx);

    const int rc = result.Run();

    if ( initial )
        pango_font_description_free(initial);

    g_object_unref(dialog);

    return rc;
}

#endif // __WXGTK4__

wxFontDialog::~wxFontDialog()
{
}

#endif // wxUSE_FONTDLG
