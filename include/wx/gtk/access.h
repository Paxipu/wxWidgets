///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/access.h
// Purpose:     declaration of the wxAccessible class for wxGTK
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_ACCESS_H_
#define _WX_GTK_ACCESS_H_

#if wxUSE_ACCESSIBILITY

class wxGTKAccessibleImpl;

// ----------------------------------------------------------------------------
// wxAccessible: the wxGTK end of the accessibility API
// ----------------------------------------------------------------------------
//
// The API this implements was modelled on MSAA, which asks the application
// questions. GTK4 does not ask: it keeps a cache of what the application last
// told it, and the application is expected to keep that cache up to date. So
// the two ends have to be pushed together at the moments when something might
// have changed, and NotifyEvent() -- which is exactly a report that something
// has changed -- is where that happens. See docs/gtk/gtk4-accessibility.md.

class WXDLLIMPEXP_CORE wxAccessible : public wxAccessibleBase
{
public:
    explicit wxAccessible(wxWindow* win = nullptr);
    virtual ~wxAccessible();

    // Copy everything this object currently reports into GTK's cache.
    //
    // NotifyEvent() does this for you, so it is only needed when an
    // application changes what it would answer without reporting an event.
    void Update();

    // Report that something about window changed. objectId is wxACC_SELF for
    // the window itself or a child id, and objectType is accepted for
    // compatibility with the other ports but not used: GTK has no equivalent
    // of MSAA's window parts.
    static void NotifyEvent(int eventType, wxWindow* window,
                            wxAccObject objectType, int objectId);

private:
    wxGTKAccessibleImpl* const m_impl;

    wxDECLARE_NO_COPY_CLASS(wxAccessible);
};

#endif // wxUSE_ACCESSIBILITY

#endif // _WX_GTK_ACCESS_H_
