///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private.h
// Purpose:     wxGTK private macros, functions &c
// Author:      Vadim Zeitlin
// Created:     12.03.02
// Copyright:   (c) 2002 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_PRIVATE_H_
#define _WX_GTK_PRIVATE_H_

#include "wx/gtk/private/wrapgtk.h"

#include "wx/gtk/private/string.h"

#ifndef G_VALUE_INIT
    // introduced in GLib 2.30
    #define G_VALUE_INIT { 0, { { 0 } } }
#endif

// pango_version_check symbol is quite recent ATM (4/2007)... so we
// use our own wrapper which implements a smart trick.
// Use this function as you'd use pango_version_check:
//
//  if (!wx_pango_version_check(1,18,0))
//     ... call to a function available only in pango >= 1.18 ...
//
// and use it only to test for pango versions >= 1.16.0
extern const gchar *wx_pango_version_check(int major, int minor, int micro);

// Define a macro for converting wxString to char* in appropriate encoding for
// the file names.
#ifdef G_OS_WIN32
    // Under MSW, UTF-8 file name encodings are always used.
    #define wxGTK_CONV_FN(s) (s).utf8_str()
#else
    // Under Unix use GLib file name encoding (which is also UTF-8 by default
    // but may be different from it).
    #define wxGTK_CONV_FN(s) (s).fn_str()
#endif

// ----------------------------------------------------------------------------
// various private helper functions
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// Scrollbars
//
// A GtkScrollbar was a GtkRange under GTK3 and is not one under GTK4: the two
// are unrelated widgets there, and everything the range API did is done
// through the scrollbar's GtkAdjustment instead. These wrap the handful of
// operations wxWindowGTK performs on its scrollbars so that its code does not
// have to say which it is talking to.
//
// Note that GTK_RANGE() on a GTK4 scrollbar is not merely deprecated but an
// invalid cast, so this is not something a rename could have covered.
// ----------------------------------------------------------------------------

// Repeating the typedef from wx/gtk/window.h, which is a public header this
// one must not pull in. Identical typedefs may be repeated in C++.
#ifdef __WXGTK4__
typedef GtkScrollbar wxGtkScrollbar;
#else
typedef GtkRange wxGtkScrollbar;
#endif

inline GtkAdjustment* wxGtkScrollbarGetAdjustment(wxGtkScrollbar* sb)
{
#ifdef __WXGTK4__
    return gtk_scrollbar_get_adjustment(sb);
#else
    return gtk_range_get_adjustment(sb);
#endif
}

inline double wxGtkScrollbarGetValue(wxGtkScrollbar* sb)
{
#ifdef __WXGTK4__
    return gtk_adjustment_get_value(gtk_scrollbar_get_adjustment(sb));
#else
    return gtk_range_get_value(sb);
#endif
}

inline void wxGtkScrollbarSetValue(wxGtkScrollbar* sb, double value)
{
#ifdef __WXGTK4__
    gtk_adjustment_set_value(gtk_scrollbar_get_adjustment(sb), value);
#else
    gtk_range_set_value(sb, value);
#endif
}

inline void wxGtkScrollbarSetRange(wxGtkScrollbar* sb, double lower, double upper)
{
#ifdef __WXGTK4__
    GtkAdjustment* const adj = gtk_scrollbar_get_adjustment(sb);

    // gtk_range_set_range() clamped the value into the new range; the
    // adjustment does the same, but only once both bounds are set, so freeze
    // the notifications while they are.
    g_object_freeze_notify(G_OBJECT(adj));
    gtk_adjustment_set_lower(adj, lower);
    gtk_adjustment_set_upper(adj, upper);
    g_object_thaw_notify(G_OBJECT(adj));
#else
    gtk_range_set_range(sb, lower, upper);
#endif
}

inline void
wxGtkScrollbarSetIncrements(wxGtkScrollbar* sb, double step, double page)
{
#ifdef __WXGTK4__
    GtkAdjustment* const adj = gtk_scrollbar_get_adjustment(sb);

    g_object_freeze_notify(G_OBJECT(adj));
    gtk_adjustment_set_step_increment(adj, step);
    gtk_adjustment_set_page_increment(adj, page);
    g_object_thaw_notify(G_OBJECT(adj));
#else
    gtk_range_set_increments(sb, step, page);
#endif
}

// The object which emits "value_changed" for this scrollbar: the scrollbar
// itself under GTK3, its adjustment under GTK4, where GtkScrollbar has no such
// signal because the value is not its own.
inline void* wxGtkScrollbarValueNotifier(wxGtkScrollbar* sb)
{
#ifdef __WXGTK4__
    return gtk_scrollbar_get_adjustment(sb);
#else
    return sb;
#endif
}

inline void wxGtkScrollbarSetInverted(wxGtkScrollbar* sb, bool inverted)
{
#ifdef __WXGTK4__
    // GtkScrollbar has no "inverted" property of its own any more. For the
    // only thing wx uses this for -- a right to left horizontal scrollbar --
    // setting the widget's text direction has the same effect, and is how GTK4
    // itself mirrors a scrollbar.
    gtk_widget_set_direction(GTK_WIDGET(sb),
                             inverted ? GTK_TEXT_DIR_RTL : GTK_TEXT_DIR_LTR);
#else
    gtk_range_set_inverted(sb, inverted);
#endif
}

#ifdef __WXGTK4__
// Called from toplevel.cpp when a top level window is activated or
// deactivated, so that wxApp can generate wxEVT_ACTIVATE_APP: GTK4 has no
// focus-in/out signals for src/gtk/app.cpp to hook for this.
WXDLLIMPEXP_CORE void wxGTKAppNotifyWindowActivated(bool active);

// Create the GtkWindow subclass used by wx top-level windows and apply or
// clear its visual and input shape. GTK4 removed native visual window shapes,
// so the subclass implements them by masking its snapshot instead.
WXDLLIMPEXP_CORE GtkWidget* wxGTKCreateTopLevelWindow();
WXDLLIMPEXP_CORE bool wxGTKSetWindowShape(GtkWidget* widget,
                                          const cairo_region_t* region);

// Suppresses wx idle processing -- idle events, and with them the deletion of
// the windows queued by Destroy() -- for as long as an object of this class
// exists.
//
// Hold one around any loop that runs the main loop to make GTK4 do something
// synchronously. Such a loop dispatches whatever source happens to be ready,
// wxApp's idle source included, and that one deletes windows: without this a
// repaint can free the very window whose method is on the stack above it.
class WXDLLIMPEXP_CORE wxGTKIdleSuppressor
{
public:
    wxGTKIdleSuppressor();
    ~wxGTKIdleSuppressor();

    wxDECLARE_NO_COPY_CLASS(wxGTKIdleSuppressor);
};
#endif // __WXGTK4__

namespace wxGTKPrivate
{

// these functions create the GTK widgets of the specified types which can then
// used to retrieve their styles, pass them to drawing functions &c
//
// the returned widgets shouldn't be destroyed, this is done automatically on
// shutdown
WXDLLIMPEXP_CORE GtkWidget *GetButtonWidget();
WXDLLIMPEXP_CORE GtkWidget *GetCheckButtonWidget();
WXDLLIMPEXP_CORE GtkWidget *GetComboBoxWidget();
WXDLLIMPEXP_CORE GtkWidget *GetEntryWidget();
WXDLLIMPEXP_CORE GtkWidget *GetHeaderButtonWidgetFirst();
WXDLLIMPEXP_CORE GtkWidget *GetHeaderButtonWidgetLast();
WXDLLIMPEXP_CORE GtkWidget *GetHeaderButtonWidget();
WXDLLIMPEXP_CORE GtkWidget *GetNotebookWidget();
WXDLLIMPEXP_CORE GtkWidget *GetRadioButtonWidget();
WXDLLIMPEXP_CORE GtkWidget *GetSplitterWidget(wxOrientation orient = wxHORIZONTAL);
WXDLLIMPEXP_CORE GtkWidget *GetTreeWidget();
#ifdef __WXGTK4__
// Used to measure the expander arrow, whose size used to be readable as the
// "expander-size" style property before GTK4 removed style properties.
WXDLLIMPEXP_CORE GtkWidget *GetExpanderWidget();
#endif

} // wxGTKPrivate

#endif // _WX_GTK_PRIVATE_H_
