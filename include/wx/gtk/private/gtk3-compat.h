///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/gtk3-compat.h
// Purpose:     Compatibility code for older GTK+ 3 versions
// Author:      Paul Cornett
// Created:     2015-10-10
// Copyright:   (c) 2015 Paul Cornett
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_PRIVATE_COMPAT3_H_
#define _WX_GTK_PRIVATE_COMPAT3_H_

#ifdef __WXGTK4__

inline GdkDevice* wx_get_gdk_device_from_display(GdkDisplay* display)
{
    GdkSeat* seat = gdk_display_get_default_seat(display);
    return gdk_seat_get_pointer(seat);
}

// gtk_widget_get_toplevel() doesn't exist under GTK4; the replacement,
// gtk_widget_get_root(), returns a GtkRoot* (an interface, implemented by
// GtkWindow among others) rather than a GtkWidget*, and returns nullptr
// for a widget with no root yet instead of GTK3's confusing convention of
// returning the widget itself in that case. Every call site in this
// codebase already treats the return value defensively (GTK_IS_WINDOW()
// checks or an assumption it's parented under a window by this point), so
// this is a safe, likely more-correct substitution -- not just a rename.
static inline GtkWidget* wx_gtk_widget_get_toplevel(GtkWidget* widget)
{
    GtkRoot* root = gtk_widget_get_root(widget);
    return root ? GTK_WIDGET(root) : nullptr;
}
#define gtk_widget_get_toplevel(widget) wx_gtk_widget_get_toplevel(widget)

// gtk_box_pack_start()/pack_end() don't exist under GTK4: expand/fill/
// padding moved from box-call parameters to per-child widget properties
// (hexpand/vexpand, halign/valign, margins), and packing itself is just
// gtk_box_append(). #define'd over the old names so call sites (which
// only ever use one or a few pack_end calls per box in this codebase,
// never GTK3's "stack backward from the end" multi-pack_end pattern)
// don't need individual porting -- append(), called in the same order as
// the original pack_start/pack_end calls, already gives the same visual
// result for every case actually used here.
static inline void wx_gtk_box_pack_start(GtkBox* box, GtkWidget* child,
                                          gboolean expand, gboolean fill, guint padding)
{
    const GtkOrientation orient = gtk_orientable_get_orientation(GTK_ORIENTABLE(box));
    if (expand)
    {
        if (orient == GTK_ORIENTATION_HORIZONTAL)
            gtk_widget_set_hexpand(child, true);
        else
            gtk_widget_set_vexpand(child, true);
    }
    if (!fill)
    {
        if (orient == GTK_ORIENTATION_HORIZONTAL)
            gtk_widget_set_halign(child, GTK_ALIGN_CENTER);
        else
            gtk_widget_set_valign(child, GTK_ALIGN_CENTER);
    }
    if (padding)
    {
        if (orient == GTK_ORIENTATION_HORIZONTAL)
        {
            gtk_widget_set_margin_start(child, gint(padding));
            gtk_widget_set_margin_end(child, gint(padding));
        }
        else
        {
            gtk_widget_set_margin_top(child, gint(padding));
            gtk_widget_set_margin_bottom(child, gint(padding));
        }
    }
    gtk_box_append(box, child);
}
#define gtk_box_pack_start(box, child, expand, fill, padding) \
    wx_gtk_box_pack_start(box, child, expand, fill, padding)
#define gtk_box_pack_end(box, child, expand, fill, padding) \
    wx_gtk_box_pack_start(box, child, expand, fill, padding)

#else // !__WXGTK4__

wxGCC_WARNING_SUPPRESS(deprecated-declarations)

// ----------------------------------------------------------------------------
// the following were introduced in GTK+ 3.20

static inline gboolean wx_gtk_text_iter_starts_tag(const GtkTextIter* iter, GtkTextTag* tag)
{
    return gtk_text_iter_begins_tag(iter, tag);
}
#define gtk_text_iter_starts_tag wx_gtk_text_iter_starts_tag

#ifdef __WXGTK3__

// ----------------------------------------------------------------------------
// the following were introduced in GTK+ 3.12

static inline void wx_gtk_widget_set_margin_start(GtkWidget* widget, gint margin)
{
    gtk_widget_set_margin_left(widget, margin);
}
#define gtk_widget_set_margin_start wx_gtk_widget_set_margin_start

static inline void wx_gtk_widget_set_margin_end(GtkWidget* widget, gint margin)
{
    gtk_widget_set_margin_right(widget, margin);
}
#define gtk_widget_set_margin_end wx_gtk_widget_set_margin_end

inline GdkDevice* wx_get_gdk_device_from_display(GdkDisplay* display)
{
    GdkDeviceManager* manager = gdk_display_get_device_manager(display);
    return gdk_device_manager_get_client_pointer(manager);
}

#endif // __WXGTK3__

wxGCC_WARNING_RESTORE()

#endif // __WXGTK4__/!__WXGTK4__

#if defined(__WXGTK4__) || !defined(__WXGTK3__)
static inline bool wx_is_at_least_gtk3(int /* minor */)
{
#ifdef __WXGTK4__
    return true;
#else
    return false;
#endif
}
#else
static inline bool wx_is_at_least_gtk3(int minor)
{
    return gtk_check_version(3, minor, 0) == nullptr;
}
#endif

#endif // _WX_GTK_PRIVATE_COMPAT3_H_
