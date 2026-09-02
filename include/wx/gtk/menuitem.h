///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/menuitem.h
// Purpose:     wxMenuItem class
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTKMENUITEM_H_
#define _WX_GTKMENUITEM_H_

//-----------------------------------------------------------------------------
// wxMenuItem
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxMenuItem : public wxMenuItemBase
{
public:
    wxMenuItem(wxMenu *parentMenu = nullptr,
               int id = wxID_SEPARATOR,
               const wxString& text = wxEmptyString,
               const wxString& help = wxEmptyString,
               wxItemKind kind = wxITEM_NORMAL,
               wxMenu *subMenu = nullptr);
    virtual ~wxMenuItem();

    // implement base class virtuals
    virtual void SetItemLabel( const wxString& str ) override;
    virtual void Enable( bool enable = true ) override;
    virtual void Check( bool check = true ) override;
    virtual bool IsChecked() const override;
    void SetupBitmaps(wxWindow *win);

#if wxUSE_ACCEL
    virtual void AddExtraAccel(const wxAcceleratorEntry& accel) override;
    virtual void ClearExtraAccels() override;
#endif // wxUSE_ACCEL

    // implementation
#ifdef __WXGTK4__
    // GTK4 has no menu item widgets at all: a menu is a GMenuModel describing
    // the structure and the behaviour of each item lives in a GAction owned by
    // the action group of the item's menu. See docs/gtk/gtk4-phase-menu-design.md.
    //
    // Name of this item's action, without the menu's prefix, e.g. "i42" for a
    // normal or check item and "r7" for a radio item, in which case the action
    // is shared with the rest of the radio group. Empty for separators and for
    // sub menu items, which have no action of their own.
    const wxString& GTKGetActionName() const { return m_actionName; }
    void GTKSetActionName(const wxString& name) { m_actionName = name; }

    // For radio items, the target value identifying this item within its radio
    // group's shared action; empty for all other kinds.
    const wxString& GTKGetRadioTarget() const { return m_radioTarget; }
    void GTKSetRadioTarget(const wxString& target) { m_radioTarget = target; }

    // The window last passed to SetupBitmaps(), used to pick the bitmap
    // variant matching its DPI when the menu model is (re)built.
    wxWindow* GTKGetBitmapWindow() const { return m_bitmapWin; }
#else
    void SetMenuItem(GtkWidget *menuItem);
    GtkWidget *GetMenuItem() const { return m_menuItem; }
#endif // __WXGTK4__/!__WXGTK4__

    void SetGtkLabel();

#if wxUSE_ACCEL
    void GTKSetExtraAccels();
#endif // wxUSE_ACCEL

private:
#ifdef __WXGTK4__
    wxString m_actionName;
    wxString m_radioTarget;
    wxWindow* m_bitmapWin;
#else
    GtkWidget *m_menuItem;  // GtkMenuItem
#endif

    wxDECLARE_DYNAMIC_CLASS(wxMenuItem);
};

#endif // _WX_GTKMENUITEM_H_
