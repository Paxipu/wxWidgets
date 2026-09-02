///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/wrapgtk.h
// Purpose:     Include gtk/gtk.h without warnings and with compatibility
// Author:      Vadim Zeitlin
// Created:     2018-05-20
// Copyright:   (c) 2018 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_PRIVATE_WRAPGTK_H_
#define _WX_GTK_PRIVATE_WRAPGTK_H_

wxGCC_WARNING_SUPPRESS(deprecated-declarations)
wxGCC_WARNING_SUPPRESS(parentheses)
#include <gtk/gtk.h>
wxGCC_WARNING_RESTORE(parentheses)
wxGCC_WARNING_RESTORE(deprecated-declarations)

#include "wx/gtk/private/gtk2-compat.h"

// gtk_check_version() reports a version *mismatch*, not a version *ordering*:
// it requires the major version to match exactly, so under GTK4 it answers
// "incompatible" to every GTK 3.x requirement rather than "newer than that".
//
// This codebase asks it about sixty times, always meaning "do we have at least
// this GTK3 feature level", so left alone every one of those guards silently
// inverts under GTK4 and runs a pre-3.N fallback instead of the modern path.
// Any GTK 3.x requirement is satisfied by GTK4, which is what the call sites
// mean, so answer that; requirements on GTK4 itself pass through unchanged.
//
// This lives here, rather than in gtk3-compat.h where it started, because a
// call site gets it wrong by *not* being adapted, so it has to reach every
// file that can call gtk_check_version() rather than only those that opted
// into the GTK4 compatibility shims.
//
// The real function has to be called before the macro below hides it.
#ifdef __WXGTK4__

static inline const char*
wx_gtk_check_version(guint required_major, guint required_minor, guint required_micro)
{
    if ( required_major < GTK_MAJOR_VERSION )
        return nullptr;

    return gtk_check_version(required_major, required_minor, required_micro);
}
#define gtk_check_version(ma, mi, mc) wx_gtk_check_version(ma, mi, mc)

#endif // __WXGTK4__

#endif // _WX_GTK_PRIVATE_WRAPGTK_H_
