///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/mediactrl.h
// Purpose:     Wrap runtime checks to manage GTK windows with Wayland and X11
// Author:      Pierluigi Passaro
// Created:     2021-03-18
// Copyright:   (c) 2021 Pierluigi Passaro <pierluigi.p@variscite.com>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_PRIVATE_MEDIACTRL_H_
#define _WX_GTK_PRIVATE_MEDIACTRL_H_

#include "wx/gtk/private/wrapgdk.h"
#include "wx/gtk/private/backend.h"

//-----------------------------------------------------------------------------
// "wxGtkGetIdFromWidget" from widget
//
// Get the windows_id performing run-time checks If the window wasn't realized
// when Load was called, this is the callback for when it is - the purpose of
// which is to tell GStreamer to play the video in our control
//-----------------------------------------------------------------------------
extern "C" {
inline gpointer wxGtkGetIdFromWidget(GtkWidget* widget)
{
    GdkDisplay* display = gtk_widget_get_display(widget);
    gdk_display_flush(display);

#ifdef __WXGTK4__
    // Only top levels have a native surface under GTK4: an ordinary widget
    // has nothing for GStreamer to draw into any more. So the video goes onto
    // the surface of the widget's GtkNative and is confined to the widget's
    // area by a render rectangle -- which is exactly what the GTK3 code below
    // already had to do under Wayland, where subsurfaces don't exist either.
    GtkNative* native = gtk_widget_get_native(widget);
    wxASSERT(native);
    if (!native)
        return (gpointer)nullptr;

    GdkSurface* surface = gtk_native_get_surface(native);
    wxASSERT(surface);
    if (!surface)
        return (gpointer)nullptr;

#ifdef GDK_WINDOWING_X11
    if (wxGTKImpl::IsX11(surface))
    {
        // Same as in wxGLCanvas::GetXWindow(): a stale XID handed to the media
        // backend would have it name a window the server no longer knows.
        if (!wxGTKImpl::CanAskServerAbout(surface))
            return (gpointer)nullptr;

        return (gpointer)GDK_SURFACE_XID(surface);
    }
#endif
#ifdef GDK_WINDOWING_WAYLAND
    if (wxGTKImpl::IsWayland(surface))
    {
        return (gpointer)gdk_wayland_surface_get_wl_surface(surface);
    }
#endif
#else // !__WXGTK4__
    GdkWindow* window = gtk_widget_get_window(widget);
    wxASSERT(window);

#ifdef GDK_WINDOWING_X11
#ifdef __WXGTK3__
    if (wxGTKImpl::IsX11(window))
#endif
    {
        return (gpointer)GDK_WINDOW_XID(window);
    }
#endif
#ifdef GDK_WINDOWING_WAYLAND
    if (wxGTKImpl::IsWayland(window))
    {
        return (gpointer)gdk_wayland_window_get_wl_surface(window);
    }
#endif
#endif // __WXGTK4__/!__WXGTK4__

    return (gpointer)nullptr;
}
}

#ifdef __WXGTK4__

//-----------------------------------------------------------------------------
// "wxGtkGetVideoRect" from widget
//
// Compute the area the video should occupy, in the coordinates of the surface
// returned by wxGtkGetIdFromWidget() above, i.e. the widget's own area offset
// by its position within its top level. Returns false if the widget isn't
// laid out yet, in which case the rectangle is left untouched.
//-----------------------------------------------------------------------------
inline bool wxGtkGetVideoRect(GtkWidget* widget, GdkRectangle* rect)
{
    GtkNative* native = gtk_widget_get_native(widget);
    if (!native)
        return false;

    graphene_rect_t bounds;
    if (!gtk_widget_compute_bounds(widget, GTK_WIDGET(native), &bounds))
        return false;

    // Under client-side decorations the native widget doesn't start at the
    // surface origin, so the shadow offset has to be added on top.
    double dx = 0, dy = 0;
    gtk_native_get_surface_transform(native, &dx, &dy);

    rect->x = int(bounds.origin.x + dx);
    rect->y = int(bounds.origin.y + dy);
    rect->width = int(bounds.size.width);
    rect->height = int(bounds.size.height);

    return true;
}

#endif // __WXGTK4__

#endif // _WX_GTK_PRIVATE_MEDIACTRL_H_
