/////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/filedlg.h
// Purpose:
// Author:      Robert Roebling
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTKFILEDLG_H_
#define _WX_GTKFILEDLG_H_

#include "wx/gtk/filectrl.h"    // for wxGtkFileChooser

typedef struct _GtkFileChooser GtkFileChooser;

//-------------------------------------------------------------------------
// wxFileDialog
//-------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxFileDialog: public wxFileDialogBase
{
    typedef wxFileDialogBase BaseType;
public:
    wxFileDialog() = default;

    wxFileDialog(wxWindow *parent,
                 const wxString& message = wxASCII_STR(wxFileSelectorPromptStr),
                 const wxString& defaultDir = wxEmptyString,
                 const wxString& defaultFile = wxEmptyString,
                 const wxString& wildCard = wxASCII_STR(wxFileSelectorDefaultWildcardStr),
                 long style = wxFD_DEFAULT_STYLE,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& sz = wxDefaultSize,
                 const wxString& name = wxASCII_STR(wxFileDialogNameStr));
    bool Create(wxWindow *parent,
                 const wxString& message = wxASCII_STR(wxFileSelectorPromptStr),
                 const wxString& defaultDir = wxEmptyString,
                 const wxString& defaultFile = wxEmptyString,
                 const wxString& wildCard = wxASCII_STR(wxFileSelectorDefaultWildcardStr),
                 long style = wxFD_DEFAULT_STYLE,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& sz = wxDefaultSize,
                 const wxString& name = wxASCII_STR(wxFileDialogNameStr));
    virtual ~wxFileDialog();

    virtual wxString GetPath() const override;
    virtual void GetPaths(wxArrayString& paths) const override;
    virtual wxString GetFilename() const override;
    virtual void GetFilenames(wxArrayString& files) const override;
    virtual int GetFilterIndex() const override;
    virtual wxString GetDirectory() const override;

    virtual void SetMessage(const wxString& message) override;
    virtual void SetPath(const wxString& path) override;
    virtual void SetDirectory(const wxString& dir) override;
    virtual void SetFilename(const wxString& name) override;
    virtual void SetWildcard(const wxString& wildCard) override;
    virtual void SetFilterIndex(int filterIndex) override;

    virtual int ShowModal() override;
    virtual void EndModal(int retCode) override;

    virtual bool AddShortcut(const wxString& directory, int flags = 0) override;
    // GTK4 removed gtk_file_chooser_set_extra_widget(): a chooser can only be
    // extended with the fixed set of controls gtk_file_chooser_add_choice()
    // offers, not with an arbitrary widget. Saying "yes" here anyway made
    // SetExtraControlCreator() succeed, the application's creator run, and the
    // control it returned be owned and never shown -- so the application was
    // told it had an extra control and had none. Say no instead, so that
    // SetExtraControlCreator() and SetCustomizeHook() fail and the caller can
    // do something else.
#ifdef __WXGTK4__
    virtual bool SupportsExtraControl() const override { return false; }
#else
    virtual bool SupportsExtraControl() const override { return true; }
#endif

    // Implementation only.
    void GTKSelectionChanged(const wxString& filename);
    void GTKDropNative();


protected:
    // override this from wxTLW since the native
    // form doesn't have any m_wxwindow
    virtual void DoSetSize(int x, int y,
                           int width, int height,
                           int sizeFlags = wxSIZE_AUTO) override;


private:
    void OnFakeOk( wxCommandEvent &event );
    void OnSize(wxSizeEvent&);
    virtual void AddChildGTK(wxWindowGTK* child) override;

    const wxGtkFileChooser& GetFileChooser() const
    {
        return m_fcNative ? *m_fcNative : m_fc;
    }

#ifdef __WXGTK4__
    // Send the chooser its starting location once, when the dialog is about to
    // be shown, instead of whenever it is set.
    //
    // GTK4 starts loading a folder as soon as it is told about one, and a
    // second location cancels that load rather than replacing it: the dialog
    // then comes up saying "The folder contents could not be displayed --
    // Operation was cancelled" with an empty list. Since the ctor sets a
    // location and SetDirectory()/SetPath() may set another before the dialog
    // is ever shown, they only record it here.
    void GTKApplyPendingLocation();

    wxString m_pendingFolder;
    wxString m_pendingFile;
    wxString m_pendingName;

    // False until GTKApplyPendingLocation() has run, i.e. while the location
    // still has to be held back; afterwards the dialog is up and a new
    // location just navigates it, so it is sent straight through.
    bool m_locationApplied = false;
#endif // __WXGTK4__

    wxGtkFileChooser    m_fc;
    wxGtkFileChooser* m_fcNative = nullptr;
    GtkFileChooser* m_fileChooserNative = nullptr;

    wxDECLARE_DYNAMIC_CLASS(wxFileDialog);
    wxDECLARE_EVENT_TABLE();
};

#endif // _WX_GTKFILEDLG_H_
