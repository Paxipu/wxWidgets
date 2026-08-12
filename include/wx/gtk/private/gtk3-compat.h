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

// GTK4 merged the get_preferred_width()/get_preferred_height() family into a
// single gtk_widget_measure() taking an orientation. The "for size" parameter
// is -1, i.e. unspecified, matching what the old functions did, and the
// baseline is not of interest to any of the call sites.
static inline void
wx_gtk_widget_get_preferred_width(GtkWidget* widget, int* minimum, int* natural)
{
    gtk_widget_measure(widget, GTK_ORIENTATION_HORIZONTAL, -1,
                       minimum, natural, nullptr, nullptr);
}
#define gtk_widget_get_preferred_width wx_gtk_widget_get_preferred_width

static inline void
wx_gtk_widget_get_preferred_height(GtkWidget* widget, int* minimum, int* natural)
{
    gtk_widget_measure(widget, GTK_ORIENTATION_VERTICAL, -1,
                       minimum, natural, nullptr, nullptr);
}
#define gtk_widget_get_preferred_height wx_gtk_widget_get_preferred_height

static inline void
wx_gtk_widget_get_preferred_height_for_width(GtkWidget* widget, int width,
                                             int* minimum, int* natural)
{
    gtk_widget_measure(widget, GTK_ORIENTATION_VERTICAL, width,
                       minimum, natural, nullptr, nullptr);
}
#define gtk_widget_get_preferred_height_for_width \
            wx_gtk_widget_get_preferred_height_for_width

// GTK4 dropped reordering a box child by position index in favour of giving
// the sibling to place it after, so translate the index by walking the box's
// children. A null sibling means "move to the first position", which is what
// index 0 meant too.
static inline void
wx_gtk_box_reorder_child(GtkBox* box, GtkWidget* child, int pos)
{
    GtkWidget* sibling = nullptr;
    if ( pos > 0 )
    {
        int i = 0;
        for ( GtkWidget* w = gtk_widget_get_first_child(GTK_WIDGET(box));
              w != nullptr;
              w = gtk_widget_get_next_sibling(w) )
        {
            // The child being moved doesn't count towards the destination
            // index, just as it didn't under GTK+ 3.
            if ( w == child )
                continue;

            sibling = w;
            if ( ++i == pos )
                break;
        }

        // If pos is past the end, sibling is the last child and the widget
        // ends up last, which is what GTK+ 3 did for an out of range index.
    }

    gtk_box_reorder_child_after(box, child, sibling);
}
#define gtk_box_reorder_child wx_gtk_box_reorder_child

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

// The GTK_STYLE_CLASS_* string-constant macros were dropped under GTK4
// (along with most of <gtk/deprecated/gtkstylecontext.h>'s surrounding
// API), but the CSS class names themselves are unchanged and
// gtk_style_context_add_class()/gtk_widget_add_css_class() still exist, so
// just restoring the string constants is enough for existing call sites.
#define GTK_STYLE_CLASS_BUTTON "button"
#define GTK_STYLE_CLASS_CELL "cell"
#define GTK_STYLE_CLASS_EXPANDER "expander"
#define GTK_STYLE_CLASS_GRIP "grip"
#define GTK_STYLE_CLASS_INLINE_TOOLBAR "inline-toolbar"
#define GTK_STYLE_CLASS_PANE_SEPARATOR "pane-separator"

// Plain rename, same signature.
#define gtk_label_set_line_wrap(label, wrap) gtk_label_set_wrap(label, wrap)

#else // !__WXGTK4__

wxGCC_WARNING_SUPPRESS(deprecated-declarations)

// ----------------------------------------------------------------------------
// the following were introduced in GTK+ 4, when the text-manipulating parts of
// GtkEntry moved to the GtkEditable interface it implements. This matters for
// GtkSpinButton, which is no longer a GtkEntry subclass under GTK+ 4 but does
// still implement GtkEditable, so the call sites use the GtkEditable spelling
// unconditionally and these map it back to GtkEntry for older GTK+.

static inline void wx_gtk_editable_set_text(GtkEditable* editable, const char* text)
{
    gtk_entry_set_text(GTK_ENTRY(editable), text);
}
#define gtk_editable_set_text wx_gtk_editable_set_text

static inline const char* wx_gtk_editable_get_text(GtkEditable* editable)
{
    return gtk_entry_get_text(GTK_ENTRY(editable));
}
#define gtk_editable_get_text wx_gtk_editable_get_text

static inline void wx_gtk_editable_set_width_chars(GtkEditable* editable, int n)
{
    gtk_entry_set_width_chars(GTK_ENTRY(editable), n);
}
#define gtk_editable_set_width_chars wx_gtk_editable_set_width_chars

static inline int wx_gtk_editable_get_width_chars(GtkEditable* editable)
{
    return gtk_entry_get_width_chars(GTK_ENTRY(editable));
}
#define gtk_editable_get_width_chars wx_gtk_editable_get_width_chars

static inline void wx_gtk_editable_set_alignment(GtkEditable* editable, float xalign)
{
    gtk_entry_set_alignment(GTK_ENTRY(editable), xalign);
}
#define gtk_editable_set_alignment wx_gtk_editable_set_alignment

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

// Remove a widget from its parent without destroying it.
//
// GTK4 has no generic container API any more, but for the simple multi-child
// containers this is used with (GtkBox and wxPizza) gtk_widget_unparent() is
// exactly what the type-specific remove function does. It must *not* be used
// for single-child containers such as GtkScrolledWindow or GtkFrame, which
// keep their own pointer to the child and need set_child(nullptr) instead.
static inline void wx_gtk_widget_remove_from_parent(GtkWidget* child)
{
#ifdef __WXGTK4__
    gtk_widget_unparent(child);
#else
    gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(child)), child);
#endif
}

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
