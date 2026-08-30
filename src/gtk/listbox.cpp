/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/listbox.cpp
// Purpose:
// Author:      Robert Roebling
// Modified By: Ryan Norton (GtkTreeView implementation)
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_LISTBOX

#include "wx/listbox.h"

#ifndef WX_PRECOMP
    #include "wx/dynarray.h"
    #include "wx/intl.h"
    #include "wx/log.h"
    #include "wx/utils.h"
    #include "wx/settings.h"
    #include "wx/checklst.h"
    #include "wx/arrstr.h"
#endif

#if wxUSE_TOOLTIPS
    #include "wx/tooltip.h"
#endif

#include "wx/gtk/private.h"
#include "wx/gtk/private/eventsdisabler.h"
#include "wx/gtk/private/object.h"
#include "wx/gtk/private/treeentry_gtk.h"
#include "wx/gtk/private/treeview.h"
#include "wx/gtk/private/gtk3-compat.h"

//-----------------------------------------------------------------------------
// data
//-----------------------------------------------------------------------------

extern bool           g_blockEventsOnDrag;
extern bool           g_blockEventsOnScroll;



//-----------------------------------------------------------------------------
// Macro to tell which row the strings are in (1 if native checklist, 0 if not)
//-----------------------------------------------------------------------------

#if wxUSE_CHECKLISTBOX
#   define WXLISTBOX_DATACOLUMN_ARG(x)  (x->m_hasCheckBoxes ? 1 : 0)
#else
#   define WXLISTBOX_DATACOLUMN_ARG(x)  (0)
#endif // wxUSE_CHECKLISTBOX

#define WXLISTBOX_DATACOLUMN    WXLISTBOX_DATACOLUMN_ARG(this)

// ----------------------------------------------------------------------------
// helper functions
// ----------------------------------------------------------------------------

namespace
{

#ifndef __WXGTK4__
// Return the entry for the given listbox item.
wxTreeEntry *
GetEntry(GtkListStore *store, GtkTreeIter *iter, const wxListBox *listbox)
{
    wxTreeEntry* entry;
    gtk_tree_model_get(GTK_TREE_MODEL(store),
                       iter,
                       WXLISTBOX_DATACOLUMN_ARG(listbox),
                       &entry,
                       -1);
    g_object_unref(entry);
    return entry;
}
#endif // !__WXGTK4__

} // anonymous namespace

//-----------------------------------------------------------------------------
// "row-activated"
//-----------------------------------------------------------------------------

extern "C" {
#ifdef __WXGTK4__
// GtkListView reports activation as "activate" with the position directly,
// rather than as "row-activated" with a GtkTreePath to be decoded.
static void
gtk_listbox_row_activated_callback(GtkListView * WXUNUSED(listview),
                                   guint         position,
                                   wxListBox    *listbox)
{
    if (g_blockEventsOnDrag) return;
    if (g_blockEventsOnScroll) return;

    listbox->GTKOnActivated(int(position));
}
#else
static void
gtk_listbox_row_activated_callback(GtkTreeView        * WXUNUSED(treeview),
                                   GtkTreePath        *path,
                                   GtkTreeViewColumn  * WXUNUSED(col),
                                   wxListBox          *listbox)
{
    if (g_blockEventsOnDrag) return;
    if (g_blockEventsOnScroll) return;

    // This is triggered by either a double-click or a space press

    int sel = gtk_tree_path_get_indices(path)[0];

    listbox->GTKOnActivated(sel);
}
#endif // __WXGTK4__/!__WXGTK4__
}

//-----------------------------------------------------------------------------
// "changed"
//-----------------------------------------------------------------------------

extern "C" {
#ifdef __WXGTK4__
// GtkSelectionModel::selection-changed carries the range that changed; wx
// only needs to know that something did.
static void
gtk_listitem_changed_callback(GtkSelectionModel * WXUNUSED(selection),
                              guint WXUNUSED(position),
                              guint WXUNUSED(n_items),
                              wxListBox *listbox )
{
    if (g_blockEventsOnDrag) return;

    listbox->GTKOnSelectionChanged();
}
#else
static void
gtk_listitem_changed_callback(GtkTreeSelection * WXUNUSED(selection),
                              wxListBox *listbox )
{
    if (g_blockEventsOnDrag) return;

    listbox->GTKOnSelectionChanged();
}
#endif // __WXGTK4__/!__WXGTK4__

}

//-----------------------------------------------------------------------------
// "key_press_event"
//-----------------------------------------------------------------------------

// Shared by both key handlers below, which differ only in how GTK tells them
// which key was pressed.
static gboolean wxGTKListBoxHandleKey(wxListBox* listbox, guint keyval)
{
    if ((keyval == GDK_KEY_Return) ||
        (keyval == GDK_KEY_ISO_Enter) ||
        (keyval == GDK_KEY_KP_Enter))
    {
        int index = -1;
        if (!listbox->HasMultipleSelection())
            index = listbox->GetSelection();
        else
        {
            wxArrayInt sels;
            if (listbox->GetSelections( sels ) < 1)
                return FALSE;
            index = sels[0];
        }

        if (index != wxNOT_FOUND)
        {
            listbox->GTKOnActivated(index);

//          wxMac and wxMSW always invoke default action
//          if (!ret)
            {
                // DClick not handled -> invoke default action
                wxWindow *tlw = wxGetTopLevelParent( listbox );
                if (tlw)
                {
                    GtkWindow *gtk_window = GTK_WINDOW( tlw->GetHandle() );
                    if (gtk_window)
                        gtk_window_activate_default( gtk_window );
                }
            }

            // Always intercept, otherwise we'd get another dclick
            // event from row_activated
            return TRUE;
        }
    }

    return FALSE;
}

extern "C" {
#ifdef __WXGTK4__
// GTK4 has no key-press-event: keys arrive through a GtkEventControllerKey.
static gboolean
gtk_listbox_key_press_callback( GtkEventControllerKey* WXUNUSED(controller),
                                guint keyval,
                                guint WXUNUSED(keycode),
                                GdkModifierType WXUNUSED(state),
                                wxListBox *listbox )
{
    return wxGTKListBoxHandleKey(listbox, keyval);
}
#else
static gboolean
gtk_listbox_key_press_callback( GtkWidget *WXUNUSED(widget),
                                GdkEventKey *gdk_event,
                                wxListBox *listbox )
{
    return wxGTKListBoxHandleKey(listbox, gdk_event->keyval);
}
#endif // __WXGTK4__/!__WXGTK4__
}

//-----------------------------------------------------------------------------
// GtkTreeEntry destruction (to destroy client data)
//-----------------------------------------------------------------------------

extern "C" {
static void tree_entry_destroy_cb(wxTreeEntry* entry,
                                      wxListBox* listbox)
{
    if (listbox->HasClientObjectData())
    {
        void* userdata = wx_tree_entry_get_userdata(entry);
        if (userdata)
            delete (wxClientData *)userdata;
    }
}
}

//-----------------------------------------------------------------------------
// Sorting callback (standard CmpNoCase return value)
//-----------------------------------------------------------------------------

#ifndef __WXGTK4__
extern "C" {
static int
sort_callback(GtkTreeModel*, GtkTreeIter* a, GtkTreeIter* b, void* data)
{
    wxListBox* listbox = static_cast<wxListBox*>(data);
    wxTreeEntry* entry1 = GetEntry(listbox->m_liststore, a, listbox);
    wxCHECK_MSG(entry1, 0, wxT("Could not get first entry"));

    wxTreeEntry* entry2 = GetEntry(listbox->m_liststore, b, listbox);
    wxCHECK_MSG(entry2, 0, wxT("Could not get second entry"));

    //We compare collate keys here instead of calling g_utf8_collate
    //as it is rather slow (and even the docs recommend this)
    return strcmp(wx_tree_entry_get_collate_key(entry1),
                  wx_tree_entry_get_collate_key(entry2)) >= 0;
}
}

//-----------------------------------------------------------------------------
// Searching callback (TRUE == not equal, FALSE == equal)
//-----------------------------------------------------------------------------

extern "C" {
static gboolean
search_callback(GtkTreeModel*, int, const char* key, GtkTreeIter* iter, void* data)
{
    wxListBox* listbox = static_cast<wxListBox*>(data);
    wxTreeEntry* entry = GetEntry(listbox->m_liststore, iter, listbox);
    wxCHECK_MSG(entry, true, "could not get entry");

    wxGtkString keyc(g_utf8_collate_key(key, -1));

    return strncmp(keyc, wx_tree_entry_get_collate_key(entry), strlen(keyc));
}
}
#else // __WXGTK4__

// GtkSortListModel takes a GtkSorter rather than a compare function on the
// store, so the same collate-key comparison is wrapped in a GtkCustomSorter.
// Note the sign: the GTK3 callback returned ">= 0" as a boolean, which sorts
// ascending; a GtkSorter wants a real three-way result.
extern "C" {
static int
wx_listbox_sort_func(gconstpointer a, gconstpointer b, gpointer WXUNUSED(data))
{
    wxTreeEntry* const entry1 = WX_TREE_ENTRY(const_cast<gpointer>(a));
    wxTreeEntry* const entry2 = WX_TREE_ENTRY(const_cast<gpointer>(b));

    // Collate keys rather than g_utf8_collate(), which is much slower and
    // which the GLib documentation recommends against for repeated use.
    const int cmp = strcmp(wx_tree_entry_get_collate_key(entry1),
                           wx_tree_entry_get_collate_key(entry2));
    if ( cmp )
        return cmp;

    // The collate key is case-folded, so "AAA", "Aaa" and "aaa" all tie. GTK3
    // got an order out of that by accident: its callback returned ">= 0",
    // i.e. 1 for equal elements, which reverses a run of them. A GtkSorter is
    // asked for a real three-way answer and GtkSortListModel is stable, so
    // ties would keep insertion order instead -- a different order for the
    // same list, which the wxLB_SORT test notices.
    //
    // Break the tie on the labels themselves, which puts upper case first and
    // is at least a stated rule rather than a property of the sort algorithm.
    return strcmp(wx_tree_entry_get_label(entry1),
                  wx_tree_entry_get_label(entry2));
}
}

#endif // !__WXGTK4__/__WXGTK4__

//-----------------------------------------------------------------------------
// wxListBox
//-----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// construction
// ----------------------------------------------------------------------------

void wxListBox::Init()
{
#ifdef __WXGTK4__
    m_listview = nullptr;
    m_store = nullptr;
    m_selection = nullptr;
    m_model = nullptr;
#else
    m_treeview = nullptr;
#endif
#if wxUSE_CHECKLISTBOX
    m_hasCheckBoxes = false;
#endif // wxUSE_CHECKLISTBOX
}

#ifdef __WXGTK4__

// The list item factory. One GtkLabel per row, wrapped in a box so that
// wxCheckListBox can put a check button beside it without a second factory.
extern "C" {

#if wxUSE_CHECKLISTBOX
// The check button in a row was clicked. Which row that is has to come from
// the GtkListItem the row was bound to, since a row widget is recycled and
// its position changes as the list scrolls.
static void wx_listbox_check_toggled(GtkCheckButton* check, wxListBox* listbox)
{
    GtkWidget* const box = gtk_widget_get_parent(GTK_WIDGET(check));
    if ( !box )
        return;

    GtkListItem* const item =
        GTK_LIST_ITEM(g_object_get_data(G_OBJECT(box), "wx-item"));
    if ( !item )
        return;

    const int n = int(gtk_list_item_get_position(item));
    if ( n < 0 || unsigned(n) >= listbox->GetCount() )
        return;

    wxCheckListBox* const clb = static_cast<wxCheckListBox*>(listbox);

    // Check() writes the state back and re-binds the row, which would call us
    // again through set_active(); bind() blocks this handler for that reason.
    clb->Check(n, gtk_check_button_get_active(check));

    wxCommandEvent event(wxEVT_CHECKLISTBOX, listbox->GetId());
    event.SetEventObject(listbox);
    event.SetInt(n);
    event.SetString(listbox->GetString(n));
    listbox->HandleWindowEvent(event);
}
#endif // wxUSE_CHECKLISTBOX

static void wx_listbox_item_setup(GtkSignalListItemFactory* WXUNUSED(factory),
                                  GtkListItem* item,
                                  wxListBox* listbox)
{
    GtkWidget* const box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* check = nullptr;

#if wxUSE_CHECKLISTBOX
    if ( listbox->m_hasCheckBoxes )
    {
        check = gtk_check_button_new();
        // The row's own click handling drives selection, so the check button
        // must not take the focus with it.
        gtk_widget_set_focus_on_click(check, FALSE);
        g_signal_connect(check, "toggled",
                         G_CALLBACK(wx_listbox_check_toggled), listbox);
        gtk_box_append(GTK_BOX(box), check);
    }
#else
    wxUnusedVar(listbox);
#endif

    GtkWidget* const label = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_widget_set_hexpand(label, TRUE);
    gtk_box_append(GTK_BOX(box), label);

    // Looked up again in bind(); the box is what GtkListItem owns.
    g_object_set_data(G_OBJECT(box), "wx-label", label);
    g_object_set_data(G_OBJECT(box), "wx-check", check);
    // Lets DoListHitTest() get from the picked widget back to its position.
    g_object_set_data(G_OBJECT(box), "wx-item", item);

    gtk_list_item_set_child(item, box);
}

static void wx_listbox_item_bind(GtkSignalListItemFactory* WXUNUSED(factory),
                                 GtkListItem* item,
                                 wxListBox* listbox)
{
    GtkWidget* const box = gtk_list_item_get_child(item);
    if ( !box )
        return;

    wxTreeEntry* const entry = WX_TREE_ENTRY(gtk_list_item_get_item(item));
    if ( !entry )
        return;

    GtkWidget* const label =
        GTK_WIDGET(g_object_get_data(G_OBJECT(box), "wx-label"));
    gtk_label_set_text(GTK_LABEL(label), wx_tree_entry_get_label(entry));

#if wxUSE_CHECKLISTBOX
    GtkWidget* const check =
        GTK_WIDGET(g_object_get_data(G_OBJECT(box), "wx-check"));
    if ( check )
    {
        // set_active() emits "toggled", which would report a click the user
        // never made -- and, through Check(), re-enter binding.
        g_signal_handlers_block_by_func(
            check, (gpointer)wx_listbox_check_toggled, listbox);

        gtk_check_button_set_active(GTK_CHECK_BUTTON(check),
                                    wx_tree_entry_get_checked(entry));

        g_signal_handlers_unblock_by_func(
            check, (gpointer)wx_listbox_check_toggled, listbox);
    }
#endif
    wxUnusedVar(listbox);
}

} // extern "C"

namespace
{

// Where is this item in the model? g_list_store_find() only works on a
// GListStore, and with wxLB_SORT the model wx counts positions in is a
// GtkSortListModel wrapping one, so this walks it. Compares by identity, which
// is what is wanted: two entries with the same label are still two items.
bool wxGTKFindInModel(GListModel* model, gpointer item, guint* pos)
{
    const guint n = g_list_model_get_n_items(model);
    for ( guint i = 0; i < n; i++ )
    {
        wxGtkObject<GObject> candidate(G_OBJECT(g_list_model_get_item(model, i)));
        if ( static_cast<gpointer>(candidate.get()) == item )
        {
            *pos = i;
            return true;
        }
    }

    return false;
}

} // anonymous namespace

#endif // __WXGTK4__

bool wxListBox::Create( wxWindow *parent, wxWindowID id,
                        const wxPoint &pos, const wxSize &size,
                        const wxArrayString& choices,
                        long style, const wxValidator& validator,
                        const wxString &name )
{
    wxCArrayString chs(choices);

    return Create( parent, id, pos, size, chs.GetCount(), chs.GetStrings(),
                   style, validator, name );
}

bool wxListBox::Create( wxWindow *parent, wxWindowID id,
                        const wxPoint &pos, const wxSize &size,
                        int n, const wxString choices[],
                        long style, const wxValidator& validator,
                        const wxString &name )
{
    if (!PreCreation( parent, pos, size ) ||
        !CreateBase( parent, id, pos, size, style, validator, name ))
    {
        wxFAIL_MSG( wxT("wxListBox creation failed") );
        return false;
    }

    m_widget = gtk_scrolled_window_new( nullptr, nullptr );
    g_object_ref(m_widget);

    GtkPolicyType vPolicy = GTK_POLICY_AUTOMATIC;
    if (style & wxLB_ALWAYS_SB)
        vPolicy = GTK_POLICY_ALWAYS;
    else if (style & wxLB_NO_SB)
        vPolicy = GTK_POLICY_NEVER;

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(m_widget),
        GTK_POLICY_AUTOMATIC, vPolicy);


    GTKScrolledWindowSetBorder(m_widget, style);

#ifdef __WXGTK4__
    // ---- GTK4: GListStore of wxTreeEntry -> [sorter] -> selection -> view ---

    m_store = g_list_store_new(WX_TYPE_TREE_ENTRY);

    if ( HasFlag(wxLB_SORT) )
    {
        GtkSorter* const sorter = GTK_SORTER(
            gtk_custom_sorter_new(wx_listbox_sort_func, nullptr, nullptr));
        // Takes its own reference on the store, so ours is still ours.
        m_model = G_LIST_MODEL(
            gtk_sort_list_model_new(G_LIST_MODEL(g_object_ref(m_store)),
                                    sorter));
    }
    else
    {
        m_model = G_LIST_MODEL(g_object_ref(m_store));
    }

    if ( style & (wxLB_MULTIPLE | wxLB_EXTENDED) )
    {
        m_selection = GTK_SELECTION_MODEL(
            gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(m_model))));
    }
    else
    {
        m_windowStyle |= wxLB_SINGLE;

        GtkSingleSelection* const sel =
            gtk_single_selection_new(G_LIST_MODEL(g_object_ref(m_model)));
        // wxListBox starts with nothing selected and SetSelection(wxNOT_FOUND)
        // has to work, which is GTK_SELECTION_BROWSE's behaviour under GTK3
        // only by accident. GtkSingleSelection says it explicitly.
        gtk_single_selection_set_autoselect(sel, FALSE);
        gtk_single_selection_set_can_unselect(sel, TRUE);
        m_selection = GTK_SELECTION_MODEL(sel);
    }

    {
        GtkListItemFactory* const factory = gtk_signal_list_item_factory_new();
        g_signal_connect(factory, "setup",
                         G_CALLBACK(wx_listbox_item_setup), this);
        g_signal_connect(factory, "bind",
                         G_CALLBACK(wx_listbox_item_bind), this);

        // Both are consumed by the view.
        m_listview = GTK_LIST_VIEW(
            gtk_list_view_new(GTK_SELECTION_MODEL(g_object_ref(m_selection)),
                              factory));
    }

    gtk_list_view_set_single_click_activate(m_listview, FALSE);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_widget),
                                  GTK_WIDGET(m_listview));

    m_focusWidget = GTK_WIDGET(m_listview);

    Append(n, choices); // insert initial items

    g_signal_connect_after(m_listview, "activate",
                     G_CALLBACK(gtk_listbox_row_activated_callback), this);

    {
        GtkEventController* const key = gtk_event_controller_key_new();
        g_signal_connect (key, "key-pressed",
                          G_CALLBACK (gtk_listbox_key_press_callback), this);
        gtk_widget_add_controller(GTK_WIDGET(m_listview), key);
    }

    m_parent->DoAddChild( this );

    PostCreation(size);

    g_signal_connect_after (m_selection, "selection-changed",
                            G_CALLBACK (gtk_listitem_changed_callback), this);

    return true;
}
#else // !__WXGTK4__
    m_treeview = GTK_TREE_VIEW( gtk_tree_view_new( ) );

    //wxListBox doesn't have a header :)
    //NB: If enabled SetFirstItem doesn't work correctly
    gtk_tree_view_set_headers_visible(m_treeview, FALSE);

#if wxUSE_CHECKLISTBOX
    if(m_hasCheckBoxes)
        ((wxCheckListBox*)this)->DoCreateCheckList();
#endif // wxUSE_CHECKLISTBOX

    // Create the data column
    gtk_tree_view_insert_column_with_attributes(m_treeview, -1, "",
                                                gtk_cell_renderer_text_new(),
                                                "text",
                                                WXLISTBOX_DATACOLUMN, nullptr);

    // Now create+set the model (GtkListStore) - first argument # of columns
#if wxUSE_CHECKLISTBOX
    if(m_hasCheckBoxes)
        m_liststore = gtk_list_store_new(2, G_TYPE_BOOLEAN,
                                            WX_TYPE_TREE_ENTRY);
    else
#endif
        m_liststore = gtk_list_store_new(1, WX_TYPE_TREE_ENTRY);

    gtk_tree_view_set_model(m_treeview, GTK_TREE_MODEL(m_liststore));

    g_object_unref (m_liststore); //free on treeview destruction

    // Disable the pop-up textctrl that enables searching - note that
    // the docs specify that even if this disabled (which we are doing)
    // the user can still have it through the start-interactive-search
    // key binding...either way we want to provide a searchequal callback
    // NB: If this is enabled a doubleclick event (activate) gets sent
    //     on a successful search
    gtk_tree_view_set_search_column(m_treeview, WXLISTBOX_DATACOLUMN);
    gtk_tree_view_set_search_equal_func(m_treeview, search_callback, this, nullptr);

    gtk_tree_view_set_enable_search(m_treeview, FALSE);

    GtkSelectionMode mode;
    // GTK_SELECTION_EXTENDED is a deprecated synonym for GTK_SELECTION_MULTIPLE
    if ( style & (wxLB_MULTIPLE | wxLB_EXTENDED) )
    {
        mode = GTK_SELECTION_MULTIPLE;
    }
    else // no multi-selection flags specified
    {
        m_windowStyle |= wxLB_SINGLE;

        // Notice that we must use BROWSE and not GTK_SELECTION_SINGLE because
        // the latter allows to not select any items at all while a single
        // selection listbox is supposed to always have a selection (at least
        // once the user selected something, it might not have any initially).
        mode = GTK_SELECTION_BROWSE;
    }

    GtkTreeSelection* selection = gtk_tree_view_get_selection( m_treeview );
    gtk_tree_selection_set_mode( selection, mode );

    // Handle sortable stuff
    if(HasFlag(wxLB_SORT))
    {
        // Setup sorting in ascending (wx) order
        gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(m_liststore),
                                             WXLISTBOX_DATACOLUMN,
                                             GTK_SORT_ASCENDING);

        // Set the sort callback
        gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(m_liststore),
                                        WXLISTBOX_DATACOLUMN,
                                        sort_callback,
                                        this, //userdata
                                        nullptr //"destroy notifier"
                                       );
    }


#ifdef __WXGTK4__
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(m_widget),
                                  GTK_WIDGET(m_treeview));
#else
    gtk_container_add (GTK_CONTAINER (m_widget), GTK_WIDGET(m_treeview) );
#endif

    gtk_widget_show( GTK_WIDGET(m_treeview) );
    m_focusWidget = GTK_WIDGET(m_treeview);

    Append(n, choices); // insert initial items

    // generate dclick events
    g_signal_connect_after(m_treeview, "row-activated",
                     G_CALLBACK(gtk_listbox_row_activated_callback), this);

    // for intercepting dclick generation by <ENTER>
#ifdef __WXGTK4__
    {
        GtkEventController* const key = gtk_event_controller_key_new();
        g_signal_connect (key, "key-pressed",
                          G_CALLBACK (gtk_listbox_key_press_callback), this);
        gtk_widget_add_controller(GTK_WIDGET(m_treeview), key);
    }
#else
    g_signal_connect (m_treeview, "key_press_event",
                      G_CALLBACK (gtk_listbox_key_press_callback),
                           this);
#endif
    m_parent->DoAddChild( this );

    PostCreation(size);

    g_signal_connect_after (selection, "changed",
                            G_CALLBACK (gtk_listitem_changed_callback), this);

    return true;
}
#endif // __WXGTK4__/!__WXGTK4__

wxListBox::~wxListBox()
{
#ifdef __WXGTK4__
    if (m_listview)
    {
        GTKDisconnect(m_listview);
        if (m_selection)
            GTKDisconnect(m_selection);
    }

    // The entries can outlive this object. The list view's bound rows and the
    // selection model hold references to them and release those on a later
    // main loop pass, by which time wxListBox is gone -- and their destroy
    // callback takes a wxListBox*, so it went through a dangling one into
    // HasClientObjectData(). GTK3 never had the problem because
    // gtk_list_store_clear() releases its rows synchronously.
    //
    // So do what the callback would have done, here where the object is still
    // alive, and disarm it.
    if (m_store)
    {
        GListModel* const model = G_LIST_MODEL(m_store);
        const guint count = g_list_model_get_n_items(model);
        const bool ownsData = HasClientObjectData();

        for (guint i = 0; i < count; i++)
        {
            wxGtkObject<GObject> obj(G_OBJECT(g_list_model_get_item(model, i)));
            wxTreeEntry* const entry = WX_TREE_ENTRY(obj.get());

            if (ownsData)
                delete static_cast<wxClientData*>(wx_tree_entry_get_userdata(entry));

            wx_tree_entry_set_userdata(entry, nullptr);
            wx_tree_entry_set_destroy_func(entry, nullptr, nullptr);
        }
    }

    Clear();

    // Clear() empties the store; these are the references taken in Create().
    if (m_selection) { g_object_unref(m_selection); m_selection = nullptr; }
    if (m_model)     { g_object_unref(m_model);     m_model = nullptr; }
    if (m_store)     { g_object_unref(m_store);     m_store = nullptr; }
#else
    if (m_treeview)
    {
        GTKDisconnect(m_treeview);
        GtkTreeSelection* selection = gtk_tree_view_get_selection(m_treeview);
        if (selection)
            GTKDisconnect(selection);
    }

    Clear();
#endif // __WXGTK4__/!__WXGTK4__
}

void wxListBox::GTKDisableEvents()
{
#ifdef __WXGTK4__
    g_signal_handlers_block_by_func(m_selection,
                                (gpointer) gtk_listitem_changed_callback, this);
#else
    GtkTreeSelection* selection = gtk_tree_view_get_selection( m_treeview );

    g_signal_handlers_block_by_func(selection,
                                (gpointer) gtk_listitem_changed_callback, this);
#endif
}

void wxListBox::GTKEnableEvents()
{
#ifdef __WXGTK4__
    g_signal_handlers_unblock_by_func(m_selection,
                                (gpointer) gtk_listitem_changed_callback, this);
#else
    GtkTreeSelection* selection = gtk_tree_view_get_selection( m_treeview );

    g_signal_handlers_unblock_by_func(selection,
                                (gpointer) gtk_listitem_changed_callback, this);
#endif

    UpdateOldSelections();
}


void wxListBox::Update()
{
    wxWindow::Update();

#ifndef __WXGTK4__
    // There is no way to force a synchronous repaint under GTK4: rendering is
    // driven entirely by the frame clock and gdk_window_process_updates() has
    // no replacement. wxWindow::Update() above has already queued the redraw,
    // which is all that can be asked for.
    if (m_treeview)
    {
        wxGCC_WARNING_SUPPRESS(deprecated-declarations)
        gdk_window_process_updates(gtk_widget_get_window(GTK_WIDGET(m_treeview)), true);
        wxGCC_WARNING_RESTORE(deprecated-declarations)
    }
#endif // !__WXGTK4__
}

// The widget that must exist for the control to be usable, which is what all
// the wxCHECK()s below are really asking about.
#ifdef __WXGTK4__
    #define WX_LISTBOX_VIEW  m_listview
#else
    #define WX_LISTBOX_VIEW  m_treeview
#endif

// ----------------------------------------------------------------------------
// adding items
// ----------------------------------------------------------------------------

int wxListBox::DoInsertItems(const wxArrayStringsAdapter& items,
                             unsigned int pos,
                             void **clientData,
                             wxClientDataType type)
{
    wxCHECK_MSG( WX_LISTBOX_VIEW != nullptr, wxNOT_FOUND, wxT("invalid listbox") );

    InvalidateBestSize();
    int n = DoInsertItemsInLoop(items, pos, clientData, type);
    UpdateOldSelections();
    return n;
}

int wxListBox::DoInsertOneItem(const wxString& item, unsigned int pos)
{
    wxTreeEntry* entry = wx_tree_entry_new();
    wx_tree_entry_set_label(entry, item.utf8_str());
    wx_tree_entry_set_destroy_func(entry, (wxTreeEntryDestroy)tree_entry_destroy_cb, this);

#ifdef __WXGTK4__
    // Insert into the store; with wxLB_SORT the position wx sees is the one in
    // the sorted model, which is not where it went in the store, so look it up
    // there afterwards rather than assuming.
    g_list_store_insert(m_store, pos, entry);

    if ( HasFlag(wxLB_SORT) )
    {
        guint sortedPos = 0;
        if ( wxGTKFindInModel(m_model, entry, &sortedPos) )
            pos = sortedPos;
    }

    g_object_unref(entry);

    return pos;
#else
#if wxUSE_CHECKLISTBOX
    int entryCol = int(m_hasCheckBoxes);
#else
    int entryCol = 0;
#endif
    GtkTreeIter iter;
    gtk_list_store_insert_with_values(m_liststore, &iter, pos, entryCol, entry, -1);
    g_object_unref(entry);

    if (HasFlag(wxLB_SORT))
        pos = GTKGetIndexFor(iter);

    return pos;
#endif // __WXGTK4__/!__WXGTK4__
}

// ----------------------------------------------------------------------------
// deleting items
// ----------------------------------------------------------------------------

void wxListBox::DoClear()
{
    wxCHECK_RET( WX_LISTBOX_VIEW != nullptr, wxT("invalid listbox") );

    {
        wxGtkEventsDisabler<wxListBox> noEvents(this);

        InvalidateBestSize();

#ifdef __WXGTK4__
        g_list_store_remove_all(m_store);
#else
        gtk_list_store_clear( m_liststore ); /* well, THAT was easy :) */
#endif
    }

    UpdateOldSelections();
}

void wxListBox::DoDeleteOneItem(unsigned int n)
{
    wxCHECK_RET( WX_LISTBOX_VIEW != nullptr, wxT("invalid listbox") );

    InvalidateBestSize();

    wxGtkEventsDisabler<wxListBox> noEvents(this);

#ifdef __WXGTK4__
    // n indexes the model wx counts in, which with wxLB_SORT is not the store,
    // so go through the item rather than through the position.
    wxGtkObject<GObject> item(G_OBJECT(g_list_model_get_item(m_model, n)));
    wxCHECK_RET( item, wxT("wrong listbox index") );

    guint storePos = 0;
    if ( g_list_store_find(m_store, item, &storePos) )
        g_list_store_remove(m_store, storePos);

    // Invalidate the selection in a single-selection control for consistency
    // with MSW and GTK+ 2, where deleting the selected item or one before it
    // does this by itself.
    if ( !HasMultipleSelection() )
    {
        const int sel = GetSelection();
        if ( sel != wxNOT_FOUND && static_cast<unsigned>(sel) >= n )
            gtk_selection_model_unselect_all(m_selection);
    }
#else
    GtkTreeIter iter;
    wxCHECK_RET( GTKGetIteratorFor(n, &iter), wxT("wrong listbox index") );

    // this returns false if iter is invalid (e.g. deleting item at end) but
    // since we don't use iter, we ignore the return value
    gtk_list_store_remove(m_liststore, &iter);

#ifdef __WXGTK3__
    // Invalidate selection in a single-selection control for consistency with
    // MSW and GTK+ 2 where this happens automatically when deleting the
    // selected item or any item before it.
    if ( !HasMultipleSelection() )
    {
        const int sel = GetSelection();
        if ( sel != wxNOT_FOUND && static_cast<unsigned>(sel) >= n )
        {
            // Don't call SetSelection() from here, it's not totally clear if
            // it is safe to do, so just do this at GTK+ level.
            gtk_tree_selection_unselect_all
            (
                gtk_tree_view_get_selection(m_treeview)
            );
        }
    }
#endif // __WXGTK3__
#endif // __WXGTK4__/!__WXGTK4__
}

// ----------------------------------------------------------------------------
// helper functions for working with iterators
// ----------------------------------------------------------------------------

#ifndef __WXGTK4__
bool wxListBox::GTKGetIteratorFor(unsigned pos, GtkTreeIter *iter) const
{
    if ( !gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(m_liststore),
                                        iter, nullptr, pos) )
    {
        wxLogDebug(wxT("gtk_tree_model_iter_nth_child(%u) failed"), pos);
        return false;
    }

    return true;
}

int wxListBox::GTKGetIndexFor(GtkTreeIter& iter) const
{
    wxGtkTreePath path(
        gtk_tree_model_get_path(GTK_TREE_MODEL(m_liststore), &iter));

    gint* pIntPath = gtk_tree_path_get_indices(path);

    wxCHECK_MSG( pIntPath, wxNOT_FOUND, wxT("failed to get iterator path") );

    return pIntPath[0];
}

#endif // !__WXGTK4__

// get GtkTreeEntry from position (note: the caller does NOT own a reference)
wxTreeEntry* wxListBox::GTKGetEntry(unsigned n) const
{
#ifdef __WXGTK4__
    if ( !m_model || n >= g_list_model_get_n_items(m_model) )
        return nullptr;

    // The model hands out a reference; drop it again so that callers see the
    // same borrowed-pointer contract the GTK3 version has always had. The
    // store keeps the item alive for as long as it is in the list.
    wxGtkObject<GObject> item(G_OBJECT(g_list_model_get_item(m_model, n)));
    return WX_TREE_ENTRY(item.get());
#else
    GtkTreeIter iter;
    if ( !GTKGetIteratorFor(n, &iter) )
        return nullptr;

    return GetEntry(m_liststore, &iter, this);
#endif // __WXGTK4__/!__WXGTK4__
}

// ----------------------------------------------------------------------------
// client data
// ----------------------------------------------------------------------------

void* wxListBox::DoGetItemClientData(unsigned int n) const
{
    wxTreeEntry* entry = GTKGetEntry(n);
    wxCHECK_MSG(entry, nullptr, wxT("could not get entry"));

    return wx_tree_entry_get_userdata(entry);
}

void wxListBox::DoSetItemClientData(unsigned int n, void* clientData)
{
    wxTreeEntry* entry = GTKGetEntry(n);
    wxCHECK_RET(entry, wxT("could not get entry"));

    wx_tree_entry_set_userdata(entry, clientData);
}

// ----------------------------------------------------------------------------
// string list access
// ----------------------------------------------------------------------------

void wxListBox::SetString(unsigned int n, const wxString& label)
{
    wxCHECK_RET( WX_LISTBOX_VIEW != nullptr, wxT("invalid listbox") );

#ifdef __WXGTK4__
    wxTreeEntry* const entry = GTKGetEntry(n);
    wxCHECK_RET( entry, "invalid index" );

    wx_tree_entry_set_label(entry, label.utf8_str());

    // A GListModel has no row-changed signal: re-binding the item is done by
    // telling the store that one item was replaced by itself. The store holds
    // a reference throughout, so the item cannot be destroyed in between.
    guint storePos = 0;
    if ( g_list_store_find(m_store, entry, &storePos) )
    {
        wxGtkObject<GObject> keepAlive(G_OBJECT(g_object_ref(entry)));
        gpointer item = entry;
        g_list_store_splice(m_store, storePos, 1, &item, 1);
    }
#else
    GtkTreeIter iter;
    wxCHECK_RET(GTKGetIteratorFor(n, &iter), "invalid index");
    wxTreeEntry* entry = GetEntry(m_liststore, &iter, this);

    // update the item itself
    wx_tree_entry_set_label(entry, label.utf8_str());

    // signal row changed
    GtkTreeModel* tree_model = GTK_TREE_MODEL(m_liststore);
    wxGtkTreePath path(gtk_tree_model_get_path(tree_model, &iter));
    gtk_tree_model_row_changed(tree_model, path, &iter);
#endif // __WXGTK4__/!__WXGTK4__
}

wxString wxListBox::GetString(unsigned int n) const
{
    wxCHECK_MSG( WX_LISTBOX_VIEW != nullptr, wxEmptyString, wxT("invalid listbox") );

    wxTreeEntry* entry = GTKGetEntry(n);
    wxCHECK_MSG( entry, wxEmptyString, wxT("wrong listbox index") );

    return wxString::FromUTF8Unchecked(wx_tree_entry_get_label(entry));
}

unsigned int wxListBox::GetCount() const
{
    wxCHECK_MSG( WX_LISTBOX_VIEW != nullptr, 0, wxT("invalid listbox") );

#ifdef __WXGTK4__
    return g_list_model_get_n_items(m_model);
#else
    return (unsigned int)gtk_tree_model_iter_n_children(GTK_TREE_MODEL(m_liststore), nullptr);
#endif
}

int wxListBox::FindString( const wxString &item, bool bCase ) const
{
    wxCHECK_MSG( WX_LISTBOX_VIEW != nullptr, wxNOT_FOUND, wxT("invalid listbox") );

    //Sort of hackish - maybe there is a faster way
    unsigned int nCount = wxListBox::GetCount();

    for(unsigned int i = 0; i < nCount; ++i)
    {
        if( item.IsSameAs( wxListBox::GetString(i), bCase ) )
            return (int)i;
    }


    // it's not an error if the string is not found -> no wxCHECK
    return wxNOT_FOUND;
}

// ----------------------------------------------------------------------------
// selection
// ----------------------------------------------------------------------------

void wxListBox::GTKOnActivated(int item)
{
    SendEvent(wxEVT_LISTBOX_DCLICK, item, IsSelected(item));
}

void wxListBox::GTKOnSelectionChanged()
{
    if ( HasFlag(wxLB_MULTIPLE | wxLB_EXTENDED) )
    {
        CalcAndSendEvent();
    }
    else // single selection
    {
        const int item = GetSelection();
        if (item >= 0 && DoChangeSingleSelection(item))
            SendEvent(wxEVT_LISTBOX, item, true);
    }
}

#ifdef __WXGTK4__

// ---------------------------------------------------------------------------
// GTK4: selection and geometry through the selection model and the list view
// ---------------------------------------------------------------------------

namespace
{

// The row widget at (x, y) in list view coordinates, or nullptr.
//
// GtkListView has no hit test of its own. gtk_widget_pick() supplies one, but
// only with GTK_PICK_NON_TARGETABLE: the label inside a row is not targetable,
// and with the default flags the pick stops at the list view itself and every
// query answers "nothing here" -- which looks exactly like the mechanism not
// working. See docs/gtk/probes/gtk4-listview-vs-listbox.c.
GtkListItem* wxGTKPickListItem(GtkWidget* listview, double x, double y)
{
    GtkWidget* w = gtk_widget_pick(listview, x, y,
                                   GtkPickFlags(GTK_PICK_NON_TARGETABLE |
                                                GTK_PICK_INSENSITIVE));

    for ( ; w && w != listview; w = gtk_widget_get_parent(w) )
    {
        gpointer const item = g_object_get_data(G_OBJECT(w), "wx-item");
        if ( item )
            return GTK_LIST_ITEM(item);
    }

    return nullptr;
}

} // anonymous namespace

int wxListBox::GetSelection() const
{
    wxCHECK_MSG( m_listview != nullptr, wxNOT_FOUND, wxT("invalid listbox"));
    wxCHECK_MSG( HasFlag(wxLB_SINGLE), wxNOT_FOUND,
                    wxT("must be single selection listbox"));

    const guint sel =
        gtk_single_selection_get_selected(GTK_SINGLE_SELECTION(m_selection));

    return sel == GTK_INVALID_LIST_POSITION ? wxNOT_FOUND : int(sel);
}

int wxListBox::GetSelections( wxArrayInt& aSelections ) const
{
    wxCHECK_MSG( m_listview != nullptr, wxNOT_FOUND, wxT("invalid listbox") );

    aSelections.Empty();

    const guint n = g_list_model_get_n_items(m_model);
    for ( guint i = 0; i < n; i++ )
    {
        if ( gtk_selection_model_is_selected(m_selection, i) )
            aSelections.Add(int(i));
    }

    return aSelections.GetCount();
}

bool wxListBox::IsSelected( int n ) const
{
    wxCHECK_MSG( m_listview != nullptr, false, wxT("invalid listbox") );
    wxCHECK_MSG( IsValid(n), false, wxT("Invalid index") );

    return gtk_selection_model_is_selected(m_selection, guint(n)) != 0;
}

void wxListBox::DoSetSelection( int n, bool select )
{
    wxCHECK_RET( m_listview != nullptr, wxT("invalid listbox") );

    wxGtkEventsDisabler<wxListBox> noEvents(this);

    // passing -1 to SetSelection() is documented to deselect all items
    if ( n == wxNOT_FOUND )
    {
        gtk_selection_model_unselect_all(m_selection);
        return;
    }

    wxCHECK_RET( IsValid(n), wxT("invalid index in wxListBox::SetSelection") );

    if (select)
    {
        // The last argument is "unselect everything else", which is right for
        // a single-selection control and destroys a multiple one: selecting
        // the second item would drop the first.
        gtk_selection_model_select_item(m_selection, guint(n),
                                        !HasMultipleSelection());
    }
    else
        gtk_selection_model_unselect_item(m_selection, guint(n));

    gtk_list_view_scroll_to(m_listview, guint(n), GTK_LIST_SCROLL_NONE, nullptr);
}

void wxListBox::DoScrollToCell(int n, float WXUNUSED(alignY), float WXUNUSED(alignX))
{
    wxCHECK_RET( m_listview, wxT("invalid listbox") );
    wxCHECK_RET( IsValid(n), wxT("invalid index"));

    // GtkListView scrolls just far enough to make the row visible and takes no
    // alignment, so the caller's alignY/alignX have nowhere to go. The
    // difference is only where an off-screen row lands, and both callers --
    // DoSetFirstItem() and EnsureVisible() -- are satisfied by it being on
    // screen at all.
    gtk_list_view_scroll_to(m_listview, guint(n), GTK_LIST_SCROLL_NONE, nullptr);
}

void wxListBox::DoSetFirstItem(int n)
{
    DoScrollToCell(n, 0, 0);
}

void wxListBox::EnsureVisible(int n)
{
    DoScrollToCell(n, 0.5, 0);
}

int wxListBox::GetTopItem() const
{
    wxCHECK_MSG( m_listview, wxNOT_FOUND, wxT("invalid listbox") );

    // Whatever row is at the top left corner, which is what "top item" means.
    GtkListItem* const item = wxGTKPickListItem(GTK_WIDGET(m_listview), 1, 1);

    return item ? int(gtk_list_item_get_position(item)) : wxNOT_FOUND;
}

int wxListBox::GetCountPerPage() const
{
    wxCHECK_MSG( m_listview, -1, wxT("invalid listbox") );

    GtkListItem* const item = wxGTKPickListItem(GTK_WIDGET(m_listview), 1, 1);
    if ( !item )
        return -1;

    GtkWidget* const row = gtk_widget_get_parent(gtk_list_item_get_child(item));
    graphene_rect_t bounds;
    if ( !row || !gtk_widget_compute_bounds(row, GTK_WIDGET(m_listview), &bounds) )
        return -1;

    if ( bounds.size.height <= 0 )
        return -1;

    return int(gtk_widget_get_height(GTK_WIDGET(m_listview)) / bounds.size.height);
}

// ----------------------------------------------------------------------------
// hittest
// ----------------------------------------------------------------------------

int wxListBox::DoListHitTest(const wxPoint& point) const
{
    // Items outside the visible area are not hit, so check that first: the
    // pick below would answer for them too if they happened to be laid out.
    if ( !GetClientRect().Contains(point) )
        return wxNOT_FOUND;

    GtkListItem* const item =
        wxGTKPickListItem(GTK_WIDGET(m_listview), point.x, point.y);

    return item ? int(gtk_list_item_get_position(item)) : wxNOT_FOUND;
}

#else // !__WXGTK4__

int wxListBox::GetSelection() const
{
    wxCHECK_MSG( m_treeview != nullptr, wxNOT_FOUND, wxT("invalid listbox"));
    wxCHECK_MSG( HasFlag(wxLB_SINGLE), wxNOT_FOUND,
                    wxT("must be single selection listbox"));

    GtkTreeIter iter;
    GtkTreeSelection* selection = gtk_tree_view_get_selection(m_treeview);

    // only works on single-sel
    if (!gtk_tree_selection_get_selected(selection, nullptr, &iter))
        return wxNOT_FOUND;

    return GTKGetIndexFor(iter);
}

int wxListBox::GetSelections( wxArrayInt& aSelections ) const
{
    wxCHECK_MSG( WX_LISTBOX_VIEW != nullptr, wxNOT_FOUND, wxT("invalid listbox") );

    aSelections.Empty();

    GtkTreeIter iter;
    GtkTreeSelection* selection = gtk_tree_view_get_selection(m_treeview);

    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(m_liststore), &iter))
    { //gtk_tree_selection_get_selected_rows is GTK 2.2+ so iter instead
        int i = 0;
        do
        {
            if (gtk_tree_selection_iter_is_selected(selection, &iter))
                aSelections.Add(i);

            i++;
        } while(gtk_tree_model_iter_next(GTK_TREE_MODEL(m_liststore), &iter));
    }

    return aSelections.GetCount();
}

bool wxListBox::IsSelected( int n ) const
{
    wxCHECK_MSG( m_treeview != nullptr, false, wxT("invalid listbox") );

    GtkTreeSelection* selection = gtk_tree_view_get_selection(m_treeview);

    GtkTreeIter iter;
    wxCHECK_MSG( GTKGetIteratorFor(n, &iter), false, wxT("Invalid index") );

    return gtk_tree_selection_iter_is_selected(selection, &iter) != 0;
}

void wxListBox::DoSetSelection( int n, bool select )
{
    wxCHECK_RET( WX_LISTBOX_VIEW != nullptr, wxT("invalid listbox") );

    wxGtkEventsDisabler<wxListBox> noEvents(this);

    GtkTreeSelection* selection = gtk_tree_view_get_selection(m_treeview);

    // passing -1 to SetSelection() is documented to deselect all items
    if ( n == wxNOT_FOUND )
    {
        gtk_tree_selection_unselect_all(selection);
        return;
    }

    wxCHECK_RET( IsValid(n), wxT("invalid index in wxListBox::SetSelection") );


    GtkTreeIter iter;
    wxCHECK_RET( GTKGetIteratorFor(n, &iter), wxT("Invalid index") );

    if (select)
        gtk_tree_selection_select_iter(selection, &iter);
    else
        gtk_tree_selection_unselect_iter(selection, &iter);

    wxGtkTreePath path(
            gtk_tree_model_get_path(GTK_TREE_MODEL(m_liststore), &iter));

    gtk_tree_view_scroll_to_cell(m_treeview, path, nullptr, FALSE, 0.0f, 0.0f);
}

void wxListBox::DoScrollToCell(int n, float alignY, float alignX)
{
    wxCHECK_RET( m_treeview, wxT("invalid listbox") );
    wxCHECK_RET( IsValid(n), wxT("invalid index"));

#ifndef __WXGTK4__
    //RN: I have no idea why this line is needed...
    //
    // GTK4 removed widget grabs entirely -- there is nothing left to ask about
    // -- so this check is simply gone there.
    if (gtk_widget_has_grab(GTK_WIDGET(m_treeview)))
        return;
#endif // !__WXGTK4__

    GtkTreeIter iter;
    if ( !GTKGetIteratorFor(n, &iter) )
        return;

    wxGtkTreePath path(
            gtk_tree_model_get_path(GTK_TREE_MODEL(m_liststore), &iter));

    // Scroll to the desired cell (0.0 == topleft alignment)
    gtk_tree_view_scroll_to_cell(m_treeview, path, nullptr,
                                 TRUE, alignY, alignX);
}

void wxListBox::DoSetFirstItem(int n)
{
    DoScrollToCell(n, 0, 0);
}

void wxListBox::EnsureVisible(int n)
{
    DoScrollToCell(n, 0.5, 0);
}

int wxListBox::GetTopItem() const
{
    int idx = wxNOT_FOUND;

#if GTK_CHECK_VERSION(2,8,0)
    wxGtkTreePath start;
    if (
        wx_is_at_least_gtk2(8) &&
        gtk_tree_view_get_visible_range(m_treeview, start.ByRef(), nullptr))
    {
        gint *ptr = gtk_tree_path_get_indices(start);

        if ( ptr )
            idx = *ptr;
    }
#endif

    return idx;
}

int wxListBox::GetCountPerPage() const
{
    wxGtkTreePath path;
    GtkTreeViewColumn *column;

    if ( !gtk_tree_view_get_path_at_pos
          (
            m_treeview,
            0,
            0,
            path.ByRef(),
            &column,
            nullptr,
            nullptr
          ) )
    {
        return -1;
    }

    GdkRectangle rect;
    gtk_tree_view_get_cell_area(m_treeview, path, column, &rect);

    if ( !rect.height )
        return -1;

    GdkRectangle vis;
    gtk_tree_view_get_visible_rect(m_treeview, &vis);

    return vis.height / rect.height;
}

// ----------------------------------------------------------------------------
// hittest
// ----------------------------------------------------------------------------

int wxListBox::DoListHitTest(const wxPoint& point) const
{
    // gtk_tree_view_get_path_at_pos() also gets items that are not visible and
    // we only want visible items we need to check for it manually here
    if ( !GetClientRect().Contains(point) )
        return wxNOT_FOUND;

    // need to translate from master window since it is in client coords
#ifdef __WXGTK4__
    // gtk_tree_view_get_bin_window() went away with GdkWindow, but the
    // conversion it was being used for has its own function.
    gint binPosX, binPosY;
    gtk_tree_view_convert_widget_to_bin_window_coords(m_treeview,
                                                      point.x, point.y,
                                                      &binPosX, &binPosY);
#else
    gint binx, biny;
    gdk_window_get_geometry(gtk_tree_view_get_bin_window(m_treeview),
                            &binx, &biny, nullptr, nullptr);

    const gint binPosX = point.x - binx;
    const gint binPosY = point.y - biny;
#endif // __WXGTK4__/!__WXGTK4__

    wxGtkTreePath path;
    if ( !gtk_tree_view_get_path_at_pos
          (
            m_treeview,
            binPosX,
            binPosY,
            path.ByRef(),
            nullptr,   // [out] column (always 0 here)
            nullptr,   // [out] x-coord relative to the cell (not interested)
            nullptr    // [out] y-coord relative to the cell
          ) )
    {
        return wxNOT_FOUND;
    }

    return gtk_tree_path_get_indices(path)[0];
}

#endif // __WXGTK4__/!__WXGTK4__

// ----------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------

GtkWidget *wxListBox::GetConnectWidget() const
{
    // the correct widget for listbox events (such as mouse clicks for example)
    // is the list itself, not the parent scrolled window
    return GTK_WIDGET(WX_LISTBOX_VIEW);
}

#ifndef __WXGTK4__
GdkWindow *wxListBox::GTKGetWindow(wxArrayGdkWindows& WXUNUSED(windows)) const
{
    return gtk_tree_view_get_bin_window(m_treeview);
}
#endif // !__WXGTK4__

void wxListBox::DoApplyWidgetStyle(GtkRcStyle *style)
{
#ifdef __WXGTK3__
    // don't know if this is even necessary, or how to do it
#else
    if (m_hasBgCol && m_backgroundColour.IsOk())
    {
        GdkWindow *window = gtk_tree_view_get_bin_window(m_treeview);
        if (window)
        {
            m_backgroundColour.CalcPixel( gdk_drawable_get_colormap( window ) );
            gdk_window_set_background( window, m_backgroundColour.GetColor() );
            gdk_window_clear( window );
        }
    }
#endif

    GTKApplyStyle(GTK_WIDGET(WX_LISTBOX_VIEW), style);
}

wxSize wxListBox::DoGetBestSize() const
{
    wxCHECK_MSG(WX_LISTBOX_VIEW, wxDefaultSize, wxT("invalid tree view"));

    // Start with a minimum size that's not too small
    int cx, cy;
    GetTextExtent( wxT("X"), &cx, &cy);
    int lbWidth = 0;
    int lbHeight = 10;

    // Find the widest string.
    const unsigned int count = GetCount();
    if ( count )
    {
        int wLine;
        for ( unsigned int i = 0; i < count; i++ )
        {
            GetTextExtent(GetString(i), &wLine, nullptr);
            if ( wLine > lbWidth )
                lbWidth = wLine;
        }
    }

    lbWidth += 3 * cx;

    // And just a bit more for the checkbox if present and then some
    // (these are rough guesses)
#if wxUSE_CHECKLISTBOX
    if ( m_hasCheckBoxes )
    {
        lbWidth += 35;
        cy = cy > 25 ? cy : 25; // rough height of checkbox
    }
#endif

    // Add room for the scrollbar
    lbWidth += wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);

    // Don't make the listbox too tall but don't make it too small either
    lbHeight = (cy+4) * wxMin(wxMax(count, 3), 10);

    wxSize size(lbWidth, lbHeight);
#ifdef __WXGTK3__
    // Ensure size is at least the required minimum
    int w, h;
    gtk_widget_get_size_request(m_widget, &w, &h);
    gtk_widget_set_size_request(m_widget, -1, -1);
    wxSize min;
    gtk_widget_get_preferred_width(m_widget, &min.x, nullptr);
    gtk_widget_get_preferred_height_for_width(m_widget, min.x, &min.y, nullptr);
    gtk_widget_set_size_request(m_widget, w, h);
    size.IncTo(min);
#endif
    return size;
}

// static
wxVisualAttributes
wxListBox::GetClassDefaultAttributes(wxWindowVariant WXUNUSED(variant))
{
#ifdef __WXGTK4__
    // The list itself, not the scrolled window around it: the colours wanted
    // here are the ones the rows are drawn against.
    return GetDefaultAttributesFromGTKWidget(gtk_list_view_new(nullptr, nullptr),
                                             true);
#else
    return GetDefaultAttributesFromGTKWidget(gtk_tree_view_new(), true);
#endif
}

#endif // wxUSE_LISTBOX
