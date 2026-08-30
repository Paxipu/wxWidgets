/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/checklst.cpp
// Purpose:
// Author:      Robert Roebling
// Modified by: Ryan Norton (Native GTK2.0+ checklist)
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_CHECKLISTBOX

#include "wx/checklst.h"

#include "wx/gtk/private.h"
#include "wx/gtk/private/treeview.h"
#include "wx/gtk/private/treeentry_gtk.h"

//-----------------------------------------------------------------------------
// "toggled"
//-----------------------------------------------------------------------------
#ifndef __WXGTK4__
extern "C" {
static void gtk_checklist_toggled(GtkCellRendererToggle * WXUNUSED(renderer),
                                  gchar                 *stringpath,
                                  wxCheckListBox        *listbox)
{
    wxCHECK_RET( listbox->m_treeview != nullptr, wxT("invalid listbox") );

    wxGtkTreePath path(stringpath);
    wxCommandEvent new_event( wxEVT_CHECKLISTBOX,
                              listbox->GetId() );
    new_event.SetEventObject( listbox );
    new_event.SetInt( gtk_tree_path_get_indices(path)[0] );
    new_event.SetString( listbox->GetString( new_event.GetInt() ));
    listbox->Check( new_event.GetInt(), !listbox->IsChecked(new_event.GetInt()));
    listbox->HandleWindowEvent( new_event );
}
}
#endif // !__WXGTK4__

//-----------------------------------------------------------------------------
// wxCheckListBox
//-----------------------------------------------------------------------------

wxCheckListBox::wxCheckListBox() : wxCheckListBoxBase()
{
    m_hasCheckBoxes = true;
}

wxCheckListBox::wxCheckListBox(wxWindow *parent, wxWindowID id,
                               const wxPoint& pos,
                               const wxSize& size,
                               int nStrings,
                               const wxString *choices,
                               long style,
                               const wxValidator& validator,
                               const wxString& name )
{
    m_hasCheckBoxes = true;
    wxListBox::Create( parent, id, pos, size, nStrings, choices, style, validator, name );
}

wxCheckListBox::wxCheckListBox(wxWindow *parent, wxWindowID id,
                               const wxPoint& pos,
                               const wxSize& size,
                               const wxArrayString& choices,
                               long style,
                               const wxValidator& validator,
                               const wxString& name )
{
    m_hasCheckBoxes = true;
    wxListBox::Create( parent, id, pos, size, choices,
                       style, validator, name );
}

void wxCheckListBox::DoCreateCheckList()
{
#ifdef __WXGTK4__
    // Nothing to do: GtkListView has no columns, so the check button is part
    // of the row widget the list item factory builds, and m_hasCheckBoxes --
    // already set by the time wxListBox::Create() gets here -- is what tells
    // it to add one. The toggle is reported from there too, since only the
    // GtkListItem knows which row a recycled row widget currently shows.
#else
    //Create the checklist in our treeview and set up events for it
    GtkCellRenderer* renderer =
        gtk_cell_renderer_toggle_new();
    GtkTreeViewColumn* column =
        gtk_tree_view_column_new_with_attributes( "", renderer,
                                                  "active", 0,
                                                  nullptr );
    gtk_tree_view_column_set_fixed_width(column, 22);

    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_clickable(column, TRUE);

    g_signal_connect (renderer, "toggled",
                      G_CALLBACK (gtk_checklist_toggled),
                      this);

    gtk_tree_view_append_column(m_treeview, column);
#endif // __WXGTK4__/!__WXGTK4__
}

bool wxCheckListBox::IsChecked(unsigned int index) const
{
#ifdef __WXGTK4__
    wxCHECK_MSG( m_listview != nullptr, false, wxT("invalid checklistbox") );

    wxTreeEntry* const entry = GTKGetEntry(index);
    wxCHECK_MSG( entry, false, wxT("invalid checklistbox index") );

    return wx_tree_entry_get_checked(entry) != 0;
#else
    wxCHECK_MSG( m_treeview != nullptr, false, wxT("invalid checklistbox") );

    GtkTreeIter iter;
    gboolean res = gtk_tree_model_iter_nth_child(
                        GTK_TREE_MODEL(m_liststore),
                        &iter, nullptr, //nullptr = parent = get first
                        index
                   );
    if(!res)
        return false;

    GValue value = G_VALUE_INIT;
    gtk_tree_model_get_value(GTK_TREE_MODEL(m_liststore),
                             &iter,
                             0, //column
                             &value);

    return g_value_get_boolean(&value) != 0;
#endif // __WXGTK4__/!__WXGTK4__
}

void wxCheckListBox::Check(unsigned int index, bool check)
{
#ifdef __WXGTK4__
    wxCHECK_RET( m_listview != nullptr, wxT("invalid checklistbox") );

    wxTreeEntry* const entry = GTKGetEntry(index);
    wxCHECK_RET( entry, wxT("invalid checklistbox index") );

    if ( (wx_tree_entry_get_checked(entry) != 0) == check )
        return;

    wx_tree_entry_set_checked(entry, check);

    // Re-bind the row so the check button follows. SetString() does the same
    // thing for the same reason: a GListModel has no row-changed signal.
    SetString(index, GetString(index));
#else
    wxCHECK_RET( m_treeview != nullptr, wxT("invalid checklistbox") );

    GtkTreeIter iter;
    gboolean res = gtk_tree_model_iter_nth_child(
                        GTK_TREE_MODEL(m_liststore),
                        &iter, nullptr, //nullptr = parent = get first
                        index
                   );
    if(!res)
        return;

    gtk_list_store_set(m_liststore,
                       &iter,
                       0, //column
                       check ? TRUE : FALSE, -1);
#endif // __WXGTK4__/!__WXGTK4__
}

int wxCheckListBox::GetItemHeight() const
{
#ifdef __WXGTK4__
    wxCHECK_MSG( m_listview != nullptr, 0, wxT("invalid listbox"));

    // No columns to ask, so measure what a row actually needs. A check button
    // is the tallest thing in one, and the row adds nothing of its own.
    int height = 0;
    GtkWidget* const check = gtk_check_button_new();
    g_object_ref_sink(check);
    gtk_widget_measure(check, GTK_ORIENTATION_VERTICAL, -1,
                       &height, nullptr, nullptr, nullptr);
    g_object_unref(check);

    return height;
#else
    wxCHECK_MSG( m_treeview != nullptr, 0, wxT("invalid listbox"));

    gint height;
    gtk_tree_view_column_cell_get_size(
        gtk_tree_view_get_column(m_treeview, 0),
                                       nullptr, nullptr, nullptr, nullptr,
                                       &height);
    return height;
#endif // __WXGTK4__/!__WXGTK4__
}

#endif //wxUSE_CHECKLISTBOX
