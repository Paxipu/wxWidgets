/////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/popupwin.h
// Purpose:
// Author:      Robert Roebling
// Created:
// Copyright:   (c) 2001 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_POPUPWIN_H_
#define _WX_GTK_POPUPWIN_H_

//-----------------------------------------------------------------------------
// wxPopUpWindow
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxPopupWindow: public wxPopupWindowBase
{
public:
    wxPopupWindow() = default;
    virtual ~wxPopupWindow();

    wxPopupWindow(wxWindow *parent, int flags = wxBORDER_NONE)
        { (void)Create(parent, flags); }
    bool Create(wxWindow *parent, int flags = wxBORDER_NONE);

    virtual bool Show(bool show = true) override;

    virtual void SetFocus() override;

    // implementation
    // --------------

    // GTK time when connecting to button_press signal
    wxUint32  m_time;

protected:
    virtual void DoSetSize(int x, int y,
                           int width, int height,
                           int sizeFlags = wxSIZE_AUTO) override;

    virtual void DoMoveWindow(int x, int y, int width, int height) override;

#ifdef __WXGTK4__
public:
    // Place the GtkPopover which stands in for the popup toplevel under GTK4,
    // see popupwin.cpp. Public because the handlers there call it once the
    // popover has an allocation, which is the first time the inset below can
    // be measured.
    void GTKUpdatePointingTo();

    // How far below its own top edge a popover starts its content. Measured
    // from the allocation, so it only answers once there is one; the last
    // answer is remembered for the placements that happen before that.
    int GTKGetContentTopInset();

    unsigned int m_placeAgainIdle = 0;
    int m_contentTopInset = 0;

protected:
#endif // __WXGTK4__

#ifdef __WXUNIVERSAL__
    wxDECLARE_EVENT_TABLE();
#endif
    wxDECLARE_DYNAMIC_CLASS(wxPopupWindow);
};

#endif // _WX_GTK_POPUPWIN_H_
