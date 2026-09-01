///////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/private.cpp
// Purpose:     implementation of wxGTK private functions
// Author:      Marcin Malich
// Created:     28.06.2008
// Copyright:   (c) 2008 Marcin Malich <me@malcom.pl>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#ifndef WX_PRECOMP
    #include "wx/module.h"
#endif

#include "wx/gtk/private.h"

// ----------------------------------------------------------------------------
// wxGTKPrivate functions implementation
// ----------------------------------------------------------------------------

namespace wxGTKPrivate
{

static GtkWidget *gs_container = nullptr;

// GetContainer()'s return type and AddToContainer()'s implementation differ
// between GTK3 (GtkContainer*, still exists) and GTK4 (GtkContainer doesn't
// exist; use gtk_fixed_put() on the GtkFixed directly instead). Callers
// should use AddToContainer() rather than GetContainer() directly so they
// work under both -- same pattern as ContainerWidget()/
// ContainerWidgetAddChild() in settings.cpp.
#ifdef __WXGTK4__
static GtkWidget* GetContainer()
{
    if ( gs_container == nullptr )
    {
        // Never shown, just used to host scratch widgets for style/metric
        // queries -- GTK_WINDOW_POPUP doesn't exist under GTK4 any more,
        // but a plain, never-shown toplevel serves the same purpose here.
        GtkWidget* window = gtk_window_new();
        gs_container = gtk_fixed_new();
        gtk_window_set_child(GTK_WINDOW(window), gs_container);
    }
    return gs_container;
}

static void AddToContainer(GtkWidget* widget)
{
    gtk_fixed_put(GTK_FIXED(GetContainer()), widget, 0, 0);
}
#else
static GtkContainer* GetContainer()
{
    if ( gs_container == nullptr )
    {
        GtkWidget* window = gtk_window_new(GTK_WINDOW_POPUP);
        gs_container = gtk_fixed_new();
        gtk_container_add(GTK_CONTAINER(window), gs_container);
    }
    return GTK_CONTAINER(gs_container);
}

static void AddToContainer(GtkWidget* widget)
{
    gtk_container_add(GetContainer(), widget);
}
#endif // __WXGTK4__/!__WXGTK4__

GtkWidget *GetButtonWidget()
{
    static GtkWidget *s_button = nullptr;

    if ( !s_button )
    {
        s_button = gtk_button_new();
        g_object_add_weak_pointer(G_OBJECT(s_button), (void**)&s_button);
        AddToContainer(s_button);
        gtk_widget_realize(s_button);
    }

    return s_button;
}

GtkWidget *GetNotebookWidget()
{
    static GtkWidget *s_notebook = nullptr;

    if ( !s_notebook )
    {
        s_notebook = gtk_notebook_new();
        g_object_add_weak_pointer(G_OBJECT(s_notebook), (void**)&s_notebook);
        AddToContainer(s_notebook);
        gtk_widget_realize(s_notebook);
    }

    return s_notebook;
}

GtkWidget *GetCheckButtonWidget()
{
    static GtkWidget *s_button = nullptr;

    if ( !s_button )
    {
        s_button = gtk_check_button_new();
        g_object_add_weak_pointer(G_OBJECT(s_button), (void**)&s_button);
        AddToContainer(s_button);
        gtk_widget_realize(s_button);
    }

    return s_button;
}

GtkWidget *GetSpinButtonWidget()
{
    static GtkWidget *s_spinButton = nullptr;

    if ( !s_spinButton )
    {
        s_spinButton = gtk_spin_button_new(nullptr, 1, 0);
        g_object_add_weak_pointer(G_OBJECT(s_spinButton), (void**)&s_spinButton);
        AddToContainer(s_spinButton);
        gtk_widget_realize(s_spinButton);
    }

    return s_spinButton;
}

#ifndef __WXGTK4__

// GtkComboBox is deprecated under GTK4 and nothing there asks for one: the
// renderer photographs a GtkDropDown instead.
GtkWidget * GetComboBoxWidget()
{
    static GtkWidget *s_button = nullptr;

    if ( !s_button )
    {
        s_button = gtk_combo_box_new();
        g_object_add_weak_pointer(G_OBJECT(s_button), (void**)&s_button);
        AddToContainer(s_button);
        gtk_widget_realize( s_button );
    }

    return s_button;
}

#endif // !__WXGTK4__


GtkWidget *GetEntryWidget()
{
    static GtkWidget *s_entry = nullptr;

    if ( !s_entry )
    {
        s_entry = gtk_entry_new();
        g_object_add_weak_pointer(G_OBJECT(s_entry), (void**)&s_entry);
        AddToContainer(s_entry);
        gtk_widget_realize(s_entry);
    }

    return s_entry;
}

#ifndef __WXGTK4__

// This one just gets the button used by the column header. Although it's
// still a gtk_button the themes will typically differentiate and draw them
// differently if the button is in a treeview.
//
// GTK4 has no GtkTreeView column to take a button from. Its header button is
// the "button" node inside a GtkColumnView's title row, which renderer.cpp
// builds and measures for itself.
static GtkWidget *s_first_button = nullptr;
static GtkWidget *s_other_button = nullptr;
static GtkWidget *s_last_button = nullptr;

static void CreateHeaderButtons()
{
        // Get the dummy tree widget, give it a column, and then use the
        // widget in the column header for the rendering code.
        GtkWidget* treewidget = GetTreeWidget();

        GtkTreeViewColumn *column = gtk_tree_view_column_new();
        gtk_tree_view_append_column(GTK_TREE_VIEW(treewidget), column);
#ifdef __WXGTK3__
        s_first_button = gtk_tree_view_column_get_button(column);
#else
        s_first_button = column->button;
#endif
        wxASSERT(s_first_button);
        g_object_add_weak_pointer(G_OBJECT(s_first_button), (void**)&s_first_button);

        column = gtk_tree_view_column_new();
        gtk_tree_view_append_column(GTK_TREE_VIEW(treewidget), column);
#ifdef __WXGTK3__
        s_other_button = gtk_tree_view_column_get_button(column);
#else
        s_other_button = column->button;
#endif
        g_object_add_weak_pointer(G_OBJECT(s_other_button), (void**)&s_other_button);

        column = gtk_tree_view_column_new();
        gtk_tree_view_append_column(GTK_TREE_VIEW(treewidget), column);
#ifdef __WXGTK3__
        s_last_button = gtk_tree_view_column_get_button(column);
#else
        s_last_button = column->button;
#endif
        g_object_add_weak_pointer(G_OBJECT(s_last_button), (void**)&s_last_button);
}

GtkWidget *GetHeaderButtonWidgetFirst()
{
    if (!s_first_button)
      CreateHeaderButtons();

    return s_first_button;
}

GtkWidget *GetHeaderButtonWidgetLast()
{
    if (!s_last_button)
      CreateHeaderButtons();

    return s_last_button;
}

GtkWidget *GetHeaderButtonWidget()
{
    if (!s_other_button)
      CreateHeaderButtons();

    return s_other_button;
}

#endif // !__WXGTK4__

GtkWidget * GetRadioButtonWidget()
{
    static GtkWidget *s_button = nullptr;

    if ( !s_button )
    {
#ifdef __WXGTK4__
        // GtkRadioButton doesn't exist under GTK4, a radio button being a
        // GtkCheckButton in a group there.
        //
        // The group is not incidental: it is what turns the button's "check"
        // CSS node into a "radio" one, so an ungrouped button would style and
        // measure as a check box. renderer.cpp measures this widget to size
        // the radio indicator, so it is given a group member to be in. The
        // partner is deliberately never shown or added to anything; it exists
        // only to make this one a radio button.
        s_button = gtk_check_button_new();
        gtk_check_button_set_group(GTK_CHECK_BUTTON(s_button),
                                   GTK_CHECK_BUTTON(gtk_check_button_new()));
#else
        s_button = gtk_radio_button_new(nullptr);
#endif
        g_object_add_weak_pointer(G_OBJECT(s_button), (void**)&s_button);
        AddToContainer(s_button);
        gtk_widget_realize( s_button );
    }

    return s_button;
}

GtkWidget* GetSplitterWidget(wxOrientation orient)
{
    static GtkWidget* widgets[2];
    const GtkOrientation gtkOrient =
        orient == wxHORIZONTAL ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    GtkWidget*& widget = widgets[gtkOrient];
    if (widget == nullptr)
    {
#ifdef __WXGTK3__
        widget = gtk_paned_new(gtkOrient);
#else
        if (orient == wxHORIZONTAL)
            widget = gtk_hpaned_new();
        else
            widget = gtk_vpaned_new();
#endif
        g_object_add_weak_pointer(G_OBJECT(widget), (void**)&widgets[gtkOrient]);
        AddToContainer(widget);
        gtk_widget_realize(widget);
    }

    return widget;
}

#ifdef __WXGTK4__

GtkWidget *GetExpanderWidget()
{
    static GtkWidget *s_expander = nullptr;

    if ( !s_expander )
    {
        s_expander = gtk_expander_new(nullptr);
        g_object_add_weak_pointer(G_OBJECT(s_expander), (void**)&s_expander);
        AddToContainer(s_expander);
        gtk_widget_realize(s_expander);
    }

    return s_expander;
}

#endif // __WXGTK4__

GtkWidget *GetTreeWidget()
{
    static GtkWidget *s_tree = nullptr;

    if ( !s_tree )
    {
#ifdef __WXGTK4__
        // GtkTreeView is deprecated. The one thing still asked of this widget
        // under GTK4 is what border a scrolling control has -- win_gtk.cpp
        // draws wxBORDER_THEME with it -- and a GtkScrolledWindow is what a
        // scrolling control is made of there.
        //
        // The values do not change: both report a border of zero on GTK 4.14
        // and 4.22, measured. That the answer is zero at all is a separate
        // question, and an older one than this port.
        s_tree = gtk_scrolled_window_new();
#else
        s_tree = gtk_tree_view_new();
#endif
        g_object_add_weak_pointer(G_OBJECT(s_tree), (void**)&s_tree);
        AddToContainer(s_tree);
        gtk_widget_realize(s_tree);
    }

    return s_tree;
}

// Module for destroying created widgets
class WidgetsCleanupModule : public wxModule
{
public:
    virtual bool OnInit() override
    {
        return true;
    }

    virtual void OnExit() override
    {
        if ( gs_container )
        {
            GtkWidget* parent = gtk_widget_get_parent(gs_container);
#ifdef __WXGTK4__
            gtk_window_destroy(GTK_WINDOW(parent));
#else
            gtk_widget_destroy(parent);
#endif
            gs_container = nullptr;
        }
    }

    wxDECLARE_DYNAMIC_CLASS(WidgetsCleanupModule);
};

wxIMPLEMENT_DYNAMIC_CLASS(WidgetsCleanupModule, wxModule);

} // wxGTKPrivate namespace
