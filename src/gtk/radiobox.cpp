/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/radiobox.cpp
// Purpose:
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_RADIOBOX

#include "wx/radiobox.h"

#if wxUSE_TOOLTIPS
    #include "wx/tooltip.h"
#endif

#include "wx/gtk/private.h"
#include "wx/gtk/private/gtk3-compat.h"

// GTK4 merged GtkRadioButton into GtkCheckButton: a check button with a group
// *is* a radio button, so there is one type fewer rather than a replacement to
// design. Grouping works by pointing one button at another instead of passing
// a GSList from one construction call to the next.
#ifdef __WXGTK4__
    typedef GtkCheckButton wxGtkRadioButton;
    #define WX_GTK_RADIO_BUTTON(w) GTK_CHECK_BUTTON(w)
#else
    typedef GtkRadioButton wxGtkRadioButton;
    #define WX_GTK_RADIO_BUTTON(w) GTK_RADIO_BUTTON(w)
#endif

//-----------------------------------------------------------------------------
// wxGTKRadioButtonInfo
//-----------------------------------------------------------------------------
// structure internally used by wxRadioBox to store its child buttons

class wxGTKRadioButtonInfo
{
public:
    explicit wxGTKRadioButtonInfo( wxGtkRadioButton * abutton )
    : button( abutton ) {}

    wxGtkRadioButton * button;
    wxRect             rect;
};

//-----------------------------------------------------------------------------
// data
//-----------------------------------------------------------------------------

extern bool          g_blockEventsOnDrag;

//-----------------------------------------------------------------------------
// "clicked"
//-----------------------------------------------------------------------------

extern "C" {
#ifdef __WXGTK4__
// GtkCheckButton no longer derives from GtkToggleButton, and it is "toggled"
// rather than "clicked" that reports the change.
static void gtk_radiobutton_clicked_callback( GtkCheckButton *button, wxRadioBox *rb )
{
    if (g_blockEventsOnDrag) return;

    if (!gtk_check_button_get_active(button)) return;
#else
static void gtk_radiobutton_clicked_callback( GtkToggleButton *button, wxRadioBox *rb )
{
    if (g_blockEventsOnDrag) return;

    if (!gtk_toggle_button_get_active(button)) return;
#endif

    wxCommandEvent event( wxEVT_RADIOBOX, rb->GetId() );
    event.SetInt( rb->GetSelection() );
    event.SetString( rb->GetStringSelection() );
    event.SetEventObject( rb );
    rb->HandleWindowEvent(event);
}
}

//-----------------------------------------------------------------------------
// "key_press_event"
//-----------------------------------------------------------------------------

// Common part of the GTK3 and GTK4 key handlers below: they only differ in how
// GTK hands over the key, not in what wxRadioBox does with it.
static bool
wxGTKRadioBoxHandleKey(GtkWidget* widget, wxRadioBox* rb,
                       guint keyval, GdkModifierType state)
{
    if (g_blockEventsOnDrag) return false;

    if ( ((keyval == GDK_KEY_Tab) ||
          (keyval == GDK_KEY_ISO_Left_Tab)) &&
         rb->GetParent() && (rb->GetParent()->HasFlag( wxTAB_TRAVERSAL)) )
    {
        wxNavigationKeyEvent new_event;
        new_event.SetEventObject( rb->GetParent() );
        // GDK reports GDK_ISO_Left_Tab for SHIFT-TAB
        new_event.SetDirection( (keyval == GDK_KEY_Tab) );
        // CTRL-TAB changes the (parent) window, i.e. switch notebook page
        new_event.SetWindowChange( (state & GDK_CONTROL_MASK) != 0 );
        new_event.SetCurrentFocus( rb );
        return rb->GetParent()->HandleWindowEvent(new_event);
    }

    if ((keyval != GDK_KEY_Up) &&
        (keyval != GDK_KEY_Down) &&
        (keyval != GDK_KEY_Left) &&
        (keyval != GDK_KEY_Right))
    {
        return false;
    }

    const auto begin = rb->m_buttonsInfo.begin();
    const auto end = rb->m_buttonsInfo.end();

    auto it = begin;
    while( it != end && GTK_WIDGET( it->button ) != widget )
    {
        ++it;
    }
    if (it == end)
    {
        return false;
    }

    if ((keyval == GDK_KEY_Up) ||
        (keyval == GDK_KEY_Left))
    {
        if (it == begin)
            it = end;

        --it;
    }
    else
    {
        ++it;

        if (it == end)
            it = begin;
    }

    GtkWidget *button = GTK_WIDGET( it->button );

    gtk_widget_grab_focus( button );

    return true;
}

extern "C" {
#ifdef __WXGTK4__
// GTK4 has no key-press-event: keys arrive through a GtkEventControllerKey.
static gboolean gtk_radiobox_keypress_callback( GtkEventControllerKey* controller,
                                                guint keyval,
                                                guint WXUNUSED(keycode),
                                                GdkModifierType state,
                                                wxRadioBox *rb )
{
    GtkWidget* const widget =
        gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));

    return wxGTKRadioBoxHandleKey(widget, rb, keyval, state);
}
#else
static gint gtk_radiobox_keypress_callback( GtkWidget *widget, GdkEventKey *gdk_event, wxRadioBox *rb )
{
    return wxGTKRadioBoxHandleKey(widget, rb, gdk_event->keyval,
                                  GdkModifierType(gdk_event->state));
}
#endif
}

#ifndef __WXGTK4__
extern "C" {
static gint gtk_radiobutton_focus_out( GtkWidget * WXUNUSED(widget),
                                       GdkEventFocus *WXUNUSED(event),
                                       wxRadioBox *win )
{
    // NB: This control is composed of several GtkRadioButton widgets and
    //     when focus changes from one of them to another in the same
    //     wxRadioBox, we get a focus-out event followed by focus-in for
    //     another GtkRadioButton owned by the same control. We don't want
    //     to generate two spurious wxEVT_SET_FOCUS events in this case,
    //     so we defer sending wx events until idle time.
    win->GTKHandleFocusOut();

    // never stop the signal emission, it seems to break the kbd handling
    // inside the radiobox
    return FALSE;
}

static gint gtk_radiobutton_focus_in( GtkWidget * WXUNUSED(widget),
                                      GdkEventFocus *WXUNUSED(event),
                                      wxRadioBox *win )
{
    win->GTKHandleFocusIn();

    // never stop the signal emission, it seems to break the kbd handling
    // inside the radiobox
    return FALSE;
}
}
#endif // !__WXGTK4__

#ifndef __WXGTK4__
// GTK4 removed the size-allocate signal, so the button rectangles are no longer
// tracked as they change; GetItemFromPoint() computes them on demand instead,
// which is the only place they were ever used.
extern "C" {
static void gtk_radiobutton_size_allocate( GtkWidget *widget,
                                           GtkAllocation * alloc,
                                           wxRadioBox *win )
{
    for ( auto& info : win->m_buttonsInfo )
    {
        if (widget == GTK_WIDGET(info.button))
        {
            const wxPoint origin = win->GetPosition();
            wxRect rect = wxRect( alloc->x - origin.x, alloc->y - origin.y,
                                  alloc->width, alloc->height );
            info.rect = rect;
            break;
        }
    }
}
}
#endif // !__WXGTK4__

#ifndef __WXGTK3__
extern "C" {
static gboolean expose_event(GtkWidget* widget, GdkEventExpose*, wxWindow*)
{
    const GtkAllocation& a = widget->allocation;
    gtk_paint_flat_box(gtk_widget_get_style(widget), gtk_widget_get_window(widget),
        GTK_STATE_NORMAL, GTK_SHADOW_NONE, nullptr, widget, "", a.x, a.y, a.width, a.height);
    return false;
}
}
#endif

//-----------------------------------------------------------------------------
// wxRadioBox
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxRadioBox, wxControl);

wxRadioBox::wxRadioBox() = default;

wxRadioBox::wxRadioBox(wxWindow *parent,
           wxWindowID id,
           const wxString& title,
           const wxPoint& pos,
           const wxSize& size,
           int n,
           const wxString choices[],
           int majorDim,
           long style,
           const wxValidator& val,
           const wxString& name)
{
    Create( parent, id, title, pos, size, n, choices, majorDim, style, val, name );
}

wxRadioBox::wxRadioBox(wxWindow *parent,
           wxWindowID id,
           const wxString& title,
           const wxPoint& pos,
           const wxSize& size,
           const wxArrayString& choices,
           int majorDim,
           long style,
           const wxValidator& val,
           const wxString& name)
{
    Create( parent, id, title, pos, size, choices, majorDim, style, val, name );
}

bool wxRadioBox::Create( wxWindow *parent, wxWindowID id,
                         const wxString& title,
                         const wxPoint &pos, const wxSize &size,
                         const wxArrayString& choices, int majorDim,
                         long style, const wxValidator& validator,
                         const wxString &name )
{
    wxCArrayString chs(choices);

    return Create( parent, id, title, pos, size, chs.GetCount(),
                   chs.GetStrings(), majorDim, style, validator, name );
}

bool wxRadioBox::Create( wxWindow *parent, wxWindowID id, const wxString& title,
                         const wxPoint &pos, const wxSize &size,
                         int n, const wxString choices[], int majorDim,
                         long style, const wxValidator& validator,
                         const wxString &name )
{
    if (!PreCreation( parent, pos, size ) ||
        !CreateBase( parent, id, pos, size, style, validator, name ))
    {
        wxFAIL_MSG( wxT("wxRadioBox creation failed") );
        return false;
    }

    m_widget = GTKCreateFrame(title);
    g_object_ref(m_widget);
    if ( HasFlag(wxNO_BORDER) )
    {
        // If we don't do this here, the wxNO_BORDER style is ignored in Show()
#ifdef __WXGTK4__
        // GtkShadowType is gone: a frame's border is CSS now, and the "flat"
        // style class is how a themed frame is asked not to draw one.
        gtk_widget_add_css_class(m_widget, "flat");
#else
        gtk_frame_set_shadow_type(GTK_FRAME(m_widget), GTK_SHADOW_NONE);
#endif
    }


    // majorDim may be 0 if all trailing parameters were omitted, so don't
    // assert here but just use the correct value for it
    SetMajorDim(majorDim == 0 ? n : majorDim, style);


    unsigned int num_of_cols = GetColumnCount();
    unsigned int num_of_rows = GetRowCount();

    wxGtkRadioButton *rbtn = nullptr;

#ifdef __WXGTK4__
    GtkWidget* grid = gtk_grid_new();
    gtk_frame_set_child(GTK_FRAME(m_widget), grid);
#elif defined(__WXGTK3__)
    GtkWidget* grid = gtk_grid_new();
    gtk_widget_set_visible(grid, TRUE);
    gtk_container_add(GTK_CONTAINER(m_widget), grid);
#else
    GtkWidget *table = gtk_table_new( num_of_rows, num_of_cols, FALSE );
    gtk_table_set_col_spacings( GTK_TABLE(table), 1 );
    gtk_table_set_row_spacings( GTK_TABLE(table), 1 );
    gtk_widget_set_visible(table, TRUE);
    gtk_container_add( GTK_CONTAINER(m_widget), table );
#endif

#ifdef __WXGTK4__
    // GTK4 groups check buttons by pointing each at the first one, rather than
    // by threading a GSList through the construction calls.
    wxGtkRadioButton *radio_button_group = nullptr;
#else
    GSList *radio_button_group = nullptr;
#endif
    for (unsigned int i = 0; i < (unsigned int)n; i++)
    {
#ifndef __WXGTK4__
        if ( i != 0 )
            radio_button_group = gtk_radio_button_get_group( GTK_RADIO_BUTTON(rbtn) );
#endif

        // Process mnemonic in the label
        wxString label;
        bool hasMnemonic = false;
        for ( wxString::const_iterator pc = choices[i].begin();
              pc != choices[i].end(); ++pc )
        {
            if ( *pc == wxS('_') )
            {
                // If we have a literal underscore character in the label
                // containing mnemonic, two underscores should be used.
                if ( hasMnemonic )
                    label += wxS('_');
            }
            else if ( *pc == wxS('&') )
            {
                ++pc; // skip it
                 if ( pc == choices[i].end() )
                 {
                     break;
                 }
                 else if ( *pc != wxS('&') )
                 {
                     if ( !hasMnemonic )
                     {
                         hasMnemonic = true;
                         // So far we assumed that label doesn't contain mnemonic
                         // and therefore single underscore characters were not
                         // replaced by two underscores. Now we have to double
                         // all existing underscore characters.
                         label.Replace(wxS("_"), wxS("__"));
                         label += wxS('_');
                     }
                     else
                     {
                         wxFAIL_MSG(wxT("duplicate mnemonic char in radio button label"));
                     }
                 }
            }

            label += *pc;
        }
#ifdef __WXGTK4__
        if ( hasMnemonic )
            rbtn = GTK_CHECK_BUTTON( gtk_check_button_new_with_mnemonic( label.utf8_str() ) );
        else
            rbtn = GTK_CHECK_BUTTON( gtk_check_button_new_with_label( label.utf8_str() ) );

        if ( radio_button_group )
            gtk_check_button_set_group( rbtn, radio_button_group );
        else
            radio_button_group = rbtn;

        GtkEventController* const key = gtk_event_controller_key_new();
        g_signal_connect (key, "key-pressed",
                          G_CALLBACK (gtk_radiobox_keypress_callback), this);
        gtk_widget_add_controller( GTK_WIDGET(rbtn), key );
#else
        if ( hasMnemonic )
            rbtn = GTK_RADIO_BUTTON( gtk_radio_button_new_with_mnemonic( radio_button_group, label.utf8_str() ) );
        else
            rbtn = GTK_RADIO_BUTTON( gtk_radio_button_new_with_label( radio_button_group, label.utf8_str() ) );

        gtk_widget_set_visible(GTK_WIDGET(rbtn), TRUE);

        g_signal_connect (rbtn, "key_press_event",
                          G_CALLBACK (gtk_radiobox_keypress_callback), this);
#endif

        m_buttonsInfo.emplace_back(rbtn);

#ifdef __WXGTK3__
        int left, top;
        if (HasFlag(wxRA_SPECIFY_COLS))
        {
            left = i % num_of_cols;
            top = i / num_of_cols;
        }
        else
        {
            left = i / num_of_rows;
            top = i % num_of_rows;
        }
        gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(rbtn), left, top, 1, 1);
#elif !defined(__WXGTK3__)
        if (HasFlag(wxRA_SPECIFY_COLS))
        {
            int left = i%num_of_cols;
            int right = (i%num_of_cols) + 1;
            int top = i/num_of_cols;
            int bottom = (i/num_of_cols)+1;
            gtk_table_attach( GTK_TABLE(table), GTK_WIDGET(rbtn), left, right, top, bottom,
                  GTK_FILL, GTK_FILL, 1, 1 );
        }
        else
        {
            int left = i/num_of_rows;
            int right = (i/num_of_rows) + 1;
            int top = i%num_of_rows;
            int bottom = (i%num_of_rows)+1;
            gtk_table_attach( GTK_TABLE(table), GTK_WIDGET(rbtn), left, right, top, bottom,
                  GTK_FILL, GTK_FILL, 1, 1 );
        }
#endif

        ConnectWidget( GTK_WIDGET(rbtn) );

#ifdef __WXGTK4__
        if (!i)
            gtk_check_button_set_active( rbtn, TRUE );

        g_signal_connect (rbtn, "toggled",
                          G_CALLBACK (gtk_radiobutton_clicked_callback), this);
#else
        if (!i)
            gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(rbtn), TRUE );

        g_signal_connect (rbtn, "clicked",
                          G_CALLBACK (gtk_radiobutton_clicked_callback), this);
        g_signal_connect (rbtn, "focus_in_event",
                          G_CALLBACK (gtk_radiobutton_focus_in), this);
        g_signal_connect (rbtn, "focus_out_event",
                          G_CALLBACK (gtk_radiobutton_focus_out), this);
        g_signal_connect (rbtn, "size_allocate",
                          G_CALLBACK (gtk_radiobutton_size_allocate), this);
#endif
    }

    m_parent->DoAddChild( this );

    wxControl::SetLabel(title);
    PostCreation(size);

    return true;
}

wxRadioBox::~wxRadioBox()
{
    for ( const auto& info : m_buttonsInfo )
    {
        GtkWidget *button = GTK_WIDGET( info.button );
        GTKDisconnect(button);
#ifdef __WXGTK4__
        wx_gtk_widget_remove_from_parent( button );
#else
        gtk_widget_destroy( button );
#endif
    }
}

bool wxRadioBox::Show( bool show )
{
    wxCHECK_MSG( m_widget != nullptr, false, wxT("invalid radiobox") );

    if (!wxControl::Show(show))
    {
        // nothing to do
        return false;
    }

    if ( HasFlag(wxNO_BORDER) )
        gtk_widget_set_visible(m_widget, FALSE);

    for ( const auto& info : m_buttonsInfo )
    {
        GtkWidget *button = GTK_WIDGET( info.button );

        if (show)
            gtk_widget_set_visible(button, TRUE);
        else
            gtk_widget_set_visible(button, FALSE);
    }

    return true;
}

void wxRadioBox::SetSelection( int n )
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid radiobox") );

    wxCHECK_RET( n >= 0 && n < (int)m_buttonsInfo.size(), wxT("radiobox wrong index") );

    GtkDisableEvents();

#ifdef __WXGTK4__
    // GtkCheckButton is not a GtkToggleButton under GTK4: it has an equivalent
    // API of its own, and the cast is invalid rather than merely deprecated.
    gtk_check_button_set_active( m_buttonsInfo[n].button, TRUE );
#else
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( m_buttonsInfo[n].button ), 1 );
#endif

    GtkEnableEvents();
}

int wxRadioBox::GetSelection(void) const
{
    wxCHECK_MSG( m_widget != nullptr, wxNOT_FOUND, wxT("invalid radiobox") );

    int count = 0;

    for ( const auto& info : m_buttonsInfo )
    {
#ifdef __WXGTK4__
        if (gtk_check_button_get_active(info.button)) return count;
#else
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(info.button))) return count;
#endif
        count++;
    }

    wxFAIL_MSG( wxT("wxRadioBox none selected") );

    return wxNOT_FOUND;
}

wxString wxRadioBox::GetString(unsigned int n) const
{
    wxCHECK_MSG( m_widget != nullptr, wxEmptyString, wxT("invalid radiobox") );

    wxCHECK_MSG( n < m_buttonsInfo.size(), wxEmptyString, wxT("radiobox wrong index") );

#ifdef __WXGTK4__
    // GtkCheckButton owns its label internally under GTK4, so it is read and
    // written through the button rather than through a child GtkLabel.
    return wxString::FromUTF8Unchecked(
                gtk_check_button_get_label(m_buttonsInfo[n].button) );
#else
    GtkLabel* label = GTK_LABEL(gtk_bin_get_child(GTK_BIN(m_buttonsInfo[n].button)));

    return wxString::FromUTF8Unchecked( gtk_label_get_text(label) );
#endif
}

void wxRadioBox::SetLabel( const wxString& label )
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid radiobox") );

    GTKSetLabelForFrame(GTK_FRAME(m_widget), label);
}

void wxRadioBox::SetString(unsigned int n, const wxString& label)
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid radiobox") );

    wxCHECK_RET( n < m_buttonsInfo.size(), wxT("radiobox wrong index") );

#ifdef __WXGTK4__
    gtk_check_button_set_label( m_buttonsInfo[n].button, label.utf8_str() );
#else
    GtkLabel* g_label = GTK_LABEL(gtk_bin_get_child(GTK_BIN(m_buttonsInfo[n].button)));

    gtk_label_set_text( g_label, label.utf8_str() );
#endif
}

bool wxRadioBox::Enable( bool enable )
{
    // Explicitly forward to the base class just because we need to override
    // this function to prevent it from being hidden by Enable(int, bool)
    // overload.
    return wxControl::Enable(enable);
}

void wxRadioBox::DoEnable(bool enable)
{
    if ( !m_widget )
        return;

    wxControl::DoEnable(enable);

    for ( const auto& info : m_buttonsInfo )
    {
        GtkWidget *button = GTK_WIDGET( info.button );

        gtk_widget_set_sensitive( button, enable );
    }

    if (enable)
        GTKFixSensitivity();
}

bool wxRadioBox::Enable(unsigned int n, bool enable)
{
    wxCHECK_MSG( m_widget != nullptr, false, wxT("invalid radiobox") );

    wxCHECK_MSG( n < m_buttonsInfo.size(), false, wxT("radiobox wrong index") );

    // Note that there is no separate label widget to disable as well under
    // GTK4: GtkCheckButton owns its label internally and dims it with itself.
    GtkWidget *button = GTK_WIDGET( m_buttonsInfo[n].button );

    gtk_widget_set_sensitive( button, enable );

    return true;
}

bool wxRadioBox::IsItemEnabled(unsigned int n) const
{
    wxCHECK_MSG( m_widget != nullptr, false, wxT("invalid radiobox") );

    wxCHECK_MSG( n < m_buttonsInfo.size(), false, wxT("radiobox wrong index") );

    GtkWidget *button = GTK_WIDGET( m_buttonsInfo[n].button );

    // don't use GTK_WIDGET_IS_SENSITIVE() here, we want to return true even if
    // the parent radiobox is disabled
    return gtk_widget_get_sensitive(button) != 0;
}

bool wxRadioBox::Show(unsigned int n, bool show)
{
    wxCHECK_MSG( m_widget != nullptr, false, wxT("invalid radiobox") );

    wxCHECK_MSG( n < m_buttonsInfo.size(), false, wxT("radiobox wrong index") );

    GtkWidget *button = GTK_WIDGET( m_buttonsInfo[n].button );

    if (show)
        gtk_widget_set_visible(button, TRUE);
    else
        gtk_widget_set_visible(button, FALSE);

    return true;
}

bool wxRadioBox::IsItemShown(unsigned int n) const
{
    wxCHECK_MSG( m_widget != nullptr, false, wxT("invalid radiobox") );

    wxCHECK_MSG( n < m_buttonsInfo.size(), false, wxT("radiobox wrong index") );

    GtkWidget *button = GTK_WIDGET( m_buttonsInfo[n].button );

    return gtk_widget_get_visible(button) != 0;
}

unsigned int wxRadioBox::GetCount() const
{
    return m_buttonsInfo.size();
}

void wxRadioBox::GtkDisableEvents()
{
    for ( const auto& info : m_buttonsInfo )
    {
        g_signal_handlers_block_by_func(info.button,
            (gpointer)gtk_radiobutton_clicked_callback, this);
    }
}

void wxRadioBox::GtkEnableEvents()
{
    for ( const auto& info : m_buttonsInfo )
    {
        g_signal_handlers_unblock_by_func(info.button,
            (gpointer)gtk_radiobutton_clicked_callback, this);
    }
}

void wxRadioBox::DoApplyWidgetStyle(GtkRcStyle *style)
{
    GTKFrameApplyWidgetStyle(GTK_FRAME(m_widget), style);

    for ( const auto& info : m_buttonsInfo )
    {
        GtkWidget *widget = GTK_WIDGET( info.button );

        GTKApplyStyle(widget, style);
#ifndef __WXGTK4__
        GTKApplyStyle(gtk_bin_get_child(GTK_BIN(widget)), style);
#endif
    }

#ifndef __WXGTK3__
    g_signal_handlers_disconnect_by_func(m_widget, (void*)expose_event, this);
    if (m_backgroundColour.IsOk())
        g_signal_connect(m_widget, "expose-event", G_CALLBACK(expose_event), this);
#endif
}

bool wxRadioBox::GTKWidgetNeedsMnemonic() const
{
    return true;
}

void wxRadioBox::GTKWidgetDoSetMnemonic(GtkWidget* w)
{
    GTKFrameSetMnemonicWidget(GTK_FRAME(m_widget), w);
}

#if wxUSE_TOOLTIPS
void wxRadioBox::GTKApplyToolTip(const char* tip)
{
    // set this tooltip for all radiobuttons which don't have their own tips
    unsigned n = 0;
    for ( const auto& info : m_buttonsInfo )
    {
        if ( !GetItemToolTip(n++) )
        {
            wxToolTip::GTKApply(GTK_WIDGET(info.button), tip);
        }
    }
}

void wxRadioBox::DoSetItemToolTip(unsigned int n, wxToolTip *tooltip)
{
    wxCharBuffer buf;
    if ( !tooltip )
        tooltip = GetToolTip();
    if ( tooltip )
        buf = tooltip->GetTip().utf8_str();

    wxToolTip::GTKApply(GTK_WIDGET(m_buttonsInfo[n].button), buf);
}

#endif // wxUSE_TOOLTIPS

#ifndef __WXGTK4__
GdkWindow *wxRadioBox::GTKGetWindow(wxArrayGdkWindows& windows) const
{
    windows.push_back(gtk_widget_get_window(m_widget));

    for ( const auto& info : m_buttonsInfo )
    {
        GtkWidget *button = GTK_WIDGET( info.button );

        // don't put null pointers in the 'windows' array!
        if (gtk_widget_get_window(button))
            windows.push_back(gtk_widget_get_window(button));
    }

    return nullptr;
}
#endif // !__WXGTK4__

// static
wxVisualAttributes
wxRadioBox::GetClassDefaultAttributes(wxWindowVariant WXUNUSED(variant))
{
#ifdef __WXGTK4__
    return GetDefaultAttributesFromGTKWidget(gtk_check_button_new_with_label(""));
#else
    return GetDefaultAttributesFromGTKWidget(gtk_radio_button_new_with_label(nullptr, ""));
#endif
}

int wxRadioBox::GetItemFromPoint(const wxPoint& point) const
{
    const wxPoint pt = ScreenToClient(point);
    unsigned n = 0;
    for ( const auto& info : m_buttonsInfo )
    {
#ifdef __WXGTK4__
        graphene_rect_t bounds;
        if ( gtk_widget_compute_bounds(GTK_WIDGET(info.button), m_widget,
                                       &bounds) )
        {
            const wxRect rect(int(bounds.origin.x), int(bounds.origin.y),
                              int(bounds.size.width), int(bounds.size.height));
            if ( rect.Contains(pt) )
                return n;
        }
#else
        if ( info.rect.Contains(pt) )
            return n;
#endif

        ++n;
    }

    return wxNOT_FOUND;
}

#endif // wxUSE_RADIOBOX
