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

// gtk_true()/gtk_false() were dropped from GTK4's public API. They exist only
// to be used as signal handlers that unconditionally return a value.
static inline gboolean wx_gtk_true(...)
{
    return TRUE;
}

// gtk_dialog_run() and gtk_native_dialog_run() are gone under GTK4: dialogs
// there are asynchronous, the caller connecting to ::response instead of
// blocking. wx's API is synchronous (wxDialog::ShowModal() returns the result),
// so the blocking behaviour has to exist somewhere, and these reproduce what
// gtk_dialog_run() did internally -- make the dialog modal, show it, and spin
// a nested main loop until it responds.
//
// Faithful to the original in the ways that matter to the call sites: the
// dialog is not destroyed on return, and a response of GTK_RESPONSE_NONE is
// produced if it is destroyed while running rather than leaving the loop
// spinning forever.
//
// The nested loop is a real one rather than wx's wxGUIEventLoop because these
// are called from code (the assert dialog in particular) that must work when
// wx's own event loop machinery may not be in a usable state.

struct wxGtkDialogRunData
{
    GMainLoop* loop;
    int response;
};

static inline void wx_gtk_dialog_run_response(void*, int response_id, void* data)
{
    wxGtkDialogRunData* const d = static_cast<wxGtkDialogRunData*>(data);
    d->response = response_id;
    if ( g_main_loop_is_running(d->loop) )
        g_main_loop_quit(d->loop);
}

static inline void wx_gtk_dialog_run_destroy(void*, void* data)
{
    wxGtkDialogRunData* const d = static_cast<wxGtkDialogRunData*>(data);
    d->response = GTK_RESPONSE_NONE;
    if ( g_main_loop_is_running(d->loop) )
        g_main_loop_quit(d->loop);
}

static inline int wx_gtk_dialog_run(GtkDialog* dialog)
{
    wxGtkDialogRunData data;
    data.loop = g_main_loop_new(nullptr, FALSE);
    data.response = GTK_RESPONSE_NONE;

    const gulong idResponse = g_signal_connect(
        dialog, "response", G_CALLBACK(wx_gtk_dialog_run_response), &data);
    const gulong idDestroy = g_signal_connect(
        dialog, "destroy", G_CALLBACK(wx_gtk_dialog_run_destroy), &data);

    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_present(GTK_WINDOW(dialog));

    g_main_loop_run(data.loop);

    // The dialog may already be gone if it was destroyed rather than answered.
    if ( g_signal_handler_is_connected(dialog, idResponse) )
        g_signal_handler_disconnect(dialog, idResponse);
    if ( g_signal_handler_is_connected(dialog, idDestroy) )
        g_signal_handler_disconnect(dialog, idDestroy);

    g_main_loop_unref(data.loop);

    return data.response;
}
#define gtk_dialog_run(dialog) wx_gtk_dialog_run(dialog)

static inline int wx_gtk_native_dialog_run(GtkNativeDialog* dialog)
{
    wxGtkDialogRunData data;
    data.loop = g_main_loop_new(nullptr, FALSE);
    data.response = GTK_RESPONSE_NONE;

    const gulong idResponse = g_signal_connect(
        dialog, "response", G_CALLBACK(wx_gtk_dialog_run_response), &data);

    gtk_native_dialog_set_modal(dialog, TRUE);
    gtk_native_dialog_show(dialog);

    g_main_loop_run(data.loop);

    if ( g_signal_handler_is_connected(dialog, idResponse) )
        g_signal_handler_disconnect(dialog, idResponse);

    g_main_loop_unref(data.loop);

    return data.response;
}
#define gtk_native_dialog_run(dialog) wx_gtk_native_dialog_run(dialog)

// GTK4 dropped the GdkWMDecoration/GdkWMFunction hints along with the rest of
// the GdkWindow API: a GTK4 application cannot tell the window manager which
// decorations or which window-menu entries it wants. What it *can* still do is
// turn its own decorations off entirely (gtk_window_set_decorated()) and, when
// drawing them itself, choose which buttons the header bar shows
// (gtk_header_bar_set_decoration_layout()).
//
// The flags are kept as plain wx-internal bits so that the style-to-hints
// translation in wxTopLevelWindowGTK::Create() and wxMiniFrame stays one body
// of code across GTK versions; what changes is only how much of the result can
// be acted on. See docs/gtk/gtk4-status.md for the resulting fidelity gaps.
enum
{
    GDK_DECOR_ALL       = 1 << 0,
    GDK_DECOR_BORDER    = 1 << 1,
    GDK_DECOR_RESIZEH   = 1 << 2,
    GDK_DECOR_TITLE     = 1 << 3,
    GDK_DECOR_MENU      = 1 << 4,
    GDK_DECOR_MINIMIZE  = 1 << 5,
    GDK_DECOR_MAXIMIZE  = 1 << 6
};

enum
{
    GDK_FUNC_ALL        = 1 << 0,
    GDK_FUNC_RESIZE     = 1 << 1,
    GDK_FUNC_MOVE       = 1 << 2,
    GDK_FUNC_MINIMIZE   = 1 << 3,
    GDK_FUNC_MAXIMIZE   = 1 << 4,
    GDK_FUNC_CLOSE      = 1 << 5
};

// The GdkSurface of the toplevel a widget belongs to, or null if it isn't
// realized yet. This is as close as GTK4 gets to GTK3's
// gtk_widget_get_window(): only natives (toplevels and popups) have a surface,
// and every widget inside one shares it.
static inline GdkSurface* wx_gtk_widget_get_surface(GtkWidget* widget)
{
    GtkNative* const native = gtk_widget_get_native(widget);
    return native ? gtk_native_get_surface(native) : nullptr;
}

// Version-neutral spelling of "the native drawing target of this widget", for
// the few places which need it under both GTK3 and GTK4 and so can't simply be
// written in terms of one or the other. See the GTK3 counterpart below.
#define wx_gtk_widget_get_surface_or_window(w) wx_gtk_widget_get_surface(w)

// GTK4 renamed the toplevel state flags along with GdkWindow itself, and
// renamed iconify/deiconify to say what they do.
#define GDK_WINDOW_STATE_MAXIMIZED  GDK_TOPLEVEL_STATE_MAXIMIZED
#define GDK_WINDOW_STATE_FULLSCREEN GDK_TOPLEVEL_STATE_FULLSCREEN
#define GDK_WINDOW_STATE_ICONIFIED  GDK_TOPLEVEL_STATE_MINIMIZED

#define gtk_window_iconify(window)   gtk_window_minimize(window)
#define gtk_window_deiconify(window) gtk_window_unminimize(window)

// GTK4 removed GtkStateType, the pre-3.0 enumeration of widget states, leaving
// only the GtkStateFlags bitfield.
//
// The two are emphatically NOT interchangeable: GtkStateType is an index
// (GTK_STATE_INSENSITIVE == 4) while GtkStateFlags is a bitfield
// (GTK_STATE_FLAG_INSENSITIVE == 8, and 4 is GTK_STATE_FLAG_SELECTED). Casting
// one to the other would compile and draw the wrong state.
//
// renderer.cpp is aware of that and already funnels every conversion through a
// single stateTypeToFlags[] table, using the enumeration as a compact way to
// name one state at a time. Keeping the enumeration therefore preserves that
// design exactly, where rewriting each drawing function to accumulate flags
// would be a much larger change with more room to get a state wrong.
// GTK4 dropped the state argument from these getters: a style context now
// always reports the state it is set to.
//
// These are C++ overloads rather than macros on purpose. Some call sites have
// already been ported to GTK4's two-argument form while others still use the
// GTK3 three-argument one, and a fixed-arity function-like macro breaks the
// former -- as it did, turning statbox.cpp red until this was noticed by
// diffing the failed targets rather than the error count.
static inline void
gtk_style_context_get_color(GtkStyleContext* sc, GtkStateFlags state,
                            GdkRGBA* out)
{
    gtk_style_context_save(sc);
    gtk_style_context_set_state(sc, state);
    gtk_style_context_get_color(sc, out);
    gtk_style_context_restore(sc);
}

static inline void
gtk_style_context_get_border(GtkStyleContext* sc, GtkStateFlags state,
                             GtkBorder* out)
{
    gtk_style_context_save(sc);
    gtk_style_context_set_state(sc, state);
    gtk_style_context_get_border(sc, out);
    gtk_style_context_restore(sc);
}

static inline void
gtk_style_context_get_padding(GtkStyleContext* sc, GtkStateFlags state,
                              GtkBorder* out)
{
    gtk_style_context_save(sc);
    gtk_style_context_set_state(sc, state);
    gtk_style_context_get_padding(sc, out);
    gtk_style_context_restore(sc);
}

static inline void
gtk_style_context_get_margin(GtkStyleContext* sc, GtkStateFlags state,
                             GtkBorder* out)
{
    gtk_style_context_save(sc);
    gtk_style_context_set_state(sc, state);
    gtk_style_context_get_margin(sc, out);
    gtk_style_context_restore(sc);
}

typedef enum
{
    GTK_STATE_NORMAL,
    GTK_STATE_ACTIVE,
    GTK_STATE_PRELIGHT,
    GTK_STATE_SELECTED,
    GTK_STATE_INSENSITIVE,
    GTK_STATE_INCONSISTENT,
    GTK_STATE_FOCUSED
} GtkStateType;

// GtkContainer is gone: under GTK4 any widget can have children, so the GTK3
// question "is this a container?" becomes "does it have any children?", which
// is what the call sites are really asking before descending into them.
static inline bool wx_gtk_widget_is_container(GtkWidget* widget)
{
    return widget && gtk_widget_get_first_child(widget) != nullptr;
}

// No widget owns a GdkWindow under GTK4 -- the concept is gone -- so the
// GTK3 question "does this widget have its own window?" is always answered
// no. Call sites use it to decide whether coordinates need translating
// through a child window, which under GTK4 they never do.
static inline gboolean wx_gtk_widget_get_has_window(GtkWidget*)
{
    return FALSE;
}

#else // !__WXGTK4__

wxGCC_WARNING_SUPPRESS(deprecated-declarations)

// Counterpart of the GTK4 macro above.
#define wx_gtk_widget_get_surface_or_window(w) gtk_widget_get_window(w)

static inline bool wx_gtk_widget_is_container(GtkWidget* widget)
{
    return widget && GTK_IS_CONTAINER(widget);
}

// Counterpart of the GTK4 stub above: here the question is real, so just
// forward to GTK.
static inline gboolean wx_gtk_widget_get_has_window(GtkWidget* widget)
{
    return gtk_widget_get_has_window(widget);
}

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
