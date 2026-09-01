/////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/choice.h
// Purpose:
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_CHOICE_H_
#define _WX_GTK_CHOICE_H_

class WXDLLIMPEXP_FWD_BASE wxSortedArrayString;
class WXDLLIMPEXP_FWD_BASE wxArrayString;

#ifdef __WXGTK4__
    typedef struct _GtkStringList GtkStringList;
    typedef struct _GtkSingleSelection GtkSingleSelection;
#endif

//-----------------------------------------------------------------------------
// wxChoice
//-----------------------------------------------------------------------------

class wxGtkCollatedArrayString;

class WXDLLIMPEXP_CORE wxChoice : public wxChoiceBase
{
public:
    wxChoice()
    {
        Init();
    }
    wxChoice( wxWindow *parent, wxWindowID id,
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            int n = 0, const wxString choices[] = (const wxString *) nullptr,
            long style = 0,
            const wxValidator& validator = wxDefaultValidator,
            const wxString& name = wxASCII_STR(wxChoiceNameStr) )
    {
        Init();
        Create(parent, id, pos, size, n, choices, style, validator, name);
    }
    wxChoice( wxWindow *parent, wxWindowID id,
            const wxPoint& pos,
            const wxSize& size,
            const wxArrayString& choices,
            long style = 0,
            const wxValidator& validator = wxDefaultValidator,
            const wxString& name = wxASCII_STR(wxChoiceNameStr) )
    {
        Init();
        Create(parent, id, pos, size, choices, style, validator, name);
    }
    virtual ~wxChoice();
    bool Create( wxWindow *parent, wxWindowID id,
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            int n = 0, const wxString choices[] = nullptr,
            long style = 0,
            const wxValidator& validator = wxDefaultValidator,
            const wxString& name = wxASCII_STR(wxChoiceNameStr) );
    bool Create( wxWindow *parent, wxWindowID id,
            const wxPoint& pos,
            const wxSize& size,
            const wxArrayString& choices,
            long style = 0,
            const wxValidator& validator = wxDefaultValidator,
            const wxString& name = wxASCII_STR(wxChoiceNameStr) );

    int GetSelection() const override;
    void SetSelection(int n) override;

    virtual unsigned int GetCount() const override;
    virtual int FindString(const wxString& s, bool bCase = false) const override;
    virtual wxString GetString(unsigned int n) const override;
    virtual void SetString(unsigned int n, const wxString& string) override;

    virtual void SetColumns(int n=1) override;
    virtual int GetColumns() const override;

    virtual void GTKDisableEvents();
    virtual void GTKEnableEvents();

    static wxVisualAttributes
    GetClassDefaultAttributes(wxWindowVariant variant = wxWINDOW_VARIANT_NORMAL);

protected:
    // this array is only used for controls with wxCB_SORT style, so only
    // allocate it if it's needed (hence using pointer)
    wxGtkCollatedArrayString *m_strings;

    // contains the client data for the items
    wxArrayPtrVoid m_clientData;

#ifdef __WXGTK4__
    // The items live here rather than in the widget.
    //
    // wxComboBox derives from this class and does not override the item
    // methods -- GetCount(), GetString(), DoInsertItems() and the rest are all
    // wxChoice's -- but under GTK4 the two are shown by different widgets: a
    // GtkDropDown here and, because GTK4 has no editable combo box at all, a
    // GtkEntry beside a list in a popover there. Keeping the items in the
    // control rather than in the widget is what lets one set of item methods
    // serve both. Owned; a reference is held.
    GtkStringList* m_itemModel;

    // The list the items are chosen from, and the selection in it.
    //
    // This is deliberately not a GtkDropDown, which is what GTK4 offers in
    // place of the deprecated GtkComboBox. A GtkDropDown owns its selection
    // and, from GTK 4.22, refuses to be cleared once it has any items:
    // SetSelection(wxNOT_FOUND) simply does not take. That is a change from
    // GTK 4.14, where it worked, and build/tools/gtk4-invariants.c reports
    // both. wx needs "nothing selected", so the selection model is ours.
    //
    // Both are borrowed; the popover owns them.
    GtkSingleSelection* m_listSelection;
    GtkWidget* m_listView;

    // The button that shows the popover. It is m_widget itself for a
    // wxChoice, and one of two children of it for a wxComboBox.
    GtkWidget* m_dropButton;

    int GTKGetSelection() const;
    void GTKSetSelection(int n);

    // Show the current selection in whatever the closed control looks like: a
    // button label here, the entry's text for wxComboBox.
    virtual void GTKUpdateSelectionDisplay();

    // wxEVT_CHOICE here, wxEVT_COMBOBOX for wxComboBox.
    virtual wxEventType GTKGetSelectionEventType() const;

    // The part of the widget that shows one item, which is what
    // DoGetSizeFromTextSize() measures against to find how much of the width
    // is arrow, separator and padding.
    virtual GtkWidget* GTKGetSizeChildPart() const;

public:
    // A row in the popover was chosen. Public because a C callback calls it.
    void GTKOnListActivated(unsigned int pos);
protected:

    // Called after the item model has changed, because GTK does not leave the
    // selection alone across a change: appending the first item to an empty
    // model selects it, and removing an item moves it.
    void GTKRestoreSelection(int sel);

    // Build the popover holding the list, and the selection model with it.
    // Fills in m_listSelection and m_listView.
    GtkWidget* GTKCreateItemPopover();

    // Let Up, Down, Home and End move the selection while the popover is
    // closed, which is what a GtkComboBox did and what wxUIActionSimulator
    // drives the control with.
    void GTKConnectSelectionKeys(GtkWidget* widget);

public:
    // Reached from the key controller, which is a C callback.
    bool GTKMoveSelection(unsigned int keyval);
protected:
#else
    // index to GtkListStore cell which displays the item text
    int m_stringCellIndex;
#endif // __WXGTK4__

    virtual wxSize DoGetBestSize() const override;
    virtual wxSize DoGetSizeFromTextSize(int xlen, int ylen = -1) const override;
    virtual int DoInsertItems(const wxArrayStringsAdapter& items,
                              unsigned int pos,
                              void **clientData, wxClientDataType type) override;
    virtual void DoSetItemClientData(unsigned int n, void* clientData) override;
    virtual void* DoGetItemClientData(unsigned int n) const override;
    virtual void DoClear() override;
    virtual void DoDeleteOneItem(unsigned int n) override;

    virtual bool GTKHandleFocusOut() override;
#ifndef __WXGTK4__
    virtual GdkWindow *GTKGetWindow(wxArrayGdkWindows& windows) const override;
#endif // !__WXGTK4__
    virtual void DoApplyWidgetStyle(GtkRcStyle *style) override;

private:
    void Init();

    wxDECLARE_DYNAMIC_CLASS(wxChoice);
};


#endif // _WX_GTK_CHOICE_H_
