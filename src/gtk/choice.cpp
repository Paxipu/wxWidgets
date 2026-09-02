/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/choice.cpp
// Purpose:
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_CHOICE || wxUSE_COMBOBOX

#include "wx/choice.h"

#ifndef WX_PRECOMP
    #include "wx/arrstr.h"
#endif

#include "wx/gtk/private.h"
#include "wx/gtk/private/event.h"
#include "wx/gtk/private/eventsdisabler.h"
#include "wx/gtk/private/list.h"
#include "wx/gtk/private/value.h"
#include "wx/gtk/private/gtk3-compat.h"

// ----------------------------------------------------------------------------
// GTK callbacks
// ----------------------------------------------------------------------------

extern "C" {

#ifndef __WXGTK4__
static void
gtk_choice_changed_callback( GtkWidget *WXUNUSED(widget), wxChoice *choice )
{
    choice->SendSelectionChangedEvent(wxEVT_CHOICE);
}
#endif // !__WXGTK4__

#ifdef __WXGTK4__

// GTK4 has no crossing events: the pointer entering and leaving a widget is
// reported by a GtkEventControllerMotion instead, exactly as in window.cpp.

static void
wx_gtk_choice_enter_notify(GtkEventControllerMotion* controller,
                           double x, double y,
                           wxChoice* choice)
{
    wxGTKImpl::WindowEnterCallback(
        choice,
        gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(controller)),
        x, y);
}

static void
wx_gtk_choice_leave_notify(GtkEventControllerMotion* controller,
                           wxChoice* choice)
{
    wxGTKImpl::WindowLeaveCallback(
        choice,
        gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(controller)));
}

#elif defined(__WXGTK3__)

static gboolean
wx_gtk_choice_enter_notify(GtkWidget* widget,
                           GdkEventCrossing* gdk_event,
                           wxChoice *choice)
{
    return wxGTKImpl::WindowEnterCallback(widget, gdk_event, choice);
}

static gboolean
wx_gtk_choice_leave_notify(GtkWidget* widget,
                           GdkEventCrossing* gdk_event,
                           wxChoice* choice)
{
    return wxGTKImpl::WindowLeaveCallback(widget, gdk_event, choice);
}

#endif // __WXGTK4__/__WXGTK3__

}

#ifndef __WXGTK4__
static inline GtkWidget* wxGTKComboBoxGetChild(GtkWidget* combo)
{
    return gtk_bin_get_child(GTK_BIN(combo));
}
#endif // !__WXGTK4__

#ifdef __WXGTK4__

extern "C" {

// A row in the popover was clicked, or Enter was pressed on it.
static void
wx_gtk_choice_row_activated(GtkListView*, guint position, wxChoice* choice)
{
    choice->GTKOnListActivated(position);
}

// Arrow keys on the closed control.
static gboolean
wx_gtk_choice_key_pressed(GtkEventControllerKey* WXUNUSED(controller),
                          guint keyval,
                          guint WXUNUSED(keycode),
                          GdkModifierType state,
                          wxChoice* choice)
{
    // Only the plain keys: Ctrl+Home in a wxComboBox's entry is the entry's.
    if ( state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK) )
        return FALSE;

    return choice->GTKMoveSelection(keyval);
}

// GtkDropDown builds each row with a factory. The default one makes a plain
// GtkLabel, and a plain GtkLabel does not ellipsize -- which the cell renderer
// GtkComboBoxText used did, for the reason given in Create() below. So build
// the label here instead of taking the default.
static void
wx_gtk_dropdown_setup_label(GtkSignalListItemFactory*, GtkListItem* item, gpointer)
{
    GtkWidget* const label = gtk_label_new(nullptr);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_list_item_set_child(item, label);
}

static void
wx_gtk_dropdown_bind_label(GtkSignalListItemFactory*, GtkListItem* item, gpointer)
{
    GtkWidget* const label = gtk_list_item_get_child(item);
    GtkStringObject* const obj =
        GTK_STRING_OBJECT(gtk_list_item_get_item(item));

    gtk_label_set_label(GTK_LABEL(label),
                        obj ? gtk_string_object_get_string(obj) : "");
}

} // extern "C"

// A factory building one ellipsizing label per item. GTK4's default builds a
// plain GtkLabel, and a plain GtkLabel does not ellipsize -- which the cell
// renderer this replaces did, deliberately; see Create() below.
static GtkListItemFactory* wxGTKCreateEllipsizingLabelFactory()
{
    GtkListItemFactory* const factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup",
                     G_CALLBACK(wx_gtk_dropdown_setup_label), nullptr);
    g_signal_connect(factory, "bind",
                     G_CALLBACK(wx_gtk_dropdown_bind_label), nullptr);
    return factory;
}

// Depth-first search for the first descendant of the given type. GtkDropDown's
// insides are not public API, so everything found this way is checked for
// rather than assumed, and the callers cope with not finding it.
static GtkWidget* wxGTKFindDescendant(GtkWidget* parent, GType type)
{
    for ( GtkWidget* child = gtk_widget_get_first_child(parent);
          child; child = gtk_widget_get_next_sibling(child) )
    {
        if ( G_TYPE_CHECK_INSTANCE_TYPE(child, type) )
            return child;

        if ( GtkWidget* const found = wxGTKFindDescendant(child, type) )
            return found;
    }

    return nullptr;
}

#endif // __WXGTK4__

//-----------------------------------------------------------------------------
// wxChoice
//-----------------------------------------------------------------------------

void wxChoice::Init()
{
    m_strings = nullptr;
#ifdef __WXGTK4__
    m_itemModel = nullptr;
    m_listSelection = nullptr;
    m_listView = nullptr;
    m_dropButton = nullptr;
#else
    m_stringCellIndex = 0;
#endif
}

bool wxChoice::Create( wxWindow *parent, wxWindowID id,
                       const wxPoint &pos, const wxSize &size,
                       const wxArrayString& choices,
                       long style, const wxValidator& validator,
                       const wxString &name )
{
    wxCArrayString chs(choices);

    return Create( parent, id, pos, size, chs.GetCount(), chs.GetStrings(),
                   style, validator, name );
}

bool wxChoice::Create( wxWindow *parent, wxWindowID id,
                       const wxPoint &pos, const wxSize &size,
                       int n, const wxString choices[],
                       long style, const wxValidator& validator,
                       const wxString &name )
{
    if (!PreCreation( parent, pos, size ) ||
        !CreateBase( parent, id, pos, size, style, validator, name ))
    {
        wxFAIL_MSG( wxT("wxChoice creation failed") );
        return false;
    }

    if ( IsSorted() )
    {
        // if our m_strings != nullptr, Append() will check for it and insert
        // items in the correct order
        m_strings = new wxGtkCollatedArrayString;
    }

#ifdef __WXGTK4__
    // GTK4 has no GtkComboBox worth using: it is deprecated in favour of
    // GtkDropDown, which is a different shape -- a GListModel of items and a
    // factory building a widget for each, rather than a tree model and cell
    // renderers.
    //
    // The items are kept in the control (see choice.h) rather than in the
    // widget, so that wxComboBox, which inherits every item method from this
    // class but is shown by a different widget, can use the same ones.
    m_itemModel = gtk_string_list_new(nullptr);

    // Not a GtkDropDown -- see the note on m_listSelection in choice.h. A menu
    // button showing the current item, with the list in its popover, is the
    // same control built from parts whose selection wx can actually clear.
    m_widget = gtk_menu_button_new();
    m_dropButton = m_widget;
    gtk_menu_button_set_always_show_arrow(GTK_MENU_BUTTON(m_widget), TRUE);

    // Not gtk_menu_button_set_label(): that centres the text and builds a new
    // label every time it is called, so neither the alignment a combo box has
    // nor the ellipsizing survives. This label is ours and stays.
    {
        GtkWidget* const label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_widget_set_hexpand(label, TRUE);
        gtk_menu_button_set_child(GTK_MENU_BUTTON(m_widget), label);
    }

    gtk_menu_button_set_popover(GTK_MENU_BUTTON(m_widget),
                                GTKCreateItemPopover());
    g_signal_connect(m_listView, "activate",
                     G_CALLBACK(wx_gtk_choice_row_activated), this);

    GTKConnectSelectionKeys(m_widget);
#elif defined(__WXGTK3__)
    m_widget = gtk_combo_box_text_new();

    // If any choices don't fit into the available space (in the always visible
    // part of the control, not the dropdown), GTK shows just the tail of the
    // string which does fit, which is bad for long strings and even worse for
    // the shorter ones, as they may end up being shown as completely blank.
    // Work around this brokenness by enabling ellipsization, especially as it
    // seems to be safe to do it unconditionally, i.e. there doesn't seem to be
    // any ill effects from having it on if everything does fit.
    const wxGtkList cells(gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(m_widget)));
    if (GTK_IS_CELL_RENDERER_TEXT(cells->data))
        g_object_set(G_OBJECT(cells->data), "ellipsize", PANGO_ELLIPSIZE_END, nullptr);
#else
    m_widget = gtk_combo_box_new_text();
#endif
    g_object_ref(m_widget);

    Append(n, choices);

    m_parent->DoAddChild( this );

    PostCreation(size);

#ifndef __WXGTK4__
    g_signal_connect_after (m_widget, "changed",
                            G_CALLBACK (gtk_choice_changed_callback), this);
#endif

#ifdef __WXGTK3__
    // Internal structure of GtkComboBoxText is complicated: it contains a
    // GtkBox which contains a GtkToggleButton which contains another GtkBox
    // which, in turn, contains GtkCellView (and more).
    //
    // And it's this internal GtkToggleButton which receives the mouse events
    // and not the main widget itself, so find it and connect to its events.

    // We could find it either by using gtk_container_forall() to get the box
    // inside GtkComboBoxText and then get its only child, or by doing what we
    // do here and getting GtkCellView directly and then getting its parent,
    // which is simpler because GtkComboBoxText sets things up in such a way
    // that its only child is the GtkCellView (even if, again, this is not how
    // things really are internally).
    //
    // This layout is unchanged in GTK4, which is checked for by the
    // gtk4-invariants CI test as it is not part of any documented API.
#ifdef __WXGTK4__
    // A GtkMenuButton contains a toggle button, which is what receives the
    // pointer, exactly as GtkComboBoxText's did.
    auto button = wxGTKFindDescendant(m_dropButton, GTK_TYPE_TOGGLE_BUTTON);
    wxCHECK_MSG( button, true, "No toggle button in GtkMenuButton?" );
#else
    auto cellView = wxGTKComboBoxGetChild(m_widget);
    wxCHECK_MSG( cellView, true, "No cell view in GtkComboBoxText?" );

    auto box = gtk_widget_get_parent(cellView);

    auto button = gtk_widget_get_parent(box);
    wxCHECK_MSG( GTK_IS_TOGGLE_BUTTON(button), true,
                 "Unexpected grandparent of GtkCellView in GtkComboBoxText" );
#endif

#ifdef __WXGTK4__
    GtkEventController* const motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter",
                     G_CALLBACK(wx_gtk_choice_enter_notify), this);
    g_signal_connect(motion, "leave",
                     G_CALLBACK(wx_gtk_choice_leave_notify), this);
    gtk_widget_add_controller(button, motion);
#else
    g_signal_connect(button, "enter_notify_event",
                     G_CALLBACK(wx_gtk_choice_enter_notify), this);
    g_signal_connect(button, "leave_notify_event",
                     G_CALLBACK(wx_gtk_choice_leave_notify), this);
#endif
#endif // __WXGTK3__

    return true;
}

wxChoice::~wxChoice()
{
    Clear();
    delete m_strings;

#ifdef __WXGTK4__
    if ( m_itemModel )
    {
        g_object_unref(m_itemModel);
        m_itemModel = nullptr;
    }
#endif

 #ifdef __WXGTK3__
    // At least with GTK+ 3.22.9, destroying a shown combobox widget results in
    // a Gtk-CRITICAL debug message when the assertion fails inside a signal
    // handler called from gtk_widget_unrealize(), which is annoying, so avoid
    // it by hiding the widget before destroying it -- this doesn't look right,
    // but shouldn't do any harm either.
    Hide();
 #endif // __WXGTK3__
}

bool wxChoice::GTKHandleFocusOut()
{
#ifdef __WXGTK4__
    // There is no "popup-shown" property here; ask the popover itself.
    if ( m_dropButton )
    {
        GtkPopover* const popover =
            gtk_menu_button_get_popover(GTK_MENU_BUTTON(m_dropButton));
        if ( popover && gtk_widget_get_visible(GTK_WIDGET(popover)) )
            return true;
    }
#else
    if ( wx_is_at_least_gtk2(10) )
    {
        gboolean isShown;
        g_object_get( m_widget, "popup-shown", &isShown, nullptr );

        // Don't send "focus lost" events if the focus is grabbed by our own
        // popup, it counts as part of this window, even though wx doesn't know
        // about it (and can't, because GtkComboBox doesn't expose it).
        if ( isShown )
            return true;
    }
#endif // __WXGTK4__/!__WXGTK4__

    return wxChoiceBase::GTKHandleFocusOut();
}

#ifdef __WXGTK4__

GtkWidget* wxChoice::GTKCreateItemPopover()
{
    // autoselect off and can-unselect on are the whole point of doing this by
    // hand: they are what a GtkDropDown's own selection will not do.
    GtkSingleSelection* const selection =
        gtk_single_selection_new(G_LIST_MODEL(g_object_ref(m_itemModel)));
    gtk_single_selection_set_autoselect(selection, FALSE);
    gtk_single_selection_set_can_unselect(selection, TRUE);
    gtk_single_selection_set_selected(selection, GTK_INVALID_LIST_POSITION);
    m_listSelection = selection;

    GtkListItemFactory* const factory = wxGTKCreateEllipsizingLabelFactory();
    m_listView = gtk_list_view_new(GTK_SELECTION_MODEL(selection), factory);
    gtk_list_view_set_single_click_activate(GTK_LIST_VIEW(m_listView), TRUE);

    GtkWidget* const scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_height(
        GTK_SCROLLED_WINDOW(scrolled), TRUE);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scrolled),
                                               400);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), m_listView);

    GtkWidget* const popover = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(popover), scrolled);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);

    return popover;
}

int wxChoice::GTKGetSelection() const
{
    if ( !m_listSelection )
        return wxNOT_FOUND;

    const guint sel = gtk_single_selection_get_selected(m_listSelection);

    return sel == GTK_INVALID_LIST_POSITION ? wxNOT_FOUND : int(sel);
}

void wxChoice::GTKSetSelection(int n)
{
    if ( !m_listSelection )
        return;

    gtk_single_selection_set_selected(
        m_listSelection,
        n == wxNOT_FOUND ? GTK_INVALID_LIST_POSITION : guint(n));

    GTKUpdateSelectionDisplay();
}

void wxChoice::GTKUpdateSelectionDisplay()
{
    if ( !GTK_IS_MENU_BUTTON(m_widget) )
        return;

    GtkWidget* const label = gtk_menu_button_get_child(GTK_MENU_BUTTON(m_widget));
    if ( !GTK_IS_LABEL(label) )
        return;

    const int sel = GTKGetSelection();
    gtk_label_set_label(
        GTK_LABEL(label),
        sel == wxNOT_FOUND ? "" : GetString(unsigned(sel)).utf8_str().data());
}

void wxChoice::GTKConnectSelectionKeys(GtkWidget* widget)
{
    GtkEventController* const keys = gtk_event_controller_key_new();

    // Capture phase, so that the entry of a wxComboBox does not get Up and
    // Down first: those move the selection, as they did with a GtkComboBox,
    // and are not text editing.
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed",
                     G_CALLBACK(wx_gtk_choice_key_pressed), this);
    gtk_widget_add_controller(widget, keys);
}

bool wxChoice::GTKMoveSelection(unsigned int keyval)
{
    const unsigned int count = GetCount();
    if ( !count )
        return false;

    const int current = GTKGetSelection();
    int wanted;

    switch ( keyval )
    {
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            wanted = current == wxNOT_FOUND ? int(count) - 1
                                            : wxMax(0, current - 1);
            break;

        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            wanted = current == wxNOT_FOUND
                        ? 0 : wxMin(int(count) - 1, current + 1);
            break;

        case GDK_KEY_Home:
        case GDK_KEY_KP_Home:
            wanted = 0;
            break;

        case GDK_KEY_End:
        case GDK_KEY_KP_End:
            wanted = int(count) - 1;
            break;

        default:
            return false;
    }

    if ( wanted == current )
        return true;            // handled, even though nothing moved

    SetSelection(wanted);
    SendSelectionChangedEvent(wxEVT_CHOICE);

    return true;
}

void wxChoice::GTKOnListActivated(unsigned int pos)
{
    if ( m_dropButton )
        gtk_menu_button_popdown(GTK_MENU_BUTTON(m_dropButton));

    if ( pos >= GetCount() )
        return;

    SetSelection(int(pos));
    SendSelectionChangedEvent(wxEVT_CHOICE);
}

GtkWidget* wxChoice::GTKGetSizeChildPart() const
{
    GtkWidget* const label = wxGTKFindDescendant(m_widget, GTK_TYPE_LABEL);

    return label ? label : m_widget;
}

void wxChoice::GTKRestoreSelection(int sel)
{
    // An index past the end of what is left is not a selection any more.
    if ( sel != wxNOT_FOUND && (unsigned)sel >= GetCount() )
        sel = wxNOT_FOUND;

    if ( !m_listSelection || GTKGetSelection() == sel )
        return;

    const bool lost = sel == wxNOT_FOUND;

    gtk_single_selection_set_selected(
        m_listSelection, lost ? GTK_INVALID_LIST_POSITION : guint(sel));

    // Deliberately not through GTKSetSelection(): the display is only touched
    // when the selection is gone. An index that merely shifted still shows the
    // same item, and rewriting the display then would wipe what the user has
    // typed into a wxComboBox.
    if ( lost )
        GTKUpdateSelectionDisplay();
}

#endif // __WXGTK4__

int wxChoice::DoInsertItems(const wxArrayStringsAdapter & items,
                            unsigned int pos,
                            void **clientData, wxClientDataType type)
{
    wxCHECK_MSG( m_widget != nullptr, -1, wxT("invalid control") );

    wxASSERT_MSG( !IsSorted() || (pos == GetCount()),
                 wxT("In a sorted choice data could only be appended"));

    const int count = items.GetCount();

    int n = wxNOT_FOUND;

#ifdef __WXGTK4__
    // Inserting moves the selection: GTK selects the first item appended to an
    // empty model, and shifts the selected index for anything inserted before
    // it. Neither is what wx means by inserting an item, so the selection is
    // read first and put back afterwards.
    const int sel = GTKGetSelection();
    int selAfter = sel;

    for ( int i = 0; i < count; ++i )
    {
        n = pos + i;
        // If sorted, use this wxSortedArrayStrings to determine
        // the right insertion point
        if (m_strings)
            n = m_strings->Add(items[i]);

        const wxScopedCharBuffer text(items[i].utf8_str());
        const char* const additions[] = { text.data(), nullptr };
        gtk_string_list_splice(m_itemModel, n, 0, additions);

        if ( selAfter != wxNOT_FOUND && n <= selAfter )
            selAfter++;

        m_clientData.Insert( nullptr, n );
        AssignNewItemClientData(n, clientData, i, type);
    }

    GTKRestoreSelection(selAfter);
#else
    GtkTreeIter iter;
    GtkTreeModel *model = gtk_combo_box_get_model( GTK_COMBO_BOX( m_widget ) );
    GtkListStore *store = GTK_LIST_STORE( model );

    gtk_widget_freeze_child_notify(m_widget);

    for ( int i = 0; i < count; ++i )
    {
        n = pos + i;
        // If sorted, use this wxSortedArrayStrings to determine
        // the right insertion point
        if (m_strings)
            n = m_strings->Add(items[i]);

        gtk_list_store_insert_with_values(store, &iter, n, m_stringCellIndex,
                                          items[i].utf8_str().data(), -1);

        m_clientData.Insert( nullptr, n );
        AssignNewItemClientData(n, clientData, i, type);
    }

    gtk_widget_thaw_child_notify(m_widget);
#endif // __WXGTK4__/!__WXGTK4__


    InvalidateBestSize();

    return n;
}

void wxChoice::DoSetItemClientData(unsigned int n, void* clientData)
{
    m_clientData[n] = clientData;
}

void* wxChoice::DoGetItemClientData(unsigned int n) const
{
    return m_clientData[n];
}

void wxChoice::DoClear()
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid control") );

    wxGtkEventsDisabler<wxChoice> noEvents(this);

#ifdef __WXGTK4__
    if ( const unsigned int count = GetCount() )
        gtk_string_list_splice(m_itemModel, 0, count, nullptr);

    GTKSetSelection(wxNOT_FOUND);
#else
    GtkComboBox* combobox = GTK_COMBO_BOX( m_widget );
    GtkTreeModel* model = gtk_combo_box_get_model( combobox );
    if (model)
        gtk_list_store_clear(GTK_LIST_STORE(model));
#endif

    m_clientData.Clear();

    if (m_strings)
        m_strings->Clear();

    InvalidateBestSize();
}

void wxChoice::DoDeleteOneItem(unsigned int n)
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid control") );
    wxCHECK_RET( IsValid(n), wxT("invalid index in wxChoice::Delete") );

#ifdef __WXGTK4__
    // Deleting the selected item leaves wx with nothing selected, and deleting
    // one before it moves the selection down; GTK does neither on its own.
    const int sel = GTKGetSelection();

    gtk_string_list_remove(m_itemModel, n);

    int selAfter = sel;
    if ( sel != wxNOT_FOUND )
    {
        if ( (unsigned)sel == n )
            selAfter = wxNOT_FOUND;
        else if ( (unsigned)sel > n )
            selAfter = sel - 1;
    }
    GTKRestoreSelection(selAfter);
#else
    GtkComboBox* combobox = GTK_COMBO_BOX( m_widget );
    GtkTreeModel* model = gtk_combo_box_get_model( combobox );
    GtkListStore* store = GTK_LIST_STORE(model);
    GtkTreeIter iter;
    if ( !gtk_tree_model_iter_nth_child(model, &iter, nullptr, n) )
    {
        // This is really not supposed to happen for a valid index.
        wxFAIL_MSG(wxS("Item unexpectedly not found."));
        return;
    }
    gtk_list_store_remove( store, &iter );
#endif // __WXGTK4__/!__WXGTK4__

    m_clientData.RemoveAt( n );
    if ( m_strings )
        m_strings->RemoveAt( n );

    InvalidateBestSize();
}

int wxChoice::FindString( const wxString &item, bool bCase ) const
{
    wxCHECK_MSG( m_widget != nullptr, wxNOT_FOUND, wxT("invalid control") );

#ifdef __WXGTK4__
    const unsigned int count = GetCount();
    for ( unsigned int i = 0; i < count; ++i )
    {
        if ( item.IsSameAs(GetString(i), bCase) )
            return int(i);
    }
#else
    GtkComboBox* combobox = GTK_COMBO_BOX( m_widget );
    GtkTreeModel* model = gtk_combo_box_get_model( combobox );
    GtkTreeIter iter;
    gtk_tree_model_get_iter_first( model, &iter );
    if (!gtk_list_store_iter_is_valid(GTK_LIST_STORE(model), &iter ))
        return -1;
    int count = 0;
    do
    {
        wxGtkValue value;
        gtk_tree_model_get_value( model, &iter, m_stringCellIndex, value );
        wxString str = wxString::FromUTF8Unchecked( g_value_get_string( value ) );

        if (item.IsSameAs( str, bCase ) )
            return count;

        count++;
    }
    while ( gtk_tree_model_iter_next(model, &iter) );
#endif // __WXGTK4__/!__WXGTK4__

    return wxNOT_FOUND;
}

int wxChoice::GetSelection() const
{
#ifdef __WXGTK4__
    return GTKGetSelection();
#else
    return gtk_combo_box_get_active( GTK_COMBO_BOX( m_widget ) );
#endif
}

void wxChoice::SetString(unsigned int n, const wxString &text)
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid control") );

    wxCHECK_RET( IsValid(n), wxT("invalid index") );

#ifdef __WXGTK4__
    // A GtkStringList has no "set" -- an item is replaced by splicing one out
    // and one in, which moves the selection, so it is put back afterwards.
    const int sel = GTKGetSelection();

    const wxScopedCharBuffer buf(text.utf8_str());
    const char* const additions[] = { buf.data(), nullptr };
    gtk_string_list_splice(m_itemModel, n, 1, additions);

    GTKRestoreSelection(sel);
#else
    GtkComboBox* combobox = GTK_COMBO_BOX( m_widget );

    GtkTreeModel *model = gtk_combo_box_get_model( combobox );
    GtkTreeIter iter;
    if (gtk_tree_model_iter_nth_child (model, &iter, nullptr, n))
    {
        wxGtkValue value(G_TYPE_STRING);
        g_value_set_string( value, text.utf8_str() );
        gtk_list_store_set_value( GTK_LIST_STORE(model), &iter, m_stringCellIndex, value );
    }
#endif // __WXGTK4__/!__WXGTK4__

    InvalidateBestSize();
}

wxString wxChoice::GetString(unsigned int n) const
{
    wxCHECK_MSG( m_widget != nullptr, wxEmptyString, wxT("invalid control") );

#ifdef __WXGTK4__
    if ( n >= GetCount() )
    {
        wxFAIL_MSG( "invalid index" );
        return wxString();
    }

    return wxString::FromUTF8Unchecked(gtk_string_list_get_string(m_itemModel, n));
#else
    GtkComboBox* combobox = GTK_COMBO_BOX( m_widget );
    GtkTreeModel *model = gtk_combo_box_get_model( combobox );
    GtkTreeIter iter;
    if (!gtk_tree_model_iter_nth_child (model, &iter, nullptr, n))
    {
        wxFAIL_MSG( "invalid index" );
        return wxString();
    }

    wxGtkValue value;
    gtk_tree_model_get_value( model, &iter, m_stringCellIndex, value );
    return wxString::FromUTF8Unchecked( g_value_get_string( value ) );
#endif // __WXGTK4__/!__WXGTK4__
}

unsigned int wxChoice::GetCount() const
{
    wxCHECK_MSG( m_widget != nullptr, 0, wxT("invalid control") );

#ifdef __WXGTK4__
    return m_itemModel
             ? g_list_model_get_n_items(G_LIST_MODEL(m_itemModel))
             : 0;
#else
    GtkComboBox* combobox = GTK_COMBO_BOX( m_widget );
    GtkTreeModel* model = gtk_combo_box_get_model( combobox );
    GtkTreeIter iter;
    gtk_tree_model_get_iter_first( model, &iter );
    if (!gtk_list_store_iter_is_valid(GTK_LIST_STORE(model), &iter ))
        return 0;
    unsigned int ret = 1;
    while (gtk_tree_model_iter_next( model, &iter ))
        ret++;
    return ret;
#endif // __WXGTK4__/!__WXGTK4__
}

void wxChoice::SetSelection( int n )
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid control") );

    wxGtkEventsDisabler<wxChoice> noEvents(this);

#ifdef __WXGTK4__
    GTKSetSelection(n);
#else
    GtkComboBox* combobox = GTK_COMBO_BOX( m_widget );
    gtk_combo_box_set_active( combobox, n );
#endif
}

void wxChoice::SetColumns(int n)
{
#ifdef __WXGTK4__
    // GTK4 dropped the wrap-width property along with the grid layout of the
    // drop-down list it controlled, so a multi-column popup can't be asked
    // for any more and this is a no-op. The list is always a single column.
    wxUnusedVar(n);
#else
    gtk_combo_box_set_wrap_width(GTK_COMBO_BOX(m_widget), n);
#endif
}

int wxChoice::GetColumns() const
{
#ifdef __WXGTK4__
    return 1; // see SetColumns()
#else
    return gtk_combo_box_get_wrap_width(GTK_COMBO_BOX(m_widget));
#endif
}

void wxChoice::GTKDisableEvents()
{
#ifdef __WXGTK4__
    // Nothing to block: what reports a selection change is
    // GTKOnListActivated(), which only a user action reaches, rather than a
    // signal raised by wx changing the model.
#else
    g_signal_handlers_block_by_func(m_widget,
                                (gpointer) gtk_choice_changed_callback, this);
#endif
}

void wxChoice::GTKEnableEvents()
{
#ifdef __WXGTK4__
#else
    g_signal_handlers_unblock_by_func(m_widget,
                                (gpointer) gtk_choice_changed_callback, this);
#endif
}

#ifndef __WXGTK4__
GdkWindow *wxChoice::GTKGetWindow(wxArrayGdkWindows& WXUNUSED(windows)) const
{
    return gtk_widget_get_window(m_widget);
}
#endif // !__WXGTK4__

wxSize wxChoice::DoGetBestSize() const
{
    // Get the height of the control from GTK+ itself, but use our own version
    // to compute the width large enough to show all our strings as GTK+
    // doesn't seem to take the control contents into account.
    return GetSizeFromTextSize(wxChoiceBase::DoGetBestSize().x);
}

wxSize wxChoice::DoGetSizeFromTextSize(int xlen, int ylen) const
{
    wxASSERT_MSG( m_widget, wxS("GetSizeFromTextSize called before creation") );

#ifdef __WXGTK4__
    // The difference between the whole control and its "child part" is what
    // the arrow, the separators and the padding take. Under GTK+ 3 that part
    // is a GtkCellView; a GtkDropDown has no cell view, and what stands in the
    // same place is the label its button shows.
    GtkWidget* childPart = GTKGetSizeChildPart();

    // Same workaround as below: an empty control reports a preferred size that
    // does not include what one line of text needs.
    const bool addedTemporaryItem = GetCount() == 0;
    if ( addedTemporaryItem )
    {
        const char* const additions[] = { "Gg", nullptr };
        gtk_string_list_splice(m_itemModel, 0, 0, additions);
    }
#else
    // a GtkEntry for wxComboBox and a GtkCellView for wxChoice
    GtkWidget* childPart = wxGTKComboBoxGetChild(m_widget);

#ifdef __WXGTK3__
    // Preferred size for wxChoice can be incorrect when control is empty,
    // work around this by temporarily adding an item.
    GtkTreeModel* model = nullptr;
    if (GTK_IS_CELL_VIEW(childPart))
    {
        model = gtk_combo_box_get_model(GTK_COMBO_BOX(m_widget));
        GtkTreeIter iter;
        if (gtk_tree_model_get_iter_first(model, &iter))
            model = nullptr;
        else
        {
            gtk_list_store_insert_with_values
            (
                GTK_LIST_STORE(model),
                nullptr,                // No output iterator.
                -1,                     // Position: append.
                m_stringCellIndex,      // Text column index.
                "Gg",                   // This column value.
                -1                      // Terminate the list of values.
            );
        }
    }
#endif
#endif // __WXGTK4__/!__WXGTK4__

    // We are interested in the difference of sizes between the whole contol
    // and its child part. I.e. arrow, separators, etc.
    GtkRequisition req;
    gtk_widget_get_preferred_size(childPart, nullptr, &req);
    wxSize tsize(GTKGetPreferredSize(m_widget));

#ifdef __WXGTK4__
    if ( addedTemporaryItem )
        gtk_string_list_remove(m_itemModel, 0);
#elif defined(__WXGTK3__)
    if (model)
        gtk_list_store_clear(GTK_LIST_STORE(model));
#endif

    tsize.x -= req.width;
    if (tsize.x < 0)
        tsize.x = 0;
    tsize.x += xlen;

    // For a wxChoice, not for wxComboBox, add some margins
    if ( !GTK_IS_ENTRY(childPart) )
        tsize.IncBy(5, 0);

    // Perhaps the user wants something different from CharHeight
    if ( ylen > 0 )
        tsize.IncBy(0, ylen - GetCharHeight());

    return tsize;
}

void wxChoice::DoApplyWidgetStyle(GtkRcStyle *style)
{
    GTKApplyStyle(m_widget, style);
#ifndef __WXGTK4__
    GTKApplyStyle(wxGTKComboBoxGetChild(m_widget), style);
#endif
}

// static
wxVisualAttributes
wxChoice::GetClassDefaultAttributes(wxWindowVariant WXUNUSED(variant))
{
#ifdef __WXGTK4__
    return GetDefaultAttributesFromGTKWidget(gtk_menu_button_new());
#else
    return GetDefaultAttributesFromGTKWidget(gtk_combo_box_new());
#endif
}

#endif // wxUSE_CHOICE || wxUSE_COMBOBOX
