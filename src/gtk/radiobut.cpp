/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/radiobut.cpp
// Purpose:
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_RADIOBTN

#include "wx/radiobut.h"

#include "wx/gtk/private.h"
#include "wx/gtk/private/gtk3-compat.h"

//-----------------------------------------------------------------------------
// data
//-----------------------------------------------------------------------------

extern bool           g_blockEventsOnDrag;

//-----------------------------------------------------------------------------
// "clicked"
//-----------------------------------------------------------------------------

extern "C" {
#ifdef __WXGTK4__
// GtkCheckButton is not a GtkToggleButton any more under GTK4 -- the two are
// now unrelated widgets -- and it reports the change as "toggled" rather than
// as "clicked".
static
void gtk_radiobutton_clicked_callback( GtkCheckButton *button, wxRadioButton *rb )
{
    if (g_blockEventsOnDrag) return;

    if (!gtk_check_button_get_active(button)) return;
#else
static
void gtk_radiobutton_clicked_callback( GtkToggleButton *button, wxRadioButton *rb )
{
    if (g_blockEventsOnDrag) return;

    if (!gtk_toggle_button_get_active(button)) return;
#endif

    wxCommandEvent event( wxEVT_RADIOBUTTON, rb->GetId());
    event.SetInt( rb->GetValue() );
    event.SetEventObject( rb );
    rb->HandleWindowEvent( event );
}

#ifdef __WXGTK4__

// "toggled" only says that the selection changed, while GTK3's "clicked" said
// that the button was clicked, whether or not that changed anything. Clicking
// a radio button which is already selected therefore went unreported here,
// even though every other port sends wxEVT_RADIOBUTTON for it -- and the
// RadioButtonTestCase::Click test, which clicks the single button of its
// group, expects exactly that.
//
// So watch the click as well, and report one when the button came out of it
// selected without "toggled" having said so.

// Remember what the button was before the click, to tell the two apart.
static const char* const wxGTK_RADIO_WAS_ACTIVE = "wx-radio-was-active";

static void
wx_gtk_radio_pressed(GtkGestureClick* gesture, int, double, double, wxRadioButton*)
{
    GtkWidget* const w =
        gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));

    g_object_set_data(G_OBJECT(w), wxGTK_RADIO_WAS_ACTIVE,
                      GINT_TO_POINTER(
                          gtk_check_button_get_active(GTK_CHECK_BUTTON(w))));
}

static void
wx_gtk_radio_released(GtkGestureClick* gesture, int, double x, double y,
                      wxRadioButton* rb)
{
    if (g_blockEventsOnDrag) return;

    GtkWidget* const w =
        gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    // A release which wandered off the button does not activate it, so it must
    // not report anything either.
    if ( x < 0 || y < 0 ||
            x >= gtk_widget_get_width(w) || y >= gtk_widget_get_height(w) )
        return;

    // This runs before the button acts on the click, so the state to judge by
    // is the one recorded at press time. Already selected means the click
    // cannot change anything and "toggled" will stay silent -- exactly the
    // case that needs reporting here. Not selected means the click will select
    // it and "toggled" will report that itself, so keep quiet.
    if ( !g_object_get_data(G_OBJECT(w), wxGTK_RADIO_WAS_ACTIVE) )
        return;

    wxCommandEvent event( wxEVT_RADIOBUTTON, rb->GetId());
    event.SetInt( rb->GetValue() );
    event.SetEventObject( rb );
    rb->HandleWindowEvent( event );
}

#endif // __WXGTK4__
}

//-----------------------------------------------------------------------------
// wxRadioButton
//-----------------------------------------------------------------------------

bool wxRadioButton::Create( wxWindow *parent,
                            wxWindowID id,
                            const wxString& label,
                            const wxPoint& pos,
                            const wxSize& size,
                            long style,
                            const wxValidator& validator,
                            const wxString& name )
{
    if (!PreCreation( parent, pos, size ) ||
        !CreateBase( parent, id, pos, size, style, validator, name ))
    {
        wxFAIL_MSG( wxT("wxRadioButton creation failed") );
        return false;
    }

    // Check if this radio button should be put into an existing group. This
    // shouldn't be done if it's given a style to explicitly start a new group
    // or if it's not meant to be a part of a group at all.
#ifdef __WXGTK4__
    // A group is identified by any one of its members under GTK4, instead of
    // by a GSList holding all of them.
    GtkWidget* radioButtonGroup = nullptr;
#else
    GSList* radioButtonGroup = nullptr;
#endif
    if (!HasFlag(wxRB_GROUP) && !HasFlag(wxRB_SINGLE))
    {
        // search backward for last group start
        wxWindowList::compatibility_iterator node = parent->GetChildren().GetLast();
        for (; node; node = node->GetPrevious())
        {
            wxWindow *child = node->GetData();

            // We stop at the first previous radio button in any case as it
            // wouldn't make sense to put this button in a group with another
            // one if there is a radio button that is not part of the same
            // group between them.
            if (wxIsKindOf(child, wxRadioButton))
            {
                // Any preceding radio button can be used to get its group, not
                // necessarily one with wxRB_GROUP style, but exclude
                // wxRB_SINGLE ones as their group should never be shared.
                if (!child->HasFlag(wxRB_SINGLE))
                {
#ifdef __WXGTK4__
                    radioButtonGroup = child->m_widget;
#else
                    radioButtonGroup = gtk_radio_button_get_group(
                        GTK_RADIO_BUTTON(child->m_widget));
#endif
                }

                break;
            }
        }
    }

    // GTK does not allow a radio button to be inactive if it is the only radio
    // button in its group, so we need to work around this by creating a second
    // hidden radio button.
#ifdef __WXGTK4__
    m_widget = gtk_check_button_new_with_label( label.utf8_str() );

    if (HasFlag(wxRB_SINGLE) || !radioButtonGroup)
    {
        // GTK4 has no call for starting a group: a GtkCheckButton is in one
        // only once another button has been linked to it, and until then it is
        // an ordinary check box -- drawn as one, and switched off again by a
        // second click on it. So a button which starts its own group needs the
        // hidden partner just as much as a wxRB_SINGLE one does, or it would
        // not be a radio button at all.
        m_hiddenButton = gtk_check_button_new();
        g_object_ref_sink(m_hiddenButton);

        gtk_check_button_set_group( GTK_CHECK_BUTTON(m_widget),
                                    GTK_CHECK_BUTTON(m_hiddenButton) );
    }
    else
    {
        gtk_check_button_set_group( GTK_CHECK_BUTTON(m_widget),
                                    GTK_CHECK_BUTTON(radioButtonGroup) );
    }

    // Unlike GTK3, GTK4 doesn't make the first button of a group active by
    // itself, so a group would start out with nothing selected at all -- do it
    // explicitly.  For wxRB_SINGLE this also ensures that it is this button and
    // not the hidden one which starts out active, which GTK3 needed too.
    if (HasFlag(wxRB_SINGLE) || !radioButtonGroup)
        gtk_check_button_set_active( GTK_CHECK_BUTTON(m_widget), TRUE );
#else // !__WXGTK4__
    if (HasFlag(wxRB_SINGLE))
    {
        m_hiddenButton = gtk_radio_button_new( nullptr );
        m_widget = gtk_radio_button_new_with_label_from_widget( GTK_RADIO_BUTTON(m_hiddenButton), label.utf8_str() );
        // Since this is the second button in the group, we need to ensure it
        // is active by default and not the first hidden one.
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(m_widget), TRUE );
    }
    else
    {
        m_widget = gtk_radio_button_new_with_label( radioButtonGroup, label.utf8_str() );
    }
#endif // __WXGTK4__/!__WXGTK4__

    g_object_ref(m_widget);

    SetLabel(label);

#ifdef __WXGTK4__
    g_signal_connect_after (m_widget, "toggled",
                            G_CALLBACK (gtk_radiobutton_clicked_callback), this);

    // See the comment above wx_gtk_radio_pressed(): "toggled" alone misses a
    // click on an already selected button.
    GtkGesture* const clickGesture = gtk_gesture_click_new();
    g_signal_connect (clickGesture, "pressed",
                      G_CALLBACK (wx_gtk_radio_pressed), this);
    g_signal_connect (clickGesture, "released",
                      G_CALLBACK (wx_gtk_radio_released), this);
    gtk_event_controller_set_propagation_phase(
        GTK_EVENT_CONTROLLER(clickGesture), GTK_PHASE_CAPTURE);
    gtk_widget_add_controller (m_widget, GTK_EVENT_CONTROLLER(clickGesture));
#else
    g_signal_connect_after (m_widget, "clicked",
                            G_CALLBACK (gtk_radiobutton_clicked_callback), this);
#endif

    m_parent->DoAddChild( this );

    PostCreation(size);

    return true;
}

#ifdef __WXGTK4__

wxRadioButton::~wxRadioButton()
{
    // This one is never added to any widget hierarchy, so nothing else will
    // ever drop the reference we took on it in Create().
    if ( m_hiddenButton )
        g_object_unref(m_hiddenButton);
}

#endif // __WXGTK4__

void wxRadioButton::SetLabel( const wxString& label )
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid radiobutton") );

    // save the original label
    wxControlBase::SetLabel(label);

#ifdef __WXGTK4__
    // GtkCheckButton owns its label internally under GTK4: there is no child
    // GtkLabel to reach for.
    const wxString labelGTK = GTKConvertMnemonics(label);
    gtk_check_button_set_use_underline( GTK_CHECK_BUTTON(m_widget), TRUE );
    gtk_check_button_set_label( GTK_CHECK_BUTTON(m_widget), labelGTK.utf8_str() );
#else
    GTKSetLabelForLabel(GTK_LABEL(gtk_bin_get_child(GTK_BIN(m_widget))), label);
#endif
}

void wxRadioButton::SetValue( bool val )
{
    wxCHECK_RET( m_widget != nullptr, wxT("invalid radiobutton") );

    if (val == GetValue())
        return;

    g_signal_handlers_block_by_func(
        m_widget, (void*)gtk_radiobutton_clicked_callback, this);

    if (val)
    {
#ifdef __WXGTK4__
        gtk_check_button_set_active( GTK_CHECK_BUTTON(m_widget), TRUE );
#else
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(m_widget), TRUE );
#endif
    }
    else
    {
        // Normal radio buttons can only be turned off by turning on another
        // button in the same group, but the single ones can be turned off
        // manually, which is implemented by turning a hidden button on, as
        // it's the only way to do it with GTK.
        if (HasFlag(wxRB_SINGLE))
        {
#ifdef __WXGTK4__
            gtk_check_button_set_active( GTK_CHECK_BUTTON(m_hiddenButton), TRUE );
#else
            gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(m_hiddenButton), TRUE );
#endif
        }
    }

    g_signal_handlers_unblock_by_func(
        m_widget, (void*)gtk_radiobutton_clicked_callback, this);
}

bool wxRadioButton::GetValue() const
{
    wxCHECK_MSG( m_widget != nullptr, false, wxT("invalid radiobutton") );

#ifdef __WXGTK4__
    return gtk_check_button_get_active(GTK_CHECK_BUTTON(m_widget)) != 0;
#else
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m_widget)) != 0;
#endif
}

void wxRadioButton::DoEnable(bool enable)
{
    if ( !m_widget )
        return;

    base_type::DoEnable(enable);

#ifndef __WXGTK4__
    // The label is a child widget of its own under GTK3 and needs to follow;
    // under GTK4 it belongs to the GtkCheckButton itself, which base_type has
    // already taken care of.
    gtk_widget_set_sensitive(gtk_bin_get_child(GTK_BIN(m_widget)), enable);
#endif

    if (enable)
        GTKFixSensitivity();
}

void wxRadioButton::DoApplyWidgetStyle(GtkRcStyle *style)
{
    GTKApplyStyle(m_widget, style);
#ifndef __WXGTK4__
    GTKApplyStyle(gtk_bin_get_child(GTK_BIN(m_widget)), style);
#endif
}

#ifndef __WXGTK4__
GdkWindow *
wxRadioButton::GTKGetWindow(wxArrayGdkWindows& WXUNUSED(windows)) const
{
    return gtk_button_get_event_window(GTK_BUTTON(m_widget));
}
#endif // !__WXGTK4__

// static
wxVisualAttributes
wxRadioButton::GetClassDefaultAttributes(wxWindowVariant WXUNUSED(variant))
{
#ifdef __WXGTK4__
    return GetDefaultAttributesFromGTKWidget(gtk_check_button_new_with_label(""));
#else
    return GetDefaultAttributesFromGTKWidget(gtk_radio_button_new_with_label(nullptr, ""));
#endif
}


#endif
