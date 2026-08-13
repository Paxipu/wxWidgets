/////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/menu.h
// Purpose:
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling, Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTKMENU_H_
#define _WX_GTKMENU_H_

//-----------------------------------------------------------------------------
// wxMenuBar
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxMenuBar : public wxMenuBarBase
{
public:
    // ctors
    wxMenuBar();
    wxMenuBar(long style);
    wxMenuBar(size_t n, wxMenu *menus[], const wxString titles[], long style = 0);
    ~wxMenuBar();

    // implement base class (pure) virtuals
    virtual bool Append( wxMenu *menu, const wxString &title ) override;
    virtual bool Insert(size_t pos, wxMenu *menu, const wxString& title) override;
    virtual wxMenu *Replace(size_t pos, wxMenu *menu, const wxString& title) override;
    virtual wxMenu *Remove(size_t pos) override;

    virtual int FindMenuItem(const wxString& menuString,
                             const wxString& itemString) const override;
    virtual wxMenuItem* FindItem( int id, wxMenu **menu = nullptr ) const override;

    virtual void EnableTop( size_t pos, bool flag ) override;
    virtual bool IsEnabledTop(size_t pos) const override;
    virtual void SetMenuLabel( size_t pos, const wxString& label ) override;
    virtual wxString GetMenuLabel( size_t pos ) const override;

    void SetLayoutDirection(wxLayoutDirection dir) override;
    wxLayoutDirection GetLayoutDirection() const override;

    virtual void Attach(wxFrame *frame) override;
    virtual void Detach() override;

#ifdef __WXGTK4__
    // Rebuild the GMenuModel backing the menu bar from the current menu list
    // and, if we're attached to a frame, the shortcuts for the accelerators of
    // all the items in all of them.
    void GTKRebuildModel();
#endif // __WXGTK4__

private:
    // common part of Append and Insert
    void GtkAppend(wxMenu* menu, const wxString& title, int pos = -1);

    void Init(size_t n, wxMenu *menus[], const wxString titles[], long style);

    // wxMenuBar is not a top level window but it still doesn't need a parent
    // window
    virtual bool GTKNeedsParent() const override { return false; }

    GtkWidget* m_menubar;

#ifdef __WXGTK4__
    // The model rendered by m_menubar (a GtkPopoverMenuBar under GTK4) and the
    // controller holding the shortcuts for all our accelerators, created when
    // we're attached to a frame and destroyed when we're detached from it.
    GMenu* m_barModel;
    GtkEventController* m_shortcuts;
#endif // __WXGTK4__

    wxDECLARE_DYNAMIC_CLASS(wxMenuBar);
};

//-----------------------------------------------------------------------------
// wxMenu
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxMenu : public wxMenuBase
{
public:
    // ctors & dtor
    wxMenu(const wxString& title, long style = 0)
        : wxMenuBase(title, style) { Init(); }

    wxMenu(long style = 0) : wxMenuBase(style) { Init(); }

    virtual ~wxMenu();

    void Attach(wxMenuBarBase *menubar) override;

    void SetupBitmaps(wxWindow *win);

    void SetLayoutDirection(wxLayoutDirection dir);
    wxLayoutDirection GetLayoutDirection() const;

    // Returns the title, with mnemonics translated to wx format
    wxString GetTitle() const;

    // Sets the title, with mnemonics translated to gtk format
    virtual void SetTitle(const wxString& title) override;

    // implementation GTK only
#ifdef __WXGTK4__
    // GTK4 replaced menu widgets with a declarative model: this menu is a
    // GMenu describing its structure plus a GSimpleActionGroup owning the
    // GActions its items act on. See docs/gtk/gtk4-phase-menu-design.md.
    GMenu* GTKGetMenuModel() const { return m_menuModel; }
    // Note that this returns the group without casting it to GActionGroup or
    // GActionMap: the GLib cast macros are not available in this header.
    GSimpleActionGroup* GTKGetActionGroup() const { return m_actionGroup; }
    const wxString& GTKGetActionPrefix() const { return m_actionPrefix; }

    // Regenerate both the model and the actions from the wx item list. Called
    // for any structural change: GMenu copies item attributes on insertion, so
    // there is nothing to patch in place, and separators are modelled as
    // sections, so wx item positions don't map to model positions anyway.
    void GTKRebuildModel();

    // Insert (or remove) the action groups of this menu and of all its sub
    // menus into (from) the given widget. Named actions are resolved by
    // walking up the widget hierarchy, so this must be a widget which is an
    // ancestor of both the menu view and any shortcut controller using them.
    void GTKInstallActions(GtkWidget* widget);
    void GTKUninstallActions(GtkWidget* widget);

    // Add the accelerators of this menu and of all its sub menus to the given
    // shortcut controller. GTK4 has no accelerator groups: menu accelerators
    // are GtkShortcuts triggering the items' named actions.
    void GTKAddShortcuts(GtkShortcutController* controller);

    // Called when one of our radio group actions changed state, with the bare
    // action name and the target value of the item which is now selected.
    void GTKOnRadioSelected(const char* actionName, const wxString& target);

    // Show this menu as a popup over the given window, blocking until it is
    // dismissed, as wxWindow::PopupMenu() is documented to do.
    bool GTKShowPopup(wxWindow* win, int x, int y);

    // True while the menu bar shows this menu as enabled; GMenuModel sub menu
    // items have no action of their own, so this can't be read back from GTK.
    bool GTKIsEnabledTop() const { return m_enabledTop; }
    void GTKSetEnabledTop(bool enable) { m_enabledTop = enable; }
#else
    GtkWidget       *m_menu;  // GtkMenu
    GtkWidget       *m_owner;
    GtkAccelGroup   *m_accel;
#endif // __WXGTK4__/!__WXGTK4__
    bool m_popupShown;

protected:
    virtual wxMenuItem* DoAppend(wxMenuItem *item) override;
    virtual wxMenuItem* DoInsert(size_t pos, wxMenuItem *item) override;
    virtual wxMenuItem* DoRemove(wxMenuItem *item) override;

private:
    // common code for all constructors:
    void Init();

    // common part of Append (if pos == -1)  and Insert
    void GtkAppend(wxMenuItem* item, int pos = -1);

#ifdef __WXGTK4__
    // Ask the menu bar we (possibly indirectly) belong to, if any, to refresh
    // the shortcuts it registered for our items' accelerators.
    void GTKRefreshShortcuts();

    GMenu* m_menuModel;
    GSimpleActionGroup* m_actionGroup;
    wxString m_actionPrefix;
    GtkWidget* m_popover;
    GMainLoop* m_popupLoop;
    bool m_enabledTop;
#endif // __WXGTK4__

    wxDECLARE_DYNAMIC_CLASS(wxMenu);
};

#endif
    // _WX_GTKMENU_H_
