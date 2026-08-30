/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/colordlg.cpp
// Purpose:     Native wxColourDialog for GTK+
// Author:      Vaclav Slavik
// Created:     2004/06/04
// Copyright:   (c) Vaclav Slavik, 2004
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#if wxUSE_COLOURDLG

#include "wx/colordlg.h"

#ifndef WX_PRECOMP
    #include "wx/intl.h"
#endif

#include "wx/gtk/private.h"
#include "wx/modalhook.h"

#ifdef __WXGTK4__
    #include "wx/gtk/private/dialogasync.h"
#endif

#ifdef __WXGTK4__

namespace
{

// Everything the asynchronous callback below needs. It lives on
// wxColourDialog::ShowModal()'s stack, which outlives the call because that
// function blocks in Run() until the callback has finished with it.
struct wxColourChooseContext
{
    wxColourData* data;
    wxGTKDialogAsyncResult* result;
};

} // anonymous namespace

extern "C" {
static void
wxgtk_colour_chosen(GObject* source, GAsyncResult* res, gpointer user_data)
{
    wxColourChooseContext* const
        ctx = static_cast<wxColourChooseContext*>(user_data);

    GError* error = nullptr;
    GdkRGBA* const rgba = gtk_color_dialog_choose_rgba_finish(
                            GTK_COLOR_DIALOG(source), res, &error);

    int rc;
    if ( rgba )
    {
        ctx->data->SetColour(wxColour(*rgba));
        gdk_rgba_free(rgba);
        rc = wxID_OK;
    }
    else
    {
        rc = wxGTKDialogAsyncResult::GetCodeForError(error, "Choosing a colour");
        g_clear_error(&error);
    }

    ctx->result->Finish(rc);
}
}

#else // !__WXGTK4__

extern "C" {
static void response(GtkDialog*, int response_id, wxColourDialog* win)
{
    win->EndModal(response_id == GTK_RESPONSE_OK ? wxID_OK : wxID_CANCEL);
}
}

#endif // __WXGTK4__/!__WXGTK4__

wxIMPLEMENT_DYNAMIC_CLASS(wxColourDialog, wxDialog);

wxColourDialog::wxColourDialog(wxWindow *parent, const wxColourData *data)
{
    Create(parent, data);
}

bool wxColourDialog::Create(wxWindow *parent, const wxColourData *data)
{
    if (data)
        m_data = *data;

    m_parent = GetParentForModalDialog(parent, 0);
    GtkWindow * const parentGTK = m_parent ? GTK_WINDOW(m_parent->m_widget)
                                           : nullptr;

    wxString title(_("Choose colour"));
#ifdef __WXGTK4__
    // GtkColorChooserDialog is deprecated since GTK 4.10 and its replacement,
    // GtkColorDialog, is not a widget at all -- there is nothing here that
    // could be m_widget. The colour is chosen in ShowModal() instead.
    //
    // A plain window is still created, because the rest of wxWindow expects
    // one: the class already declares itself "not a real wxDialog" and stubs
    // out DoSetSize() and DoMoveWindow(), and this keeps the remaining
    // inherited calls -- GetTitle(), IsShown(), the destructor -- working on
    // something rather than on nullptr. It is never shown.
    wxUnusedVar(parentGTK);
    m_widget = gtk_window_new();
    g_object_ref_sink(m_widget);
    gtk_window_set_title(GTK_WINDOW(m_widget), title.utf8_str());
#else
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    m_widget = gtk_color_selection_dialog_new(title.utf8_str());

    g_object_ref(m_widget);

    if ( parentGTK )
    {
        gtk_window_set_transient_for(GTK_WINDOW(m_widget), parentGTK);
    }

    GtkColorSelection* sel = GTK_COLOR_SELECTION(
        gtk_color_selection_dialog_get_color_selection(
        GTK_COLOR_SELECTION_DIALOG(m_widget)));
    gtk_color_selection_set_has_palette(sel, true);
    gtk_color_selection_set_has_opacity_control(sel, m_data.GetChooseAlpha());
    wxGCC_WARNING_RESTORE()
#endif

    return true;
}

int wxColourDialog::ShowModal()
{
#ifdef __WXGTK4__
    // Not wxDialog::ShowModal(), so the hook it would have called has to be
    // called here: wxExpectModal() and the rest of the modal dialog testing
    // machinery hang off it.
    WX_HOOK_MODAL_DIALOG();

    GtkColorDialog* const dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_modal(dialog, TRUE);
    gtk_color_dialog_set_with_alpha(dialog, m_data.GetChooseAlpha());

    // The title is kept on the placeholder window so that SetTitle() and
    // GetTitle() keep working; hand it to the controller here.
    if ( const char* const title = gtk_window_get_title(GTK_WINDOW(m_widget)) )
        gtk_color_dialog_set_title(dialog, title);

    GtkWindow* const parentGTK = m_parent && m_parent->m_widget
                                    ? GTK_WINDOW(m_parent->m_widget)
                                    : nullptr;

    const wxColour& colour = m_data.GetColour();

    wxGTKDialogAsyncResult result;
    wxColourChooseContext ctx = { &m_data, &result };

    gtk_color_dialog_choose_rgba(dialog, parentGTK,
                                 colour.IsOk() ? colour.GTKGetRGBA() : nullptr,
                                 nullptr /* GCancellable */,
                                 wxgtk_colour_chosen, &ctx);

    const int rc = result.Run();

    g_object_unref(dialog);

    return rc;
#else // !__WXGTK4__
    ColourDataToDialog();

    gulong id = g_signal_connect(m_widget, "response", G_CALLBACK(response), this);
    int rc = wxDialog::ShowModal();
    g_signal_handler_disconnect(m_widget, id);

    if (rc == wxID_OK)
        DialogToColourData();

    return rc;
#endif // __WXGTK4__/!__WXGTK4__
}

void wxColourDialog::ColourDataToDialog()
{
#ifdef __WXGTK4__
    // Nothing to copy into: under GTK4 there is no dialog object between
    // ShowModal() calls, and the colour is handed to the controller there.
#else
    const wxColour& color = m_data.GetColour();

    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    GtkColorSelection* sel = GTK_COLOR_SELECTION(
        gtk_color_selection_dialog_get_color_selection(
        GTK_COLOR_SELECTION_DIALOG(m_widget)));

    if (color.IsOk())
    {
#ifdef __WXGTK3__
        gtk_color_selection_set_current_rgba(sel, color.GTKGetRGBA());
#else
        gtk_color_selection_set_current_color(sel, color.GetColor());
        // Convert alpha range: [0,255] -> [0,65535]
        gtk_color_selection_set_current_alpha(sel, 257*color.Alpha());
#endif
    }

    // setup the palette:

    GdkColor colors[wxColourData::NUM_CUSTOM];
    gint n_colors = 0;
    for (unsigned i = 0; i < WXSIZEOF(colors); i++)
    {
        wxColour c = m_data.GetCustomColour(i);
        if (c.IsOk())
        {
            colors[n_colors] = *c.GetColor();
            n_colors++;
        }
    }

    wxGtkString pal(gtk_color_selection_palette_to_string(colors, n_colors));

    GtkSettings *settings = gtk_widget_get_settings(GTK_WIDGET(sel));
    g_object_set(settings, "gtk-color-palette", pal.c_str(), nullptr);
    wxGCC_WARNING_RESTORE()
#endif // !__WXGTK4__
}

void wxColourDialog::DialogToColourData()
{
#ifdef __WXGTK4__
    // Likewise nothing to copy out of: the callback in ShowModal() writes the
    // chosen colour straight into m_data.
#else
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    GtkColorSelection* sel = GTK_COLOR_SELECTION(
        gtk_color_selection_dialog_get_color_selection(
        GTK_COLOR_SELECTION_DIALOG(m_widget)));

#ifdef __WXGTK3__
    GdkRGBA clr;
    gtk_color_selection_get_current_rgba(sel, &clr);
    m_data.SetColour(clr);
#else
    GdkColor clr;
    gtk_color_selection_get_current_color(sel, &clr);
    // Set RGB colour
    wxColour cRGB(clr);
    guint16 alpha = gtk_color_selection_get_current_alpha(sel);
    // Set RGBA colour (convert alpha range: [0,65535] -> [0,255]).
    wxColour cRGBA(cRGB.Red(), cRGB.Green(), cRGB.Blue(), alpha/257);
    m_data.SetColour(cRGBA);
#endif

    // Extract custom palette:

    GtkSettings *settings = gtk_widget_get_settings(GTK_WIDGET(sel));
    wxGlibPtr<gchar> pal;
    g_object_get(settings, "gtk-color-palette", pal.Out(), nullptr);

    wxGlibPtr<GdkColor> colors;
    gint n_colors;
    if (gtk_color_selection_palette_from_string(pal, colors.Out(), &n_colors))
    {
        for (int i = 0; i < n_colors && i < wxColourData::NUM_CUSTOM; i++)
        {
            m_data.SetCustomColour(i, wxColour(colors[i]));
        }
    }

    wxGCC_WARNING_RESTORE()
#endif // !__WXGTK4__
}

#endif // wxUSE_COLOURDLG

