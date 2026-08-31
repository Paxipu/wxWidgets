/////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/fontdlg.h
// Purpose:     wxFontDialog
// Author:      Robert Roebling
// Created:
// Copyright:   (c) Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_FONTDLG_H_
#define _WX_GTK_FONTDLG_H_

//-----------------------------------------------------------------------------
// wxFontDialog
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxFontDialog : public wxFontDialogBase
{
public:
    wxFontDialog() : wxFontDialogBase() { /* must be Create()d later */ }
    wxFontDialog(wxWindow *parent)
        : wxFontDialogBase(parent) { Create(parent); }
    wxFontDialog(wxWindow *parent, const wxFontData& data)
        : wxFontDialogBase(parent, data) { Create(parent, data); }

    virtual ~wxFontDialog();

#ifdef __WXGTK4__
    // GtkFontChooserDialog is deprecated since GTK 4.10 and its replacement,
    // GtkFontDialog, is not a widget: there is nothing to show, so the base
    // class version, which shows m_widget, cannot be used.
    virtual int ShowModal() override;
#endif // __WXGTK4__

protected:
    // create the GTK dialog
    virtual bool DoCreate(wxWindow *parent) override;

    wxDECLARE_DYNAMIC_CLASS(wxFontDialog);
};

#endif
