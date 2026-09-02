///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/threads.h
// Purpose:     Wrappers for GDK threads support.
// Author:      Vadim Zeitlin
// Created:     2022-09-23
// Copyright:   (c) 2022 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_PRIVATE_THREADS_H_
#define _WX_GTK_PRIVATE_THREADS_H_

// ----------------------------------------------------------------------------
// Redefine GDK functions to avoiding deprecation warnings
// ----------------------------------------------------------------------------

#ifdef __WXGTK4__

// GTK4 removed the GDK threads API entirely. It had been deprecated since
// 3.6 and its replacement is the rule it was deprecated in favour of: all GTK
// calls happen on the main thread, and other threads hand work over with
// g_idle_add() instead of taking a lock. There is nothing left to acquire, so
// these become no-ops and wxGDKThreadsLock below is an empty RAII object.
static inline void wx_gdk_threads_enter() { }
#define gdk_threads_enter wx_gdk_threads_enter

static inline void wx_gdk_threads_leave() { }
#define gdk_threads_leave wx_gdk_threads_leave

#else // !__WXGTK4__

wxGCC_WARNING_SUPPRESS(deprecated-declarations)

static inline void wx_gdk_threads_enter() { gdk_threads_enter(); }
#define gdk_threads_enter wx_gdk_threads_enter

static inline void wx_gdk_threads_leave() { gdk_threads_leave(); }
#define gdk_threads_leave wx_gdk_threads_leave

wxGCC_WARNING_RESTORE(deprecated-declarations)

#endif // __WXGTK4__/!__WXGTK4__

// ----------------------------------------------------------------------------
// RAII wrapper for acquiring/leaving GDK lock in ctor/dtor
// ----------------------------------------------------------------------------

class wxGDKThreadsLock
{
public:
    wxGDKThreadsLock() { gdk_threads_enter(); }
   ~wxGDKThreadsLock() { gdk_threads_leave(); }

   wxDECLARE_NO_COPY_CLASS(wxGDKThreadsLock);
};

#endif // _WX_GTK_PRIVATE_THREADS_H_
