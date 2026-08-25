///////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/win_gtk.cpp
// Purpose:     native GTK+ widget for wxWindow
// Author:      Paul Cornett
// Copyright:   (c) 2007 Paul Cornett
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/gtk/private.h"
#include "wx/gtk/private/gtk3-compat.h"
#include "wx/gtk/private/win_gtk.h"
#include "wx/window.h"

#if wxUSE_ACCESSIBILITY
    #include "wx/gtk/private/access.h"
#endif

/*
wxPizza is a custom GTK+ widget derived from GtkFixed.  A custom widget
is needed to adapt GTK+ to wxWidgets needs in 3 areas: scrolling, window
borders, and RTL.

For scrolling, the "set_scroll_adjustments" signal is implemented
to make wxPizza appear scrollable to GTK+, allowing it to be put in a
GtkScrolledWindow.  Child widget positions are adjusted for the scrolling
position in size_allocate.

For borders, space is reserved in realize and size_allocate.  The border is
drawn on wxPizza's parent GdkWindow.

For RTL, child widget positions are mirrored in size_allocate.
*/

struct wxPizzaChild
{
    GtkWidget* widget;
    int x, y, width, height;
};

static GtkWidgetClass* parent_class;

#if defined(__WXGTK4__) && wxUSE_ACCESSIBILITY
// ----------------------------------------------------------------------------
// accessibility
// ----------------------------------------------------------------------------
//
// wxPizza is the widget behind every custom-drawn wx control, so it is where
// the parts of such a control that have no window of their own -- a wxGrid
// cell, a wxDataViewCtrl item -- have to be attached. GTK4 asks a widget for
// its first accessible child through the GtkAccessible interface, and only a
// re-implementation of that interface can answer with something that is not a
// widget, so wxPizza implements it too and defers to GtkWidget for everything
// else.

static GtkAccessibleInterface* parent_accessible_iface;

extern "C" {

static GtkAccessible* pizza_get_first_accessible_child(GtkAccessible* accessible)
{
    if ( GtkAccessible* const child =
            wxGTKPizzaGetFirstAccessibleChild(GTK_WIDGET(accessible)) )
        return child;

    // No wxAccessible with children here, so this is an ordinary widget with
    // ordinary widget children.
    return parent_accessible_iface->get_first_accessible_child(accessible);
}

// GTK gives every widget class its own accessible role, defaulting to WIDGET
// rather than inheriting the parent class's -- so a wxPizza would report itself
// as a bare widget where the GtkFixed it derives from reports a generic
// container. GENERIC is both more accurate and the only value GTK will let an
// instance override later, should a way of setting the role per instance be
// found: see docs/gtk/gtk4-accessibility.md.
static void pizza_set_default_accessible_role(GtkWidgetClass* widget_class)
{
    gtk_widget_class_set_accessible_role(widget_class,
                                         GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void pizza_accessible_init(void* g_iface, void*)
{
    GtkAccessibleInterface* const iface =
        static_cast<GtkAccessibleInterface*>(g_iface);

    // Re-implementing an interface starts from a copy of the implementation
    // being replaced, so only the one vfunc that differs is assigned here --
    // but the original still has to be kept to fall back on.
    parent_accessible_iface =
        static_cast<GtkAccessibleInterface*>(g_type_interface_peek_parent(iface));

    iface->get_first_accessible_child = pizza_get_first_accessible_child;
}

} // extern "C"
#endif // __WXGTK4__ && wxUSE_ACCESSIBILITY

#ifdef __WXGTK3__
enum {
    PROP_0,
    PROP_HADJUSTMENT,
    PROP_VADJUSTMENT,
    PROP_HSCROLL_POLICY,
    PROP_VSCROLL_POLICY
};
#endif

extern "C" {

struct wxPizzaClass
{
    GtkFixedClass parent;
#ifndef __WXGTK3__
    void (*set_scroll_adjustments)(GtkWidget*, GtkAdjustment*, GtkAdjustment*);
#endif
};

#ifdef __WXGTK4__
// GTK4 removed GtkWidget's "size-allocate" signal along with the GtkAllocation
// its handlers were given; only the vfunc is left, which is not something an
// outside observer can connect to. wxTopLevelWindowGTK does need to know when
// its client area has been laid out, so wxPizza provides a signal of its own,
// emitted from the vfunc below. Named distinctly on purpose: it is not GTK3's
// signal under another name and carries no allocation.
static guint gs_signalSizeAllocated;

// GTK4's size_allocate vfunc signature dropped GtkAllocation* (position is
// no longer this widget's own concern -- only its own width/height/baseline
// are, since it no longer owns a window to position) in favor of separate
// width/height/baseline parameters.
static void pizza_size_allocate(GtkWidget* widget, int width, int WXUNUSED(height), int WXUNUSED(baseline))
{
    wxPizza* pizza = WX_PIZZA(widget);
    GtkBorder border;
    pizza->get_border(border);
    int w = width - border.left - border.right;
    if (w < 0) w = 0;

    // See the KNOWN GAP comment in pizza_realize(): BORDER_STYLES
    // decoration rendering needs a real redesign under GTK4, since the
    // GdkWindow-repositioning trick this used to rely on doesn't apply.

    // adjust child positions
    for (const GList* p = pizza->m_children; p; p = p->next)
    {
        const wxPizzaChild* child = static_cast<wxPizzaChild*>(p->data);

        // put() tracks a re-parented toplevel here without making it a child
        // at GTK level, deliberately -- see the comment there. Allocating one
        // anyway is what GTK3 tolerated and GTK4 does not: allocating a widget
        // that is not your child leaves it outside the layout manager
        // ("Unable to present ... unknown auxiliary child ... widget type
        // GtkWindow") and corrupts the CSS node tree, which GTK then aborts
        // on in gtk_css_node_validate().
        if (gtk_widget_get_parent(child->widget) != widget)
            continue;

        if (gtk_widget_get_visible(child->widget))
        {
            pizza->size_allocate_child(
                child->widget, child->x, child->y, child->width, child->height, w);
        }
    }

    g_signal_emit(widget, gs_signalSizeAllocated, 0);
}
#else
static void pizza_size_allocate(GtkWidget* widget, GtkAllocation* alloc)
{
    wxPizza* pizza = WX_PIZZA(widget);
    GtkBorder border;
    pizza->get_border(border);
    int w = alloc->width - border.left - border.right;
    if (w < 0) w = 0;

    if (gtk_widget_get_realized(widget))
    {
        int h = alloc->height - border.top - border.bottom;
        if (h < 0) h = 0;
        const int x = alloc->x + border.left;
        const int y = alloc->y + border.top;

        GdkWindow* window = gtk_widget_get_window(widget);
        int old_x, old_y;
        gdk_window_get_position(window, &old_x, &old_y);

        if (x != old_x || y != old_y ||
            w != gdk_window_get_width(window) || h != gdk_window_get_height(window))
        {
            gdk_window_move_resize(window, x, y, w, h);

            if (border.left + border.right + border.top + border.bottom)
            {
                // old and new border areas need to be invalidated,
                // otherwise they will not be erased/redrawn properly
                GtkAllocation old_alloc;
                gtk_widget_get_allocation(widget, &old_alloc);
                GdkWindow* parent = gtk_widget_get_parent_window(widget);
                gdk_window_invalidate_rect(parent, &old_alloc, false);
                gdk_window_invalidate_rect(parent, alloc, false);
            }
        }
    }

    gtk_widget_set_allocation(widget, alloc);

    // adjust child positions
    for (const GList* p = pizza->m_children; p; p = p->next)
    {
        const wxPizzaChild* child = static_cast<wxPizzaChild*>(p->data);
        if (gtk_widget_get_visible(child->widget))
        {
            pizza->size_allocate_child(
                child->widget, child->x, child->y, child->width, child->height, w);
        }
    }
}
#endif // __WXGTK4__/!__WXGTK4__

static void pizza_realize(GtkWidget* widget)
{
    parent_class->realize(widget);

#ifndef __WXGTK4__
    wxPizza* pizza = WX_PIZZA(widget);
    if (pizza->m_windowStyle & wxPizza::BORDER_STYLES)
    {
        GtkBorder border;
        pizza->get_border(border);
        GtkAllocation a;
        gtk_widget_get_allocation(widget, &a);
        int x = a.x + border.left;
        int y = a.y + border.top;
        int w = a.width - border.left - border.right;
        int h = a.height - border.top - border.bottom;
        if (w < 0) w = 0;
        if (h < 0) h = 0;
        gdk_window_move_resize(gtk_widget_get_window(widget), x, y, w, h);
    }
#endif // !__WXGTK4__
    // See the comment in pizza_size_allocate() -- BORDER_STYLES has the
    // same known gap here, for the same reason.
}

static void pizza_show(GtkWidget* widget)
{
    GtkWidget* parent = gtk_widget_get_parent(widget);
    if (parent && (WX_PIZZA(widget)->m_windowStyle & wxPizza::BORDER_STYLES))
    {
        // invalidate whole allocation so borders will be drawn properly
#ifdef __WXGTK4__
        // gtk_widget_queue_draw_area() (partial-rect invalidation) doesn't
        // exist under GTK4; queue_draw() invalidates the whole parent
        // instead, which is correct if less targeted.
        gtk_widget_queue_draw(parent);
#else
        GtkAllocation a;
        gtk_widget_get_allocation(widget, &a);
        gtk_widget_queue_draw_area(parent, a.x, a.y, a.width, a.height);
#endif
    }

    parent_class->show(widget);
}

static void pizza_hide(GtkWidget* widget)
{
    GtkWidget* parent = gtk_widget_get_parent(widget);
    if (parent && (WX_PIZZA(widget)->m_windowStyle & wxPizza::BORDER_STYLES))
    {
        // invalidate whole allocation so borders will be erased properly
#ifdef __WXGTK4__
        gtk_widget_queue_draw(parent);
#else
        GtkAllocation a;
        gtk_widget_get_allocation(widget, &a);
        gtk_widget_queue_draw_area(parent, a.x, a.y, a.width, a.height);
#endif
    }

    parent_class->hide(widget);
}

// GtkContainer, and with it GtkContainerClass::add/remove, doesn't exist
// under GTK4 -- nothing can call gtk_container_add()/remove() on a wxPizza
// generically any more (the only way to add/remove a child is through
// wxPizza's own put()/RemoveChild(), which already do this bookkeeping
// directly), so these vfunc overrides have no GTK4 equivalent to provide,
// not just a missing API to shim.
#ifndef __WXGTK4__
static void pizza_add(GtkContainer* container, GtkWidget* widget)
{
    WX_PIZZA(container)->put(widget, 0, 0, 1, 1);
}

static void pizza_remove(GtkContainer* container, GtkWidget* widget)
{
    GTK_CONTAINER_CLASS(parent_class)->remove(container, widget);

    wxPizza* pizza = WX_PIZZA(container);
    for (GList* p = pizza->m_children; p; p = p->next)
    {
        wxPizzaChild* child = static_cast<wxPizzaChild*>(p->data);
        if (child->widget == widget)
        {
            pizza->m_children = g_list_delete_link(pizza->m_children, p);
            delete child;
            break;
        }
    }
}
#endif // !__WXGTK4__

#ifdef __WXGTK3__
// Get preferred size of children, to avoid GTK+ warnings complaining
// that they were size-allocated without asking their preferred size
static void children_get_preferred_size(const GList* p)
{
    for (; p; p = p->next)
    {
        const wxPizzaChild* child = static_cast<wxPizzaChild*>(p->data);
        if (gtk_widget_get_visible(child->widget))
        {
            GtkRequisition req;
            gtk_widget_get_preferred_size(child->widget, &req, nullptr);
        }
    }
}

#ifdef __WXGTK4__
// GTK4 merged get_preferred_width/height and adjust_size_request into one
// measure() vfunc. The GtkToolItem special case in the old
// pizza_adjust_size_request() below is gone here because GtkToolItem
// itself doesn't exist under GTK4 (see toolbar.cpp's deferred
// GtkToolbar/GtkToolItem redesign in docs/gtk/gtk4-status.md) -- there is
// currently no way for a wxPizza to be inside one, so always reporting a
// zero minimum (the common case in the GTK3 code below) is correct as-is.
static void pizza_measure(GtkWidget* widget, GtkOrientation orientation, int /* for_size */,
                           int* minimum, int* natural, int* minimum_baseline, int* natural_baseline)
{
    children_get_preferred_size(WX_PIZZA(widget)->m_children);
    *minimum = 0;
    int w = -1, h = -1;
    gtk_widget_get_size_request(widget, &w, &h);
    *natural = orientation == GTK_ORIENTATION_HORIZONTAL ? w : h;
    if (*natural < 0)
        *natural = 0;
    if (minimum_baseline)
        *minimum_baseline = -1;
    if (natural_baseline)
        *natural_baseline = -1;
}
#else
static void pizza_get_preferred_width(GtkWidget* widget, int* minimum, int* natural)
{
    children_get_preferred_size(WX_PIZZA(widget)->m_children);
    *minimum = 0;
    gtk_widget_get_size_request(widget, natural, nullptr);
    if (*natural < 0)
        *natural = 0;
}

static void pizza_get_preferred_height(GtkWidget* widget, int* minimum, int* natural)
{
    children_get_preferred_size(WX_PIZZA(widget)->m_children);
    *minimum = 0;
    gtk_widget_get_size_request(widget, nullptr, natural);
    if (*natural < 0)
        *natural = 0;
}

static void pizza_adjust_size_request(GtkWidget* widget, GtkOrientation orientation, int* minimum, int* natural)
{
    parent_class->adjust_size_request(widget, orientation, minimum, natural);
    // Override adjustments to minimum size. GtkWidgetClass.adjust_size_request()
    // will use the size request, if set, as the minimum.
    // But don't override if in a GtkToolbar, it uses the minimum as actual size.
    GtkWidget* parent = gtk_widget_get_parent(widget);
    if (!GTK_IS_TOOL_ITEM(parent))
        *minimum = 0;
}
#endif // __WXGTK4__/!__WXGTK4__

// GtkScrollable interface
static void pizza_get_property(GObject*, guint property_id, GValue* value, GParamSpec*)
{
    if (property_id == PROP_HSCROLL_POLICY || property_id == PROP_VSCROLL_POLICY)
    {
        // Use natural size, rather than minimum, as virtual size
        g_value_set_enum(value, GTK_SCROLL_NATURAL);
    }
}

static void pizza_set_property(GObject*, guint, const GValue*, GParamSpec*)
{
}
#else
// not used, but needs to exist so gtk_widget_set_scroll_adjustments will work
static void pizza_set_scroll_adjustments(GtkWidget*, GtkAdjustment*, GtkAdjustment*)
{
}

// Marshaller needed for set_scroll_adjustments signal,
// generated with GLib-2.4.6 glib-genmarshal
#define g_marshal_value_peek_object(v)   g_value_get_object (v)
static void
g_cclosure_user_marshal_VOID__OBJECT_OBJECT (GClosure     *closure,
                                             GValue       * /*return_value*/,
                                             guint         n_param_values,
                                             const GValue *param_values,
                                             gpointer      /*invocation_hint*/,
                                             gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__OBJECT_OBJECT) (gpointer     data1,
                                                    gpointer     arg_1,
                                                    gpointer     arg_2,
                                                    gpointer     data2);
  GMarshalFunc_VOID__OBJECT_OBJECT callback;
  GCClosure *cc = (GCClosure*) closure;
  gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = (GMarshalFunc_VOID__OBJECT_OBJECT) (marshal_data ? marshal_data : cc->callback);

  callback (data1,
            g_marshal_value_peek_object (param_values + 1),
            g_marshal_value_peek_object (param_values + 2),
            data2);
}
#endif

#ifdef __WXGTK4__
// GTK4 replaced the "draw" signal with a snapshot vfunc building render nodes.
// wx paints with cairo throughout, so rather than rewrite every wxDC operation
// onto render nodes, take the cairo escape hatch: gtk_snapshot_append_cairo()
// hands back a real cairo_t, and measurement confirms it is in widget-relative
// coordinates, exactly as the GTK3 draw vfunc was for a windowless widget --
// so everything downstream of GTKSendPaintEvents() is unaffected. See
// docs/gtk/gtk4-phase4-paint-model-design.md.
//
// A vfunc carries no user data where the signal carried the wxWindow, so the
// owner is looked up from the widget; wxWindowGTK sets it when it would
// previously have connected the signal.
static void pizza_snapshot(GtkWidget* widget, GtkSnapshot* snapshot)
{
    // Frozen by wxWindow::Freeze(): paint nothing at all, neither this
    // widget's content nor its children, until thawed.
    if ( g_object_get_data(G_OBJECT(widget), "wx-frozen") != nullptr )
        return;

    wxWindow* const win = static_cast<wxWindow*>(
        g_object_get_data(G_OBJECT(widget), "wx-pizza-owner"));

    const int w = gtk_widget_get_width(widget);
    const int h = gtk_widget_get_height(widget);

    if ( win && w > 0 && h > 0 )
    {
        graphene_rect_t bounds;
        bounds.origin.x = 0;
        bounds.origin.y = 0;
        bounds.size.width = float(w);
        bounds.size.height = float(h);

        cairo_t* const cr = gtk_snapshot_append_cairo(snapshot, &bounds);
        win->GTKSendPaintEvents(cr);
        cairo_destroy(cr);
    }

    // Children are no longer drawn by chaining up to a parent draw handler:
    // each has to be snapshotted explicitly.
    for ( GtkWidget* child = gtk_widget_get_first_child(widget);
          child != nullptr;
          child = gtk_widget_get_next_sibling(child) )
    {
        // A child which has not been allocated yet -- shown between its
        // parent's last size_allocate and the next one -- has nothing to draw,
        // and asking GTK to draw it anyway earns a warning for every frame it
        // stays in that state. It is drawn from the first frame after it has
        // been given a size.
        //
        // Deciding this from the size is deliberate: gtk_widget_compute_bounds()
        // and gtk_widget_get_realized() were both tried here and caught exactly
        // the same children, at more cost per child per frame.
        if ( gtk_widget_get_width(child) <= 0 ||
                gtk_widget_get_height(child) <= 0 )
            continue;

        gtk_widget_snapshot_child(widget, child, snapshot);
    }
}
#endif // __WXGTK4__

#ifdef __WXGTK4__
// Under GTK3 the m_children entries were freed by pizza_remove() as
// GtkContainer tore the children down. With no such vfunc under GTK4 the list
// has to be released here, or it leaks one wxPizzaChild per child every time a
// wxPizza is destroyed.
//
// GtkFixed unparents the remaining children itself when it is disposed, so
// this deliberately only drops wx's own bookkeeping; freeing it before
// chaining up means nothing can walk a list of children that are on their way
// out.
static void pizza_dispose(GObject* object)
{
    wxPizza* const pizza = WX_PIZZA(object);

    for (GList* p = pizza->m_children; p; p = p->next)
        delete static_cast<wxPizzaChild*>(p->data);

    g_list_free(pizza->m_children);
    pizza->m_children = nullptr;

    G_OBJECT_CLASS(parent_class)->dispose(object);
}
#endif // __WXGTK4__

static void class_init(void* g_class, void*)
{
    GtkWidgetClass* widget_class = (GtkWidgetClass*)g_class;
    widget_class->size_allocate = pizza_size_allocate;
    widget_class->realize = pizza_realize;
    widget_class->show = pizza_show;
    widget_class->hide = pizza_hide;
#if defined(__WXGTK4__) && wxUSE_ACCESSIBILITY
    pizza_set_default_accessible_role(widget_class);
#endif
#ifndef __WXGTK4__
    // GtkContainerClass doesn't exist under GTK4 -- see the comment above
    // pizza_add()/pizza_remove().
    GtkContainerClass* container_class = (GtkContainerClass*)g_class;
    container_class->add = pizza_add;
    container_class->remove = pizza_remove;
#endif

#ifdef __WXGTK4__
    widget_class->measure = pizza_measure;
    widget_class->snapshot = pizza_snapshot;

    // Use the exported constant rather than repeating the name: window.cpp and
    // toplevel.cpp connect through it, and a silent divergence would cost every
    // window its wxEVT_SIZE without any diagnostic at build time.
    gs_signalSizeAllocated = g_signal_new(wxPIZZA_SIGNAL_SIZE_ALLOCATED,
        G_TYPE_FROM_CLASS(g_class), G_SIGNAL_RUN_LAST, 0,
        nullptr, nullptr, nullptr, G_TYPE_NONE, 0);

    GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
    gobject_class->set_property = pizza_set_property;
    gobject_class->get_property = pizza_get_property;
    gobject_class->dispose = pizza_dispose;
    g_object_class_override_property(gobject_class, PROP_HADJUSTMENT, "hadjustment");
    g_object_class_override_property(gobject_class, PROP_VADJUSTMENT, "vadjustment");
    g_object_class_override_property(gobject_class, PROP_HSCROLL_POLICY, "hscroll-policy");
    g_object_class_override_property(gobject_class, PROP_VSCROLL_POLICY, "vscroll-policy");
#elif defined(__WXGTK3__)
    widget_class->get_preferred_width = pizza_get_preferred_width;
    widget_class->get_preferred_height = pizza_get_preferred_height;
    widget_class->adjust_size_request = pizza_adjust_size_request;
    GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
    gobject_class->set_property = pizza_set_property;
    gobject_class->get_property = pizza_get_property;
    g_object_class_override_property(gobject_class, PROP_HADJUSTMENT, "hadjustment");
    g_object_class_override_property(gobject_class, PROP_VADJUSTMENT, "vadjustment");
    g_object_class_override_property(gobject_class, PROP_HSCROLL_POLICY, "hscroll-policy");
    g_object_class_override_property(gobject_class, PROP_VSCROLL_POLICY, "vscroll-policy");
#else
    wxPizzaClass* klass = static_cast<wxPizzaClass*>(g_class);
    // needed to make widget appear scrollable to GTK+
    klass->set_scroll_adjustments = pizza_set_scroll_adjustments;
    widget_class->set_scroll_adjustments_signal =
        g_signal_new(
            "set_scroll_adjustments",
            G_TYPE_FROM_CLASS(g_class),
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET(wxPizzaClass, set_scroll_adjustments),
            nullptr, nullptr,
            g_cclosure_user_marshal_VOID__OBJECT_OBJECT,
            G_TYPE_NONE, 2, GTK_TYPE_ADJUSTMENT, GTK_TYPE_ADJUSTMENT);
#endif
    parent_class = GTK_WIDGET_CLASS(g_type_class_peek_parent(g_class));
}

} // extern "C"

#ifdef __WXGTK4__
// "extern" is what makes the visibility attribute apply here: without it a
// const object at namespace scope is not yet known to have external linkage
// when GCC processes the attribute, so GCC drops it and warns. The exported
// symbol came out right anyway, because the declaration in win_gtk.h carries
// the same attribute, but the warning appeared in every build.
//
// The initialiser is a constant expression, so this is initialised before any
// code runs and class_init() above may use it despite coming first.
extern WXDLLIMPEXP_DATA_CORE(const char* const)
wxPIZZA_SIGNAL_SIZE_ALLOCATED = "wx-size-allocated";
#endif // __WXGTK4__

GType wxPizza::type()
{
    static GType type;
    if (type == 0)
    {
        const char* name = "wxPizza";
        char buf[30];
        for (unsigned i = 0; g_type_from_name(name); i++)
        {
            g_snprintf(buf, sizeof(buf), "wxPizza%u", i);
            name = buf;
        }
        const GTypeInfo info = {
            sizeof(wxPizzaClass),
            nullptr, nullptr,
            class_init,
            nullptr, nullptr,
            sizeof(wxPizza), 0,
            nullptr, nullptr
        };
        type = g_type_register_static(
            GTK_TYPE_FIXED, name, &info, GTypeFlags(0));
#ifdef __WXGTK3__
        const GInterfaceInfo interface_info = { nullptr, nullptr, nullptr };
        g_type_add_interface_static(type, GTK_TYPE_SCROLLABLE, &interface_info);
#endif
#if defined(__WXGTK4__) && wxUSE_ACCESSIBILITY
        const GInterfaceInfo accessible_info =
            { pizza_accessible_init, nullptr, nullptr };
        g_type_add_interface_static(type, GTK_TYPE_ACCESSIBLE, &accessible_info);
#endif
    }
    return type;
}

GtkWidget* wxPizza::New(long windowStyle)
{
    GtkWidget* widget = GTK_WIDGET(g_object_new(type(), nullptr));
    wxPizza* pizza = WX_PIZZA(widget);
    pizza->m_children = nullptr;
    pizza->m_scroll_x = 0;
    pizza->m_scroll_y = 0;
    pizza->m_windowStyle = windowStyle;
#ifdef __WXGTK4__
    // GtkFixed installs a GtkFixedLayout layout manager, and GTK4 dispatches
    // both measure() and size_allocate() to a widget's layout manager *instead
    // of* to its class vfuncs when it has one. So wxPizza's own layout code --
    // pizza_measure() and pizza_size_allocate(), which is what positions every
    // wx child window -- was never being called at all, and GtkFixedLayout was
    // laying children out instead: at the origin, at their own measured size,
    // which for a wxPizza with no natural size of its own is 0x0.
    //
    // Nothing about this is diagnosable from the code: the vfuncs are assigned
    // in class_init exactly as under GTK3, and GTK simply never asks for them.
    // See docs/gtk/probes/gtk4-layout-manager.c.
    //
    // wx does all of its own layout, so it wants no layout manager. put()
    // below therefore cannot use gtk_fixed_put() either, as that reaches
    // through the layout manager to set the child's transform.
    gtk_widget_set_layout_manager(widget, nullptr);

    // Neither gtk_widget_set_has_window() nor event masks exist under
    // GTK4: no widget (other than a toplevel's implicit surface) has its
    // own window any more, and event delivery goes entirely through
    // GtkEventController objects instead of enabling raw event types via
    // a mask -- see docs/gtk/gtk4-phase3-input-model-design.md, not yet
    // implemented.
#elif defined(__WXGTK3__)
    gtk_widget_set_has_window(widget, true);
    gtk_widget_add_events(widget,
        GDK_EXPOSURE_MASK |
        GDK_SCROLL_MASK |
#if GTK_CHECK_VERSION(3,4,0)
        GDK_SMOOTH_SCROLL_MASK |
#endif
        GDK_POINTER_MOTION_MASK |
        GDK_POINTER_MOTION_HINT_MASK |
        GDK_BUTTON_MOTION_MASK |
        GDK_BUTTON1_MOTION_MASK |
        GDK_BUTTON2_MOTION_MASK |
        GDK_BUTTON3_MOTION_MASK |
        GDK_BUTTON_PRESS_MASK |
        GDK_BUTTON_RELEASE_MASK |
        GDK_KEY_PRESS_MASK |
        GDK_KEY_RELEASE_MASK |
        GDK_ENTER_NOTIFY_MASK |
        GDK_LEAVE_NOTIFY_MASK |
        GDK_FOCUS_CHANGE_MASK);
#else
    gtk_fixed_set_has_window(GTK_FIXED(widget), true);
    gtk_widget_add_events(widget,
        GDK_EXPOSURE_MASK |
        GDK_SCROLL_MASK |
        GDK_POINTER_MOTION_MASK |
        GDK_POINTER_MOTION_HINT_MASK |
        GDK_BUTTON_MOTION_MASK |
        GDK_BUTTON1_MOTION_MASK |
        GDK_BUTTON2_MOTION_MASK |
        GDK_BUTTON3_MOTION_MASK |
        GDK_BUTTON_PRESS_MASK |
        GDK_BUTTON_RELEASE_MASK |
        GDK_KEY_PRESS_MASK |
        GDK_KEY_RELEASE_MASK |
        GDK_ENTER_NOTIFY_MASK |
        GDK_LEAVE_NOTIFY_MASK |
        GDK_FOCUS_CHANGE_MASK);
#endif // __WXGTK4__/__WXGTK3__/!__WXGTK3__
    return widget;
}

void wxPizza::move(GtkWidget* widget, int x, int y, int width, int height)
{
    for (const GList* p = m_children; p; p = p->next)
    {
        wxPizzaChild* child = static_cast<wxPizzaChild*>(p->data);
        if (child->widget == widget)
        {
            child->x = x;
            child->y = y;
            child->width = width;
            child->height = height;
            // normally a queue-resize would be needed here, but we know
            // wxWindowGTK::DoMoveWindow() will take care of it
            break;
        }
    }
}

void wxPizza::size_allocate_child(
    GtkWidget* child, int x, int y, int width, int height, int parent_width)
{
    if (width <= 0 || height <= 0)
        return;

    GtkAllocation child_alloc;
    // note that child positions do not take border into account, they need to
    // be relative to widget->window, which has already been adjusted
    child_alloc.x = x - m_scroll_x;
    child_alloc.y = y - m_scroll_y;
    child_alloc.width  = width;
    child_alloc.height = height;
    if (gtk_widget_get_direction(GTK_WIDGET(this)) == GTK_TEXT_DIR_RTL)
    {
        if (parent_width < 0)
        {
            GtkBorder border;
            get_border(border);
            GtkAllocation alloc;
            gtk_widget_get_allocation(GTK_WIDGET(this), &alloc);
            parent_width = alloc.width - border.left - border.right;
        }
        child_alloc.x = parent_width - child_alloc.x - child_alloc.width;
    }
#ifdef __WXGTK4__
    // gtk_widget_size_allocate() gained a baseline parameter under GTK4;
    // -1 means "no baseline alignment", matching the previous behavior.
    gtk_widget_size_allocate(child, &child_alloc, -1);
#else
    gtk_widget_size_allocate(child, &child_alloc);
#endif
}

void wxPizza::put(GtkWidget* widget, int x, int y, int width, int height)
{
    // Re-parenting a TLW under a child window is possible at wx level but
    // using a TLW as child at GTK+ level results in problems, so don't do it.
#ifdef __WXGTK4__
    // gtk_widget_is_toplevel() doesn't exist under GTK4; GTK_IS_WINDOW()
    // is the direct equivalent for "is this a toplevel-capable widget".
    if (!GTK_IS_WINDOW(widget))
    {
        // Not gtk_fixed_put(): it asks the layout manager for the child's
        // GtkFixedLayoutChild in order to set a transform, and wxPizza has no
        // layout manager, for the reason given in New(). Parenting the child
        // is all that is wanted here in any case -- its position and size come
        // from size_allocate_child().
        gtk_widget_set_parent(widget, GTK_WIDGET(this));
        gtk_widget_set_size_request(widget, -1, -1);
    }
#else
    if (!gtk_widget_is_toplevel(GTK_WIDGET(widget)))
    {
        gtk_fixed_put(GTK_FIXED(this), widget, 0, 0);
        gtk_widget_set_size_request(widget, -1, -1);
    }
#endif

    wxPizzaChild* child = new wxPizzaChild;
    child->widget = widget;
    child->x = x;
    child->y = y;
    child->width = width;
    child->height = height;
    m_children = g_list_append(m_children, child);
}

#ifdef __WXGTK4__
// The counterpart to put(), which GTK3 got for free: GtkContainer's "remove"
// vfunc (pizza_remove() above) fired whenever a child left, and kept
// m_children in step.
//
// GTK4 has no GtkContainer and so no such vfunc, and nothing was maintaining
// m_children at all: every child that went away left behind an entry pointing
// at a freed GtkWidget, which pizza_size_allocate(), pizza_measure() and
// pizza_snapshot() then walked. gtk_widget_unparent() also bypasses
// gtk_fixed_remove(), so GtkFixed's own layout-child bookkeeping was left
// stale in the same way -- the two together showed up as
// "unknown auxiliary child surface" from the layout manager and eventually as
// GTK aborting inside gtk_css_node_validate().
void wxPizza::remove(GtkWidget* widget)
{
    for (GList* p = m_children; p; p = p->next)
    {
        wxPizzaChild* const child = static_cast<wxPizzaChild*>(p->data);
        if (child->widget == widget)
        {
            m_children = g_list_delete_link(m_children, p);
            delete child;
            break;
        }
    }

    // put() does not parent toplevels, so this is not redundant.
    if (gtk_widget_get_parent(widget) == GTK_WIDGET(this))
    {
        // The window has to be told before the widget goes: see
        // wx_gtk_widget_forget_in_root().
        wx_gtk_widget_forget_in_root(widget);

        // The counterpart of the gtk_widget_set_parent() in put():
        // gtk_fixed_remove() would go through the layout manager wxPizza
        // deliberately does not have.
        gtk_widget_unparent(widget);
    }
}
#endif // __WXGTK4__

#ifndef __WXGTK4__
struct AdjustData {
    GdkWindow* window;
    int dx, dy;
};

// Adjust allocations for all widgets using the GdkWindow which was just scrolled
extern "C" {
static void scroll_adjust(GtkWidget* widget, void* data)
{
    if (!gtk_widget_get_visible(widget))
        return;

    const AdjustData* p = static_cast<AdjustData*>(data);
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);
    a.x += p->dx;
    a.y += p->dy;
    gtk_widget_set_allocation(widget, &a);

    if (gtk_widget_get_window(widget) == p->window)
    {
        // GtkFrame requires a queue_resize, otherwise parts of
        // the frame newly exposed by the scroll are not drawn.
        // To be safe, do it for all widgets.
        gtk_widget_queue_resize_no_redraw(widget);
        if (GTK_IS_CONTAINER(widget))
            gtk_container_forall(GTK_CONTAINER(widget), scroll_adjust, data);
    }
}
}
#endif // !__WXGTK4__

void wxPizza::scroll(int dx, int dy)
{
    GtkWidget* widget = GTK_WIDGET(this);
#ifndef __WXGTK3__
    if (gtk_widget_get_direction(widget) == GTK_TEXT_DIR_RTL)
        dx = -dx;
#endif
    m_scroll_x -= dx;
    m_scroll_y -= dy;
#ifdef __WXGTK4__
    // No more low-level pixel-blit scrolling under GTK4: GdkWindow and
    // gdk_window_scroll() don't exist any more, and there's no direct
    // replacement (compositing handles this differently now). A normal
    // re-allocate is enough to get correct (if not blit-optimized)
    // behavior, since size_allocate_child() above already positions
    // every child from m_scroll_x/m_scroll_y on every allocation pass.
    gtk_widget_queue_allocate(widget);
    gtk_widget_queue_draw(widget);
#else
    GdkWindow* window = gtk_widget_get_window(widget);
    if (window)
    {
        gdk_window_scroll(window, dx, dy);
        // Adjust child allocations. Doing a queue_resize on the children is not
        // enough, sometimes they redraw in the wrong place during fast scrolling.
        AdjustData data = { window, dx, dy };
        gtk_container_forall(GTK_CONTAINER(widget), scroll_adjust, &data);
    }
#endif // __WXGTK4__/!__WXGTK4__
}

void wxPizza::get_border(GtkBorder& border)
{
#ifndef __WXUNIVERSAL__
    if (m_windowStyle & wxBORDER_SIMPLE)
        border.left = border.right = border.top = border.bottom = 1;
    else if (m_windowStyle & (wxBORDER_RAISED | wxBORDER_SUNKEN | wxBORDER_THEME))
    {
#ifdef __WXGTK3__
        GtkStyleContext* sc;
        if (m_windowStyle & (wxHSCROLL | wxVSCROLL))
            sc = gtk_widget_get_style_context(wxGTKPrivate::GetTreeWidget());
        else
            sc = gtk_widget_get_style_context(wxGTKPrivate::GetEntryWidget());

        gtk_style_context_set_state(sc, GTK_STATE_FLAG_NORMAL);
#ifdef __WXGTK4__
        // gtk_style_context_get_border() dropped the separate GtkStateFlags
        // parameter under GTK4 -- it always queries the context's current
        // state, which gtk_style_context_set_state() above already set.
        gtk_style_context_get_border(sc, &border);
#else
        gtk_style_context_get_border(sc, GTK_STATE_FLAG_NORMAL, &border);
#endif
#else // !__WXGTK3__
        GtkStyle* style;
        if (m_windowStyle & (wxHSCROLL | wxVSCROLL))
            style = gtk_widget_get_style(wxGTKPrivate::GetTreeWidget());
        else
            style = gtk_widget_get_style(wxGTKPrivate::GetEntryWidget());

        border.left = border.right = style->xthickness;
        border.top = border.bottom = style->ythickness;
#endif // !__WXGTK3__
    }
    else
#endif // !__WXUNIVERSAL__
    {
        border.left = border.right = border.top = border.bottom = 0;
    }
}
