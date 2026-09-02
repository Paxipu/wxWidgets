///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/access.h
// Purpose:     private accessibility helpers shared inside wxGTK
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_PRIVATE_ACCESS_H_
#define _WX_GTK_PRIVATE_ACCESS_H_

#if wxUSE_ACCESSIBILITY

extern "C"
{

// Returns the first of the accessible children a wxAccessible attached to this
// widget describes, or null if there is no wxAccessible or it describes none.
// The caller owns the returned reference.
//
// Defined in src/gtk/accessgtk.cpp, called from wxPizza's implementation of
// GtkAccessible in src/gtk/win_gtk.cpp: those children have no widget of their
// own, so nothing else can find them.
GtkAccessible* wxGTKPizzaGetFirstAccessibleChild(GtkWidget* widget);

}

#endif // wxUSE_ACCESSIBILITY

#endif // _WX_GTK_PRIVATE_ACCESS_H_
