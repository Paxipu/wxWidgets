/* ///////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/win_gtk.h
// Purpose:     native GTK+ widget for wxWindow
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////// */

#ifndef _WX_GTK_PIZZA_H_
#define _WX_GTK_PIZZA_H_

#define WX_PIZZA(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, wxPizza::type(), wxPizza)
#define WX_IS_PIZZA(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, wxPizza::type())

#ifdef __WXGTK4__
// Name of the signal wxPizza emits once it has laid its children out, standing
// in for GtkWidget's "size-allocate" which GTK4 removed. See win_gtk.cpp.
extern WXDLLIMPEXP_DATA_CORE(const char* const) wxPIZZA_SIGNAL_SIZE_ALLOCATED;
#endif // __WXGTK4__

struct WXDLLIMPEXP_CORE wxPizza
{
    // borders styles which can be used with wxPizza
    enum { BORDER_STYLES =
        wxBORDER_SIMPLE | wxBORDER_RAISED | wxBORDER_SUNKEN | wxBORDER_THEME };

    static GtkWidget* New(long windowStyle = 0);
    static GType type();
    void move(GtkWidget* widget, int x, int y, int width, int height);
    void put(GtkWidget* widget, int x, int y, int width, int height);
#ifdef __WXGTK4__
    // Undo put(). GTK3 got this from GtkContainer's "remove" vfunc, which
    // GTK4 does not have, so it has to be called explicitly -- see the
    // comment on the definition.
    void remove(GtkWidget* widget);
#endif
    void scroll(int dx, int dy);
    void get_border(GtkBorder& border);
    void size_allocate_child(
        GtkWidget* child, int x, int y, int width, int height, int parent_width = -1);

    GtkFixed m_fixed;
    GList* m_children;
    int m_scroll_x;
    int m_scroll_y;
    int m_windowStyle;
};

#endif // _WX_GTK_PIZZA_H_
