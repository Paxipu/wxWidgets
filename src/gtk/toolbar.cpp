/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/toolbar.cpp
// Purpose:     GTK toolbar
// Author:      Robert Roebling
// Modified:    13.12.99 by VZ to derive from wxToolBarBase
// Copyright:   (c) Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_TOOLBAR_NATIVE

#include "wx/toolbar.h"

#include "wx/gtk/private.h"
#ifndef __WXGTK4__
    // wxGtkImage derives from GtkImage, which is an opaque final type under
    // GTK4. The bitmap variant it picks is chosen directly instead, see
    // wxToolBarTool::SetImage().
    #include "wx/gtk/private/image.h"
#endif
#include "wx/gtk/private/gtk3-compat.h"

// ----------------------------------------------------------------------------
// globals
// ----------------------------------------------------------------------------

// data
extern bool       g_blockEventsOnDrag;

// ----------------------------------------------------------------------------
// wxToolBarTool
// ----------------------------------------------------------------------------

class wxToolBarTool : public wxToolBarToolBase
{
public:
    wxToolBarTool(wxToolBar *tbar,
                  int id,
                  const wxString& label,
                  const wxBitmapBundle& bitmap1,
                  const wxBitmapBundle& bitmap2,
                  wxItemKind kind,
                  wxObject *clientData,
                  const wxString& shortHelpString,
                  const wxString& longHelpString)
        : wxToolBarToolBase(tbar, id, label, bitmap1, bitmap2, kind,
                            clientData, shortHelpString, longHelpString)
    {
        m_item = nullptr;
#ifdef __WXGTK4__
        m_button = nullptr;
        m_image = nullptr;
#endif
    }

    wxToolBarTool(wxToolBar *tbar, wxControl *control, const wxString& label)
        : wxToolBarToolBase(tbar, control, label)
    {
        m_item = nullptr;
#ifdef __WXGTK4__
        m_button = nullptr;
        m_image = nullptr;
#endif
    }

    void SetImage();
    void CreateDropDown();
    void ShowDropdown(GtkToggleButton* button);
    virtual void SetLabel(const wxString& label) override;

#ifdef __WXGTK4__
    // GTK4 removed the entire GtkToolItem family, so a tool is assembled from
    // ordinary widgets: m_item is what sits in the toolbar box (a button, a
    // separator, a control's widget, or the box wrapping a dropdown button and
    // its arrow), m_button is the button proper, and m_image is the GtkImage
    // inside it, if the tool shows one.
    GtkWidget* m_item;
    GtkWidget* m_button;
    GtkWidget* m_image;
#else
    GtkToolItem* m_item;
#endif
};

// ----------------------------------------------------------------------------
// wxWin macros
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxToolBar, wxControl);

// ============================================================================
// implementation
// ============================================================================

//-----------------------------------------------------------------------------
// "clicked" from m_item
//-----------------------------------------------------------------------------

#ifdef __WXGTK4__

extern "C" {
static void item_clicked(GtkButton*, wxToolBarTool* tool)
{
    if (g_blockEventsOnDrag) return;

    tool->GetToolBar()->OnLeftClick(tool->GetId(), false);
}
}

//-----------------------------------------------------------------------------
// "toggled" from m_item
//-----------------------------------------------------------------------------

extern "C" {
static void item_toggled(GtkToggleButton* button, wxToolBarTool* tool)
{
    if (g_blockEventsOnDrag) return;

    const bool active = gtk_toggle_button_get_active(button) != 0;
    tool->Toggle(active);
    if (!active && tool->GetKind() == wxITEM_RADIO)
        return;

    if (!tool->GetToolBar()->OnLeftClick(tool->GetId(), active))
    {
        // revert back
        tool->Toggle();
    }
}
}

//-----------------------------------------------------------------------------
// right click on a tool
//-----------------------------------------------------------------------------

// GTK4 has no button-press-event: a gesture restricted to the right button
// takes its place.
extern "C" {
static void
tool_right_pressed(GtkGestureClick* gesture,
                   int, double x, double y, wxToolBarTool* tool)
{
    if (g_blockEventsOnDrag)
        return;

    tool->GetToolBar()->OnRightClick(tool->GetId(), int(x), int(y));

    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}
}

//-----------------------------------------------------------------------------
// pointer entering/leaving a tool
//-----------------------------------------------------------------------------

extern "C" {
static void tool_enter(GtkEventControllerMotion*, double, double,
                       wxToolBarTool* tool)
{
    if (g_blockEventsOnDrag)
        return;

    tool->GetToolBar()->OnMouseEnter(tool->GetId());
}

static void tool_leave(GtkEventControllerMotion*, wxToolBarTool* tool)
{
    if (g_blockEventsOnDrag)
        return;

    tool->GetToolBar()->OnMouseEnter(-1);
}
}

// Attach the above to a tool's button.
static void wxGTKConnectToolControllers(wxToolBarTool* tool, GtkWidget* widget)
{
    GtkGesture* const click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    g_signal_connect(click, "pressed", G_CALLBACK(tool_right_pressed), tool);
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click));

    GtkEventController* const motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "enter", G_CALLBACK(tool_enter), tool);
    g_signal_connect(motion, "leave", G_CALLBACK(tool_leave), tool);
    gtk_widget_add_controller(widget, motion);
}

// Insert a child into a GtkBox at the given position, -1 meaning the end.
// GtkBox only offers "after this sibling", so the sibling has to be found.
static void wxGTKBoxInsert(GtkWidget* box, GtkWidget* child, int pos)
{
    if (pos < 0)
    {
        gtk_box_append(GTK_BOX(box), child);
        return;
    }

    GtkWidget* sibling = nullptr;
    GtkWidget* c = gtk_widget_get_first_child(box);
    for (int i = 0; i < pos && c; ++i)
    {
        sibling = c;
        c = gtk_widget_get_next_sibling(c);
    }

    if (sibling)
        gtk_box_insert_child_after(GTK_BOX(box), child, sibling);
    else
        gtk_box_prepend(GTK_BOX(box), child);
}

// The tool a toolbar box child belongs to, or null if it has none.
static wxToolBarTool* wxGTKToolFromWidget(GtkWidget* widget)
{
    return static_cast<wxToolBarTool*>(
        g_object_get_data(G_OBJECT(widget), "wx-toolbar-tool"));
}

#else // !__WXGTK4__

extern "C" {
static void item_clicked(GtkToolButton*, wxToolBarTool* tool)
{
    if (g_blockEventsOnDrag) return;

    tool->GetToolBar()->OnLeftClick(tool->GetId(), false);
}
}

//-----------------------------------------------------------------------------
// "toggled" from m_item
//-----------------------------------------------------------------------------

extern "C" {
static void item_toggled(GtkToggleToolButton* button, wxToolBarTool* tool)
{
    if (g_blockEventsOnDrag) return;

    const bool active = gtk_toggle_tool_button_get_active(button) != 0;
    tool->Toggle(active);
    if (!active && tool->GetKind() == wxITEM_RADIO)
        return;

    if (!tool->GetToolBar()->OnLeftClick(tool->GetId(), active))
    {
        // revert back
        tool->Toggle();
    }
}
}

//-----------------------------------------------------------------------------
// "button_press_event" from m_item child
//-----------------------------------------------------------------------------

extern "C" {
static gboolean
button_press_event(GtkWidget*, GdkEventButton* event, wxToolBarTool* tool)
{
    if (event->button != 3)
        return FALSE;

    if (g_blockEventsOnDrag) return TRUE;

    tool->GetToolBar()->OnRightClick(
        tool->GetId(), int(event->x), int(event->y));

    return TRUE;
}
}

//-----------------------------------------------------------------------------
// "child_detached" from m_widget
//-----------------------------------------------------------------------------

// The handle box these two belong to was removed in GTK3.19.7 and is gone
// entirely under GTK4, so a detachable toolbar has no backing there.
extern "C" {
static void child_detached(GtkWidget*, GtkToolbar* toolbar, void*)
{
    // disable showing overflow arrow when toolbar is detached,
    // otherwise toolbar collapses to just an arrow
    gtk_toolbar_set_show_arrow(toolbar, false);
}
}

//-----------------------------------------------------------------------------
// "child_attached" from m_widget
//-----------------------------------------------------------------------------

extern "C" {
static void child_attached(GtkWidget*, GtkToolbar* toolbar, void*)
{
    gtk_toolbar_set_show_arrow(toolbar, true);
}
}

//-----------------------------------------------------------------------------
// "enter_notify_event" / "leave_notify_event" from m_item
//-----------------------------------------------------------------------------

extern "C" {
static gboolean
enter_notify_event(GtkWidget*, GdkEventCrossing* event, wxToolBarTool* tool)
{
    if (g_blockEventsOnDrag) return TRUE;

    int id = -1;
    if (event->type == GDK_ENTER_NOTIFY)
        id = tool->GetId();
    tool->GetToolBar()->OnMouseEnter(id);

    return FALSE;
}
}

#endif // __WXGTK4__/!__WXGTK4__

//-----------------------------------------------------------------------------

#ifndef __WXGTK4__

namespace
{
struct BitmapProvider: wxGtkImage::BitmapProvider
{
    BitmapProvider(wxToolBarTool* tool) : m_tool(tool) { }

    virtual wxBitmap Get(int scale) const override;
    wxToolBarTool* const m_tool;
};

wxBitmap BitmapProvider::Get(int scale) const
{
#ifdef __WXGTK3__
    const bool isEnabled = m_tool->IsEnabled();
    const wxBitmapBundle bundle(
        isEnabled ? m_tool->GetNormalBitmapBundle() : m_tool->GetDisabledBitmapBundle());
    wxBitmap bitmap(GetAtScale(bundle, scale));
    if (!isEnabled && !bitmap.IsOk())
    {
        // Create disabled bitmap from normal one
        bitmap = GetAtScale(m_tool->GetNormalBitmapBundle(), scale).CreateDisabled();
    }
    return bitmap;
#else
    wxUnusedVar(scale);
    if (!m_tool->IsEnabled())
        return m_tool->GetDisabledBitmap();
    return wxBitmap();
#endif
}
} // namespace

#endif // !__WXGTK4__

//-----------------------------------------------------------------------------
// "toggled" from dropdown menu button
//-----------------------------------------------------------------------------

extern "C" {
static void arrow_toggled(GtkToggleButton* button, wxToolBarTool* tool)
{
    if (gtk_toggle_button_get_active(button))
    {
        tool->ShowDropdown(button);
        gtk_toggle_button_set_active(button, false);
    }
}
}

//-----------------------------------------------------------------------------
// "button_press_event" from dropdown menu button
//-----------------------------------------------------------------------------

#ifndef __WXGTK4__
extern "C" {
static gboolean
arrow_button_press_event(GtkToggleButton* button, GdkEventButton* event, wxToolBarTool* tool)
{
    if (event->button == 1)
    {
        g_signal_handlers_block_by_func(button, (void*)arrow_toggled, tool);
        gtk_toggle_button_set_active(button, true);
        tool->ShowDropdown(button);
        gtk_toggle_button_set_active(button, false);
        g_signal_handlers_unblock_by_func(button, (void*)arrow_toggled, tool);
        return true;
    }
    return false;
}
}
#endif // !__WXGTK4__

void wxToolBar::AddChildGTK(wxWindowGTK* child)
{
#ifdef __WXGTK4__
    // There is no GtkToolItem to wrap the control in: it goes into the toolbar
    // box directly, centred the way the tool item used to centre it.
    gtk_widget_set_valign(child->m_widget, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(child->m_widget, GTK_ALIGN_CENTER);

    // position will be corrected in DoInsertTool if necessary
    gtk_box_append(GTK_BOX(m_toolbar), child->m_widget);
#else
    GtkToolItem* item = gtk_tool_item_new();
#ifdef __WXGTK3__
    gtk_widget_set_valign(child->m_widget, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(child->m_widget, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(item), child->m_widget);
#else
    GtkWidget* align = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_widget_show(align);
    gtk_container_add(GTK_CONTAINER(align), child->m_widget);
    gtk_container_add(GTK_CONTAINER(item), align);
#endif
    // position will be corrected in DoInsertTool if necessary
    gtk_toolbar_insert(GTK_TOOLBAR(gtk_bin_get_child(GTK_BIN(m_widget))), item, -1);
#endif // __WXGTK4__/!__WXGTK4__
}

// ----------------------------------------------------------------------------
// wxToolBarTool
// ----------------------------------------------------------------------------

void wxToolBarTool::SetImage()
{
#ifdef __WXGTK4__
    if (m_image == nullptr)
        return;

    // GtkImage is final under GTK4, so wxGtkImage -- whose whole purpose is to
    // pick the right bitmap variant for the scale factor and enabled state
    // itself -- cannot be used. The same choice is made here instead and the
    // result handed to a plain GtkImage as a texture.
    const int scale = gtk_widget_get_scale_factor(m_image);
    const bool isEnabled = IsEnabled();

    const wxBitmapBundle bundle(
        isEnabled ? GetNormalBitmapBundle() : GetDisabledBitmapBundle());
    const wxSize sizeDefault = bundle.IsOk() ? bundle.GetDefaultSize()
                                             : GetNormalBitmapBundle().GetDefaultSize();

    wxBitmap bitmap;
    if (bundle.IsOk())
        bitmap = bundle.GetBitmap(bundle.GetDefaultSize() * scale);

    if (!isEnabled && !bitmap.IsOk())
    {
        // Create disabled bitmap from the normal one
        const wxBitmapBundle normal(GetNormalBitmapBundle());
        if (normal.IsOk())
        {
            bitmap = normal.GetBitmap(normal.GetDefaultSize() * scale)
                        .CreateDisabled();
        }
    }

    if (!bitmap.IsOk())
    {
        gtk_image_clear(GTK_IMAGE(m_image));
        return;
    }

    GdkPixbuf* const pixbuf = bitmap.GetPixbuf();
    if (pixbuf == nullptr)
    {
        gtk_image_clear(GTK_IMAGE(m_image));
        return;
    }

    GdkTexture* const texture = gdk_texture_new_for_pixbuf(pixbuf);
    gtk_image_set_from_paintable(GTK_IMAGE(m_image), GDK_PAINTABLE(texture));
    g_object_unref(texture);

    // Without this the image is laid out at the texture's pixel size, which is
    // the scaled one and so too big on a HiDPI display.
    if (sizeDefault.y > 0)
        gtk_image_set_pixel_size(GTK_IMAGE(m_image), sizeDefault.y);
#else
    const wxBitmap& bitmap = GetNormalBitmap();

    GtkWidget* image = gtk_tool_button_get_icon_widget(GTK_TOOL_BUTTON(m_item));
    WX_GTK_IMAGE(image)->Set(bitmap);
#endif // __WXGTK4__/!__WXGTK4__
}

// helper to create a dropdown menu item
void wxToolBarTool::CreateDropDown()
{
#ifdef __WXGTK4__
    GtkOrientation orient = GTK_ORIENTATION_HORIZONTAL;
    if (GetToolBar()->HasFlag(wxTB_LEFT | wxTB_RIGHT))
        orient = GTK_ORIENTATION_VERTICAL;

    const char* const icon = orient == GTK_ORIENTATION_VERTICAL
                                ? "pan-end-symbolic" : "pan-down-symbolic";

    // The button is already in place; wrap it and an arrow button in a box and
    // let that box be what the toolbar holds.
    GtkWidget* const box = gtk_box_new(orient, 0);
    GtkWidget* const arrow_button = gtk_toggle_button_new();
    gtk_button_set_child(GTK_BUTTON(arrow_button),
                         gtk_image_new_from_icon_name(icon));
    gtk_widget_add_css_class(arrow_button, "flat");

    gtk_box_append(GTK_BOX(box), m_button);
    gtk_box_append(GTK_BOX(box), arrow_button);

    m_item = box;

    // Unlike GTK3 there is no second handler for the button press: a toggle
    // button reports the press through "toggled" anyway, and GTK4 has no
    // button-press-event to hook the other one to.
    g_signal_connect(arrow_button, "toggled", G_CALLBACK(arrow_toggled), this);
#else
    gtk_tool_item_set_homogeneous(m_item, false);
    GtkOrientation orient = GTK_ORIENTATION_HORIZONTAL;
    if (GetToolBar()->HasFlag(wxTB_LEFT | wxTB_RIGHT))
        orient = GTK_ORIENTATION_VERTICAL;
    GtkWidget* box = gtk_box_new(orient, 0);
    GtkWidget* arrow;
    if (wx_is_at_least_gtk3(14))
    {
        const char* icon = "pan-down-symbolic";
        if (orient == GTK_ORIENTATION_VERTICAL)
            icon = "pan-end-symbolic";
        arrow = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
    }
#ifndef __WXGTK4__
    else
    {
        wxGCC_WARNING_SUPPRESS(deprecated-declarations)
        GtkArrowType arrowType = GTK_ARROW_DOWN;
        if (orient == GTK_ORIENTATION_VERTICAL)
            arrowType = GTK_ARROW_RIGHT;
        arrow = gtk_arrow_new(arrowType, GTK_SHADOW_NONE);
        wxGCC_WARNING_RESTORE()
    }
#endif
    GtkWidget* tool_button = gtk_bin_get_child(GTK_BIN(m_item));
    g_object_ref(tool_button);
    gtk_container_remove(GTK_CONTAINER(m_item), tool_button);
    gtk_container_add(GTK_CONTAINER(box), tool_button);
    g_object_unref(tool_button);
    GtkWidget* arrow_button = gtk_toggle_button_new();
    gtk_button_set_relief(GTK_BUTTON(arrow_button),
        gtk_tool_item_get_relief_style(GTK_TOOL_ITEM(m_item)));
    gtk_container_add(GTK_CONTAINER(arrow_button), arrow);
    gtk_container_add(GTK_CONTAINER(box), arrow_button);
    gtk_widget_show_all(box);
    gtk_container_add(GTK_CONTAINER(m_item), box);

    g_signal_connect(arrow_button, "toggled", G_CALLBACK(arrow_toggled), this);
    g_signal_connect(arrow_button, "button_press_event",
        G_CALLBACK(arrow_button_press_event), this);
#endif // __WXGTK4__/!__WXGTK4__
}

void wxToolBarTool::ShowDropdown(GtkToggleButton* button)
{
    wxToolBarBase* toolbar = GetToolBar();
    wxCommandEvent event(wxEVT_TOOL_DROPDOWN, GetId());
    if (!toolbar->HandleWindowEvent(event))
    {
        wxMenu* menu = GetDropdownMenu();
        if (menu)
        {
#ifdef __WXGTK4__
            // gtk_widget_get_allocation() is deprecated and its x/y are always
            // zero under GTK4 anyway: the position relative to the toolbar has
            // to be computed explicitly.
            graphene_point_t origin = GRAPHENE_POINT_INIT(0, 0);
            graphene_point_t pt;
            if (!gtk_widget_compute_point(GTK_WIDGET(button),
                                          toolbar->m_widget, &origin, &pt))
            {
                pt.x = 0;
                pt.y = 0;
            }

            int x = int(pt.x);
            int y = int(pt.y);
            if (toolbar->HasFlag(wxTB_LEFT | wxTB_RIGHT))
                x += gtk_widget_get_width(GTK_WIDGET(button));
            else
                y += gtk_widget_get_height(GTK_WIDGET(button));
#else
            GtkAllocation alloc;
            gtk_widget_get_allocation(GTK_WIDGET(button), &alloc);
            int x = alloc.x;
            int y = alloc.y;
            if (toolbar->HasFlag(wxTB_LEFT | wxTB_RIGHT))
                x += alloc.width;
            else
                y += alloc.height;
#endif
            toolbar->PopupMenu(menu, x, y);
        }
    }
}

void wxToolBarTool::SetLabel(const wxString& label)
{
    wxASSERT_MSG( IsButton(),
       wxS("Label can be set for button tool only") );

    if ( label == m_label )
        return;

    wxToolBarToolBase::SetLabel(label);
    if ( IsButton() )
    {
#ifdef __WXGTK4__
        // GTK4 has no "is important" flag deciding whether a label is shown
        // next to the icon in a horizontal layout: the button contains exactly
        // the widgets we put in it, so the label is rebuilt instead.
        if ( m_button )
            static_cast<wxToolBar*>(GetToolBar())->GTKUpdateToolContent(this);
#else
        if ( !label.empty() )
        {
            wxString newLabel = wxControl::RemoveMnemonics(label);
            gtk_tool_button_set_label(GTK_TOOL_BUTTON(m_item),
                                      newLabel.utf8_str());
            // To show the label for toolbar with wxTB_HORZ_LAYOUT.
            gtk_tool_item_set_is_important(m_item, true);
        }
        else
        {
            gtk_tool_button_set_label(GTK_TOOL_BUTTON(m_item), nullptr);
            // To hide the label for toolbar with wxTB_HORZ_LAYOUT.
            gtk_tool_item_set_is_important(m_item, false);
        }
#endif // __WXGTK4__/!__WXGTK4__
    }

    // TODO: Set label for control tool, if it's possible.
}

wxToolBarToolBase *wxToolBar::CreateTool(int id,
                                         const wxString& text,
                                         const wxBitmapBundle& bitmap1,
                                         const wxBitmapBundle& bitmap2,
                                         wxItemKind kind,
                                         wxObject *clientData,
                                         const wxString& shortHelpString,
                                         const wxString& longHelpString)
{
    return new wxToolBarTool(this, id, text, bitmap1, bitmap2, kind,
                             clientData, shortHelpString, longHelpString);
}

wxToolBarToolBase *
wxToolBar::CreateTool(wxControl *control, const wxString& label)
{
    return new wxToolBarTool(this, control, label);
}

//-----------------------------------------------------------------------------
// wxToolBar construction
//-----------------------------------------------------------------------------

void wxToolBar::Init()
{
    m_toolbar = nullptr;
#ifndef __WXGTK4__
    m_tooltips = nullptr;
#endif
}

wxToolBar::~wxToolBar()
{
#ifndef __WXGTK3__
    if (m_tooltips) // always null if GTK >= 2.12
    {
        gtk_object_destroy(GTK_OBJECT(m_tooltips));
        g_object_unref(m_tooltips);
    }
#endif
}

bool wxToolBar::Create( wxWindow *parent,
                        wxWindowID id,
                        const wxPoint& pos,
                        const wxSize& size,
                        long style,
                        const wxString& name )
{
    if ( !PreCreation( parent, pos, size ) ||
         !CreateBase( parent, id, pos, size, style, wxDefaultValidator, name ))
    {
        wxFAIL_MSG( wxT("wxToolBar creation failed") );

        return false;
    }

    FixupStyle();

#ifdef __WXGTK4__
    // A GTK4 "toolbar" is a box with the matching style class: GtkToolbar and
    // every GtkToolItem subclass were removed outright.
    m_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, m_toolPacking);
    gtk_widget_add_css_class(m_toolbar, "toolbar");
#else
    m_toolbar = GTK_TOOLBAR( gtk_toolbar_new() );
#endif
#ifndef __WXGTK3__
    if (!wx_is_at_least_gtk2(12))
    {
        m_tooltips = gtk_tooltips_new();
        g_object_ref(m_tooltips);
        gtk_object_sink(GTK_OBJECT(m_tooltips));
    }
#endif
    GtkSetStyle();

#ifdef __WXGTK4__
    // wxTB_DOCKABLE has no backing: GtkHandleBox was removed in GTK3 already.
    m_widget = m_toolbar;
#else
    wxGCC_WARNING_SUPPRESS(deprecated-declarations)
    if ((style & wxTB_DOCKABLE)
#ifdef __WXGTK3__
        // using GtkHandleBox prevents toolbar from drawing with GTK+ >= 3.19.7
        && gtk_check_version(3,19,7)
#endif
        )
    {
        m_widget = gtk_handle_box_new();

        g_signal_connect(m_widget, "child_detached",
            G_CALLBACK(child_detached), nullptr);
        g_signal_connect(m_widget, "child_attached",
            G_CALLBACK(child_attached), nullptr);

        if (style & wxTB_FLAT)
            gtk_handle_box_set_shadow_type( GTK_HANDLE_BOX(m_widget), GTK_SHADOW_NONE );
    }
    else
    {
        m_widget = gtk_event_box_new();
    }
    gtk_container_add(GTK_CONTAINER(m_widget), GTK_WIDGET(m_toolbar));
    wxGCC_WARNING_RESTORE()
#endif // !__WXGTK4__
    g_object_ref(m_widget);
    gtk_widget_show(GTK_WIDGET(m_toolbar));

    m_parent->DoAddChild( this );

    PostCreation(size);

    return true;
}

#ifndef __WXGTK4__
GdkWindow *wxToolBar::GTKGetWindow(wxArrayGdkWindows& WXUNUSED(windows)) const
{
    return gtk_widget_get_window(GTK_WIDGET(m_toolbar));
}
#endif // !__WXGTK4__

void wxToolBar::GtkSetStyle()
{
    GtkOrientation orient = GTK_ORIENTATION_HORIZONTAL;
    if (HasFlag(wxTB_LEFT | wxTB_RIGHT))
        orient = GTK_ORIENTATION_VERTICAL;

#ifdef __WXGTK4__
    // There is no GtkToolbarStyle to set: whether a tool shows its icon, its
    // label or both is decided by what we put inside its button, so a style
    // change means rebuilding the tools' contents.
    gtk_orientable_set_orientation(GTK_ORIENTABLE(m_toolbar), orient);

    for ( wxToolBarToolsList::const_iterator i = m_tools.begin();
          i != m_tools.end();
          ++i )
    {
        GTKUpdateToolContent(*i);
    }
#else
    GtkToolbarStyle style = GTK_TOOLBAR_ICONS;
    if (HasFlag(wxTB_NOICONS))
        style = GTK_TOOLBAR_TEXT;
    else if (HasFlag(wxTB_TEXT))
    {
        style = GTK_TOOLBAR_BOTH;
        if (HasFlag(wxTB_HORZ_LAYOUT))
            style = GTK_TOOLBAR_BOTH_HORIZ;
    }

#ifdef __WXGTK3__
    gtk_orientable_set_orientation(GTK_ORIENTABLE(m_toolbar), orient);
#else
    gtk_toolbar_set_orientation(m_toolbar, orient);
#endif
    gtk_toolbar_set_style(m_toolbar, style);
#endif // __WXGTK4__/!__WXGTK4__
}

void wxToolBar::SetWindowStyleFlag( long style )
{
    wxToolBarBase::SetWindowStyleFlag(style);

    if ( m_toolbar )
        GtkSetStyle();
}

#ifdef __WXGTK4__
void wxToolBar::SetToolPacking(int packing)
{
    if ( packing < 0 || packing == m_toolPacking )
        return;

    wxToolBarBase::SetToolPacking(packing);

    if ( m_toolbar )
    {
        gtk_box_set_spacing(GTK_BOX(m_toolbar), packing);
        InvalidateBestSize();
    }
}
#endif // __WXGTK4__

bool wxToolBar::Realize()
{
    if ( !wxToolBarBase::Realize() )
        return false;

    // bring the initial state of all the toolbar items in line with the
    // internal state if the latter was changed by calling wxToolBarTool::
    // Enable(): this works under MSW, where the toolbar items are only created
    // in Realize() which uses the internal state to determine the initial
    // button state, so make it work under GTK too
    for ( wxToolBarToolsList::const_iterator i = m_tools.begin();
          i != m_tools.end();
          ++i )
    {
        // by default the toolbar items are enabled and not toggled, so we only
        // have to do something if their internal state doesn't correspond to
        // this
        if ( !(*i)->IsEnabled() )
            DoEnableTool(*i, false);
        if ( (*i)->IsToggled() )
            DoToggleTool(*i, true);
    }

    return true;
}

#ifdef __WXGTK4__

void wxToolBar::GTKUpdateToolContent(wxToolBarToolBase* toolBase)
{
    wxToolBarTool* const tool = static_cast<wxToolBarTool*>(toolBase);
    if (tool->m_button == nullptr)
        return;

    const bool showIcon = !HasFlag(wxTB_NOICONS);
    const bool showText = HasFlag(wxTB_NOICONS) || HasFlag(wxTB_TEXT);
    const wxString label = showText && !tool->GetLabel().empty()
                            ? wxControl::RemoveMnemonics(tool->GetLabel())
                            : wxString();

    // wxTB_HORZ_LAYOUT puts the label beside the icon rather than under it,
    // which is what GTK_TOOLBAR_BOTH_HORIZ used to mean.
    const GtkOrientation orient = HasFlag(wxTB_HORZ_LAYOUT)
                                    ? GTK_ORIENTATION_HORIZONTAL
                                    : GTK_ORIENTATION_VERTICAL;

    tool->m_image = showIcon ? gtk_image_new() : nullptr;

    GtkWidget* content;
    if (tool->m_image && !label.empty())
    {
        content = gtk_box_new(orient, 4);
        gtk_box_append(GTK_BOX(content), tool->m_image);
        gtk_box_append(GTK_BOX(content), gtk_label_new(label.utf8_str()));
    }
    else if (tool->m_image)
    {
        content = tool->m_image;
    }
    else
    {
        content = gtk_label_new(label.utf8_str());
    }

    gtk_button_set_child(GTK_BUTTON(tool->m_button), content);

    if (tool->m_image)
        tool->SetImage();
}

wxToolBarToolBase* wxToolBar::GTKGetToolAt(size_t pos) const
{
    GtkWidget* c = gtk_widget_get_first_child(m_toolbar);
    for (size_t i = 0; i < pos && c; ++i)
        c = gtk_widget_get_next_sibling(c);

    return c ? wxGTKToolFromWidget(c) : nullptr;
}

GtkToggleButton* wxToolBar::GetRadioGroup(size_t pos)
{
    // Mirrors the GTK3 logic: a radio tool joins the group of the radio tool
    // just before it, or of the one just after it if there is none before.
    // What differs is only how a group is named -- GTK4 points one toggle
    // button at another instead of passing a GSList around.
    for ( int i = 0; i < 2; ++i )
    {
        if ( i == 0 && pos == 0 )
            continue;
        if ( i == 1 && pos >= m_tools.size() )
            continue;

        wxToolBarTool* const neighbour = static_cast<wxToolBarTool*>(
            GTKGetToolAt(i == 0 ? pos - 1 : pos));

        // IsButton() first: GetKind() asserts for anything else, and the
        // neighbour of a radio tool is very often a separator or a control.
        if ( neighbour && neighbour->IsButton() &&
                neighbour->GetKind() == wxITEM_RADIO &&
                neighbour->m_button &&
                    GTK_IS_TOGGLE_BUTTON(neighbour->m_button) )
        {
            return GTK_TOGGLE_BUTTON(neighbour->m_button);
        }
    }

    return nullptr;
}

bool wxToolBar::DoInsertTool(size_t pos, wxToolBarToolBase *toolBase)
{
    wxToolBarTool* tool = static_cast<wxToolBarTool*>(toolBase);

    switch ( tool->GetStyle() )
    {
        case wxTOOL_STYLE_BUTTON:
            switch (tool->GetKind())
            {
                case wxITEM_CHECK:
                    tool->m_button = gtk_toggle_button_new();
                    g_signal_connect(tool->m_button, "toggled",
                        G_CALLBACK(item_toggled), tool);
                    break;

                case wxITEM_RADIO:
                    {
                        GtkToggleButton* const group = GetRadioGroup(pos);

                        tool->m_button = gtk_toggle_button_new();
                        if (group)
                        {
                            gtk_toggle_button_set_group(
                                GTK_TOGGLE_BUTTON(tool->m_button), group);
                        }
                        else
                        {
                            // Unlike gtk_radio_tool_button_new(), GTK4 does not
                            // activate the first member of a group by itself,
                            // so do it here to keep wx's behaviour that the
                            // first radio tool of a group starts selected.
                            gtk_toggle_button_set_active(
                                GTK_TOGGLE_BUTTON(tool->m_button), TRUE);
                            tool->Toggle(true);
                        }

                        g_signal_connect(tool->m_button, "toggled",
                            G_CALLBACK(item_toggled), tool);
                    }
                    break;

                default:
                    wxFAIL_MSG("unknown toolbar child type");
                    wxFALLTHROUGH;
                case wxITEM_DROPDOWN:
                case wxITEM_NORMAL:
                    tool->m_button = gtk_button_new();
                    g_signal_connect(tool->m_button, "clicked",
                        G_CALLBACK(item_clicked), tool);
                    break;
            }

            // GtkToolbar used to give its buttons a flat look; a plain button
            // in a box does not, so ask for it.
            gtk_widget_add_css_class(tool->m_button, "flat");

            tool->m_item = tool->m_button;

            GTKUpdateToolContent(tool);

            if (!HasFlag(wxTB_NO_TOOLTIPS) && !tool->GetShortHelp().empty())
            {
                gtk_widget_set_tooltip_text(tool->m_button,
                    tool->GetShortHelp().utf8_str());
            }

            wxGTKConnectToolControllers(tool, tool->m_button);

            if (tool->GetKind() == wxITEM_DROPDOWN)
                tool->CreateDropDown();  // this replaces m_item with a box

            g_object_set_data(G_OBJECT(tool->m_item), "wx-toolbar-tool", tool);
            wxGTKBoxInsert(m_toolbar, tool->m_item, int(pos));
            break;

        case wxTOOL_STYLE_SEPARATOR:
            tool->m_item = gtk_separator_new(
                HasFlag(wxTB_LEFT | wxTB_RIGHT) ? GTK_ORIENTATION_HORIZONTAL
                                                : GTK_ORIENTATION_VERTICAL);
            if ( tool->IsStretchable() )
            {
                // A stretchable separator is a gap, not a line: it takes up
                // the slack without drawing anything.
                gtk_widget_set_opacity(tool->m_item, 0);
                if (HasFlag(wxTB_LEFT | wxTB_RIGHT))
                    gtk_widget_set_vexpand(tool->m_item, TRUE);
                else
                    gtk_widget_set_hexpand(tool->m_item, TRUE);
            }
            g_object_set_data(G_OBJECT(tool->m_item), "wx-toolbar-tool", tool);
            wxGTKBoxInsert(m_toolbar, tool->m_item, int(pos));
            break;

        case wxTOOL_STYLE_CONTROL:
            wxWindow* control = tool->GetControl();
            if (gtk_widget_get_parent(control->m_widget) == nullptr)
                AddChildGTK(control);

            // There is no tool item wrapping it any more: the control's own
            // widget is what the toolbar box holds.
            tool->m_item = control->m_widget;
            tool->m_button = nullptr;

            // The widget size is not controlled by wx, so at least make sure
            // that its minimal size is respected by GTK.
            wxSize minSize = control->GetMinSize();
            if ( !minSize.IsFullySpecified() )
            {
                minSize.SetDefaults(control->GetSize());
            }

            gtk_widget_set_size_request(control->m_widget, minSize.x, minSize.y);

            g_object_set_data(G_OBJECT(tool->m_item), "wx-toolbar-tool", tool);

            // AddChildGTK() appended it, so move it if that isn't where it goes
            g_object_ref(tool->m_item);
            gtk_box_remove(GTK_BOX(m_toolbar), tool->m_item);
            wxGTKBoxInsert(m_toolbar, tool->m_item, int(pos));
            g_object_unref(tool->m_item);
            break;
    }

    gtk_widget_set_visible(tool->m_item, TRUE);

    InvalidateBestSize();

    return true;
}

bool wxToolBar::DoDeleteTool(size_t /* pos */, wxToolBarToolBase* toolBase)
{
    wxToolBarTool* tool = static_cast<wxToolBarTool*>(toolBase);

    if (tool->m_item)
    {
        // For a control tool this also takes the control's widget out of the
        // toolbar without destroying it, which is what we want: we may be
        // called from RemoveTool(), which keeps the control alive.
        gtk_box_remove(GTK_BOX(m_toolbar), tool->m_item);
    }

    tool->m_item = nullptr;
    tool->m_button = nullptr;
    tool->m_image = nullptr;

    InvalidateBestSize();
    return true;
}

#else // !__WXGTK4__

bool wxToolBar::DoInsertTool(size_t pos, wxToolBarToolBase *toolBase)
{
    wxToolBarTool* tool = static_cast<wxToolBarTool*>(toolBase);

    GSList* radioGroup;
    GtkWidget* bin_child;
    switch ( tool->GetStyle() )
    {
        case wxTOOL_STYLE_BUTTON:
            switch (tool->GetKind())
            {
                case wxITEM_CHECK:
                    tool->m_item = gtk_toggle_tool_button_new();
                    g_signal_connect(tool->m_item, "toggled",
                        G_CALLBACK(item_toggled), tool);
                    break;
                case wxITEM_RADIO:
                    radioGroup = GetRadioGroup(pos);
                    if (!radioGroup)
                    {
                        // this is the first button in the radio button group,
                        // it will be toggled automatically by GTK so bring the
                        // internal flag in sync
                        tool->Toggle(true);
                    }
                    tool->m_item = gtk_radio_tool_button_new(radioGroup);
                    g_signal_connect(tool->m_item, "toggled",
                        G_CALLBACK(item_toggled), tool);
                    break;
                default:
                    wxFAIL_MSG("unknown toolbar child type");
                    wxFALLTHROUGH;
                case wxITEM_DROPDOWN:
                case wxITEM_NORMAL:
                    tool->m_item = gtk_tool_button_new(nullptr, "");
                    g_signal_connect(tool->m_item, "clicked",
                        G_CALLBACK(item_clicked), tool);
                    break;
            }
            if (!HasFlag(wxTB_NOICONS))
            {
                GtkWidget* image = wxGtkImage::New(new BitmapProvider(tool));
                gtk_tool_button_set_icon_widget(
                    GTK_TOOL_BUTTON(tool->m_item), image);
                tool->SetImage();
                gtk_widget_show(image);
            }
            if (!tool->GetLabel().empty())
            {
                wxString const
                    label = wxControl::RemoveMnemonics(tool->GetLabel());

                gtk_tool_button_set_label(
                    GTK_TOOL_BUTTON(tool->m_item), label.utf8_str());
                // needed for labels in horizontal toolbar with wxTB_HORZ_LAYOUT
                gtk_tool_item_set_is_important(tool->m_item, true);
            }
            if (!HasFlag(wxTB_NO_TOOLTIPS) && !tool->GetShortHelp().empty())
            {
#if GTK_CHECK_VERSION(2, 12, 0)
                if (wx_is_at_least_gtk2(12))
                {
                    gtk_tool_item_set_tooltip_text(tool->m_item,
                        tool->GetShortHelp().utf8_str());
                }
                else
#endif
                {
#ifndef __WXGTK3__
                    gtk_tool_item_set_tooltip(tool->m_item,
                        m_tooltips, tool->GetShortHelp().utf8_str(), "");
#endif
                }
            }
            bin_child = gtk_bin_get_child(GTK_BIN(tool->m_item));
            g_signal_connect(bin_child, "button_press_event",
                G_CALLBACK(button_press_event), tool);
            g_signal_connect(bin_child, "enter_notify_event",
                G_CALLBACK(enter_notify_event), tool);
            g_signal_connect(bin_child, "leave_notify_event",
                G_CALLBACK(enter_notify_event), tool);

            if (tool->GetKind() == wxITEM_DROPDOWN)
                tool->CreateDropDown();
            gtk_toolbar_insert(m_toolbar, tool->m_item, int(pos));
            break;

        case wxTOOL_STYLE_SEPARATOR:
            tool->m_item = gtk_separator_tool_item_new();
            if ( tool->IsStretchable() )
            {
                gtk_separator_tool_item_set_draw
                (
                    GTK_SEPARATOR_TOOL_ITEM(tool->m_item),
                    FALSE
                );
                gtk_tool_item_set_expand(tool->m_item, TRUE);
            }
            gtk_toolbar_insert(m_toolbar, tool->m_item, int(pos));
            break;

        case wxTOOL_STYLE_CONTROL:
            wxWindow* control = tool->GetControl();
            if (gtk_widget_get_parent(control->m_widget) == nullptr)
                AddChildGTK(control);
#ifdef __WXGTK3__
            tool->m_item = GTK_TOOL_ITEM(gtk_widget_get_parent(control->m_widget));
#else
            tool->m_item = GTK_TOOL_ITEM(gtk_widget_get_parent(gtk_widget_get_parent(control->m_widget)));
#endif
            // The widget size is not controlled by wx, so at least make sure
            // that its minimal size is respected by GTK.
            wxSize minSize = control->GetMinSize();
            if ( !minSize.IsFullySpecified() )
            {
                // Note that we intentionally don't use GetBestSize() here as
                // the control may be explicitly given smaller size than its
                // best size when adding it to the toolbar to prevent it from
                // taking too much space, see b0ad9ccffd (Use control current,
                // not best, size in wxMSW wxToolBar layout code, 2019-03-31).
                minSize.SetDefaults(control->GetSize());
            }

            gtk_widget_set_size_request(control->m_widget, minSize.x, minSize.y);

            if (gtk_toolbar_get_item_index(m_toolbar, tool->m_item) != int(pos))
            {
                g_object_ref(tool->m_item);
                gtk_container_remove(
                    GTK_CONTAINER(m_toolbar), GTK_WIDGET(tool->m_item));
                gtk_toolbar_insert(m_toolbar, tool->m_item, int(pos));
                g_object_unref(tool->m_item);
            }
            break;
    }
    gtk_widget_show(GTK_WIDGET(tool->m_item));

    InvalidateBestSize();

    return true;
}

bool wxToolBar::DoDeleteTool(size_t /* pos */, wxToolBarToolBase* toolBase)
{
    wxToolBarTool* tool = static_cast<wxToolBarTool*>(toolBase);

    if (tool->GetStyle() == wxTOOL_STYLE_CONTROL)
    {
        // don't destroy the control here as we can be called from
        // RemoveTool() and then we need to keep the control alive;
        // while if we're called from DeleteTool() the control will
        // be destroyed when wxToolBarToolBase itself is deleted
        GtkWidget* widget = tool->GetControl()->m_widget;
        gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(widget)), widget);
    }
    gtk_widget_destroy(GTK_WIDGET(tool->m_item));
    tool->m_item = nullptr;

    InvalidateBestSize();
    return true;
}

GSList* wxToolBar::GetRadioGroup(size_t pos)
{
    GSList* radioGroup = nullptr;
    GtkToolItem* item = nullptr;
    if (pos > 0)
    {
        item = gtk_toolbar_get_nth_item(m_toolbar, int(pos) - 1);
        if (!GTK_IS_RADIO_TOOL_BUTTON(item))
            item = nullptr;
    }
    if (item == nullptr && pos < m_tools.size())
    {
        item = gtk_toolbar_get_nth_item(m_toolbar, int(pos));
        if (!GTK_IS_RADIO_TOOL_BUTTON(item))
            item = nullptr;
    }
    if (item)
        radioGroup = gtk_radio_tool_button_get_group((GtkRadioToolButton*)item);
    return radioGroup;
}

#endif // __WXGTK4__/!__WXGTK4__

// ----------------------------------------------------------------------------
// wxToolBar tools state
// ----------------------------------------------------------------------------

void wxToolBar::DoEnableTool(wxToolBarToolBase *toolBase, bool enable)
{
    wxToolBarTool* tool = static_cast<wxToolBarTool*>(toolBase);

    if (tool->m_item)
    {
        gtk_widget_set_sensitive(GTK_WIDGET(tool->m_item), enable);
#ifdef __WXGTK4__
        // Unlike wxGtkImage, which chose the variant when it drew, the bitmap
        // shown is fixed at the point it is set, so it has to be reset here.
        tool->SetImage();
#endif
    }
}

void wxToolBar::DoToggleTool( wxToolBarToolBase *toolBase, bool toggle )
{
    wxToolBarTool* tool = static_cast<wxToolBarTool*>(toolBase);

    if (tool->m_item)
    {
#ifdef __WXGTK4__
        if (tool->m_button == nullptr || !GTK_IS_TOGGLE_BUTTON(tool->m_button))
            return;

        g_signal_handlers_block_by_func(tool->m_button, (void*)item_toggled, tool);

        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tool->m_button), toggle);

        g_signal_handlers_unblock_by_func(tool->m_button, (void*)item_toggled, tool);
#else
        g_signal_handlers_block_by_func(tool->m_item, (void*)item_toggled, tool);

        gtk_toggle_tool_button_set_active(
            GTK_TOGGLE_TOOL_BUTTON(tool->m_item), toggle);

        g_signal_handlers_unblock_by_func(tool->m_item, (void*)item_toggled, tool);
#endif
    }
}

void wxToolBar::DoSetToggle(wxToolBarToolBase * WXUNUSED(tool),
                            bool WXUNUSED(toggle))
{
    // VZ: absolutely no idea about how to do it
    wxFAIL_MSG( wxT("not implemented") );
}

// ----------------------------------------------------------------------------
// wxToolBar geometry
// ----------------------------------------------------------------------------

wxSize wxToolBar::DoGetBestSize() const
{
    // Unfortunately, if overflow arrow is enabled GtkToolbar only reports size
    // of arrow. To get the real size, the arrow is temporarily disabled here.
    // This is gross, since it will cause a queue_resize, and could potentially
    // lead to an infinite loop. But there seems to be no alternative, short of
    // disabling the arrow entirely.
#ifdef __WXGTK4__
    // No workaround needed: there is no overflow arrow under GTK4, a toolbar
    // is a plain box and reports the size of its children.
    return wxToolBarBase::DoGetBestSize();
#else
    gtk_toolbar_set_show_arrow(m_toolbar, false);
    const wxSize size = wxToolBarBase::DoGetBestSize();
    gtk_toolbar_set_show_arrow(m_toolbar, true);
    return size;
#endif
}

wxToolBarToolBase *wxToolBar::FindToolForPosition(wxCoord WXUNUSED(x),
                                                  wxCoord WXUNUSED(y)) const
{
    // TODO: implement this using gtk_toolbar_get_drop_index()
    wxFAIL_MSG( wxT("wxToolBar::FindToolForPosition() not implemented") );

    return nullptr;
}

void wxToolBar::SetToolShortHelp( int id, const wxString& helpString )
{
    wxToolBarTool* tool = static_cast<wxToolBarTool*>(FindById(id));

    if ( tool )
    {
        (void)tool->SetShortHelp(helpString);
        if (tool->m_item)
        {
#ifdef __WXGTK4__
            gtk_widget_set_tooltip_text(tool->m_item, helpString.utf8_str());
#else
#if GTK_CHECK_VERSION(2, 12, 0)
            if (wx_is_at_least_gtk2(12))
            {
                gtk_tool_item_set_tooltip_text(tool->m_item,
                    helpString.utf8_str());
            }
            else
#endif
            {
#ifndef __WXGTK3__
                gtk_tool_item_set_tooltip(tool->m_item,
                    m_tooltips, helpString.utf8_str(), "");
#endif
            }
#endif // __WXGTK4__/!__WXGTK4__
        }
    }
}

void wxToolBar::SetToolNormalBitmap( int id, const wxBitmapBundle& bitmap )
{
    wxToolBarTool* tool = static_cast<wxToolBarTool*>(FindById(id));
    if ( tool )
    {
        wxCHECK_RET( tool->IsButton(), wxT("Can only set bitmap on button tools."));

        tool->SetNormalBitmap(bitmap);
        tool->SetImage();
    }
}

void wxToolBar::SetToolDisabledBitmap( int id, const wxBitmapBundle& bitmap )
{
    wxToolBarTool* tool = static_cast<wxToolBarTool*>(FindById(id));
    if ( tool )
    {
        wxCHECK_RET( tool->IsButton(), wxT("Can only set bitmap on button tools."));

        tool->SetDisabledBitmap(bitmap);
    }
}

// ----------------------------------------------------------------------------

// static
wxVisualAttributes
wxToolBar::GetClassDefaultAttributes(wxWindowVariant WXUNUSED(variant))
{
#ifdef __WXGTK4__
    return GetDefaultAttributesFromGTKWidget(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
#else
    return GetDefaultAttributesFromGTKWidget(gtk_toolbar_new());
#endif
}

#endif // wxUSE_TOOLBAR_NATIVE
