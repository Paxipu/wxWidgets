/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/cursor.cpp
// Purpose:     wxCursor implementation
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/cursor.h"

#ifndef WX_PRECOMP
    #include "wx/window.h"
    #include "wx/image.h"
    #include "wx/bitmap.h"
    #include "wx/log.h"
#endif // WX_PRECOMP

#include "wx/gtk/private/wrapgtk.h"
#include "wx/gtk/private/object.h"
#include "wx/gtk/private/backend.h"
#include "wx/gtk/private/gtk3-compat.h"

#ifndef __WXGTK4__
GdkWindow* wxGetTopLevelGDK();
#endif
GdkDisplay* wxGetTopLevelGdkDisplay();

//-----------------------------------------------------------------------------
// wxCursorRefData
//-----------------------------------------------------------------------------

class wxCursorRefData: public wxGDIRefData
{
public:
    wxCursorRefData();
    virtual ~wxCursorRefData();

    virtual bool IsOk() const override { return m_cursor != nullptr; }

    GdkCursor *m_cursor;

private:
    // There is no way to copy m_cursor so we can't implement a copy ctor
    // properly.
    wxDECLARE_NO_COPY_CLASS(wxCursorRefData);
};

wxCursorRefData::wxCursorRefData()
{
    m_cursor = nullptr;
}

wxCursorRefData::~wxCursorRefData()
{
    if (m_cursor)
    {
#ifdef __WXGTK3__
        g_object_unref(m_cursor);
#else
        gdk_cursor_unref(m_cursor);
#endif
    }
}

//-----------------------------------------------------------------------------
// wxCursor
//-----------------------------------------------------------------------------

#define M_CURSORDATA static_cast<wxCursorRefData*>(m_refData)

wxIMPLEMENT_DYNAMIC_CLASS(wxCursor, wxGDIObject);

wxCursor::wxCursor()
{
}

wxCursor::wxCursor(const wxBitmap& bitmap, int hotSpotX, int hotSpotY)
{
    InitFromBitmap(bitmap, hotSpotX, hotSpotY);
}

wxCursor::wxCursor(const wxString& cursor_file,
                   wxBitmapType type,
                   int hotSpotX, int hotSpotY)
{
#if wxUSE_IMAGE
    wxImage img;
    if (!img.LoadFile(cursor_file, type))
        return;

    // eventually set the hotspot:
    if (!img.HasOption(wxIMAGE_OPTION_CUR_HOTSPOT_X))
        img.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_X, hotSpotX);
    if (!img.HasOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y))
        img.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y, hotSpotY);

    InitFromImage(img);
#endif // wxUSE_IMAGE
}

#if wxUSE_IMAGE
wxCursor::wxCursor(const wxImage& img)
{
    InitFromImage(img);
}

wxCursor::wxCursor(const char* const* xpmData)
{
    InitFromImage(wxImage(xpmData));
}
#endif // wxUSE_IMAGE

wxCursor::wxCursor(const char bits[], int width, int height,
                   int hotSpotX, int hotSpotY,
                   const char maskBits[], const wxColour *fg, const wxColour *bg)
{
    wxBitmap bitmap(bits, width, height);
    if (maskBits)
        bitmap.SetMask(new wxMask(wxBitmap(maskBits, width, height), *wxWHITE));

    InitFromBitmap(bitmap, hotSpotX, hotSpotY, fg, bg);
}

void
wxCursor::InitFromBitmap(const wxBitmap& bitmap, int hotSpotX, int hotSpotY,
                         const wxColour *fg, const wxColour *bg)
{
    const int width = bitmap.GetWidth();
    const int height = bitmap.GetHeight();

    m_refData = new wxCursorRefData;
    if (hotSpotX < 0 || hotSpotX >= width)
        hotSpotX = 0;
    if (hotSpotY < 0 || hotSpotY >= height)
        hotSpotY = 0;
#ifdef __WXGTK3__
    GdkPixbuf* pixbuf = bitmap.GetPixbuf();
    if ((fg && *fg != *wxBLACK) || (bg && *bg != *wxWHITE))
    {
        const int stride = gdk_pixbuf_get_rowstride(pixbuf);
        const int n_channels = gdk_pixbuf_get_n_channels(pixbuf);
        guchar* data = gdk_pixbuf_get_pixels(pixbuf);
        for (int j = 0; j < height; j++, data += stride)
        {
            guchar* p = data;
            for (int i = 0; i < width; i++, p += n_channels)
            {
                if (p[0] == 0)
                {
                    if (fg)
                    {
                        p[0] = fg->Red();
                        p[1] = fg->Green();
                        p[2] = fg->Blue();
                    }
                }
                else
                {
                    if (bg)
                    {
                        p[0] = bg->Red();
                        p[1] = bg->Green();
                        p[2] = bg->Blue();
                    }
                }
            }
        }
    }

    GdkDisplay* const display = wxGetTopLevelGdkDisplay();

    // Prefer to create cursor from surface as this allows us to specify the
    // bitmap scaling factor.
#ifdef __WXGTK4__
    // GdkCursor is only constructible from a GdkTexture under GTK4 --
    // there's no cairo-surface or GdkPixbuf constructor, and
    // gdk_cursor_new_from_name()/new_from_texture() dropped the
    // GdkDisplay* argument entirely (a cursor is display-independent now).
    wxUnusedVar(display);
    GdkTexture* texture = gdk_texture_new_for_pixbuf(pixbuf);
    M_CURSORDATA->m_cursor = gdk_cursor_new_from_texture(
        texture, hotSpotX, hotSpotY, nullptr);
    g_object_unref(texture);
#elif GTK_CHECK_VERSION(3,10,0)
    if (wx_is_at_least_gtk3(10))
    {
        cairo_surface_t* const
            surface = gdk_cairo_surface_create_from_pixbuf(pixbuf, 1, nullptr);
        const double scaleFactor = bitmap.GetScaleFactor();
        cairo_surface_set_device_scale(surface, scaleFactor, scaleFactor);

        M_CURSORDATA->m_cursor = gdk_cursor_new_from_surface(
            display, surface, hotSpotX / scaleFactor, hotSpotY / scaleFactor);

        cairo_surface_destroy(surface);
    }
    else
#endif // GTK 3 > 3.10
#ifndef __WXGTK4__
    {
        M_CURSORDATA->m_cursor = gdk_cursor_new_from_pixbuf(
            display, pixbuf, hotSpotX, hotSpotY);
    }
#endif
#else
    if (!fg)
        fg = wxBLACK;
    if (!bg)
        bg = wxWHITE;

    wxBitmap mask(bitmap.GetMask() ? bitmap.GetMask()->GetBitmap() : bitmap);

    M_CURSORDATA->m_cursor = gdk_cursor_new_from_pixmap(
                 bitmap.GetPixmap(),
                 mask.GetPixmap(),
                 fg->GetColor(), bg->GetColor(),
                 hotSpotX, hotSpotY );
#endif
}

wxPoint wxCursor::GetHotSpot() const
{
#ifdef __WXGTK4__
    // gdk_cursor_get_image() (returning a GdkPixbuf with "x_hot"/"y_hot"
    // options) doesn't exist under GTK4 -- the hotspot is queried directly
    // via its own getters now. Note this only reflects a hotspot actually
    // set on construction (InitFromBitmap/InitFromImage); named cursors
    // from InitFromStock don't carry a meaningful one.
    if (GetCursor())
        return wxPoint(gdk_cursor_get_hotspot_x(GetCursor()),
                        gdk_cursor_get_hotspot_y(GetCursor()));
#elif GTK_CHECK_VERSION(2,8,0)
    if (GetCursor())
    {
        if (wx_is_at_least_gtk2(8))
        {
            GdkPixbuf *pixbuf = gdk_cursor_get_image(GetCursor());
            if (pixbuf)
            {
                wxPoint hotSpot = wxDefaultPosition;
                const gchar* opt_xhot = gdk_pixbuf_get_option(pixbuf, "x_hot");
                const gchar* opt_yhot = gdk_pixbuf_get_option(pixbuf, "y_hot");
                if (opt_xhot && opt_yhot)
                {
                    const int xhot = atoi(opt_xhot);
                    const int yhot = atoi(opt_yhot);
                    hotSpot = wxPoint(xhot, yhot);
                }
                g_object_unref(pixbuf);
                return hotSpot;
            }
        }
    }
#endif

    return wxDefaultPosition;
}

#ifdef __WXGTK4__
void wxCursor::InitFromStock( wxStockCursor cursorId )
{
    m_refData = new wxCursorRefData();

    // GdkCursorType (the old X-cursor-font shape enum) and
    // gdk_cursor_new_for_display() (which took one) don't exist under
    // GTK4 -- a GdkCursor can now only be built by CSS cursor name (see
    // https://www.w3.org/TR/css-ui-4/#cursor) or from a GdkTexture (used
    // by InitFromBitmap/InitFromImage instead). Mapped each stock cursor
    // to the closest standard CSS keyword; a few have no real equivalent
    // (paint brush/spraycan/pencil, the three mouse-button cursors, and
    // "point at scrollbar arrow") and fall back to "default" -- a known,
    // minor fidelity gap, not runtime-verified.
    const char* name = "default";
    switch (cursorId)
    {
        case wxCURSOR_BLANK:            name = "none"; break;
        case wxCURSOR_ARROW:            // fall through to default
        case wxCURSOR_DEFAULT:          name = "default"; break;
        case wxCURSOR_RIGHT_ARROW:      name = "default"; break;
        case wxCURSOR_HAND:             name = "pointer"; break;
        case wxCURSOR_CROSS:            name = "crosshair"; break;
        case wxCURSOR_SIZEWE:           name = "ew-resize"; break;
        case wxCURSOR_SIZENS:           name = "ns-resize"; break;
        case wxCURSOR_ARROWWAIT:
        case wxCURSOR_WAIT:
        case wxCURSOR_WATCH:            name = "wait"; break;
        // Cursor themes don't have a generic four-way "sizing" cursor
        // (this is what the pre-GTK4 code fell back to "move" for too,
        // on non-X11 displays where cursor themes are always in play).
        case wxCURSOR_SIZING:           name = "move"; break;
        case wxCURSOR_SPRAYCAN:         name = "default"; break;
        case wxCURSOR_IBEAM:            name = "text"; break;
        case wxCURSOR_PENCIL:           name = "default"; break;
        case wxCURSOR_NO_ENTRY:         name = "not-allowed"; break;
        case wxCURSOR_SIZENWSE:         name = "nwse-resize"; break;
        case wxCURSOR_SIZENESW:         name = "nesw-resize"; break;
        case wxCURSOR_QUESTION_ARROW:   name = "help"; break;
        case wxCURSOR_PAINT_BRUSH:      name = "default"; break;
        case wxCURSOR_MAGNIFIER:        name = "zoom-in"; break;
        case wxCURSOR_CHAR:             name = "text"; break;
        case wxCURSOR_LEFT_BUTTON:      name = "default"; break;
        case wxCURSOR_MIDDLE_BUTTON:    name = "default"; break;
        case wxCURSOR_RIGHT_BUTTON:     name = "default"; break;
        case wxCURSOR_BULLSEYE:         name = "crosshair"; break;

        case wxCURSOR_POINT_LEFT:       name = "default"; break;
        case wxCURSOR_POINT_RIGHT:      name = "default"; break;
/*
        case wxCURSOR_DOUBLE_ARROW:
        case wxCURSOR_CROSS_REVERSE:
        case wxCURSOR_BASED_ARROW_UP:
        case wxCURSOR_BASED_ARROW_DOWN:
*/

        default:
            wxFAIL_MSG(wxT("unsupported cursor type"));
            // will use the standard one
            break;
    }

    M_CURSORDATA->m_cursor = gdk_cursor_new_from_name(name, nullptr);
}
#else
void wxCursor::InitFromStock( wxStockCursor cursorId )
{
    m_refData = new wxCursorRefData();

    GdkCursorType gdk_cur = GDK_LEFT_PTR;
    switch (cursorId)
    {
#ifdef __WXGTK3__
        case wxCURSOR_BLANK:            gdk_cur = GDK_BLANK_CURSOR; break;
#else
        case wxCURSOR_BLANK:
            {
                const char bits[] = { 0 };
                const GdkColor color = { 0, 0, 0, 0 };

                GdkPixmap *pixmap = gdk_bitmap_create_from_data(nullptr, bits, 1, 1);
                M_CURSORDATA->m_cursor = gdk_cursor_new_from_pixmap(pixmap,
                                                                    pixmap,
                                                                    &color,
                                                                    &color,
                                                                    0, 0);
                g_object_unref(pixmap);
            }
            return;
#endif
        case wxCURSOR_ARROW:            // fall through to default
        case wxCURSOR_DEFAULT:          gdk_cur = GDK_LEFT_PTR; break;
        case wxCURSOR_RIGHT_ARROW:      gdk_cur = GDK_RIGHT_PTR; break;
        case wxCURSOR_HAND:             gdk_cur = GDK_HAND2; break;
        case wxCURSOR_CROSS:            gdk_cur = GDK_CROSSHAIR; break;
        case wxCURSOR_SIZEWE:           gdk_cur = GDK_SB_H_DOUBLE_ARROW; break;
        case wxCURSOR_SIZENS:           gdk_cur = GDK_SB_V_DOUBLE_ARROW; break;
        case wxCURSOR_ARROWWAIT:
        case wxCURSOR_WAIT:
        case wxCURSOR_WATCH:            gdk_cur = GDK_WATCH; break;
        case wxCURSOR_SIZING:           gdk_cur = GDK_SIZING; break;
        case wxCURSOR_SPRAYCAN:         gdk_cur = GDK_SPRAYCAN; break;
        case wxCURSOR_IBEAM:            gdk_cur = GDK_XTERM; break;
        case wxCURSOR_PENCIL:           gdk_cur = GDK_PENCIL; break;
        case wxCURSOR_NO_ENTRY:         gdk_cur = GDK_PIRATE; break;
        case wxCURSOR_SIZENWSE:
        case wxCURSOR_SIZENESW:         gdk_cur = GDK_FLEUR; break;
        case wxCURSOR_QUESTION_ARROW:   gdk_cur = GDK_QUESTION_ARROW; break;
        case wxCURSOR_PAINT_BRUSH:      gdk_cur = GDK_SPRAYCAN; break;
        case wxCURSOR_MAGNIFIER:        gdk_cur = GDK_PLUS; break;
        case wxCURSOR_CHAR:             gdk_cur = GDK_XTERM; break;
        case wxCURSOR_LEFT_BUTTON:      gdk_cur = GDK_LEFTBUTTON; break;
        case wxCURSOR_MIDDLE_BUTTON:    gdk_cur = GDK_MIDDLEBUTTON; break;
        case wxCURSOR_RIGHT_BUTTON:     gdk_cur = GDK_RIGHTBUTTON; break;
        case wxCURSOR_BULLSEYE:         gdk_cur = GDK_TARGET; break;

        case wxCURSOR_POINT_LEFT:       gdk_cur = GDK_SB_LEFT_ARROW; break;
        case wxCURSOR_POINT_RIGHT:      gdk_cur = GDK_SB_RIGHT_ARROW; break;
/*
        case wxCURSOR_DOUBLE_ARROW:     gdk_cur = GDK_DOUBLE_ARROW; break;
        case wxCURSOR_CROSS_REVERSE:    gdk_cur = GDK_CROSS_REVERSE; break;
        case wxCURSOR_BASED_ARROW_UP:   gdk_cur = GDK_BASED_ARROW_UP; break;
        case wxCURSOR_BASED_ARROW_DOWN: gdk_cur = GDK_BASED_ARROW_DOWN; break;
*/

        default:
            wxFAIL_MSG(wxT("unsupported cursor type"));
            // will use the standard one
            break;
    }

    GdkDisplay* display = wxGetTopLevelGdkDisplay();
#ifdef __WXGTK3__
    // Cursor themes don't have "sizing"
    if (gdk_cur == GDK_SIZING && !wxGTKImpl::IsX11(display))
    {
        M_CURSORDATA->m_cursor = gdk_cursor_new_from_name(display, "move");
        return;
    }
#endif
    M_CURSORDATA->m_cursor = gdk_cursor_new_for_display(display, gdk_cur);
}
#endif // __WXGTK4__/!__WXGTK4__

#if wxUSE_IMAGE

void wxCursor::InitFromImage( const wxImage & image )
{
    const int w = image.GetWidth();
    const int h = image.GetHeight();
    const guchar* alpha = image.GetAlpha();
    const bool hasMask = image.HasMask();
    int hotSpotX = image.GetOptionInt(wxIMAGE_OPTION_CUR_HOTSPOT_X);
    int hotSpotY = image.GetOptionInt(wxIMAGE_OPTION_CUR_HOTSPOT_Y);
    if (hotSpotX < 0 || hotSpotX > w) hotSpotX = 0;
    if (hotSpotY < 0 || hotSpotY > h) hotSpotY = 0;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_data(image.GetData(), GDK_COLORSPACE_RGB, false, 8, w, h, w * 3, nullptr, nullptr);
    if (alpha || hasMask)
    {
        guchar r = 0, g = 0, b = 0;
        if (hasMask)
        {
            r = image.GetMaskRed();
            g = image.GetMaskGreen();
            b = image.GetMaskBlue();
        }
        GdkPixbuf* pixbuf0 = pixbuf;
        pixbuf = gdk_pixbuf_add_alpha(pixbuf, hasMask, r, g, b);
        g_object_unref(pixbuf0);
        if (alpha)
        {
            guchar* d = gdk_pixbuf_get_pixels(pixbuf);
            const int stride = gdk_pixbuf_get_rowstride(pixbuf);
            for (int j = 0; j < h; j++, d += stride)
                for (int i = 0; i < w; i++, alpha++)
                    if (d[4 * i + 3])
                        d[4 * i + 3] = *alpha;
        }
    }
    m_refData = new wxCursorRefData;
#ifdef __WXGTK4__
    GdkTexture* texture = gdk_texture_new_for_pixbuf(pixbuf);
    M_CURSORDATA->m_cursor = gdk_cursor_new_from_texture(
        texture, hotSpotX, hotSpotY, nullptr);
    g_object_unref(texture);
#else
    M_CURSORDATA->m_cursor = gdk_cursor_new_from_pixbuf(
        wxGetTopLevelGdkDisplay(), pixbuf, hotSpotX, hotSpotY);
#endif
    g_object_unref(pixbuf);
}

#endif // wxUSE_IMAGE

GdkCursor *wxCursor::GetCursor() const
{
    GdkCursor* cursor = nullptr;
    if (m_refData)
        cursor = M_CURSORDATA->m_cursor;
    return cursor;
}

wxGDIRefData *wxCursor::CreateGDIRefData() const
{
    return new wxCursorRefData;
}

wxGDIRefData *
wxCursor::CloneGDIRefData(const wxGDIRefData * WXUNUSED(data)) const
{
    // TODO: We can't clone GDK cursors at the moment. To do this we'd need
    //       to remember the original data from which the cursor was created
    //       (i.e. standard cursor type or the bitmap) or use
    //       gdk_cursor_get_cursor_type() (which is in 2.22+ only) and
    //       gdk_cursor_get_image().
    wxFAIL_MSG( wxS("Cloning cursors is not implemented in wxGTK.") );

    return new wxCursorRefData;
}

//-----------------------------------------------------------------------------
// busy cursor routines
//-----------------------------------------------------------------------------

wxCursor g_globalCursor;
wxCursor g_busyCursor;
static wxCursor gs_storedCursor;
static int       gs_busyCount = 0;

static void UpdateCursors(wxWindow* win, GdkCursor* globalCursor)
{
    win->GTKUpdateCursor(globalCursor);
    const wxWindowList& children = win->GetChildren();
    wxWindowList::const_iterator i = children.begin();
    for (size_t n = children.size(); n--; ++i)
        UpdateCursors(*i, globalCursor);
}

static void SetGlobalCursor(const wxCursor& cursor)
{
    GdkCursor* gdk_cursor = cursor.GetCursor();
#ifdef __WXGTK4__
    // gtk_widget_set_cursor() works directly on any widget, no window
    // needed at all -- simpler than the GdkWindow-based GTK3 code below.
    wxWindowList::const_iterator i = wxTopLevelWindows.begin();
    for (size_t n = wxTopLevelWindows.size(); n--; ++i)
    {
        wxWindow* win = *i;
        if (win->m_widget)
        {
            gtk_widget_set_cursor(win->m_widget, gdk_cursor);
            UpdateCursors(win, gdk_cursor);
        }
    }
    gdk_display_flush(wxGetTopLevelGdkDisplay());
#else
    GdkDisplay* display = nullptr;
    wxWindowList::const_iterator i = wxTopLevelWindows.begin();
    for (size_t n = wxTopLevelWindows.size(); n--; ++i)
    {
        wxWindow* win = *i;
        GdkWindow* window;
        if (win->m_widget && (window = gtk_widget_get_window(win->m_widget)))
        {
            gdk_window_set_cursor(window, gdk_cursor);
            UpdateCursors(win, gdk_cursor);
            if (display == nullptr)
                display = gdk_window_get_display(window);
        }
    }
    if (display)
        gdk_display_flush(display);
#endif // __WXGTK4__/!__WXGTK4__
}

void wxBeginBusyCursor(const wxCursor* cursor)
{
    if (gs_busyCount++ == 0)
    {
        g_busyCursor = *cursor;
        gs_storedCursor = g_globalCursor;
        SetGlobalCursor(*cursor);
    }
}

void wxEndBusyCursor()
{
    if (gs_busyCount && --gs_busyCount == 0)
    {
        g_globalCursor = gs_storedCursor;
        gs_storedCursor =
        g_busyCursor = wxCursor();
        SetGlobalCursor(g_globalCursor);
    }
}

bool wxIsBusy()
{
    return gs_busyCount > 0;
}

void wxSetCursor( const wxCursorBundle& cursors )
{
    const wxCursor& cursor = cursors.GetCursorForMainWindow();
    if (cursor.IsOk() || g_globalCursor.IsOk())
    {
        g_globalCursor = cursor;
        SetGlobalCursor(cursor);
    }
}
