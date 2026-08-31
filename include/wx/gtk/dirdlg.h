/////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/dirdlg.h
// Purpose:     wxDirDialog
// Author:      Francesco Montorsi
// Copyright:   (c) 2006 Francesco Montorsi
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTKDIRDLG_H_
#define _WX_GTKDIRDLG_H_

typedef struct _GtkFileChooser GtkFileChooser;

//-------------------------------------------------------------------------
// wxDirDialog
//-------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxDirDialog : public wxDirDialogBase
{
    typedef wxDirDialogBase BaseType;
public:
    wxDirDialog() = default;

    wxDirDialog(wxWindow *parent,
                const wxString& message = wxASCII_STR(wxDirSelectorPromptStr),
                const wxString& defaultPath = wxEmptyString,
                long style = wxDD_DEFAULT_STYLE,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                const wxString& name = wxASCII_STR(wxDirDialogNameStr));
    bool Create(wxWindow *parent,
                const wxString& message = wxASCII_STR(wxDirSelectorPromptStr),
                const wxString& defaultPath = wxEmptyString,
                long style = wxDD_DEFAULT_STYLE,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                const wxString& name = wxASCII_STR(wxDirDialogNameStr));
    ~wxDirDialog();

    virtual int ShowModal() override;
    virtual void EndModal(int retCode) override;
    void SetPath(const wxString& path) override;

    // Implementation only.

#ifdef __WXGTK4__
    // Called from the asynchronous callback in ShowModal() with what the user
    // chose.
    void GTKSetChosenPaths(const wxArrayString& paths);
#else
    void GTKOnAccept();
    void GTKOnCancel();
    void GTKDropNative();
#endif

protected:
    // override this from wxTLW since the native
    // form doesn't have any m_wxwindow
    virtual void DoSetSize(int x, int y,
                           int width, int height,
                           int sizeFlags = wxSIZE_AUTO) override;


private:
#ifndef __WXGTK4__
    void GTKAccept();
#endif

    // The part of accepting that is not about getting the paths out of GTK.
    void GTKFinishAccept();

#ifdef __WXGTK4__
    // There is no chooser object between ShowModal() calls under GTK4: a
    // GtkFileDialog is a controller, built and thrown away inside ShowModal().
    // So what the caller sets beforehand has to be remembered here instead.
    wxString m_initialPath;
#else
    GtkFileChooser* m_fileChooser = nullptr;
#endif

    wxDECLARE_DYNAMIC_CLASS(wxDirDialog);
};

#endif // _WX_GTKDIRDLG_H_
