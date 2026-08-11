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
static void gtk_fontbutton_setfont_callback(GtkFontButton *widget,
                                            wxFontButton *p)
{
    // update the m_selectedFont member of the wxFontButton
    wxASSERT(p);

#ifdef __WXGTK4__
    // gtk_font_button_get_font_name() is gone under GTK4; the equivalent
    // moved to the GtkFontChooser interface, which GtkFontButton
    // implements.
    wxGlibPtr<gchar> fontName(gtk_font_chooser_get_font(GTK_FONT_CHOOSER(widget)));
    p->SetNativeFontInfo(fontName);
#else
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    p->SetNativeFontInfo(gtk_font_button_get_font_name(widget));
    wxGCC_WARNING_RESTORE(deprecated-declarations)
#endif

    // fire the colour-changed event
    wxFontPickerEvent event(p, p->GetId(), p->GetSelectedFont());
    p->HandleWindowEvent(event);
}
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

    m_widget = gtk_font_button_new();
    g_object_ref(m_widget);

    // set initial font
    m_selectedFont = initial.IsOk() ? initial : *wxNORMAL_FONT;
    UpdateFont();

    // honour the fontbutton styles
    bool showall = (style & wxFNTP_FONTDESC_AS_LABEL) != 0,
         usefont = (style & wxFNTP_USEFONT_FOR_LABEL) != 0;
#ifndef __WXGTK4__
    // gtk_font_button_set_show_style()/set_show_size() (controlling
    // whether the button's own label includes the style/size text) are
    // gone under GTK4 with no discoverable replacement -- use_font/
    // use_size (below) still exist, but those control a different thing
    // (whether the label renders *using* the selected font/size, not
    // whether style/size text is appended to it). Known fidelity gap, not
    // yet runtime-verified.
    gtk_font_button_set_show_style(GTK_FONT_BUTTON(m_widget), showall);
    gtk_font_button_set_show_size(GTK_FONT_BUTTON(m_widget), showall);
#else
    wxUnusedVar(showall);
#endif

    gtk_font_button_set_use_size(GTK_FONT_BUTTON(m_widget), usefont);
    gtk_font_button_set_use_font(GTK_FONT_BUTTON(m_widget), usefont);

    // GtkFontButton signals
    g_signal_connect(m_widget, "font-set",
                    G_CALLBACK(gtk_fontbutton_setfont_callback), this);


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
    gtk_font_chooser_set_font(GTK_FONT_CHOOSER(m_widget), fontname.utf8_str());
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
