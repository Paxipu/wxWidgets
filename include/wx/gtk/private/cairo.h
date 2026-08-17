///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/cairo.h
// Purpose:     Cairo-related helpers
// Author:      Vadim Zeitlin
// Created:     2022-09-23
// Copyright:   (c) 2022 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_PRIVATE_CAIRO_H_
#define _WX_GTK_PRIVATE_CAIRO_H_

// Everything here is about drawing straight onto a GdkWindow, outside of any
// paint handler. GTK4 has no such thing: gdk_cairo_create() is gone and a
// GdkSurface cannot be drawn to at all except from a snapshot callback, where
// GTK hands out the context itself. See src/gtk/dc.cpp for what wx does about
// this.
#ifndef __WXGTK4__

// ----------------------------------------------------------------------------
// Redefine GDK function to avoiding deprecation warnings
// ----------------------------------------------------------------------------

wxGCC_WARNING_SUPPRESS(deprecated-declarations)

static inline
cairo_t* wx_gdk_cairo_create(GdkWindow* w) { return gdk_cairo_create(w); }
#define gdk_cairo_create wx_gdk_cairo_create

wxGCC_WARNING_RESTORE(deprecated-declarations)

// ----------------------------------------------------------------------------
// RAII helper creating a Cairo context in ctor and destroying it in dtor
// ----------------------------------------------------------------------------

namespace wxGTKImpl
{

class CairoContext
{
public:
    explicit CairoContext(GdkWindow* window)
        : m_cr(gdk_cairo_create(window))
    {
    }

    ~CairoContext()
    {
        cairo_destroy(m_cr);
    }

    operator cairo_t*() const { return m_cr; }

private:
    cairo_t* const m_cr;

    wxDECLARE_NO_COPY_CLASS(CairoContext);
};

} // namespace wxGTKImpl

#endif // !__WXGTK4__

#endif // _WX_GTK_PRIVATE_CAIRO_H_
