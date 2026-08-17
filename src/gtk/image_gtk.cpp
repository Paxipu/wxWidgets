///////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/image_gtk.cpp
// Author:      Paul Cornett
// Copyright:   (c) 2020 Paul Cornett
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/bmpbndl.h"
#include "wx/log.h"
#include "wx/window.h"

#include "wx/gtk/private/wrapgtk.h"
#include "wx/gtk/private/gtk3-compat.h"
#include "wx/gtk/private/image.h"

#ifdef __WXGTK4__

// ----------------------------------------------------------------------------
// GTK4: plain GtkImage plus eager bitmap selection, see image.h
// ----------------------------------------------------------------------------

namespace
{

// Pick the bitmap for the scale factor a GtkImage is being shown at, and put
// it on that image as a texture. Also sets the logical pixel size, without
// which GtkImage would lay the texture out at its device pixel size and so
// draw it twice too large on a HiDPI display.
void SetImageFromBitmap(GtkWidget* image, const wxBitmap& bitmap, int logicalHeight)
{
    if ( !bitmap.IsOk() )
    {
        gtk_image_clear(GTK_IMAGE(image));
        return;
    }

    GdkPixbuf* const pixbuf = bitmap.GetPixbuf();
    if ( !pixbuf )
    {
        gtk_image_clear(GTK_IMAGE(image));
        return;
    }

    GdkTexture* const texture = gdk_texture_new_for_pixbuf(pixbuf);
    gtk_image_set_from_paintable(GTK_IMAGE(image), GDK_PAINTABLE(texture));
    g_object_unref(texture);

    if ( logicalHeight > 0 )
        gtk_image_set_pixel_size(GTK_IMAGE(image), logicalHeight);
}

int GetScaleFactor(GtkWidget* widget)
{
    const int scale = gtk_widget_get_scale_factor(widget);

    return scale > 0 ? scale : 1;
}

} // anonymous namespace

/* static */
GtkWidget* wxGtkImage::New(wxWindow* WXUNUSED(win))
{
    return gtk_image_new();
}

/* static */
bool wxGtkImage::Is(GtkWidget* widget)
{
    return widget && GTK_IS_IMAGE(widget);
}

/* static */
void wxGtkImage::Set(GtkWidget* image, const wxBitmapBundle& bitmapBundle)
{
    wxCHECK_RET( Is(image), "not an image widget" );

    if ( !bitmapBundle.IsOk() )
    {
        gtk_image_clear(GTK_IMAGE(image));
        return;
    }

    const wxSize sizeDefault = bitmapBundle.GetDefaultSize();

    SetImageFromBitmap(image,
                       bitmapBundle.GetBitmap(sizeDefault * GetScaleFactor(image)),
                       sizeDefault.y);
}

/* static */
void wxGtkImage::SetDisabled(GtkWidget* image,
                             const wxBitmapBundle& normal,
                             const wxBitmapBundle& disabled)
{
    wxCHECK_RET( Is(image), "not an image widget" );

    const int scale = GetScaleFactor(image);

    if ( disabled.IsOk() )
    {
        const wxSize size = disabled.GetDefaultSize();
        SetImageFromBitmap(image, disabled.GetBitmap(size * scale), size.y);
        return;
    }

    if ( !normal.IsOk() )
    {
        gtk_image_clear(GTK_IMAGE(image));
        return;
    }

    // No disabled variant was given, so derive one, as wxGtkImage did when it
    // drew under GTK3.
    const wxSize size = normal.GetDefaultSize();
    SetImageFromBitmap(image, normal.GetBitmap(size * scale).CreateDisabled(),
                       size.y);
}

#else // !__WXGTK4__

namespace
{

#ifdef __WXGTK3__

// Default provider for HiDPI common case
struct BitmapProviderDefault: wxGtkImage::BitmapProvider
{
    BitmapProviderDefault(wxWindow* win) : m_win(win) { }

    virtual wxBitmap Get(int scale) const override;
    virtual void Set(const wxBitmapBundle& bitmap) override;

    wxWindow* const m_win;

    // All the bitmaps we use.
    wxBitmapBundle m_bitmapBundle;
};

wxBitmap BitmapProviderDefault::Get(int scale) const
{
    wxBitmap bitmap(GetAtScale(m_bitmapBundle, scale));
    if (m_win && !m_win->IsEnabled())
        bitmap = bitmap.CreateDisabled();

    return bitmap;
}

void BitmapProviderDefault::Set(const wxBitmapBundle& bitmapBundle)
{
    m_bitmapBundle = bitmapBundle;
}

#else // !__WXGTK3__

// Trivial version for GTK < 3 which doesn't provide any high DPI support.
struct BitmapProviderDefault: wxGtkImage::BitmapProvider
{
    BitmapProviderDefault(wxWindow*) { }
    virtual wxBitmap Get(int /*scale*/) const override { return wxBitmap(); }
};

#endif // __WXGTK3__/!__WXGTK3__

} // namespace

extern "C" {
static void wxGtkImageClassInit(void* g_class, void* class_data);
}

GType wxGtkImage::Type()
{
    static GType type;
    if (type == 0)
    {
        const GTypeInfo info = {
            sizeof(GtkImageClass),
            nullptr, nullptr,
            wxGtkImageClassInit, nullptr, nullptr,
            sizeof(wxGtkImage), 0, nullptr,
            nullptr
        };
        type = g_type_register_static(
            GTK_TYPE_IMAGE, "wxGtkImage", &info, GTypeFlags(0));
    }
    return type;
}

GtkWidget* wxGtkImage::New(BitmapProvider* provider)
{
    wxGtkImage* image = WX_GTK_IMAGE(g_object_new(Type(), nullptr));
    image->m_provider = provider;
    return GTK_WIDGET(image);
}

GtkWidget* wxGtkImage::New(wxWindow* win)
{
    return New(new BitmapProviderDefault(win));
}

void wxGtkImage::Set(const wxBitmapBundle& bitmapBundle)
{
    m_provider->Set(bitmapBundle);

    // Always set the default bitmap to use the correct size, even if we draw a
    // different bitmap below.
    wxBitmap bitmap = bitmapBundle.GetBitmap(wxDefaultSize);

    GdkPixbuf* pixbuf = nullptr;
    if (bitmap.IsOk())
    {
        pixbuf = bitmap.GetPixbuf();
    }
    gtk_image_set_from_pixbuf(GTK_IMAGE(this), pixbuf);
}

static GtkWidgetClass* wxGtkImageParentClass;

extern "C"
{
#ifndef __WXGTK4__
#ifdef __WXGTK3__
static gboolean wxGtkImageDraw(GtkWidget* widget, cairo_t* cr)
#else
static gboolean wxGtkImageDraw(GtkWidget* widget, GdkEventExpose* event)
#endif
{
    wxGtkImage* image = WX_GTK_IMAGE(widget);

    int scale = 1;
#if GTK_CHECK_VERSION(3,10,0)
    if (wx_is_at_least_gtk3(10))
        scale = gtk_widget_get_scale_factor(widget);
#endif
    const wxBitmap bitmap(image->m_provider->Get(scale));

    if (!bitmap.IsOk())
    {
#ifdef __WXGTK3__
        // Missing bitmap, just do the default
        return wxGtkImageParentClass->draw(widget, cr);
#else
        // We rely on GTK to draw default disabled images
        return wxGtkImageParentClass->expose_event(widget, event);
#endif
    }

    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    int x = (alloc.width  - int(bitmap.GetLogicalWidth() )) / 2;
    int y = (alloc.height - int(bitmap.GetLogicalHeight())) / 2;
#ifdef __WXGTK3__
    gtk_render_background(gtk_widget_get_style_context(widget),
        cr, 0, 0, alloc.width, alloc.height);
    bitmap.Draw(cr, x, y);
#else
    x += alloc.x;
    y += alloc.y;
    gdk_draw_pixbuf(
        gtk_widget_get_window(widget), gtk_widget_get_style(widget)->black_gc, bitmap.GetPixbuf(),
        0, 0, x, y,
        -1, -1, GDK_RGB_DITHER_NORMAL, 0, 0);
#endif
    return false;
}
#endif // !__WXGTK4__

static void wxGtkImageFinalize(GObject* object)
{
    wxGtkImage* image = WX_GTK_IMAGE(object);
    delete image->m_provider;
    image->m_provider = nullptr;
    G_OBJECT_CLASS(wxGtkImageParentClass)->finalize(object);
}

#ifdef __WXGTK4__

// GTK4 replaced the draw vfunc with snapshot; take the same cairo escape hatch
// wxPizza does (see pizza_snapshot() in win_gtk.cpp) so the drawing code below
// is shared rather than reimplemented on render nodes.
static void wxGtkImageSnapshot(GtkWidget* widget, GtkSnapshot* snapshot)
{
    const int w = gtk_widget_get_width(widget);
    const int h = gtk_widget_get_height(widget);
    if ( w <= 0 || h <= 0 )
        return;

    wxGtkImage* image = WX_GTK_IMAGE(widget);
    const wxBitmap bitmap(image->m_provider->Get(gtk_widget_get_scale_factor(widget)));

    if ( !bitmap.IsOk() )
    {
        // Missing bitmap, let GTK draw its default.
        wxGtkImageParentClass->snapshot(widget, snapshot);
        return;
    }

    graphene_rect_t bounds;
    bounds.origin.x = 0;
    bounds.origin.y = 0;
    bounds.size.width = float(w);
    bounds.size.height = float(h);

    cairo_t* const cr = gtk_snapshot_append_cairo(snapshot, &bounds);

    gtk_render_background(gtk_widget_get_style_context(widget), cr, 0, 0, w, h);
    bitmap.Draw(cr,
                (w - int(bitmap.GetLogicalWidth() )) / 2,
                (h - int(bitmap.GetLogicalHeight())) / 2);

    cairo_destroy(cr);
}

#endif // __WXGTK4__

static void wxGtkImageClassInit(void* g_class, void* /*class_data*/)
{
#ifdef __WXGTK4__
    GTK_WIDGET_CLASS(g_class)->snapshot = wxGtkImageSnapshot;
#elif defined(__WXGTK3__)
    GTK_WIDGET_CLASS(g_class)->draw = wxGtkImageDraw;
#else
    GTK_WIDGET_CLASS(g_class)->expose_event = wxGtkImageDraw;
#endif
    G_OBJECT_CLASS(g_class)->finalize = wxGtkImageFinalize;
    wxGtkImageParentClass = GTK_WIDGET_CLASS(g_type_class_peek_parent(g_class));
}
} // extern "C"

#endif // __WXGTK4__/!__WXGTK4__
