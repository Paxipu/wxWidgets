/////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/bmpcbox.h
// Purpose:     wxBitmapComboBox
// Author:      Jaakko Salli
// Created:     2008-05-19
// Copyright:   (c) 2008 Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_BMPCBOX_H_
#define _WX_GTK_BMPCBOX_H_


#include "wx/combobox.h"

// ----------------------------------------------------------------------------
// wxBitmapComboBox: a wxComboBox that allows images to be shown
// in front of string items.
// ----------------------------------------------------------------------------

class WXDLLIMPEXP_ADV wxBitmapComboBox : public wxComboBox,
                                         public wxBitmapComboBoxBase
{
public:
    // ctors and such
    wxBitmapComboBox() : wxComboBox(), wxBitmapComboBoxBase()
    {
        Init();
    }

    wxBitmapComboBox(wxWindow *parent,
                     wxWindowID id = wxID_ANY,
                     const wxString& value = wxEmptyString,
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxDefaultSize,
                     int n = 0,
                     const wxString choices[] = nullptr,
                     long style = 0,
                     const wxValidator& validator = wxDefaultValidator,
                     const wxString& name = wxASCII_STR(wxBitmapComboBoxNameStr))
        : wxComboBox(),
          wxBitmapComboBoxBase()
    {
        Init();

        (void)Create(parent, id, value, pos, size, n,
                     choices, style, validator, name);
    }

    wxBitmapComboBox(wxWindow *parent,
                     wxWindowID id,
                     const wxString& value,
                     const wxPoint& pos,
                     const wxSize& size,
                     const wxArrayString& choices,
                     long style,
                     const wxValidator& validator = wxDefaultValidator,
                     const wxString& name = wxASCII_STR(wxBitmapComboBoxNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& value,
                const wxPoint& pos,
                const wxSize& size,
                int n,
                const wxString choices[],
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxBitmapComboBoxNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& value,
                const wxPoint& pos,
                const wxSize& size,
                const wxArrayString& choices,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxBitmapComboBoxNameStr));

    virtual ~wxBitmapComboBox();

    // Sets the image for the given item.
    virtual void SetItemBitmap(unsigned int n, const wxBitmapBundle& bitmap) override;

    // Returns the image of the item with the given index.
    virtual wxBitmap GetItemBitmap(unsigned int n) const override;

    // Returns size of the image used in list
    virtual wxSize GetBitmapSize() const override
    {
        return m_bitmapSize;
    }

    // Adds item with image to the end of the combo box.
    int Append(const wxString& item, const wxBitmapBundle& bitmap = wxBitmapBundle());
    int Append(const wxString& item, const wxBitmapBundle& bitmap, void *clientData);
    int Append(const wxString& item, const wxBitmapBundle& bitmap, wxClientData *clientData);

    // Inserts item with image into the list before pos. Not valid for wxCB_SORT
    // styles, use Append instead.
    int Insert(const wxString& item, const wxBitmapBundle& bitmap, unsigned int pos);
    int Insert(const wxString& item, const wxBitmapBundle& bitmap,
               unsigned int pos, void *clientData);
    int Insert(const wxString& item, const wxBitmapBundle& bitmap,
               unsigned int pos, wxClientData *clientData);

    // Override some wxTextEntry interface.
    virtual void WriteText(const wxString& value) override;

    virtual wxString GetValue() const override;
    virtual void Remove(long from, long to) override;

    virtual void SetInsertionPoint(long pos) override;
    virtual long GetInsertionPoint() const override;
    virtual long GetLastPosition() const override;

    virtual void SetSelection(long from, long to) override;
    virtual void GetSelection(long *from, long *to) const override;

#ifdef __WXGTK4__
    virtual void SetSelection(int n) override
    {
        wxComboBox::SetSelection(n);
        GTKUpdateEntryBitmap();
    }
#else
    virtual void SetSelection(int n) override { wxComboBox::SetSelection(n); }
#endif
    virtual int GetSelection() const override { return wxComboBox::GetSelection(); }

    virtual bool IsEditable() const override;
    virtual void SetEditable(bool editable) override;

    virtual GtkWidget* GetConnectWidget() const override;

protected:
#ifndef __WXGTK4__
    virtual GdkWindow *GTKGetWindow(wxArrayGdkWindows& windows) const override;
#endif // !__WXGTK4__

    virtual void GTKCreateComboBoxWidget() override;

    virtual wxSize DoGetBestSize() const override;

    wxSize                  m_bitmapSize;
#ifdef __WXGTK4__
    // Under GTK+ 3 the bitmaps live in the GtkListStore next to the strings.
    // GTK4 shows the items from a GtkStringList, which carries strings and
    // nothing else, so the bitmaps are kept here and the list's factory reads
    // them by row. Keeping the bundle rather than a texture also means
    // GetItemBitmap() gives back exactly what was set, scale factor included,
    // which the GTK4 texture round trip could not.
    wxVector<wxBitmapBundle> m_bitmaps;

    // Keep m_bitmaps the same length as the item list.
    virtual int DoInsertItems(const wxArrayStringsAdapter& items,
                              unsigned int pos,
                              void **clientData,
                              wxClientDataType type) override;
    virtual void DoDeleteOneItem(unsigned int n) override;
    virtual void DoClear() override;

public:
    // Reached from the list factory, which is a C callback: fill in one row's
    // image and show the bitmap of the selected item in the entry.
    void GTKSetRowImage(void* image, unsigned int n) const;
protected:
    // Show the selected item's bitmap as the entry's primary icon, which is
    // where a GTK+ 3 cell view showed it.
    void GTKUpdateEntryBitmap();
#else
    int                     m_bitmapCellIndex;
#endif

private:
    void Init();

    wxDECLARE_DYNAMIC_CLASS(wxBitmapComboBox);
};

#endif // _WX_GTK_BMPCBOX_H_
