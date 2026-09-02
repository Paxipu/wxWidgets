/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/clrpicker.cpp
// Purpose:     implementation of wxColourButton
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

#if wxUSE_COLOURPICKERCTRL

#include "wx/clrpicker.h"

#include "wx/gtk/private/wrapgtk.h"

// ============================================================================
// implementation
// ============================================================================

//-----------------------------------------------------------------------------
// "color-set"
//-----------------------------------------------------------------------------

// Common tail of both change callbacks below: take the new colour and tell
// the world about it. Templated only because the native colour type differs
// between the GTK versions -- GdkColor under GTK+ 2, GdkRGBA after it -- and
// GTKSetColour() is overloaded for both.
template <typename GdkColourType>
static void wxGTKColourButtonChanged(wxColourButton* p, const GdkColourType& gdkColor)
{
    p->GTKSetColour(gdkColor);

    // Fire the corresponding event: note that we want it to appear as
    // originating from our parent, which is the user-visible window, and not
    // this button itself, which is just an implementation detail.
    wxWindow* const parent = p->GetParent();
    wxColourPickerEvent event(parent, parent->GetId(), p->GetColour());
    p->HandleWindowEvent(event);
}

extern "C" {

#ifdef __WXGTK4__

// GtkColorDialogButton has no "color-set" signal -- the colour is a plain
// property and the only notification is the property notify. That notify also
// fires when *we* set the colour from UpdateColour(), which "color-set" never
// did, so UpdateColour() blocks this handler; see the comment there.
static void gtk_clrbutton_setcolor_callback(GObject *widget,
                                            GParamSpec * WXUNUSED(pspec),
                                            wxColourButton *p)
{
    wxASSERT(p);
    wxGTKColourButtonChanged(
        p, *gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(widget)));
}

#else // !__WXGTK4__

static void gtk_clrbutton_setcolor_callback(GtkColorButton *widget,
                                            wxColourButton *p)
{
    // update the m_colour member of the wxColourButton
    wxASSERT(p);
#ifdef __WXGTK3__
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    GdkRGBA gdkColor;
    gtk_color_button_get_rgba(widget, &gdkColor);
    wxGCC_WARNING_RESTORE()
#else
    GdkColor gdkColor;
    gtk_color_button_get_color(widget, &gdkColor);
#endif
    wxGTKColourButtonChanged(p, gdkColor);
}

#endif // __WXGTK4__/!__WXGTK4__

}

//-----------------------------------------------------------------------------
// wxColourButton
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxColourButton, wxButton);

bool wxColourButton::Create( wxWindow *parent, wxWindowID id,
                        const wxColour &col,
                        const wxPoint &pos, const wxSize &size,
                        long style, const wxValidator& validator,
                        const wxString &name )
{
    if (!PreCreation( parent, pos, size ) ||
        !wxControl::CreateBase(parent, id, pos, size, style, validator, name))
    {
        wxFAIL_MSG( wxT("wxColourButton creation failed") );
        return false;
    }

    m_colour = col;
#ifdef __WXGTK4__
    // GtkColorButton is deprecated since GTK 4.10; GtkColorDialogButton is the
    // replacement. It carries the settings that used to be properties of the
    // button on a GtkColorDialog it owns, so the dialog has to exist first --
    // gtk_color_dialog_button_new() takes our reference to it.
    GtkColorDialog* const dialog = gtk_color_dialog_new();
    gtk_color_dialog_set_with_alpha(dialog, (style & wxCLRP_SHOW_ALPHA) != 0);
    m_widget = gtk_color_dialog_button_new(dialog);
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(m_widget),
                                     m_colour.GTKGetRGBA());
    g_object_ref(m_widget);

    g_signal_connect(m_widget, "notify::rgba",
                    G_CALLBACK(gtk_clrbutton_setcolor_callback), this);
#else // !__WXGTK4__
#ifdef __WXGTK3__
    m_widget = gtk_color_button_new_with_rgba(m_colour.GTKGetRGBA());
#else
    m_widget = gtk_color_button_new_with_color( m_colour.GetColor() );
#endif
    g_object_ref(m_widget);

    // Display opacity slider
    g_object_set(G_OBJECT(m_widget), "use-alpha",
                 static_cast<bool>(style & wxCLRP_SHOW_ALPHA), nullptr);
    // GtkColourButton signals
    g_signal_connect(m_widget, "color-set",
                    G_CALLBACK(gtk_clrbutton_setcolor_callback), this);
#endif // __WXGTK4__/!__WXGTK4__


    m_parent->DoAddChild( this );

    PostCreation(size);
    SetInitialSize(size);

    return true;
}

wxColourButton::~wxColourButton()
{
}

void wxColourButton::UpdateColour()
{
#ifdef __WXGTK4__
    // This is a programmatic change, so it must not look like the user picked
    // a colour. The old "color-set" signal only fired for the latter, but the
    // property notify we replaced it with fires for both, so block it here --
    // otherwise SetColour() would emit a wxColourPickerEvent of its own.
    g_signal_handlers_block_by_func(
        m_widget, (gpointer)gtk_clrbutton_setcolor_callback, this);
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(m_widget),
                                     m_colour.GTKGetRGBA());
    g_signal_handlers_unblock_by_func(
        m_widget, (gpointer)gtk_clrbutton_setcolor_callback, this);
#elif defined(__WXGTK3__)
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    gtk_color_button_set_rgba(GTK_COLOR_BUTTON(m_widget), m_colour.GTKGetRGBA());
    wxGCC_WARNING_RESTORE()
#else
    gtk_color_button_set_color(GTK_COLOR_BUTTON(m_widget), m_colour.GetColor());
#endif
}

#endif // wxUSE_COLOURPICKERCTRL
