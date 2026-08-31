///////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/renderer.cpp
// Purpose:     implementation of wxRendererNative for wxGTK
// Author:      Vadim Zeitlin
// Created:     20.07.2003
// Copyright:   (c) 2003 Vadim Zeitlin <vadim@wxwidgets.org>
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


#include "wx/renderer.h"

#ifndef WX_PRECOMP
    #include "wx/window.h"
    #include "wx/dcclient.h"
    #include "wx/settings.h"
    #include "wx/module.h"
#endif

#include "wx/dcgraph.h"
#ifndef __WXGTK3__
    #include "wx/gtk/dc.h"
    #include "wx/gtk/private/wrapgtk.h"
    #if wxUSE_GRAPHICS_CONTEXT && defined(GDK_WINDOWING_X11)
        #include <gdk/gdkx.h>
        #include <cairo-xlib.h>
    #endif
#endif

#include "wx/gtk/private.h"
#include "wx/gtk/private/gtk3-compat.h"
#include "wx/gtk/private/object.h"
#include "wx/gtk/private/stylecontext.h"
#include "wx/gtk/private/value.h"
#include "wx/gtk/private/win_gtk.h"

#if defined(__WXGTK3__) && !GTK_CHECK_VERSION(3,14,0)
    #define GTK_STATE_FLAG_CHECKED (1 << 11)
#endif

// ----------------------------------------------------------------------------
// wxRendererGTK: our wxRendererNative implementation
// ----------------------------------------------------------------------------

class WXDLLEXPORT wxRendererGTK : public wxDelegateRendererNative
{
public:
    // draw the header control button (used by wxListCtrl)
    virtual int  DrawHeaderButton(wxWindow *win,
                                  wxDC& dc,
                                  const wxRect& rect,
                                  int flags = 0,
                                  wxHeaderSortIconType sortArrow = wxHDR_SORT_ICON_NONE,
                                  wxHeaderButtonParams* params = nullptr) override;

    virtual int GetHeaderButtonHeight(wxWindow *win) override;

    virtual int GetHeaderButtonMargin(wxWindow *win) override;


    // draw the expanded/collapsed icon for a tree control item
    virtual void DrawTreeItemButton(wxWindow *win,
                                    wxDC& dc,
                                    const wxRect& rect,
                                    int flags = 0) override;

    virtual void DrawSplitterBorder(wxWindow *win,
                                    wxDC& dc,
                                    const wxRect& rect,
                                    int flags = 0) override;
    virtual void DrawSplitterSash(wxWindow *win,
                                  wxDC& dc,
                                  const wxSize& size,
                                  wxCoord position,
                                  wxOrientation orient,
                                  int flags = 0) override;

    virtual void DrawComboBoxDropButton(wxWindow *win,
                                        wxDC& dc,
                                        const wxRect& rect,
                                        int flags = 0) override;

    virtual void DrawDropArrow(wxWindow *win,
                               wxDC& dc,
                               const wxRect& rect,
                               int flags = 0) override;

    virtual void DrawCheckBox(wxWindow *win,
                              wxDC& dc,
                              const wxRect& rect,
                              int flags = 0) override;

    virtual void DrawPushButton(wxWindow *win,
                                wxDC& dc,
                                const wxRect& rect,
                                int flags = 0) override;

    virtual void DrawItemSelectionRect(wxWindow *win,
                                       wxDC& dc,
                                       const wxRect& rect,
                                       int flags = 0) override;

    virtual void DrawChoice(wxWindow* win,
                            wxDC& dc,
                            const wxRect& rect,
                            int flags=0) override;

    virtual void DrawComboBox(wxWindow* win,
                                wxDC& dc,
                                const wxRect& rect,
                                int flags=0) override;

    virtual void DrawTextCtrl(wxWindow* win,
                                wxDC& dc,
                                const wxRect& rect,
                                int flags=0) override;

    virtual void DrawRadioBitmap(wxWindow* win,
                                wxDC& dc,
                                const wxRect& rect,
                                int flags=0) override;

    virtual void DrawFocusRect(wxWindow* win, wxDC& dc, const wxRect& rect, int flags = 0) override;

    virtual wxSize GetCheckBoxSize(wxWindow *win, int flags = 0) override;

    virtual wxSplitterRenderParams GetSplitterParams(const wxWindow *win) override;
};

// ============================================================================
// implementation
// ============================================================================

/* static */
wxRendererNative& wxRendererNative::GetDefault()
{
    static wxRendererGTK s_rendererGTK;

    return s_rendererGTK;
}

#ifdef __WXGTK3__
typedef cairo_t wxGTKDrawable;

static cairo_t* wxGetGTKDrawable(const wxDC& dc)
{
    wxGraphicsContext* gc = dc.GetGraphicsContext();
    wxCHECK_MSG(gc, nullptr, "cannot use wxRendererNative on wxDC of this type");
    return static_cast<cairo_t*>(gc->GetNativeContext());
}

#ifdef __WXGTK4__

// GTK4 removed style properties -- gtk_widget_style_get() and
// gtk_style_context_get_style_property() -- and the varargs
// gtk_style_context_get() that read CSS min-width/min-height. The sizes those
// returned are obtained here the way GTK itself obtains them, by measuring a
// real widget of the right kind.
//
// "Of the right kind" matters more than it looks: grouping a GtkCheckButton is
// what turns its "check" CSS node into a "radio" one, so measuring an
// ungrouped button for a radio indicator would quietly give check box metrics.
// wxGTKPrivate::GetRadioButtonWidget() is grouped for exactly this reason.
static void wxGTKMeasureWidget(GtkWidget* widget, int* width, int* height)
{
    if ( width )
    {
        gtk_widget_measure(widget, GTK_ORIENTATION_HORIZONTAL, -1,
                           width, nullptr, nullptr, nullptr);
    }

    if ( height )
    {
        gtk_widget_measure(widget, GTK_ORIENTATION_VERTICAL, -1,
                           height, nullptr, nullptr, nullptr);
    }
}

// The first child node of the given name, or the widget itself if it has none.
static GtkWidget* wxGTKFindChildNode(GtkWidget* widget, const char* name)
{
    for ( GtkWidget* c = gtk_widget_get_first_child(widget);
          c;
          c = gtk_widget_get_next_sibling(c) )
    {
        const char* const css = gtk_widget_get_css_name(c);
        if ( css && strcmp(css, name) == 0 )
            return c;
    }

    return widget;
}

// A note on what does NOT work here, because the newer API looks like the
// obvious answer and quietly draws nothing.
//
// GTK4 deprecates gtk_render_background()/gtk_render_frame() in favour of
// snapshotting a widget and running the GskRenderNode through
// gsk_render_node_draw(). That does reproduce them byte for byte -- measured,
// see docs/gtk/probes/gtk4-renderer-snapshot.c -- but only for a widget whose
// toplevel has been *mapped*. gtk_widget_snapshot_child() yields nothing for a
// widget that has merely been realized, and a null node draws nothing at all.
//
// wx's scratch widgets live in a container whose window is deliberately never
// shown (see wxGTKPrivate::GetContainer()), and showing it is not an option:
// it would flash a window on the user's desktop every time a control part is
// drawn. So the replacement cannot be used for these, and the deprecated calls
// below stay until there is a way to rasterise a widget without mapping it.
//
// This is recorded rather than left to be rediscovered: the substitution
// compiles, passes the whole test suite and passes CI, because nothing there
// looks at the pixels wxRendererNative produces. tests/graphics/renderer.cpp
// does now.

#endif // __WXGTK4__

static const GtkStateFlags stateTypeToFlags[] = {
    GTK_STATE_FLAG_NORMAL, GTK_STATE_FLAG_ACTIVE, GTK_STATE_FLAG_PRELIGHT,
    GTK_STATE_FLAG_SELECTED, GTK_STATE_FLAG_INSENSITIVE, GTK_STATE_FLAG_INCONSISTENT,
    GTK_STATE_FLAG_FOCUSED
};

#else
#define NULL_RECT nullptr,
typedef GdkWindow wxGTKDrawable;

static GdkWindow* wxGetGTKDrawable(wxDC& dc)
{
    GdkWindow* gdk_window = nullptr;

#if wxUSE_GRAPHICS_CONTEXT && defined(GDK_WINDOWING_X11)
    cairo_t* cr = nullptr;
    wxGraphicsContext* gc = dc.GetGraphicsContext();
    if (gc)
        cr = static_cast<cairo_t*>(gc->GetNativeContext());
    if (cr)
    {
        cairo_surface_t* surf = cairo_get_target(cr);
        if (cairo_surface_get_type(surf) == CAIRO_SURFACE_TYPE_XLIB)
        {
            gdk_window = static_cast<GdkWindow*>(
                gdk_xid_table_lookup(cairo_xlib_surface_get_drawable(surf)));
        }
    }
    if (gdk_window == nullptr)
#endif
    {
        wxDCImpl *impl = dc.GetImpl();
        wxGTKDCImpl *gtk_impl = wxDynamicCast( impl, wxGTKDCImpl );
        if (gtk_impl)
            gdk_window = gtk_impl->GetGDKWindow();
        else
            wxFAIL_MSG("cannot use wxRendererNative on wxDC of this type");
    }

    return gdk_window;
}
#endif


#ifdef __WXGTK4__

// ----------------------------------------------------------------------------
// drawing a themed widget under GTK4
// ----------------------------------------------------------------------------

// GTK4 removed gtk_render_background(), gtk_render_frame() and everything that
// fed them: a GtkStyleContext can no longer be asked to paint. What replaces
// them is asking a real widget for its render node, and the conditions for
// that are exact and were measured rather than guessed
// (docs/gtk/probes/gtk4-snapshot-mapped-vs-allocated.c and
// gtk4-renderer-scratch-in-paint.c):
//
//   * the widget must have a current allocation -- gtk_widget_allocate() gives
//     it one, which is what any layout manager does;
//   * it must be inside a MAPPED toplevel. Realized is not enough. wx's own
//     scratch container is deliberately never shown, so the widget cannot stay
//     there -- but every wxRendererNative::Draw*() is handed the window it is
//     drawing into, and during a paint that window is mapped;
//   * it must be genuinely visible. set_visible(FALSE) and
//     set_child_visible(FALSE) both unmap it and give no node, and opacity 0
//     gives none either, because GTK drops a fully transparent widget.
//
// So it is parented far outside the client area instead, where it is mapped
// and visible but clipped away. Its position does not reach the render node:
// gtk_widget_snapshot_child() produces the node in the child's own coordinate
// space, so the node is drawn by translating to the target rectangle only.
//
// The result is the same picture the deprecated calls produced -- identical
// pixel-for-pixel for a button in its normal state, and different, as it must
// be, for prelight and active.

namespace
{

// Somewhere a control will never be.
const int wxGTK_SCRATCH_OFFSET = -32000;

// The widgets drawn from here are the renderer's own, not the shared ones in
// wxGTKPrivate. Those are kept by weak pointer and live in a container that
// owns them, so unparenting one to move it here destroys it -- which is a
// crash the next time anything asks for it, and was one.
//
// They are also never destroyed: one of each kind, held by a strong reference
// for the life of the process, moved between windows as the drawing does.
struct wxGTKScratchWidget
{
    explicit wxGTKScratchWidget(GtkWidget* (*factory)())
        : m_factory(factory)
    {
    }

    // Return the widget, parented into win's client area, or null if this
    // window cannot host it.
    GtkWidget* GetFor(wxWindow* win)
    {
        if ( !win )
            return nullptr;

        GtkWidget* const host = win->m_wxwindow;
        if ( !host || !WX_IS_PIZZA(host) )
            return nullptr;

        // A widget can only be snapshotted inside a mapped toplevel, so a
        // window that is not on screen cannot be drawn into this way.
        if ( !gtk_widget_get_mapped(host) )
            return nullptr;

        if ( !m_widget )
        {
            m_widget = m_factory();
            g_object_ref_sink(m_widget);
        }

        GtkWidget* const parent = gtk_widget_get_parent(m_widget);
        if ( parent != host )
        {
            // Only when the drawing moves to a different window, which does
            // not happen part-way through a paint.
            if ( parent )
                WX_PIZZA(parent)->remove(m_widget);

            WX_PIZZA(host)->put(m_widget, wxGTK_SCRATCH_OFFSET,
                                wxGTK_SCRATCH_OFFSET, 1, 1);
        }

        return m_widget;
    }

    GtkWidget* (*const m_factory)();
    GtkWidget* m_widget = nullptr;
};

} // anonymous namespace

// Draw a themed widget of the given kind into cr at rect.
//
// Returns false if this window cannot host the widget -- it is not on screen,
// or has no client area of its own. The caller then has to fall back to
// drawing something itself, because there is no way to make GTK4 produce a
// themed picture without a mapped widget.
bool
wxGTKDrawThemedWidget(wxWindow* win,
                      wxGTKScratchWidget& scratch,
                      cairo_t* cr,
                      const wxRect& rect,
                      GtkStateFlags state)
{
    if ( !cr || rect.width <= 0 || rect.height <= 0 )
        return false;

    GtkWidget* const widget = scratch.GetFor(win);
    if ( !widget )
        return false;

    GtkWidget* const host = gtk_widget_get_parent(widget);

    if ( state )
        gtk_widget_set_state_flags(widget, state, TRUE);

    gtk_widget_allocate(widget, rect.width, rect.height, -1, nullptr);

    GtkSnapshot* const snapshot = gtk_snapshot_new();
    gtk_widget_snapshot_child(host, widget, snapshot);
    GskRenderNode* const node = gtk_snapshot_free_to_node(snapshot);
    const bool drew = node != nullptr;

    if ( node )
    {
        cairo_save(cr);
        cairo_translate(cr, rect.x, rect.y);
        gsk_render_node_draw(node, cr);
        cairo_restore(cr);

        gsk_render_node_unref(node);
    }

    if ( state )
        gtk_widget_unset_state_flags(widget, state);

    return drew;
}

#endif // __WXGTK4__

// ----------------------------------------------------------------------------
// list/tree controls drawing
// ----------------------------------------------------------------------------

int
wxRendererGTK::DrawHeaderButton(wxWindow *win,
                                wxDC& dc,
                                const wxRect& rect,
                                int flags,
                                wxHeaderSortIconType sortArrow,
                                wxHeaderButtonParams* params)
{
    GtkWidget *button = wxGTKPrivate::GetHeaderButtonWidget();
    if (flags & wxCONTROL_SPECIAL)
        button = wxGTKPrivate::GetHeaderButtonWidgetFirst();
    if (flags & wxCONTROL_DIRTY)
        button = wxGTKPrivate::GetHeaderButtonWidgetLast();

    GtkStateType state = GTK_STATE_NORMAL;
    if (flags & wxCONTROL_DISABLED)
        state = GTK_STATE_INSENSITIVE;
    else
    {
        if (flags & wxCONTROL_CURRENT)
            state = GTK_STATE_PRELIGHT;
    }

#ifdef __WXGTK3__
    cairo_t* cr = wxGetGTKDrawable(dc);
    if (cr == nullptr)
        return 0;

    // AddTreeviewHeaderButton() is only available in 3.20 or later.
#if GTK_CHECK_VERSION(3,20,0)
    if (gtk_check_version(3,20,0) == nullptr)
    {
        int pos = 1;
        if (flags & wxCONTROL_SPECIAL)
            pos = 0;
        if (flags & wxCONTROL_DIRTY)
            pos = 2;

        wxGtkStyleContext sc(dc.GetContentScaleFactor());
        sc.AddTreeviewHeaderButton(pos);

        gtk_style_context_set_state(sc, stateTypeToFlags[state]);
        gtk_render_background(sc, cr, rect.x, rect.y, rect.width, rect.height);
        gtk_render_frame(sc, cr, rect.x, rect.y, rect.width, rect.height);
        if (params)
        {
            sc.Fg(params->m_arrowColour, stateTypeToFlags[state]);
            params->m_labelColour = params->m_arrowColour;
        }
    }
    else
#endif // GTK >= 3.20
    {
        GtkStyleContext* sc = gtk_widget_get_style_context(button);
        gtk_style_context_save(sc);
        gtk_style_context_set_state(sc, stateTypeToFlags[state]);
        gtk_render_background(sc, cr, rect.x, rect.y, rect.width, rect.height);
        gtk_render_frame(sc, cr, rect.x, rect.y, rect.width, rect.height);
        if (params)
        {
            GdkRGBA rgba;
            gtk_style_context_get_color(sc, stateTypeToFlags[state], &rgba);
            params->m_arrowColour = wxColour(rgba);
            params->m_labelColour = params->m_arrowColour;
        }
        gtk_style_context_restore(sc);
    }
#else
    int x_diff = 0;
    if (win->GetLayoutDirection() == wxLayout_RightToLeft)
        x_diff = rect.width;

    GdkWindow* gdk_window = wxGetGTKDrawable(dc);
    gtk_paint_box
    (
        gtk_widget_get_style(button),
        gdk_window,
        state,
        GTK_SHADOW_OUT,
        nullptr,
        button,
        "button",
        dc.LogicalToDeviceX(rect.x) - x_diff, rect.y, rect.width, rect.height
    );
#endif

    return DrawHeaderButtonContents(win, dc, rect, flags, sortArrow, params);
}

int wxRendererGTK::GetHeaderButtonHeight(wxWindow *WXUNUSED(win))
{
    GtkWidget *button = wxGTKPrivate::GetHeaderButtonWidget();

    GtkRequisition req;
#ifdef __WXGTK3__
    gtk_widget_get_preferred_height(button, nullptr, &req.height);
#else
    GTK_WIDGET_GET_CLASS(button)->size_request(button, &req);
#endif

    return req.height;
}

int wxRendererGTK::GetHeaderButtonMargin(wxWindow *WXUNUSED(win))
{
    return 0; // TODO: How to determine the real margin?
}


// draw a ">" or "v" button
void
wxRendererGTK::DrawTreeItemButton(wxWindow* WXUNUSED_IN_GTK3(win),
                                  wxDC& dc, const wxRect& rect, int flags)
{
    wxGTKDrawable* drawable = wxGetGTKDrawable(dc);
    if (drawable == nullptr)
        return;

    GtkWidget *tree = wxGTKPrivate::GetTreeWidget();

#ifdef __WXGTK3__
    int state = GTK_STATE_FLAG_NORMAL;
    if (flags & wxCONTROL_EXPANDED)
    {
        state = GTK_STATE_FLAG_ACTIVE;
        if (gtk_check_version(3,14,0) == nullptr)
            state = GTK_STATE_FLAG_CHECKED;
    }
    if (flags & wxCONTROL_CURRENT)
        state |= GTK_STATE_FLAG_PRELIGHT;
    if (flags & wxCONTROL_SELECTED)
        state |= GTK_STATE_FLAG_SELECTED;

    int expander_size;
#ifdef __WXGTK4__
    // The "expander-size" style property is gone; measure a real expander.
    wxUnusedVar(tree);
    wxGTKMeasureWidget(wxGTKPrivate::GetExpanderWidget(), &expander_size, nullptr);
#else
    gtk_widget_style_get(tree, "expander-size", &expander_size, nullptr);
#endif
    // +1 to match GtkTreeView behavior
    expander_size++;
    const int x = rect.x + (rect.width - expander_size) / 2;
    const int y = rect.y + (rect.width - expander_size) / 2;

    GtkStyleContext* sc = gtk_widget_get_style_context(tree);
    gtk_style_context_save(sc);
    gtk_style_context_set_state(sc, GtkStateFlags(state));
    gtk_style_context_add_class(sc, GTK_STYLE_CLASS_EXPANDER);
    gtk_render_expander(sc, drawable, x, y, expander_size, expander_size);
    gtk_style_context_restore(sc);
#else
    int x_diff = 0;
    if (win->GetLayoutDirection() == wxLayout_RightToLeft)
        x_diff = rect.width;

    GtkStateType state;
    if ( flags & wxCONTROL_CURRENT )
        state = GTK_STATE_PRELIGHT;
    else
        state = GTK_STATE_NORMAL;

    // x and y parameters specify the center of the expander
    gtk_paint_expander
    (
        gtk_widget_get_style(tree),
        drawable,
        state,
        nullptr,
        tree,
        "treeview",
        dc.LogicalToDeviceX(rect.x) + rect.width / 2 - x_diff,
        dc.LogicalToDeviceY(rect.y) + rect.height / 2,
        flags & wxCONTROL_EXPANDED ? GTK_EXPANDER_EXPANDED
                                   : GTK_EXPANDER_COLLAPSED
    );
#endif
}


// ----------------------------------------------------------------------------
// splitter sash drawing
// ----------------------------------------------------------------------------

static int GetGtkSplitterFullSize(GtkWidget* widget)
{
    gint handle_size;
#ifdef __WXGTK4__
    // The "handle-size" style property is gone; a GtkPaned's handle is its
    // "separator" CSS node, so measure that.
    int measured = 0;
    wxGTKMeasureWidget(wxGTKFindChildNode(widget, "separator"), &measured, nullptr);
    handle_size = measured;
#else
    gtk_widget_style_get(widget, "handle_size", &handle_size, nullptr);
#endif
    // Narrow handles don't work well with wxSplitterWindow
    if (handle_size < 5)
        handle_size = 5;

    return handle_size;
}

wxSplitterRenderParams
wxRendererGTK::GetSplitterParams(const wxWindow *WXUNUSED(win))
{
    // we don't draw any border, hence 0 for the second field
    return wxSplitterRenderParams
           (
               GetGtkSplitterFullSize(wxGTKPrivate::GetSplitterWidget()),
               0,
               true     // hot sensitive
           );
}

void
wxRendererGTK::DrawSplitterBorder(wxWindow * WXUNUSED(win),
                                  wxDC& WXUNUSED(dc),
                                  const wxRect& WXUNUSED(rect),
                                  int WXUNUSED(flags))
{
    // nothing to do
}

void
wxRendererGTK::DrawSplitterSash(wxWindow* win,
                                wxDC& dc,
                                const wxSize& size,
                                wxCoord position,
                                wxOrientation orient,
                                int flags)
{
    if (wx_gtk_widget_get_surface_or_window(win->m_wxwindow) == nullptr)
    {
        // window not realized yet
        return;
    }

    wxGTKDrawable* drawable = wxGetGTKDrawable(dc);
    if (drawable == nullptr)
        return;

    // are we drawing vertical or horizontal splitter?
    const bool isVert = orient == wxVERTICAL;

    GtkWidget* widget = wxGTKPrivate::GetSplitterWidget(orient);
    const int full_size = GetGtkSplitterFullSize(widget);

    GdkRectangle rect;

    if ( isVert )
    {
        rect.x = position;
        rect.y = 0;
        rect.width = full_size;
        rect.height = size.y;
    }
    else // horz
    {
        rect.x = 0;
        rect.y = position;
        rect.height = full_size;
        rect.width = size.x;
    }

#ifdef __WXGTK3__
    wxGtkStyleContext sc(dc.GetContentScaleFactor());
    sc.AddWindow();
    gtk_render_background(sc, drawable, rect.x, rect.y, rect.width, rect.height);

    sc.Add(GTK_TYPE_PANED, "paned", "pane-separator", nullptr);
    if (gtk_check_version(3,20,0) == nullptr)
        sc.Add("separator");

    gtk_style_context_set_state(sc,
        flags & wxCONTROL_CURRENT ? GTK_STATE_FLAG_PRELIGHT : GTK_STATE_FLAG_NORMAL);
    gtk_render_handle(sc, drawable, rect.x, rect.y, rect.width, rect.height);
#else
    int x_diff = 0;
    if (win->GetLayoutDirection() == wxLayout_RightToLeft)
        x_diff = rect.width;

    GdkWindow* gdk_window = wxGetGTKDrawable(dc);
    if (gdk_window == nullptr)
        return;
    gtk_paint_handle
    (
        gtk_widget_get_style(win->m_wxwindow),
        gdk_window,
        flags & wxCONTROL_CURRENT ? GTK_STATE_PRELIGHT : GTK_STATE_NORMAL,
        GTK_SHADOW_NONE,
        nullptr /* no clipping */,
        win->m_wxwindow,
        "paned",
        dc.LogicalToDeviceX(rect.x) - x_diff,
        dc.LogicalToDeviceY(rect.y),
        rect.width,
        rect.height,
        isVert ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL
    );
#endif
}

void
wxRendererGTK::DrawDropArrow(wxWindow*,
                             wxDC& dc,
                             const wxRect& rect,
                             int flags)
{
    // If we give WX_PIZZA(win->m_wxwindow)->bin_window as
    // a window for gtk_paint_xxx function, then it won't
    // work for wxMemoryDC. So that is why we assume wxDC
    // is wxWindowDC (wxClientDC, wxMemoryDC and wxPaintDC
    // are derived from it) and use its m_window.

    // draw arrow so that there is even space horizontally
    // on both sides
    const int size = rect.width / 2;
    const int x = rect.x + (size + 1) / 2;
    const int y = rect.y + (rect.height - size + 1) / 2;

    GtkStateType state;

    if ( flags & wxCONTROL_PRESSED )
        state = GTK_STATE_ACTIVE;
    else if ( flags & wxCONTROL_DISABLED )
        state = GTK_STATE_INSENSITIVE;
    else if ( flags & wxCONTROL_CURRENT )
        state = GTK_STATE_PRELIGHT;
    else
        state = GTK_STATE_NORMAL;

#ifdef __WXGTK4__
    cairo_t* const cr = wxGetGTKDrawable(dc);
    if (cr == nullptr)
        return;

    // GTK4 still declares gtk_render_arrow(), among its deprecated functions,
    // but it no longer puts anything on the surface: a GTK4 theme draws the
    // arrow of a combo box as an icon in a node of its own rather than as
    // something a style context paints. Measured on GTK 4.14.5, rendering an
    // arrow through a button style context leaves exactly zero non-transparent
    // pixels behind, which is why every wxComboCtrl drop-down button came up
    // blank. The icon route is what the themes themselves use.
    const double scale = dc.GetContentScaleFactor();

    wxGtkStyleContext sc(scale);
    sc.AddButton();
    gtk_style_context_set_state(sc, stateTypeToFlags[state]);

    GtkIconTheme* const theme =
        gtk_icon_theme_get_for_display(gdk_display_get_default());

    wxGtkObject<GtkIconPaintable> icon(
        gtk_icon_theme_lookup_icon(theme, "pan-down-symbolic", nullptr,
                                   size, int(scale + 0.5),
                                   GTK_TEXT_DIR_LTR,
                                   GTK_ICON_LOOKUP_FORCE_SYMBOLIC));
    if (icon == nullptr)
        return;

    // A symbolic icon has no colour of its own: it takes the one it is
    // snapshotted with, so it follows the theme into a dark one.
    wxColour fg;
    sc.Fg(fg, stateTypeToFlags[state]);

    // GdkRGBA holds floats, so divide by a float: 255.0 makes these double
    // and narrows inside the braces.
    const GdkRGBA rgba =
    {
        fg.Red() / 255.0f, fg.Green() / 255.0f, fg.Blue() / 255.0f,
        fg.Alpha() / 255.0f
    };

    GtkSnapshot* const snapshot = gtk_snapshot_new();
    gtk_symbolic_paintable_snapshot_symbolic(GTK_SYMBOLIC_PAINTABLE(icon.get()),
                                             snapshot, size, size, &rgba, 1);

    if (GskRenderNode* const node = gtk_snapshot_free_to_node(snapshot))
    {
        cairo_save(cr);
        cairo_translate(cr, x, y);
        gsk_render_node_draw(node, cr);
        cairo_restore(cr);
        gsk_render_node_unref(node);
    }
#elif defined(__WXGTK3__)
    cairo_t* cr = wxGetGTKDrawable(dc);
    if (cr)
    {
        wxGtkStyleContext sc(dc.GetContentScaleFactor());
        sc.AddButton();
        gtk_style_context_set_state(sc, stateTypeToFlags[state]);
        gtk_render_arrow(sc, cr, G_PI, x, y, size);
    }
#else
    GdkWindow* gdk_window = wxGetGTKDrawable(dc);
    if (gdk_window == nullptr)
        return;

    GtkWidget* button = wxGTKPrivate::GetButtonWidget();

    // draw arrow on button
    gtk_paint_arrow
    (
        gtk_widget_get_style(button),
        gdk_window,
        state,
        flags & wxCONTROL_PRESSED ? GTK_SHADOW_IN : GTK_SHADOW_OUT,
        nullptr,
        button,
        "arrow",
        GTK_ARROW_DOWN,
        FALSE,
        x, y,
        size, size
    );
#endif
}

void
wxRendererGTK::DrawComboBoxDropButton(wxWindow *win,
                                      wxDC& dc,
                                      const wxRect& rect,
                                      int flags)
{
    DrawPushButton(win,dc,rect,flags);
    DrawDropArrow(win,dc,rect,flags);
}

// Helper used by GetCheckBoxSize() and DrawCheckBox().
namespace
{

struct CheckBoxInfo
{
#ifdef __WXGTK3__
    CheckBoxInfo(wxGtkStyleContext& sc, int flags)
    {
        wxUnusedVar(flags);

        sc.AddCheckButton();
        if (gtk_check_version(3,20,0) == nullptr)
        {
            sc.Add("check");
#ifdef __WXGTK4__
            wxGTKMeasureWidget(
                wxGTKFindChildNode(wxGTKPrivate::GetCheckButtonWidget(), "check"),
                &indicator_width, &indicator_height);
#else
            gtk_style_context_get(sc, GTK_STATE_FLAG_NORMAL,
                                  "min-width", &indicator_width,
                                  "min-height", &indicator_height,
                                  nullptr);
#endif

            GtkBorder border, padding;
            gtk_style_context_get_border(sc, GTK_STATE_FLAG_NORMAL, &border);
            gtk_style_context_get_padding(sc, GTK_STATE_FLAG_NORMAL, &padding);

            margin_left = border.left + padding.left;
            margin_top = border.top + padding.top;
            margin_right = border.right + padding.right;
            margin_bottom = border.bottom + padding.bottom;
        }
#ifndef __WXGTK4__
        // The pre-3.20 fallback, which read style properties. GTK4 is always
        // newer than that, and style properties are gone there, so this branch
        // is unreachable and does not compile.
        else
        {
            wxGtkValue value( G_TYPE_INT);

            gtk_style_context_get_style_property(sc, "indicator-size", value);
            indicator_width =
            indicator_height = g_value_get_int(value);

            gtk_style_context_get_style_property(sc, "indicator-spacing", value);
            margin_left =
            margin_top =
            margin_right =
            margin_bottom = g_value_get_int(value);
        }
#endif // !__WXGTK4__
    }
#else // !__WXGTK3__
    CheckBoxInfo(GtkWidget* button, int flags)
    {
        gint indicator_size, indicator_margin;
        gtk_widget_style_get(button,
                             "indicator_size", &indicator_size,
                             "indicator_spacing", &indicator_margin,
                             nullptr);

        // If wxCONTROL_CELL is set then we want to get the size of wxCheckBox
        // control to draw the check mark centered and at the same position as
        // wxCheckBox does, so offset the check mark itself by the focus margin
        // in the same way as gtk_real_check_button_draw_indicator() does it, see
        // https://github.com/GNOME/gtk/blob/GTK_2_16_0/gtk/gtkcheckbutton.c#L374
        if ( flags & wxCONTROL_CELL )
        {
            gint focus_width, focus_pad;
            gtk_widget_style_get(button,
                                 "focus-line-width", &focus_width,
                                 "focus-padding", &focus_pad,
                                 nullptr);

            indicator_margin += focus_width + focus_pad;
        }

        // In GTK 2 width and height are the same and so are left/right and
        // top/bottom.
        indicator_width =
        indicator_height = indicator_size;

        margin_left =
        margin_top =
        margin_right =
        margin_bottom = indicator_margin;
    }
#endif // __WXGTK3__/!__WXGTK3__

    // Make sure we fit into the provided rectangle, eliminating margins and
    // even reducing the size if necessary.
    void FitInto(const wxRect& rect)
    {
        if ( indicator_width > rect.width )
        {
            indicator_width = rect.width;
            margin_left =
            margin_right = 0;
        }
        else if ( indicator_width + margin_left + margin_right > rect.width )
        {
            margin_left =
            margin_right = (rect.width - indicator_width) / 2;
        }

        if ( indicator_height > rect.height )
        {
            indicator_height = rect.height;
            margin_top =
            margin_bottom = 0;
        }
        else if ( indicator_height + margin_top + margin_bottom > rect.height )
        {
            margin_top =
            margin_bottom = (rect.height - indicator_height) / 2;
        }
    }

    gint indicator_width,
         indicator_height;
    gint margin_left,
         margin_top,
         margin_right,
         margin_bottom;
};

} // anonymous namespace

wxSize
wxRendererGTK::GetCheckBoxSize(wxWindow* win, int flags)
{
    wxSize size;
    // Even though we don't use the window in this implementation, still check
    // that it's valid to avoid surprises when running the same code under the
    // other platforms.
    wxCHECK_MSG(win, size, "Must have a valid window");

#ifdef __WXGTK3__
    wxGtkStyleContext sc(win->GetContentScaleFactor());

    const CheckBoxInfo info(sc, flags);
#else // !__WXGTK3__
    GtkWidget* button = wxGTKPrivate::GetCheckButtonWidget();

    const CheckBoxInfo info(button, flags);
#endif // __WXGTK3__/!__WXGTK3__

    size.x = info.indicator_width + info.margin_left + info.margin_right;
    size.y = info.indicator_height + info.margin_top + info.margin_bottom;

    return size;
}

void
wxRendererGTK::DrawCheckBox(wxWindow*,
                            wxDC& dc,
                            const wxRect& rect,
                            int flags )
{
#ifdef __WXGTK3__
    cairo_t* cr = wxGetGTKDrawable(dc);
    if (cr == nullptr)
        return;

    int state = GTK_STATE_FLAG_NORMAL;
    if (flags & wxCONTROL_CHECKED)
    {
        state = GTK_STATE_FLAG_ACTIVE;
        if (gtk_check_version(3,14,0) == nullptr)
            state = GTK_STATE_FLAG_CHECKED;
    }
    if (flags & wxCONTROL_DISABLED)
        state |= GTK_STATE_FLAG_INSENSITIVE;
    if (flags & wxCONTROL_UNDETERMINED)
        state |= GTK_STATE_FLAG_INCONSISTENT;
    if (flags & wxCONTROL_CURRENT)
        state |= GTK_STATE_FLAG_PRELIGHT;

    wxGtkStyleContext sc(dc.GetContentScaleFactor());

    CheckBoxInfo info(sc, flags);
    info.FitInto(rect);

    const int w = info.indicator_width + info.margin_left + info.margin_right;
    const int h = info.indicator_height + info.margin_top + info.margin_bottom;

    int x = rect.x + (rect.width  - w) / 2;
    int y = rect.y + (rect.height - h) / 2;

    const bool isRTL = dc.GetLayoutDirection() == wxLayout_RightToLeft;
    if (isRTL)
    {
        // checkbox is not mirrored
        cairo_save(cr);
        cairo_scale(cr, -1, 1);
        x = -x - w;
    }

    if (gtk_check_version(3,20,0) == nullptr)
    {
        gtk_style_context_set_state(sc, GtkStateFlags(state));
        gtk_render_background(sc, cr, x, y, w, h);
        gtk_render_frame(sc, cr, x, y, w, h);

        // check is rendered in content area
        gtk_render_check(sc, cr,
                         x + info.margin_left, y + info.margin_top,
                         info.indicator_width, info.indicator_height);
    }
    else
    {
        // need save/restore for GTK+ 3.6 & 3.8
        gtk_style_context_save(sc);
        gtk_style_context_set_state(sc, GtkStateFlags(state));
        gtk_render_background(sc, cr, x, y, w, h);
        gtk_render_frame(sc, cr, x, y, w, h);
        gtk_style_context_add_class(sc, "check");
        gtk_render_check(sc, cr, x, y, w, h);
        gtk_style_context_restore(sc);
    }
    if (isRTL)
        cairo_restore(cr);

#else // !__WXGTK3__
    GtkWidget* button = wxGTKPrivate::GetCheckButtonWidget();

    CheckBoxInfo info(button, flags);
    info.FitInto(rect);

    GtkStateType state;

    if ( flags & wxCONTROL_PRESSED )
        state = GTK_STATE_ACTIVE;
    else if ( flags & wxCONTROL_DISABLED )
        state = GTK_STATE_INSENSITIVE;
    else if ( flags & wxCONTROL_CURRENT )
        state = GTK_STATE_PRELIGHT;
    else
        state = GTK_STATE_NORMAL;

    GtkShadowType shadow_type;

    if ( flags & wxCONTROL_UNDETERMINED )
        shadow_type = GTK_SHADOW_ETCHED_IN;
    else if ( flags & wxCONTROL_CHECKED )
        shadow_type = GTK_SHADOW_IN;
    else
        shadow_type = GTK_SHADOW_OUT;

    GdkWindow* gdk_window = wxGetGTKDrawable(dc);
    if (gdk_window == nullptr)
        return;

    gtk_paint_check
    (
        gtk_widget_get_style(button),
        gdk_window,
        state,
        shadow_type,
        nullptr,
        button,
        "cellcheck",
        dc.LogicalToDeviceX(rect.x) + info.margin_left,
        dc.LogicalToDeviceY(rect.y) + (rect.height - info.indicator_height) / 2,
        info.indicator_width, info.indicator_height
    );
#endif // __WXGTK3__/!__WXGTK3__
}

void
wxRendererGTK::DrawPushButton(wxWindow* win,
                              wxDC& dc,
                              const wxRect& rect,
                              int flags)
{
    GtkWidget *button = wxGTKPrivate::GetButtonWidget();

    // draw button
    GtkStateType state;

    if ( flags & wxCONTROL_PRESSED )
        state = GTK_STATE_ACTIVE;
    else if ( flags & wxCONTROL_DISABLED )
        state = GTK_STATE_INSENSITIVE;
    else if ( flags & wxCONTROL_CURRENT )
        state = GTK_STATE_PRELIGHT;
    else
        state = GTK_STATE_NORMAL;

#ifdef __WXGTK4__
    static wxGTKScratchWidget s_scratchButton(gtk_button_new);
    if ( !wxGTKDrawThemedWidget(win, s_scratchButton, wxGetGTKDrawable(dc),
                                rect, stateTypeToFlags[state]) )
    {
        // The window is not on screen, so GTK cannot be asked for a themed
        // picture at all. Draw wx's own rather than nothing: silently blank is
        // the failure this whole area of the port has been bitten by.
        m_rendererNative.DrawPushButton(win, dc, rect, flags);
    }
#elif defined(__WXGTK3__)
    cairo_t* cr = wxGetGTKDrawable(dc);
    if (cr)
    {
        GtkStyleContext* sc = gtk_widget_get_style_context(button);
        gtk_style_context_save(sc);
        gtk_style_context_set_state(sc, stateTypeToFlags[state]);
        gtk_render_background(sc, cr, rect.x, rect.y, rect.width, rect.height);
        gtk_render_frame(sc, cr, rect.x, rect.y, rect.width, rect.height);
        gtk_style_context_restore(sc);
    }
#else
    GdkWindow* gdk_window = wxGetGTKDrawable(dc);
    if (gdk_window == nullptr)
        return;

    gtk_paint_box
    (
        gtk_widget_get_style(button),
        gdk_window,
        state,
        flags & wxCONTROL_PRESSED ? GTK_SHADOW_IN : GTK_SHADOW_OUT,
        nullptr,
        button,
        "button",
        dc.LogicalToDeviceX(rect.x),
        dc.LogicalToDeviceY(rect.y),
        rect.width,
        rect.height
    );
#endif
}

void
wxRendererGTK::DrawItemSelectionRect(wxWindow* win,
                                     wxDC& dc,
                                     const wxRect& rect,
                                     int flags )
{
    wxGTKDrawable* drawable = wxGetGTKDrawable(dc);
    if (drawable == nullptr)
        return;

    if (flags & wxCONTROL_SELECTED)
    {
        GtkWidget* treeWidget = wxGTKPrivate::GetTreeWidget();

#ifdef __WXGTK3__
        GtkStyleContext* sc = gtk_widget_get_style_context(treeWidget);
        gtk_style_context_save(sc);
        int state = GTK_STATE_FLAG_SELECTED;
        if (flags & wxCONTROL_FOCUSED)
            state |= GTK_STATE_FLAG_FOCUSED;
        gtk_style_context_set_state(sc, GtkStateFlags(state));
        gtk_style_context_add_class(sc, GTK_STYLE_CLASS_CELL);
        gtk_render_background(sc, drawable, rect.x, rect.y, rect.width, rect.height);
        gtk_style_context_restore(sc);
#else
        int x_diff = 0;
        if (win->GetLayoutDirection() == wxLayout_RightToLeft)
            x_diff = rect.width;

        // the wxCONTROL_FOCUSED state is deduced
        // directly from the m_wxwindow by GTK+
        gtk_paint_flat_box(gtk_widget_get_style(treeWidget),
                        drawable,
                        GTK_STATE_SELECTED,
                        GTK_SHADOW_NONE,
                        NULL_RECT
                        win->m_wxwindow,
                        "cell_even",
                        dc.LogicalToDeviceX(rect.x) - x_diff,
                        dc.LogicalToDeviceY(rect.y),
                        rect.width,
                        rect.height );
#endif
    }

    if ((flags & wxCONTROL_CURRENT) && (flags & wxCONTROL_FOCUSED))
        DrawFocusRect(win, dc, rect, flags);
}

void wxRendererGTK::DrawFocusRect(wxWindow* win, wxDC& dc, const wxRect& rect, int flags)
{
    wxGTKDrawable* drawable = wxGetGTKDrawable(dc);
    if (drawable == nullptr)
        return;

    GtkStateType state;
    if (flags & wxCONTROL_SELECTED)
        state = GTK_STATE_SELECTED;
    else
        state = GTK_STATE_NORMAL;

#ifdef __WXGTK3__
    GtkStyleContext* sc = gtk_widget_get_style_context(win->m_widget);
    gtk_style_context_save(sc);
    gtk_style_context_set_state(sc, stateTypeToFlags[state]);
    gtk_render_focus(sc, drawable, rect.x, rect.y, rect.width, rect.height);
    gtk_style_context_restore(sc);
#else
    gtk_paint_focus( gtk_widget_get_style(win->m_widget),
                     drawable,
                     state,
                     NULL_RECT
                     win->m_wxwindow,
                     nullptr,
                     dc.LogicalToDeviceX(rect.x),
                     dc.LogicalToDeviceY(rect.y),
                     rect.width,
                     rect.height );
#endif
}

// Uses the theme to draw the border and fill for something like a wxTextCtrl
void wxRendererGTK::DrawTextCtrl(wxWindow* win, wxDC& dc, const wxRect& rect, int flags)
{
    wxGTKDrawable* drawable = wxGetGTKDrawable(dc);
    if (drawable == nullptr)
        return;

#ifdef __WXGTK4__
    int state = GTK_STATE_FLAG_NORMAL;
    if (flags & wxCONTROL_FOCUSED)
        state = GTK_STATE_FLAG_FOCUSED;
    if (flags & wxCONTROL_DISABLED)
        state = GTK_STATE_FLAG_INSENSITIVE;

    static wxGTKScratchWidget s_scratchEntry(gtk_entry_new);
    if ( !wxGTKDrawThemedWidget(win, s_scratchEntry, drawable, rect,
                                GtkStateFlags(state)) )
    {
        m_rendererNative.DrawTextCtrl(win, dc, rect, flags);
    }
#elif defined(__WXGTK3__)
    int state = GTK_STATE_FLAG_NORMAL;
    if (flags & wxCONTROL_FOCUSED)
        state = GTK_STATE_FLAG_FOCUSED;
    if (flags & wxCONTROL_DISABLED)
        state = GTK_STATE_FLAG_INSENSITIVE;

    wxGtkStyleContext sc(dc.GetContentScaleFactor());
    sc.Add(GTK_TYPE_ENTRY, "entry", "entry", nullptr);

    gtk_style_context_set_state(sc, GtkStateFlags(state));
    gtk_render_background(sc, drawable, rect.x, rect.y, rect.width, rect.height);
    gtk_render_frame(sc, drawable, rect.x, rect.y, rect.width, rect.height);
#else
    GtkWidget* entry = wxGTKPrivate::GetEntryWidget();

    GtkStateType state = GTK_STATE_NORMAL;
    if ( flags & wxCONTROL_DISABLED )
        state = GTK_STATE_INSENSITIVE;

    wx_gtk_widget_set_focusable(entry, (flags & wxCONTROL_CURRENT) != 0);

    gtk_paint_shadow
    (
        gtk_widget_get_style(entry),
        drawable,
        state,
        GTK_SHADOW_OUT,
        NULL_RECT
        entry,
        "entry",
        dc.LogicalToDeviceX(rect.x),
        dc.LogicalToDeviceY(rect.y),
        rect.width,
        rect.height
  );
#endif
}

// Draw the equivalent of a wxComboBox
void wxRendererGTK::DrawComboBox(wxWindow* win, wxDC& dc, const wxRect& rect, int flags)
{
    wxGTKDrawable* drawable = wxGetGTKDrawable(dc);
    if (drawable == nullptr)
        return;

    GtkWidget* combo = wxGTKPrivate::GetComboBoxWidget();

    GtkStateType state = GTK_STATE_NORMAL;
    if ( flags & wxCONTROL_DISABLED )
       state = GTK_STATE_INSENSITIVE;

    wx_gtk_widget_set_focusable(combo, (flags & wxCONTROL_CURRENT) != 0);

#ifdef __WXGTK3__
    GtkStyleContext* sc = gtk_widget_get_style_context(combo);
    gtk_style_context_save(sc);
    gtk_style_context_set_state(sc, stateTypeToFlags[state]);
    gtk_render_background(sc, drawable, rect.x, rect.y, rect.width, rect.height);
    gtk_render_frame(sc, drawable, rect.x, rect.y, rect.width, rect.height);
    gtk_style_context_restore(sc);
    wxRect r = rect;
    r.x += r.width - r.height;
    r.width = r.height;
    DrawComboBoxDropButton(win, dc, r, flags);
#else
    wxUnusedVar(win);
    gtk_paint_shadow
    (
        gtk_widget_get_style(combo),
        drawable,
        state,
        GTK_SHADOW_OUT,
        NULL_RECT
        combo,
        "combobox",
        dc.LogicalToDeviceX(rect.x),
        dc.LogicalToDeviceY(rect.y),
        rect.width,
        rect.height
    );

    wxRect r = rect;
    int extent = rect.height / 2;
    r.x += rect.width - extent - extent/2;
    r.y += extent/2;
    r.width = extent;
    r.height = extent;

    gtk_paint_arrow
    (
        gtk_widget_get_style(combo),
        drawable,
        state,
        GTK_SHADOW_OUT,
        NULL_RECT
        combo,
        "arrow",
        GTK_ARROW_DOWN,
        TRUE,
        dc.LogicalToDeviceX(r.x),
        dc.LogicalToDeviceY(r.y),
        r.width,
        r.height
    );

    r = rect;
    r.x += rect.width - 2*extent;
    r.width = 2;

    gtk_paint_box
    (
        gtk_widget_get_style(combo),
        drawable,
        state,
        GTK_SHADOW_ETCHED_OUT,
        NULL_RECT
        combo,
        "vseparator",
        dc.LogicalToDeviceX(r.x),
        dc.LogicalToDeviceY(r.y+1),
        r.width,
        r.height-2
    );
#endif
}

void wxRendererGTK::DrawChoice(wxWindow* win, wxDC& dc,
                           const wxRect& rect, int flags)
{
    DrawComboBox( win, dc, rect, flags );
}


// Draw a themed radio button
void wxRendererGTK::DrawRadioBitmap(wxWindow*, wxDC& dc, const wxRect& rect, int flags)
{
    wxGTKDrawable* drawable = wxGetGTKDrawable(dc);
    if (drawable == nullptr)
        return;

#ifdef __WXGTK3__
    int state = GTK_STATE_FLAG_NORMAL;
    if (flags & wxCONTROL_CHECKED)
    {
        state = GTK_STATE_FLAG_ACTIVE;
        if (gtk_check_version(3,14,0) == nullptr)
            state = GTK_STATE_FLAG_CHECKED;
    }
    if (flags & wxCONTROL_DISABLED)
        state |= GTK_STATE_FLAG_INSENSITIVE;
    if (flags & wxCONTROL_UNDETERMINED)
        state |= GTK_STATE_FLAG_INCONSISTENT;
    if (flags & wxCONTROL_CURRENT)
        state |= GTK_STATE_FLAG_PRELIGHT;

    int min_width, min_height;
    wxGtkStyleContext sc(dc.GetContentScaleFactor());
#ifdef __WXGTK4__
    // GtkRadioButton is gone: a radio button is a grouped GtkCheckButton, whose
    // indicator node is "radio" rather than "check" because of that grouping.
    sc.Add(GTK_TYPE_CHECK_BUTTON, "checkbutton", nullptr);
    sc.Add("radio");
    wxGTKMeasureWidget(
        wxGTKFindChildNode(wxGTKPrivate::GetRadioButtonWidget(), "radio"),
        &min_width, &min_height);
#else
    sc.Add(GTK_TYPE_RADIO_BUTTON, "radiobutton", nullptr);
    if (gtk_check_version(3,20,0) == nullptr)
    {
        sc.Add("radio");
        gtk_style_context_get(sc, GTK_STATE_FLAG_NORMAL,
            "min-width", &min_width, "min-height", &min_height, nullptr);
    }
    else
    {
        wxGtkValue value( G_TYPE_INT);
        gtk_style_context_get_style_property(sc, "indicator-size", value);
        min_width = g_value_get_int(value);
        min_height = min_width;
    }
#endif

    // need save/restore for GTK+ 3.6 & 3.8
    gtk_style_context_save(sc);
    gtk_style_context_set_state(sc, GtkStateFlags(state));
    const int x = rect.x + (rect.width - min_width) / 2;
    const int y = rect.y + (rect.height - min_height) / 2;
    gtk_render_background(sc, drawable, x, y, min_width, min_height);
    gtk_render_frame(sc, drawable, x, y, min_width, min_height);
    gtk_style_context_add_class(sc, "radio");
    gtk_render_option(sc, drawable, x, y, min_width, min_height);
    gtk_style_context_restore(sc);
#else
    GtkWidget* button = wxGTKPrivate::GetRadioButtonWidget();

    GtkShadowType shadow_type = GTK_SHADOW_OUT;
    if ( flags & wxCONTROL_CHECKED )
        shadow_type = GTK_SHADOW_IN;
    else if ( flags & wxCONTROL_UNDETERMINED )
        shadow_type = GTK_SHADOW_ETCHED_IN;

    GtkStateType state = GTK_STATE_NORMAL;
    if ( flags & wxCONTROL_DISABLED )
        state = GTK_STATE_INSENSITIVE;
    if ( flags & wxCONTROL_PRESSED )
        state = GTK_STATE_ACTIVE;
/*
    Don't know when to set this
       state_type = GTK_STATE_PRELIGHT;
*/

    gtk_paint_option
    (
        gtk_widget_get_style(button),
        drawable,
        state,
        shadow_type,
        NULL_RECT
        button,
        "radiobutton",
        dc.LogicalToDeviceX(rect.x),
        dc.LogicalToDeviceY(rect.y),
        rect.width, rect.height
    );
#endif
}
