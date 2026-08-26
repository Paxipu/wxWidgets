///////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/overlay.cpp
// Author:      Paul Cornett
// Copyright:   (c) 2022 Paul Cornett
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#ifdef __WXGTK3__

#include "wx/private/overlay.h"
#include "wx/dc.h"
#include "wx/graphics.h"
#include "wx/window.h"
#include "wx/gtk/private/wrapgtk.h"
#include "wx/gtk/private/backend.h"
#include "wx/gtk/private/gtk3-compat.h"
#include "wx/gtk/private/win_gtk.h"

class wxOverlayImpl: public wxOverlay::Impl
{
public:
    wxOverlayImpl();
    ~wxOverlayImpl();
    virtual bool IsOk() override;
    virtual void Init(wxDC* dc, int x, int y, int width, int height) override;
    virtual void BeginDrawing(wxDC* dc) override;
    virtual void EndDrawing(wxDC* dc) override;
    virtual void Clear(wxDC* dc) override;
    virtual void Reset() override;
    void PositionOverlay(GtkWidget* tlw);

    GtkWidget* m_overlay;
    GtkWidget* m_target;
    cairo_surface_t* m_surface;
    cairo_t* m_cr;
    wxRect m_rect;
#ifdef __WXGTK4__
    // Where the overlay widget has actually been placed, so that repeating the
    // same position can be skipped: see PositionOverlay().
    wxRect m_placedRect;
#endif
};

wxOverlay::Impl* wxOverlay::Create()
{
#ifdef __WXGTK4__
    // Unlike the GTK3 implementation below, which is only worth its while on
    // Wayland, the GTK4 one is used everywhere: it does not need a surface of
    // its own at all, while the generic implementation needs to read the
    // window's pixels back, which no GTK4 backend can do.
    return new wxOverlayImpl;
#else
    if (wxGTKImpl::IsWayland(nullptr))
        return new wxOverlayImpl;

    // Use generic
    return nullptr;
#endif
}

extern "C" {
#ifdef __WXGTK4__
static void draw(GtkDrawingArea*, cairo_t* cr, int, int, void* data)
{
    wxOverlayImpl* const overlay = static_cast<wxOverlayImpl*>(data);
    if (overlay->m_surface)
    {
        cairo_set_source_surface(cr, overlay->m_surface, 0, 0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
    }
}
#else
static gboolean draw(GtkWidget*, cairo_t* cr, wxOverlayImpl* overlay)
{
    if (overlay->m_surface)
    {
        cairo_set_source_surface(cr, overlay->m_surface, 0, 0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
    }
    return true;
}

static gboolean map_event(GtkWidget* widget, GdkEvent*, wxOverlayImpl* overlay)
{
    overlay->PositionOverlay(widget);
    g_signal_handlers_disconnect_by_data(widget, overlay);
    return false;
}
#endif // __WXGTK4__/!__WXGTK4__
} // extern "C"

wxOverlayImpl::wxOverlayImpl()
{
    m_overlay = nullptr;
    m_target = nullptr;
    m_surface = nullptr;
    m_cr = nullptr;
}

wxOverlayImpl::~wxOverlayImpl()
{
    if (m_surface)
        cairo_surface_destroy(m_surface);
    if (m_overlay)
    {
#ifdef __WXGTK4__
        gtk_widget_unparent(m_overlay);
#else
        gtk_widget_destroy(m_overlay);
#endif
        g_object_unref(m_overlay);
    }
}

bool wxOverlayImpl::IsOk()
{
    return false;
}

void wxOverlayImpl::Init(wxDC* dc, int x, int y, int width, int height)
{
    wxWindow* const win = dc->GetWindow();
    if (!win)
    {
        // A wxDC with no window of its own -- a wxScreenDC, in practice -- is
        // not a programming error here. wxGenericDragImage uses one for "drag
        // across the whole screen", a documented wx feature, and it reaches
        // this function through the ordinary wxDCOverlay path.
        //
        // There is nothing to be done with it under GTK4: drawing happens
        // inside a widget's snapshot, and outside one there is no screen to
        // draw on -- which is why wxScreenDCImpl says it "leaves the drawing
        // to go nowhere". Asserting turned that missing feature into a debug
        // alert in the middle of a drag, which reads as a crash. See #97.
        //
        // Leave the overlay uninitialised instead, and in particular leave
        // m_cr null: that is what makes the rest of this class skip its work
        // rather than run it on a widget which was never created.
        return;
    }

    if (wxGraphicsContext* gc = dc->GetGraphicsContext())
        m_cr = static_cast<cairo_t*>(gc->GetNativeContext());

    wxCHECK_RET(m_cr, "invalid dc for wxOverlay");

    m_target = win->GetConnectWidget();

#ifdef __WXGTK4__
    // GTK4 has none of the pieces the GTK3 implementation below is built from:
    // no GTK_WINDOW_POPUP, no gtk_window_move() to place it with, no RGBA
    // visual selection and no input shapes.  It does not need them either,
    // though, because it renders a whole toplevel as one scene: a sibling
    // widget added last to the target's wxPizza is simply drawn on top of
    // everything else in it, which is all the overlay ever wanted.
    if (m_overlay == nullptr)
    {
        m_overlay = gtk_drawing_area_new();
        g_object_ref(m_overlay);

        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(m_overlay),
                                       draw, this, nullptr);

        // The overlay must be transparent for mouse events. This replaces the
        // 1x1 input shape hack below and, unlike it, is exact.
        gtk_widget_set_can_target(m_overlay, false);
        wx_gtk_widget_set_focusable(m_overlay, false);
    }
#else // !__WXGTK4__
    GtkWidget* const tlw = gtk_widget_get_toplevel(m_target);
    if (m_overlay == nullptr)
    {
        // The overlay is a TLW to get "save-under" behavior, which avoids
        // repainting of the underlying window.
        // GTK_WINDOW_POPUP is implemented as a subsurface on Wayland, so it
        // will be moved/stacked with the underlying window.
        m_overlay = gtk_window_new(GTK_WINDOW_POPUP);
        g_object_ref(m_overlay);
        gtk_window_set_transient_for(GTK_WINDOW(m_overlay), GTK_WINDOW(tlw));
        gtk_window_set_accept_focus(GTK_WINDOW(m_overlay), false);
        gtk_widget_set_app_paintable(m_overlay, true);
        GdkScreen* screen = gtk_widget_get_screen(tlw);
        GdkVisual* visual = gdk_screen_get_rgba_visual(screen);
        if (visual)
            gtk_widget_set_visual(m_overlay, visual);
        g_signal_connect(m_overlay, "draw", G_CALLBACK(draw), this);
    }
    // Overlay must be transparent for mouse events. gdk_window_set_pass_through()
    // doesn't work here, so use an input shape that is as small as possible.
    cairo_rectangle_int_t rect = { 0, 0, 1, 1 };
    cairo_region_t* region = cairo_region_create_rectangle(&rect);
    gtk_widget_input_shape_combine_region(m_overlay, nullptr);
    gtk_widget_input_shape_combine_region(m_overlay, region);
    cairo_region_destroy(region);
#endif // __WXGTK4__/!__WXGTK4__

    // Convert to device space
    double d1 = x;
    double d2 = y;
    cairo_user_to_device(m_cr, &d1, &d2);
    m_rect.x = int(d1);
    m_rect.y = int(d2);
    d1 = width;
    d2 = height;
    cairo_user_to_device_distance(m_cr, &d1, &d2);
    m_rect.width = int(d1);
    m_rect.height = int(d2);
    // If axis is inverted
    if (m_rect.width < 0)
    {
        m_rect.width = -m_rect.width;
        m_rect.x -= m_rect.width;
    }
    if (m_rect.height < 0)
    {
        m_rect.height = -m_rect.height;
        m_rect.y -= m_rect.height;
    }

    gtk_widget_set_size_request(m_overlay, m_rect.width, m_rect.height);
#ifdef __WXGTK4__
    // Nothing has to be mapped first here: the overlay is laid out by its
    // parent like any other child widget, so it can be placed straight away.
    PositionOverlay(m_target);
#else
    // Underlying window must be mapped before overlay can be positioned on it
    if (gtk_widget_get_mapped(tlw))
        PositionOverlay(tlw);
    else
        g_signal_connect(tlw, "map-event", G_CALLBACK(map_event), this);
#endif
}

void wxOverlayImpl::BeginDrawing(wxDC*)
{
    if (m_cr)
        cairo_push_group(m_cr);
}

void wxOverlayImpl::EndDrawing(wxDC* dc)
{
    if (m_cr == nullptr)
        return;

    cairo_pattern_t* pattern = cairo_pop_group(m_cr);
    if (m_surface)
        cairo_surface_destroy(m_surface);
    cairo_pattern_get_surface(pattern, &m_surface);
    cairo_surface_reference(m_surface);
    cairo_pattern_destroy(pattern);
    m_cr = nullptr;

    const wxSize size(dc->GetSize());
    if (m_rect.width < size.x || m_rect.height < size.y)
    {
        cairo_surface_t* surface = cairo_surface_create_similar(
            m_surface, CAIRO_CONTENT_COLOR_ALPHA, m_rect.width, m_rect.height);
        cairo_t* cr = cairo_create(surface);
        cairo_set_source_surface(cr, m_surface, -m_rect.x, -m_rect.y);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_destroy(m_surface);
        m_surface = surface;
    }
    if (m_overlay)
        gtk_widget_queue_draw(m_overlay);
}

void wxOverlayImpl::Clear(wxDC*)
{
    // Surface is already cleared by Cairo
}

void wxOverlayImpl::Reset()
{
    if (m_surface)
    {
        cairo_surface_destroy(m_surface);
        m_surface = nullptr;
    }
#ifdef __WXGTK4__
    // Deliberately not hidden: with no surface the widget draws nothing at
    // all, so hiding it would only buy a hide/show cycle -- and a widget that
    // has just been shown has no allocation until the next layout pass, which
    // makes GTK4 complain "Trying to snapshot GtkDrawingArea without a current
    // allocation" if anything paints in between. Code that resets an overlay
    // and draws on it again from inside a paint handler does exactly that.
    if (m_overlay)
        gtk_widget_queue_draw(m_overlay);
#else
    if (m_overlay)
        gtk_widget_hide(m_overlay);
#endif
}

#ifdef __WXGTK4__

void wxOverlayImpl::PositionOverlay(GtkWidget* target)
{
    wxCHECK_RET( WX_IS_PIZZA(target),
                 "wxOverlay needs a wxPizza to place its overlay in" );

    wxPizza* const pizza = WX_PIZZA(target);

    // Only touch the widget when the position actually changed. Moving a child
    // has to queue a re-allocation of the pizza below, which repaints the
    // window it belongs to, and wxCaret comes through here on every blink: a
    // window that repainted itself twice a second just to leave the caret
    // where it already was would be paying for nothing.
    if (gtk_widget_get_parent(m_overlay) != target)
    {
        // put() adds the overlay as the last child, and GtkFixed draws its
        // children in order, so this is also what puts it on top.
        pizza->put(m_overlay, m_rect.x, m_rect.y, m_rect.width, m_rect.height);
        m_placedRect = m_rect;
    }
    else if (m_placedRect != m_rect)
    {
        pizza->move(m_overlay, m_rect.x, m_rect.y, m_rect.width, m_rect.height);

        // wxPizza::move() deliberately does not queue one itself: for a
        // wxWindow child wxWindowGTK::DoMoveWindow() does that. The overlay is
        // not a wxWindow, so without this the new position would only be
        // recorded in wxPizza's child list and never reach the widget's
        // allocation -- which is why a moved caret kept being drawn wherever
        // it had first been put.
        gtk_widget_queue_allocate(GTK_WIDGET(pizza));

        m_placedRect = m_rect;
    }

    if (!gtk_widget_get_visible(m_overlay))
        gtk_widget_show(m_overlay);
}

#else // !__WXGTK4__

void wxOverlayImpl::PositionOverlay(GtkWidget* tlw)
{
    int x, y;
    gtk_widget_translate_coordinates(m_target, tlw, m_rect.x, m_rect.y, &x, &y);
    gtk_window_move(GTK_WINDOW(m_overlay), x, y);
    gtk_widget_show(m_overlay);
}

#endif // __WXGTK4__/!__WXGTK4__

#endif // __WXGTK3__
