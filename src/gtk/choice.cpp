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
#include "wx/gtk/private/object.h"
#include "wx/gtk/private/value.h"
#include "wx/gtk/private/gtk3-compat.h"

// ----------------------------------------------------------------------------
// GTK callbacks
// ----------------------------------------------------------------------------

#ifdef __WXGTK4__

// GtkComboBox is deprecated since GTK 4.10 and GtkDropDown replaces it. The
// two differ in more than their names, and these helpers hold the differences
// in one place.
namespace
{

// GTK counts "nothing selected" as GTK_INVALID_LIST_POSITION, wx as
// wxNOT_FOUND, and neither is representable in the other's type by accident.
inline guint wxGTKSelectionToGTK(int n)
{
    return n == wxNOT_FOUND ? GTK_INVALID_LIST_POSITION : guint(n);
}

inline int wxGTKSelectionFromGTK(guint n)
{
    return n == GTK_INVALID_LIST_POSITION ? wxNOT_FOUND : int(n);
}

inline GtkStringList* wxGTKChoiceList(GtkWidget* widget)
{
    return GTK_STRING_LIST(gtk_drop_down_get_model(GTK_DROP_DOWN(widget)));
}

// The popover and the toggle button are both direct children of a
// GtkDropDown, unlike GtkComboBox where the button sat three levels down
// behind a cell view. Measured in docs/gtk/probes/gtk4-dropdown-parts.c.
GtkWidget* wxGTKChoiceChildOfType(GtkWidget* widget, GType type)
{
    for ( GtkWidget* c = gtk_widget_get_first_child(widget); c;
          c = gtk_widget_get_next_sibling(c) )
    {
        if ( G_TYPE_CHECK_INSTANCE_TYPE(c, type) )
            return c;
    }

    return nullptr;
}

} // anonymous namespace

#endif // __WXGTK4__

extern "C" {

#ifdef __WXGTK4__

// GtkDropDown has no "changed" signal: the selection is a plain property.
static void
gtk_choice_changed_callback( GObject *WXUNUSED(widget),
                             GParamSpec *WXUNUSED(pspec),
                             wxChoice *choice )
{
    choice->SendSelectionChangedEvent(wxEVT_CHOICE);
}

// GtkDropDown's own label does not ellipsize, and a choice whose strings do
// not fit then shows the tail of one rather than the head -- which for a short
// string can be nothing at all. GtkComboBoxText needed the same treatment
// through its cell renderer; here it takes a list item factory.
static void
wx_gtk_choice_label_setup(GtkListItemFactory* WXUNUSED(factory),
                          GtkListItem* item, gpointer WXUNUSED(data))
{
    GtkWidget* const label = gtk_label_new(nullptr);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_list_item_set_child(item, label);
}

static void
wx_gtk_choice_label_bind(GtkListItemFactory* WXUNUSED(factory),
                         GtkListItem* item, gpointer WXUNUSED(data))
{
    GtkStringObject* const obj =
        GTK_STRING_OBJECT(gtk_list_item_get_item(item));
    gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(item)),
                       gtk_string_object_get_string(obj));
}

#else // !__WXGTK4__

static void
gtk_choice_changed_callback( GtkWidget *WXUNUSED(widget), wxChoice *choice )
{
    choice->SendSelectionChangedEvent(wxEVT_CHOICE);
}

#endif // __WXGTK4__/!__WXGTK4__

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

// GtkComboBox stopped being a GtkBin under GTK4, but it kept a direct
// accessor for the child which used to be reached through one. Under GTK4
// there is no GtkComboBox here at all any more.
static inline GtkWidget* wxGTKComboBoxGetChild(GtkWidget* combo)
{
    return gtk_bin_get_child(GTK_BIN(combo));
}

#endif // !__WXGTK4__

//-----------------------------------------------------------------------------
// wxChoice
//-----------------------------------------------------------------------------

void wxChoice::Init()
{
    m_strings = nullptr;
    m_stringCellIndex = 0;
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
    // GtkDropDown takes its model at construction and owns it from then on.
    m_widget = gtk_drop_down_new(G_LIST_MODEL(gtk_string_list_new(nullptr)),
                                 nullptr);

    // See wx_gtk_choice_label_setup() for why the default label will not do.
    {
        wxGtkObject<GtkListItemFactory>
            factory(gtk_signal_list_item_factory_new());
        g_signal_connect(factory, "setup",
                         G_CALLBACK(wx_gtk_choice_label_setup), nullptr);
        g_signal_connect(factory, "bind",
                         G_CALLBACK(wx_gtk_choice_label_bind), nullptr);
        gtk_drop_down_set_factory(GTK_DROP_DOWN(m_widget), factory);
    }

    // A fresh wxChoice has nothing selected. GtkDropDown would select the
    // first item as soon as one exists, so say so here and again after every
    // insertion; see DoInsertItems().
    gtk_drop_down_set_selected(GTK_DROP_DOWN(m_widget),
                               GTK_INVALID_LIST_POSITION);
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

#ifdef __WXGTK4__
    g_signal_connect_after (m_widget, "notify::selected",
                            G_CALLBACK (gtk_choice_changed_callback), this);
#else
    g_signal_connect_after (m_widget, "changed",
                            G_CALLBACK (gtk_choice_changed_callback), this);
#endif

#ifdef __WXGTK4__
    // GtkDropDown is much plainer than GtkComboBoxText was: the button that
    // takes the pointer is a direct child, not three levels down behind a cell
    // view. Measured in docs/gtk/probes/gtk4-dropdown-parts.c.
    auto button = wxGTKChoiceChildOfType(m_widget, GTK_TYPE_TOGGLE_BUTTON);
    wxCHECK_MSG( button, true, "No toggle button in GtkDropDown?" );

    GtkEventController* const motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter",
                     G_CALLBACK(wx_gtk_choice_enter_notify), this);
    g_signal_connect(motion, "leave",
                     G_CALLBACK(wx_gtk_choice_leave_notify), this);
    gtk_widget_add_controller(button, motion);
#elif defined(__WXGTK3__)
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
    auto cellView = wxGTKComboBoxGetChild(m_widget);
    wxCHECK_MSG( cellView, true, "No cell view in GtkComboBoxText?" );

    auto box = gtk_widget_get_parent(cellView);

    auto button = gtk_widget_get_parent(box);
    wxCHECK_MSG( GTK_IS_TOGGLE_BUTTON(button), true,
                 "Unexpected grandparent of GtkCellView in GtkComboBoxText" );

    g_signal_connect(button, "enter_notify_event",
                     G_CALLBACK(wx_gtk_choice_enter_notify), this);
    g_signal_connect(button, "leave_notify_event",
                     G_CALLBACK(wx_gtk_choice_leave_notify), this);
#endif // __WXGTK4__/__WXGTK3__

    return true;
}

wxChoice::~wxChoice()
{
    Clear();
    delete m_strings;

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
    // GtkDropDown has no "popup-shown" property -- its full property list is
    // printed by docs/gtk/probes/gtk4-dropdown-parts.c -- but its popover is a
    // direct child and knows whether it is up.
    //
    // Don't send "focus lost" events if the focus is grabbed by our own popup:
    // it counts as part of this window, even though wx doesn't know about it.
    if ( GtkWidget* const popover =
            wxGTKChoiceChildOfType(m_widget, GTK_TYPE_POPOVER) )
    {
        if ( gtk_widget_get_visible(popover) )
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
    GtkStringList* const list = wxGTKChoiceList(m_widget);

    // Adding the first item to an empty GtkDropDown makes GTK select it, and a
    // wxChoice must not do that: a freshly filled one has nothing selected.
    // So remember what was selected and put it back.  That GTK behaves this
    // way is pinned in build/tools/gtk4-invariants.c.
    const int selOld = GetSelection();

    {
        wxGtkEventsDisabler<wxChoice> noEvents(this);

        for ( int i = 0; i < count; ++i )
        {
            n = pos + i;
            // If sorted, use this wxSortedArrayStrings to determine
            // the right insertion point
            if (m_strings)
                n = m_strings->Add(items[i]);

            const char* const additions[] =
                { items[i].utf8_str().data(), nullptr };
            gtk_string_list_splice(list, guint(n), 0, additions);

            m_clientData.Insert( nullptr, n );
            AssignNewItemClientData(n, clientData, i, type);
        }

        gtk_drop_down_set_selected(GTK_DROP_DOWN(m_widget),
                                   wxGTKSelectionToGTK(selOld));
    }
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
    if ( GtkStringList* const list = wxGTKChoiceList(m_widget) )
    {
        gtk_string_list_splice(list, 0,
                               g_list_model_get_n_items(G_LIST_MODEL(list)),
                               nullptr);
    }
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
    {
        wxGtkEventsDisabler<wxChoice> noEvents(this);
        gtk_string_list_remove(wxGTKChoiceList(m_widget), guint(n));
    }
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
#endif

    m_clientData.RemoveAt( n );
    if ( m_strings )
        m_strings->RemoveAt( n );

    InvalidateBestSize();
}

int wxChoice::FindString( const wxString &item, bool bCase ) const
{
    wxCHECK_MSG( m_widget != nullptr, wxNOT_FOUND, wxT("invalid control") );

#ifdef __WXGTK4__
    GtkStringList* const list = wxGTKChoiceList(m_widget);
    const guint items = g_list_model_get_n_items(G_LIST_MODEL(list));
    for ( guint i = 0; i < items; i++ )
    {
        const wxString str = wxString::FromUTF8Unchecked(
                                gtk_string_list_get_string(list, i));
        if (item.IsSameAs( str, bCase ) )
            return int(i);
    }

    return wxNOT_FOUND;
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

    return wxNOT_FOUND;
#endif // __WXGTK4__/!__WXGTK4__
}

int wxChoice::GetSelection() const
{
#ifdef __WXGTK4__
    return wxGTKSelectionFromGTK(
                gtk_drop_down_get_selected(GTK_DROP_DOWN(m_widget)));
#else
    return gtk_combo_box_get_active( GTK_COMBO_BOX( m_widget ) );
#endif
}

void wxChoice::SetString(unsigned int n, const wxString &text)
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid control") );
    wxCHECK_RET( IsValid(n), wxT("invalid index") );

#ifdef __WXGTK4__
    // A GtkStringList cannot replace one string, so splice one out and one in.
    // A model change is where GTK moves the selection, so put it back.
    {
        wxGtkEventsDisabler<wxChoice> noEvents(this);

        const int selOld = GetSelection();
        const char* const additions[] = { text.utf8_str().data(), nullptr };
        gtk_string_list_splice(wxGTKChoiceList(m_widget), guint(n), 1, additions);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(m_widget),
                                   wxGTKSelectionToGTK(selOld));
    }
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
#endif

    InvalidateBestSize();
}

wxString wxChoice::GetString(unsigned int n) const
{
    wxCHECK_MSG( m_widget != nullptr, wxEmptyString, wxT("invalid control") );

#ifdef __WXGTK4__
    GtkStringList* const list = wxGTKChoiceList(m_widget);
    if ( n >= g_list_model_get_n_items(G_LIST_MODEL(list)) )
    {
        wxFAIL_MSG( "invalid index" );
        return wxString();
    }

    return wxString::FromUTF8Unchecked(gtk_string_list_get_string(list, n));
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
#endif
}

unsigned int wxChoice::GetCount() const
{
    wxCHECK_MSG( m_widget != nullptr, 0, wxT("invalid control") );

#ifdef __WXGTK4__
    return g_list_model_get_n_items(G_LIST_MODEL(wxGTKChoiceList(m_widget)));
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
#endif
}

void wxChoice::SetSelection( int n )
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid control") );

    wxGtkEventsDisabler<wxChoice> noEvents(this);

#ifdef __WXGTK4__
    gtk_drop_down_set_selected(GTK_DROP_DOWN(m_widget),
                               wxGTKSelectionToGTK(n));
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
    g_signal_handlers_block_by_func(m_widget,
                                (gpointer) gtk_choice_changed_callback, this);
}

void wxChoice::GTKEnableEvents()
{
    g_signal_handlers_unblock_by_func(m_widget,
                                (gpointer) gtk_choice_changed_callback, this);
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

    // a GtkEntry for wxComboBox and a GtkCellView for wxChoice
#ifdef __WXGTK4__
    // WIP: under GTK4 a wxChoice is a GtkDropDown, whose analogue of the cell
    // view is the label the factory puts inside the toggle button.
    GtkWidget* childPart = wxGTKChoiceChildOfType(m_widget, GTK_TYPE_TOGGLE_BUTTON);
    if ( !childPart )
        childPart = m_widget;
#else
    GtkWidget* childPart = wxGTKComboBoxGetChild(m_widget);
#endif

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

    // We are interested in the difference of sizes between the whole contol
    // and its child part. I.e. arrow, separators, etc.
    GtkRequisition req;
    gtk_widget_get_preferred_size(childPart, nullptr, &req);
    wxSize tsize(GTKGetPreferredSize(m_widget));

#ifdef __WXGTK3__
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
#ifdef __WXGTK4__
    if ( GtkWidget* const button =
            wxGTKChoiceChildOfType(m_widget, GTK_TYPE_TOGGLE_BUTTON) )
        GTKApplyStyle(button, style);
#else
    GTKApplyStyle(wxGTKComboBoxGetChild(m_widget), style);
#endif
}

// static
wxVisualAttributes
wxChoice::GetClassDefaultAttributes(wxWindowVariant WXUNUSED(variant))
{
    return GetDefaultAttributesFromGTKWidget(gtk_combo_box_new());
}

#endif // wxUSE_CHOICE || wxUSE_COMBOBOX
