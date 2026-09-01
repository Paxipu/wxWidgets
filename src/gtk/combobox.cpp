/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/combobox.cpp
// Purpose:
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_COMBOBOX

#include "wx/combobox.h"

#ifndef WX_PRECOMP
    #include "wx/intl.h"
    #include "wx/settings.h"
    #include "wx/textctrl.h"    // for wxEVT_TEXT
    #include "wx/arrstr.h"
#endif

#include "wx/gtk/private.h"
#include "wx/gtk/private/eventsdisabler.h"

// ----------------------------------------------------------------------------
// GTK callbacks
// ----------------------------------------------------------------------------

extern "C" {

#ifndef __WXGTK4__
static void
gtkcombobox_changed_callback( GtkWidget *WXUNUSED(widget), wxComboBox *combo )
{
    combo->SendSelectionChangedEvent(wxEVT_COMBOBOX);
}
#endif // !__WXGTK4__

#ifdef __WXGTK4__

#endif // __WXGTK4__

static void
gtkcombobox_popupshown_callback(GObject *WXUNUSED(gobject),
                                GParamSpec *WXUNUSED(param_spec),
                                wxComboBox *combo)
{
    gboolean isShown;
#ifdef __WXGTK4__
    // The drop button is a GtkMenuButton, and it is "active" exactly while its
    // popover is up.
    g_object_get( combo->GTKGetDropButton(), "active", &isShown, nullptr );
#else
    g_object_get( combo->m_widget, "popup-shown", &isShown, nullptr );
#endif
    wxCommandEvent event( isShown ? wxEVT_COMBOBOX_DROPDOWN
                                  : wxEVT_COMBOBOX_CLOSEUP,
                          combo->GetId() );
    event.SetEventObject( combo );

#ifndef __WXGTK3__
    // Process the close up event once the combobox is already closed with GTK+
    // 2, otherwise changing the combobox from its handler result in errors.
    if ( !isShown )
    {
        combo->GetEventHandler()->AddPendingEvent( event );
    }
    else
#endif // GTK+ < 3
    {
        combo->HandleWindowEvent( event );
    }
}

}

//-----------------------------------------------------------------------------
// wxComboBox
//-----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(wxComboBox, wxChoice)
    EVT_CHAR(wxComboBox::OnChar)

    EVT_MENU(wxID_CUT, wxComboBox::OnCut)
    EVT_MENU(wxID_COPY, wxComboBox::OnCopy)
    EVT_MENU(wxID_PASTE, wxComboBox::OnPaste)
    EVT_MENU(wxID_UNDO, wxComboBox::OnUndo)
    EVT_MENU(wxID_REDO, wxComboBox::OnRedo)
    EVT_MENU(wxID_CLEAR, wxComboBox::OnDelete)
    EVT_MENU(wxID_SELECTALL, wxComboBox::OnSelectAll)

    EVT_UPDATE_UI(wxID_CUT, wxComboBox::OnUpdateCut)
    EVT_UPDATE_UI(wxID_COPY, wxComboBox::OnUpdateCopy)
    EVT_UPDATE_UI(wxID_PASTE, wxComboBox::OnUpdatePaste)
    EVT_UPDATE_UI(wxID_UNDO, wxComboBox::OnUpdateUndo)
    EVT_UPDATE_UI(wxID_REDO, wxComboBox::OnUpdateRedo)
    EVT_UPDATE_UI(wxID_CLEAR, wxComboBox::OnUpdateDelete)
    EVT_UPDATE_UI(wxID_SELECTALL, wxComboBox::OnUpdateSelectAll)
wxEND_EVENT_TABLE()

wxComboBox::~wxComboBox()
{
    if (m_entry)
    {
        GTKDisconnect(m_entry);
        g_object_remove_weak_pointer(G_OBJECT(m_entry), (void**)&m_entry);
    }
}

void wxComboBox::Init()
{
    m_entry = nullptr;
}

bool wxComboBox::Create( wxWindow *parent, wxWindowID id,
                         const wxString& value,
                         const wxPoint& pos, const wxSize& size,
                         const wxArrayString& choices,
                         long style, const wxValidator& validator,
                         const wxString& name )
{
    wxCArrayString chs(choices);

    return Create( parent, id, value, pos, size, chs.GetCount(),
                   chs.GetStrings(), style, validator, name );
}

bool wxComboBox::Create( wxWindow *parent, wxWindowID id, const wxString& value,
                         const wxPoint& pos, const wxSize& size,
                         int n, const wxString choices[],
                         long style, const wxValidator& validator,
                         const wxString& name )
{
    if (!PreCreation( parent, pos, size ) ||
        !CreateBase( parent, id, pos, size, style, validator, name ))
    {
        wxFAIL_MSG( wxT("wxComboBox creation failed") );
        return false;
    }

    if (HasFlag(wxCB_SORT))
        m_strings = new wxGtkCollatedArrayString();

    GTKCreateComboBoxWidget();

    if (HasFlag(wxBORDER_NONE))
    {
        // Doesn't seem to work
        // g_object_set (m_widget, "has-frame", FALSE, nullptr);
    }

    GtkEntry * const entry = GetEntry();

    if ( entry )
    {
        // Set it up to trigger default item on enter key press
        gtk_entry_set_activates_default( entry,
                                         !HasFlag(wxTE_PROCESS_ENTER) );

        gtk_editable_set_editable(GTK_EDITABLE(entry), true);
#ifdef __WXGTK4__
        gtk_editable_set_width_chars(GTK_EDITABLE(entry), 0);
#elif defined(__WXGTK3__)
        gtk_entry_set_width_chars(entry, 0);
#endif
    }

    Append(n, choices);

    m_parent->DoAddChild( this );

    if ( entry )
        m_focusWidget = GTK_WIDGET( entry );

    if ( entry )
    {
        if (style & wxCB_READONLY)
        {
            // this will assert and do nothing if the value is not in our list
            // of strings which is the desired behaviour (for consistency with
            // wxMSW and also because it doesn't make sense to have a string
            // which is not a possible choice in a read-only combobox)
            SetStringSelection(value);
            gtk_editable_set_editable(GTK_EDITABLE(entry), false);
        }
        else // editable combobox
        {
            // any value is accepted, even if it's not in our list
#ifdef __WXGTK4__
            gtk_editable_set_text( GTK_EDITABLE(entry), value.utf8_str() );
#else
            gtk_entry_set_text( entry, value.utf8_str() );
#endif
        }

        GTKConnectChangedSignal();
        GTKConnectInsertTextSignal(entry);
        GTKConnectClipboardSignals(GTK_WIDGET(entry));
    }

    PostCreation(size);

#ifdef __WXGTK4__
    // There is no "changed" signal to connect to: the selection is the list's,
    // and a row being chosen is reported by GTKOnListActivated(). What is left
    // is the popup opening and closing, which the drop button's "active"
    // property reports.
    g_signal_connect (m_dropButton, "notify::active",
                      G_CALLBACK (gtkcombobox_popupshown_callback), this);
#else
    g_signal_connect_after (m_widget, "changed",
                        G_CALLBACK (gtkcombobox_changed_callback), this);

    if ( wx_is_at_least_gtk2(10) )
    {
        g_signal_connect (m_widget, "notify::popup-shown",
                          G_CALLBACK (gtkcombobox_popupshown_callback), this);
    }
#endif // __WXGTK4__/!__WXGTK4__

    return true;
}

#ifdef __WXGTK4__

extern "C" {

// The popover's list was clicked, or Enter was pressed on a row.
static void
wx_gtk_combo_row_activated(GtkListView*, guint position, wxComboBox* combo)
{
    combo->GTKOnListActivated(position);
}

} // extern "C"

void wxComboBox::GTKCreateComboBoxWidget()
{
    // GTK4 has no editable combo box at all. GtkComboBox is deprecated and
    // GtkDropDown, which replaces it, cannot be typed into -- so this is built
    // from the parts GTK4 does have, in the arrangement its own applications
    // use: an entry and a drop button side by side in a "linked" box, with the
    // list in the button's popover.
    //
    // The list is wxChoice's, over the item model wxChoice keeps, so every
    // item method inherited from that class works here unchanged.
    m_itemModel = gtk_string_list_new(nullptr);

    m_widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    g_object_ref(m_widget);
    gtk_widget_add_css_class(m_widget, "linked");

    m_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(m_entry), TRUE);
    gtk_box_append(GTK_BOX(m_widget), GTK_WIDGET(m_entry));
    g_object_add_weak_pointer(G_OBJECT(m_entry), (void**)&m_entry);

    m_dropButton = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(m_dropButton),
                                  "pan-down-symbolic");
    gtk_widget_set_can_focus(m_dropButton, FALSE);
    gtk_box_append(GTK_BOX(m_widget), m_dropButton);

    gtk_menu_button_set_popover(GTK_MENU_BUTTON(m_dropButton),
                                GTKCreateItemPopover());
    g_signal_connect(m_listView, "activate",
                     G_CALLBACK(wx_gtk_combo_row_activated), this);

    // A GtkComboBox with an entry moved the selection with Up and Down while
    // closed; the entry has the focus here, so the keys are taken there.
    GTKConnectSelectionKeys(GTK_WIDGET(m_entry));
}

GtkWidget* wxComboBox::GTKGetDropButton() const
{
    return m_dropButton;
}

void wxComboBox::GTKUpdateSelectionDisplay()
{
    if ( !m_entry )
        return;

    // gtk_combo_box_set_active() showed the chosen item in the entry, and a
    // great deal of wx depends on that: SetStringSelection(), the read-only
    // combo box, and GetValue() after a selection.
    const int sel = GTKGetSelection();

    wxGtkEventsDisabler<wxComboBox> noEvents(this);
    gtk_editable_set_text(GTK_EDITABLE(m_entry),
                          sel == wxNOT_FOUND
                              ? "" : GetString(unsigned(sel)).utf8_str().data());
}

wxEventType wxComboBox::GTKGetSelectionEventType() const
{
    return wxEVT_COMBOBOX;
}

GtkWidget* wxComboBox::GTKGetSizeChildPart() const
{
    return m_entry ? GTK_WIDGET(m_entry) : m_widget;
}

#else // !__WXGTK4__

void wxComboBox::GTKCreateComboBoxWidget()
{
#ifdef __WXGTK3__
    m_widget = gtk_combo_box_text_new_with_entry();
#else
    m_widget = gtk_combo_box_entry_new_text();
#endif
    g_object_ref(m_widget);

    m_entry = GTK_ENTRY(gtk_bin_get_child(GTK_BIN(m_widget)));
    g_object_add_weak_pointer(G_OBJECT(m_entry), (void**)&m_entry);
}

#endif // __WXGTK4__/!__WXGTK4__

GtkEditable *wxComboBox::GetEditable() const
{
    return GTK_EDITABLE(m_entry);
}

void wxComboBox::OnChar( wxKeyEvent &event )
{
    switch ( event.GetKeyCode() )
    {
        case WXK_RETURN:
            if ( HasFlag(wxTE_PROCESS_ENTER) && GetEntry() )
            {
                // GTK automatically selects an item if its in the list
                wxCommandEvent eventEnter(wxEVT_TEXT_ENTER, GetId());
                eventEnter.SetString( GetValue() );
                eventEnter.SetInt( GetSelection() );
                eventEnter.SetEventObject( this );

                if ( HandleWindowEvent(eventEnter) )
                {
                    // Catch GTK event so that GTK doesn't open the drop
                    // down list upon RETURN.
                    return;
                }

                // We disable built-in default button activation when
                // wxTE_PROCESS_ENTER is used, but we still should activate it
                // if the event wasn't handled, so do it from here.
                if ( ClickDefaultButtonIfPossible() )
                    return;
            }
            break;
    }

    event.Skip();
}

void wxComboBox::GTKDisableEvents()
{
    EnableTextChangedEvents(false);

#ifdef __WXGTK4__
    g_signal_handlers_block_by_func(m_dropButton,
        (gpointer)gtkcombobox_popupshown_callback, this);
#else
    g_signal_handlers_block_by_func(m_widget,
        (gpointer)gtkcombobox_changed_callback, this);
    g_signal_handlers_block_by_func(m_widget,
        (gpointer)gtkcombobox_popupshown_callback, this);
#endif
}

void wxComboBox::GTKEnableEvents()
{
    EnableTextChangedEvents(true);

#ifdef __WXGTK4__
    g_signal_handlers_unblock_by_func(m_dropButton,
        (gpointer)gtkcombobox_popupshown_callback, this);
#else
    g_signal_handlers_unblock_by_func(m_widget,
        (gpointer)gtkcombobox_changed_callback, this);
    g_signal_handlers_unblock_by_func(m_widget,
        (gpointer)gtkcombobox_popupshown_callback, this);
#endif
}

GtkWidget* wxComboBox::GetConnectWidget() const
{
    return GTK_WIDGET( GetEntry() );
}

#ifndef __WXGTK4__
GdkWindow* wxComboBox::GTKGetWindow(wxArrayGdkWindows& /* windows */) const
{
#ifdef __WXGTK3__
    return GTKFindWindow(GTK_WIDGET(GetEntry()));
#else
    return gtk_entry_get_text_window(GetEntry());
#endif
}
#endif // !__WXGTK4__

// static
wxVisualAttributes
wxComboBox::GetClassDefaultAttributes(wxWindowVariant WXUNUSED(variant))
{
#ifdef __WXGTK4__
    return GetDefaultAttributesFromGTKWidget(gtk_entry_new(), true);
#elif defined(__WXGTK3__)
    return GetDefaultAttributesFromGTKWidget(gtk_combo_box_new_with_entry(), true);
#else
    return GetDefaultAttributesFromGTKWidget(gtk_combo_box_entry_new(), true);
#endif
}

void wxComboBox::Clear()
{
    wxTextEntry::Clear();
    wxItemContainer::Clear();
}

void wxComboBox::SetValue(const wxString& value)
{
    if ( HasFlag(wxCB_READONLY) )
        SetStringSelection(value);
    else
        wxTextEntry::SetValue(value);
}

void wxComboBox::SetString(unsigned int n, const wxString& text)
{
    wxChoice::SetString(n, text);

    if ( static_cast<int>(n) == GetSelection() )
    {
        // We also need to update the currently shown text, for consistency
        // with wxMSW and also because it makes sense as leaving the old string
        // in the text but not in the list would be confusing to the user.
        SetValue(text);

        // And we need to keep the selection unchanged, modifying the item is
        // not supposed to deselect it.
        SetSelection(n);
    }
}

// ----------------------------------------------------------------------------
// standard event handling
// ----------------------------------------------------------------------------

void wxComboBox::OnCut(wxCommandEvent& WXUNUSED(event))
{
    Cut();
}

void wxComboBox::OnCopy(wxCommandEvent& WXUNUSED(event))
{
    Copy();
}

void wxComboBox::OnPaste(wxCommandEvent& WXUNUSED(event))
{
    Paste();
}

void wxComboBox::OnUndo(wxCommandEvent& WXUNUSED(event))
{
    Undo();
}

void wxComboBox::OnRedo(wxCommandEvent& WXUNUSED(event))
{
    Redo();
}

void wxComboBox::OnDelete(wxCommandEvent& WXUNUSED(event))
{
    RemoveSelection();
}

void wxComboBox::OnSelectAll(wxCommandEvent& WXUNUSED(event))
{
    SelectAll();
}

void wxComboBox::OnUpdateCut(wxUpdateUIEvent& event)
{
    event.Enable( CanCut() );
}

void wxComboBox::OnUpdateCopy(wxUpdateUIEvent& event)
{
    event.Enable( CanCopy() );
}

void wxComboBox::OnUpdatePaste(wxUpdateUIEvent& event)
{
    event.Enable( CanPaste() );
}

void wxComboBox::OnUpdateUndo(wxUpdateUIEvent& event)
{
    event.Enable( CanUndo() );
}

void wxComboBox::OnUpdateRedo(wxUpdateUIEvent& event)
{
    event.Enable( CanRedo() );
}

void wxComboBox::OnUpdateDelete(wxUpdateUIEvent& event)
{
    event.Enable(HasSelection() && IsEditable()) ;
}

void wxComboBox::OnUpdateSelectAll(wxUpdateUIEvent& event)
{
    event.Enable(!wxTextEntry::IsEmpty());
}

void wxComboBox::Popup()
{
#ifdef __WXGTK4__
    gtk_menu_button_popup( GTK_MENU_BUTTON(m_dropButton) );
#else
    gtk_combo_box_popup( GTK_COMBO_BOX(m_widget) );
#endif
}

void wxComboBox::Dismiss()
{
#ifdef __WXGTK4__
    gtk_menu_button_popdown( GTK_MENU_BUTTON(m_dropButton) );
#else
    gtk_combo_box_popdown( GTK_COMBO_BOX(m_widget) );
#endif
}

wxSize wxComboBox::DoGetSizeFromTextSize(int xlen, int ylen) const
{
    wxSize tsize( wxChoice::DoGetSizeFromTextSize(xlen, ylen) );

    GtkEntry* entry = GetEntry();
    if (entry)
    {
        // Add the margins we have previously set, but only the horizontal border
        // as vertical one has been taken account in the previous call.
        // Also get other GTK+ margins.
        tsize.IncBy(GTKGetEntryMargins(entry).x, 0);
    }

    return tsize;
}

#endif // wxUSE_COMBOBOX
