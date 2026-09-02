/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/fontpicker.cpp
// Purpose:     implementation of wxFontButton
// Author:      Francesco Montorsi
// Modified By:
// Created:     15/04/2006
// Copyright:   (c) Francesco Montorsi
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////


// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_FONTPICKERCTRL

#include "wx/fontpicker.h"

#include "wx/fontutil.h"        // for wxNativeFontInfo
#include "wx/gtk/private.h"

// ============================================================================
// implementation
// ============================================================================

//-----------------------------------------------------------------------------
// "font-set"
//-----------------------------------------------------------------------------

extern "C" {

#ifdef __WXGTK4__

// GtkFontDialogButton has no "font-set" signal -- the font is a plain property
// and the only notification is the property notify. That notify also fires
// when *we* set the font from UpdateFont(), which "font-set" never did, so
// UpdateFont() blocks this handler; see the comment there.
static void gtk_fontbutton_setfont_callback(GObject *widget,
                                            GParamSpec * WXUNUSED(pspec),
                                            wxFontButton *p)
{
    wxASSERT(p);

    // The description is owned by the button, so only the string we make from
    // it is ours to free. It is the same string the old button reported.
    const PangoFontDescription* const desc =
        gtk_font_dialog_button_get_font_desc(GTK_FONT_DIALOG_BUTTON(widget));
    if ( !desc )
        return;

    wxGlibPtr<gchar> fontName(pango_font_description_to_string(desc));
    p->SetNativeFontInfo(fontName);

    // fire the font-changed event
    wxFontPickerEvent event(p, p->GetId(), p->GetSelectedFont());
    p->HandleWindowEvent(event);
}

#else // !__WXGTK4__

static void gtk_fontbutton_setfont_callback(GtkFontButton *widget,
                                            wxFontButton *p)
{
    // update the m_selectedFont member of the wxFontButton
    wxASSERT(p);

    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    p->SetNativeFontInfo(gtk_font_button_get_font_name(widget));
    wxGCC_WARNING_RESTORE(deprecated-declarations)

    // fire the colour-changed event
    wxFontPickerEvent event(p, p->GetId(), p->GetSelectedFont());
    p->HandleWindowEvent(event);
}

#endif // __WXGTK4__/!__WXGTK4__

}

//-----------------------------------------------------------------------------
// wxFontButton
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxFontButton, wxButton);

bool wxFontButton::Create( wxWindow *parent, wxWindowID id,
                        const wxFont &initial,
                        const wxPoint &pos, const wxSize &size,
                        long style, const wxValidator& validator,
                        const wxString &name )
{
    if (!PreCreation( parent, pos, size ) ||
        !wxControl::CreateBase(parent, id, pos, size, style, validator, name))
    {
        wxFAIL_MSG( wxT("wxFontButton creation failed") );
        return false;
    }

    // honour the fontbutton styles
    bool showall = (style & wxFNTP_FONTDESC_AS_LABEL) != 0,
         usefont = (style & wxFNTP_USEFONT_FOR_LABEL) != 0;

#ifdef __WXGTK4__
    // GtkFontButton is deprecated since GTK 4.10; GtkFontDialogButton is the
    // replacement. As with the colour button, the settings live on a dialog
    // object the button owns, and the constructor takes our reference to it.
    GtkFontDialog* const dialog = gtk_font_dialog_new();
    m_widget = gtk_font_dialog_button_new(dialog);

    // Ask for a full font -- family, style and size. The other levels would
    // let the user pick only a family or only a face, and wxFontPickerCtrl
    // has to come back with a font that has a size.
    gtk_font_dialog_button_set_level(GTK_FONT_DIALOG_BUTTON(m_widget),
                                     GTK_FONT_LEVEL_FONT);

    // gtk_font_button_set_show_style()/set_show_size(), which controlled
    // whether the button's own label spelled out the style and size, have no
    // counterpart here: GtkFontDialogButton always writes the full
    // description. use_font/use_size below still exist but control a
    // different thing -- whether the label is *rendered in* the chosen font
    // and size, not what it says. So wxFNTP_FONTDESC_AS_LABEL has no effect
    // under GTK4; this is a known fidelity gap, unchanged by this port.
    wxUnusedVar(showall);

    gtk_font_dialog_button_set_use_size(GTK_FONT_DIALOG_BUTTON(m_widget), usefont);
    gtk_font_dialog_button_set_use_font(GTK_FONT_DIALOG_BUTTON(m_widget), usefont);

    g_object_ref(m_widget);

    g_signal_connect(m_widget, "notify::font-desc",
                    G_CALLBACK(gtk_fontbutton_setfont_callback), this);
#else // !__WXGTK4__
    m_widget = gtk_font_button_new();
    g_object_ref(m_widget);

    gtk_font_button_set_show_style(GTK_FONT_BUTTON(m_widget), showall);
    gtk_font_button_set_show_size(GTK_FONT_BUTTON(m_widget), showall);

    gtk_font_button_set_use_size(GTK_FONT_BUTTON(m_widget), usefont);
    gtk_font_button_set_use_font(GTK_FONT_BUTTON(m_widget), usefont);

    // GtkFontButton signals
    g_signal_connect(m_widget, "font-set",
                    G_CALLBACK(gtk_fontbutton_setfont_callback), this);
#endif // __WXGTK4__/!__WXGTK4__

    // set initial font -- after the widget exists, since UpdateFont() writes
    // straight into it
    m_selectedFont = initial.IsOk() ? initial : *wxNORMAL_FONT;
    UpdateFont();


    m_parent->DoAddChild( this );

    PostCreation(size);
    SetInitialSize(size);

    return true;
}

wxFontButton::~wxFontButton()
{
}

void wxFontButton::UpdateFont()
{
    const wxNativeFontInfo *info = m_selectedFont.GetNativeFontInfo();
    wxASSERT_MSG( info, wxT("The fontbutton's internal font is not valid ?") );

    const wxString& fontname = info->ToString();

#ifdef __WXGTK4__
    // Blocked for the same reason as in wxColourButton::UpdateColour(): the
    // property notify does not distinguish our own write from the user's
    // choice, and only the latter may raise a wxFontPickerEvent.
    PangoFontDescription* const desc =
        pango_font_description_from_string(fontname.utf8_str());
    g_signal_handlers_block_by_func(
        m_widget, (gpointer)gtk_fontbutton_setfont_callback, this);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(m_widget), desc);
    g_signal_handlers_unblock_by_func(
        m_widget, (gpointer)gtk_fontbutton_setfont_callback, this);
    pango_font_description_free(desc);
#else
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    gtk_font_button_set_font_name(GTK_FONT_BUTTON(m_widget), fontname.utf8_str());
    wxGCC_WARNING_RESTORE(deprecated-declarations)
#endif
}

void wxFontButton::SetNativeFontInfo(const char* gtkdescription)
{
    m_selectedFont.SetNativeFontInfo(wxString::FromUTF8(gtkdescription));
}
#endif // wxUSE_FONTPICKERCTRL
