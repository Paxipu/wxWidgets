/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/button.cpp
// Purpose:
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_BUTTON

#ifndef WX_PRECOMP
    #include "wx/button.h"
#endif

#include "wx/stockitem.h"

#include "wx/gtk/private.h"
#include "wx/gtk/private/list.h"
#include "wx/gtk/private/gtk3-compat.h"

// ----------------------------------------------------------------------------
// GTK callbacks
// ----------------------------------------------------------------------------

extern "C"
{

static void
wxgtk_button_clicked_callback(GtkWidget *WXUNUSED(widget), wxButton *button)
{
    if ( button->GTKShouldIgnoreEvent() )
        return;

    wxCommandEvent event(wxEVT_BUTTON, button->GetId());
    event.SetEventObject(button);
    button->HandleWindowEvent(event);
}

//-----------------------------------------------------------------------------
// "style_set" from m_widget
//-----------------------------------------------------------------------------

// GTK4 has neither the "style-set" signal, nor GtkStyle, nor style properties
// such as "default_border" -- and, more to the point, no longer needs any of
// them here: the extra room a default button used to occupy is now drawn from
// CSS within the button's own allocation, so there is nothing to compensate
// for.  See the matching comment in wxWindowGTK::DoMoveWindow().
#ifndef __WXGTK4__

static void
wxgtk_button_style_set_callback(GtkWidget* widget, GtkStyle*, wxButton* win)
{
    /* the default button has a border around it */
    wxWindow* parent = win->GetParent();
    if (parent && parent->m_wxwindow && gtk_widget_get_can_default(widget))
    {
        GtkBorder* border = nullptr;
        gtk_widget_style_get(widget, "default_border", &border, nullptr);
        if (border)
        {
            win->MoveWindow(
                win->m_x - border->left,
                win->m_y - border->top,
                win->m_width + border->left + border->right,
                win->m_height + border->top + border->bottom);
            gtk_border_free(border);
        }
    }
}

#endif // !__WXGTK4__

} // extern "C"

//-----------------------------------------------------------------------------
// wxButton
//-----------------------------------------------------------------------------

#ifndef __WXGTK3__
bool wxButton::m_exactFitStyleDefined = false;
#endif // !__WXGTK3__

bool wxButton::Create(wxWindow *parent,
                      wxWindowID id,
                      const wxString &label,
                      const wxPoint& pos,
                      const wxSize& size,
                      long style,
                      const wxValidator& validator,
                      const wxString& name)
{
    if (!PreCreation( parent, pos, size ) ||
        !CreateBase( parent, id, pos, size, style, validator, name ))
    {
        wxFAIL_MSG( wxT("wxButton creation failed") );
        return false;
    }

    // create either a standard button with text label (which may still contain
    // an image under GTK+ 2.6+) or a bitmap-only button if we don't have any
    // label
    const bool
        useLabel = !(style & wxBU_NOTEXT) && (!label.empty() || wxIsStockID(id));
    if ( useLabel )
    {
        m_widget = gtk_button_new_with_mnemonic("");
    }
    else // no label, suppose we will have a bitmap
    {
        m_widget = gtk_button_new();
    }

    g_object_ref(m_widget);

    float x_alignment = 0.5f;
    if (HasFlag(wxBU_LEFT))
        x_alignment = 0;
    else if (HasFlag(wxBU_RIGHT))
        x_alignment = 1;

    float y_alignment = 0.5f;
    if (HasFlag(wxBU_TOP))
        y_alignment = 0;
    else if (HasFlag(wxBU_BOTTOM))
        y_alignment = 1;

#ifdef __WXGTK4__
    if (useLabel)
    {
        g_object_set(gtk_button_get_child(GTK_BUTTON(m_widget)),
            "xalign", x_alignment, "yalign", y_alignment, nullptr);
    }
#else
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    gtk_button_set_alignment(GTK_BUTTON(m_widget), x_alignment, y_alignment);
    wxGCC_WARNING_RESTORE()
#endif

    if ( useLabel )
        SetLabel(label);

    if (style & wxNO_BORDER)
       gtk_button_set_relief( GTK_BUTTON(m_widget), GTK_RELIEF_NONE );

    if (style & wxBU_EXACTFIT)
    {
#ifdef __WXGTK3__
        GTKApplyCssStyle("* { padding:0 }");
#else
        // Define a special button style without inner border
        // if it's not yet done.
        if ( !m_exactFitStyleDefined )
        {
            gtk_rc_parse_string(
              "style \"wxButton_wxBU_EXACTFIT_style\"\n"
              "{ GtkButton::inner-border = { 0, 0, 0, 0 } }\n"
              "widget \"*wxButton_wxBU_EXACTFIT*\" style \"wxButton_wxBU_EXACTFIT_style\"\n"
            );
            m_exactFitStyleDefined = true;
        }

        // Assign the button to the GTK style without inner border.
        gtk_widget_set_name(m_widget, "wxButton_wxBU_EXACTFIT");
#endif // __WXGTK3__ / !__WXGTK3__
    }

    g_signal_connect_after (m_widget, "clicked",
                            G_CALLBACK (wxgtk_button_clicked_callback),
                            this);

#ifndef __WXGTK4__
    g_signal_connect_after (m_widget, "style_set",
                            G_CALLBACK (wxgtk_button_style_set_callback),
                            this);
#endif // !__WXGTK4__

    m_parent->DoAddChild( this );

    PostCreation(size);

    return true;
}


wxWindow *wxButton::SetDefault()
{
    wxWindow *oldDefault = wxButtonBase::SetDefault();

#ifdef __WXGTK4__
    // gtk_widget_set_can_default() and gtk_widget_grab_default() are both
    // gone: being the default is no longer a property of the widget at all,
    // but of the window, and any widget can be made the default one.  Note
    // that this means the "can be default but isn't" state doesn't exist any
    // more either, which is what the size adjustment below used to be for.
    GtkRoot* const root = gtk_widget_get_root(m_widget);
    if ( GTK_IS_WINDOW(root) )
        gtk_window_set_default_widget(GTK_WINDOW(root), m_widget);
#else // !__WXGTK4__
    gtk_widget_set_can_default(m_widget, TRUE);
    gtk_widget_grab_default( m_widget );

    // resize for default border
    wxgtk_button_style_set_callback( m_widget, nullptr, this );
#endif // __WXGTK4__/!__WXGTK4__

    return oldDefault;
}

/* static */
wxSize wxButtonBase::GetDefaultSize(wxWindow* WXUNUSED(win))
{
    static wxSize size = wxDefaultSize;
    if (size == wxDefaultSize)
    {
#ifdef __WXGTK4__
        // GtkButtonBox (used below, under GTK3, to get GTK's own idea of
        // the minimum default button size, since a stock button's own
        // size may be smaller than the size GtkButtonBox would give it)
        // was removed in GTK4 with no replacement. GTK4's CSS-driven
        // sizing means a button's own natural size should already
        // reflect the theme's minimum, so just use that directly.
        // Not yet visually verified against a running app.
        GtkWidget *wnd = gtk_window_new();
        wxString labelGTK = GTKConvertMnemonics(wxGetStockLabel(wxID_CANCEL));
        GtkWidget *btn = gtk_button_new_with_mnemonic(labelGTK.utf8_str());
        gtk_window_set_child(GTK_WINDOW(wnd), btn);
        GtkRequisition req;
        gtk_widget_get_preferred_size(btn, nullptr, &req);

        size.x = req.width;
        size.y = req.height;

        gtk_window_destroy(GTK_WINDOW(wnd));
#else
        // NB: Default size of buttons should be same as size of stock
        //     buttons as used in most GTK+ apps. Unfortunately it's a little
        //     tricky to obtain this size: stock button's size may be smaller
        //     than size of button in GtkButtonBox and vice versa,
        //     GtkButtonBox's minimal button size may be smaller than stock
        //     button's size. We have to retrieve both values and combine them.

        GtkWidget *wnd = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        GtkWidget *box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
        wxGCC_WARNING_SUPPRESS(deprecated-declarations)
        GtkWidget* btn = gtk_button_new_from_stock("gtk-cancel");
        wxGCC_WARNING_RESTORE()
        gtk_container_add(GTK_CONTAINER(box), btn);
        gtk_container_add(GTK_CONTAINER(wnd), box);
        GtkRequisition req;
        gtk_widget_get_preferred_size(btn, nullptr, &req);

        gint minwidth, minheight;
        gtk_widget_style_get(box,
                             "child-min-width", &minwidth,
                             "child-min-height", &minheight,
                             nullptr);

        size.x = wxMax(minwidth, req.width);
        size.y = wxMax(minheight, req.height);

        gtk_widget_destroy(wnd);
#endif // __WXGTK4__/!__WXGTK4__
    }
    return size;
}

void wxButton::SetLabel( const wxString &lbl )
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid button") );

    wxString label(lbl);

    if (label.empty() && wxIsStockID(m_windowId))
        label = wxGetStockLabel(m_windowId);

    wxAnyButton::SetLabel(label);

    // don't use label if it was explicitly disabled
    if ( HasFlag(wxBU_NOTEXT) )
        return;

#ifndef __WXGTK4__
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    if (wxIsStockID(m_windowId) && wxIsStockLabel(m_windowId, label))
    {
        const char *stock = wxGetStockGtkID(m_windowId);
        if (stock)
        {
            gtk_button_set_label(GTK_BUTTON(m_widget), stock);
            gtk_button_set_use_stock(GTK_BUTTON(m_widget), TRUE);
            return;
        }
    }
    wxGCC_WARNING_RESTORE()
#endif

#ifdef __WXGTK4__
    // wxAnyButton::SetLabel() above has already built the button's child --
    // a lone image, or a box holding an image and a label -- because GTK4 has
    // no gtk_button_set_image() to combine the two for us. Going on to call
    // gtk_button_set_label() would undo that: under GTK4 it *replaces* the
    // button's child with a plain GtkLabel, dropping the image on the floor,
    // and the next GTKUpdateBitmap() then finds no image to update. Under
    // GTK3 the two calls were independent and both were needed.
    if ( !GTKShowsImage() )
#endif // __WXGTK4__
    {
        // this call is necessary if the button had been initially created
        // without a (text) label -- then we didn't use
        // gtk_button_new_with_mnemonic() and so "use-underline" GtkButton
        // property remained unset
        gtk_button_set_use_underline(GTK_BUTTON(m_widget), TRUE);
        const wxString labelGTK = GTKConvertMnemonics(label);
        gtk_button_set_label(GTK_BUTTON(m_widget), labelGTK.utf8_str());
    }
#ifndef __WXGTK4__
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    gtk_button_set_use_stock(GTK_BUTTON(m_widget), FALSE);
    wxGCC_WARNING_RESTORE()
#endif

    GTKApplyWidgetStyle( false );
}

#if wxUSE_MARKUP
bool wxButton::DoSetLabelMarkup(const wxString& markup)
{
    wxCHECK_MSG( m_widget != nullptr, false, "invalid button" );

    const wxString stripped = RemoveMarkup(markup);
    if ( stripped.empty() && !markup.empty() )
        return false;

    SetLabel(stripped);

    GtkLabel * const label = GTKGetLabel();
    wxCHECK_MSG( label, false, "no label in this button?" );

    GTKSetLabelWithMarkupForLabel(label, markup);

    return true;
}

GtkLabel *wxButton::GTKGetLabel() const
{
#ifdef __WXGTK4__
    GtkWidget* child = gtk_button_get_child(GTK_BUTTON(m_widget));
    if (GTK_IS_LABEL(child))
        return GTK_LABEL(child);

    return nullptr;
#else
    GtkWidget* child = gtk_bin_get_child(GTK_BIN(m_widget));
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    if ( GTK_IS_ALIGNMENT(child) )
    {
        GtkWidget* box = gtk_bin_get_child(GTK_BIN(child));
        GtkLabel* label = nullptr;
        wxGtkList list(gtk_container_get_children(GTK_CONTAINER(box)));
        for (GList* item = list; item; item = item->next)
        {
            if (GTK_IS_LABEL(item->data))
                label = GTK_LABEL(item->data);
        }

        return label;
    }

    return GTK_LABEL(child);
    wxGCC_WARNING_RESTORE()
#endif
}
#endif // wxUSE_MARKUP

void wxButton::DoApplyWidgetStyle(GtkRcStyle *style)
{
    GTKApplyStyle(m_widget, style);
#ifdef __WXGTK4__
    GtkWidget* child = gtk_button_get_child(GTK_BUTTON(m_widget));
#else
    GtkWidget* child = gtk_bin_get_child(GTK_BIN(m_widget));
#endif
    GTKApplyStyle(child, style);

#ifndef __WXGTK4__
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    // for buttons with images, the path to the label is (at least in 2.12)
    // GtkButton -> GtkAlignment -> GtkHBox -> GtkLabel
    if ( GTK_IS_ALIGNMENT(child) )
    {
        GtkWidget* box = gtk_bin_get_child(GTK_BIN(child));
        if ( GTK_IS_BOX(box) )
        {
            wxGtkList list(gtk_container_get_children(GTK_CONTAINER(box)));
            for (GList* item = list; item; item = item->next)
            {
                GTKApplyStyle(GTK_WIDGET(item->data), style);
            }
        }
    }
    wxGCC_WARNING_RESTORE()
#endif
}

wxSize wxButton::DoGetBestSize() const
{
#ifdef __WXGTK4__
    // Under GTK4 the default button is not any bigger than the others: the
    // default indication is drawn from CSS inside the button's own allocation,
    // so there is nothing to compensate for here, see SetDefault().
    wxSize ret( wxAnyButton::DoGetBestSize() );
#else // !__WXGTK4__
    // the default button in wxGTK is bigger than the other ones because of an
    // extra border around it, but we don't want to take it into account in
    // our size calculations (otherwise the result is visually ugly), so
    // always return the size of non default button from here
    const bool isDefault = gtk_widget_has_default(m_widget) != 0;
    if ( isDefault )
    {
        // temporarily unset default flag
        gtk_widget_set_can_default(m_widget, FALSE);
    }

    wxSize ret( wxAnyButton::DoGetBestSize() );

    if ( isDefault )
    {
        // set it back again
        gtk_widget_set_can_default(m_widget, TRUE);
    }
#endif // __WXGTK4__/!__WXGTK4__

    if (!HasFlag(wxBU_EXACTFIT))
    {
        wxSize defaultSize = GetDefaultSize();
        if (ret.x < defaultSize.x)
            ret.x = defaultSize.x;
        if (ret.y < defaultSize.y)
            ret.y = defaultSize.y;
    }

    return ret;
}

// static
wxVisualAttributes
wxButton::GetClassDefaultAttributes(wxWindowVariant WXUNUSED(variant))
{
    return GetDefaultAttributesFromGTKWidget(gtk_button_new());
}
#endif // wxUSE_BUTTON
