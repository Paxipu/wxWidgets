/////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/toolbar.h
// Purpose:     GTK toolbar
// Author:      Robert Roebling
// Copyright:   (c) Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_TOOLBAR_H_
#define _WX_GTK_TOOLBAR_H_

typedef struct _GtkTooltips GtkTooltips;

// ----------------------------------------------------------------------------
// wxToolBar
// ----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxToolBar : public wxToolBarBase
{
public:
    // construction/destruction
    wxToolBar() { Init(); }
    wxToolBar( wxWindow *parent,
               wxWindowID id,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               long style = wxTB_DEFAULT_STYLE,
               const wxString& name = wxASCII_STR(wxToolBarNameStr) )
    {
        Init();

        Create(parent, id, pos, size, style, name);
    }

    bool Create( wxWindow *parent,
                 wxWindowID id,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize,
                 long style = wxTB_DEFAULT_STYLE,
                 const wxString& name = wxASCII_STR(wxToolBarNameStr) );

    virtual ~wxToolBar();

    virtual wxToolBarToolBase *FindToolForPosition(wxCoord x, wxCoord y) const override;

    virtual void SetToolShortHelp(int id, const wxString& helpString) override;

    virtual void SetWindowStyleFlag( long style ) override;

    virtual void SetToolNormalBitmap(int id, const wxBitmapBundle& bitmap) override;
    virtual void SetToolDisabledBitmap(int id, const wxBitmapBundle& bitmap) override;

    virtual bool Realize() override;

#ifdef __WXGTK4__
    virtual void SetToolPacking(int packing) override;
#endif // __WXGTK4__

    static wxVisualAttributes
    GetClassDefaultAttributes(wxWindowVariant variant = wxWINDOW_VARIANT_NORMAL);

    virtual wxToolBarToolBase *CreateTool(int id,
                                          const wxString& label,
                                          const wxBitmapBundle& bitmap1,
                                          const wxBitmapBundle& bitmap2 = wxNullBitmap,
                                          wxItemKind kind = wxITEM_NORMAL,
                                          wxObject *clientData = nullptr,
                                          const wxString& shortHelpString = wxEmptyString,
                                          const wxString& longHelpString = wxEmptyString) override;
    virtual wxToolBarToolBase *CreateTool(wxControl *control,
                                          const wxString& label) override;

    // implementation from now on
    // --------------------------

#ifdef __WXGTK4__
    // GTK4 removed GtkToolbar along with the whole GtkToolItem family: a
    // toolbar is a GtkBox carrying the "toolbar" style class now, holding
    // ordinary buttons. See docs/gtk/gtk4-status.md.
    GtkWidget* GTKGetToolbar() const { return m_toolbar; }

    // (Re)build the contents of a tool's button -- its image and/or its label,
    // as the toolbar's style flags ask for. GTK4 buttons hold exactly what we
    // put in them, so a change of label or of style means rebuilding this.
    void GTKUpdateToolContent(wxToolBarToolBase* tool);
#else
    GtkToolbar* GTKGetToolbar() const { return m_toolbar; }
#endif

protected:
    // choose the default border for this window
    virtual wxBorder GetDefaultBorder() const override { return wxBORDER_DEFAULT; }

    virtual wxSize DoGetBestSize() const override;
#ifndef __WXGTK4__
    virtual GdkWindow *GTKGetWindow(wxArrayGdkWindows& windows) const override;
#endif // !__WXGTK4__

    // implement base class pure virtuals
    virtual bool DoInsertTool(size_t pos, wxToolBarToolBase *tool) override;
    virtual bool DoDeleteTool(size_t pos, wxToolBarToolBase *tool) override;

    virtual void DoEnableTool(wxToolBarToolBase *tool, bool enable) override;
    virtual void DoToggleTool(wxToolBarToolBase *tool, bool toggle) override;
    virtual void DoSetToggle(wxToolBarToolBase *tool, bool toggle) override;

private:
    void Init();
    void GtkSetStyle();
#ifdef __WXGTK4__
    // The tool whose widget is at this position in the toolbar box, or null.
    wxToolBarToolBase* GTKGetToolAt(size_t pos) const;

    // The button to group a radio tool inserted at this position with, or null
    // if it starts a new group. GTK4 groups toggle buttons by pointing one at
    // another rather than by passing around a GSList.
    struct _GtkToggleButton* GetRadioGroup(size_t pos);
#else
    GSList* GetRadioGroup(size_t pos);
#endif
    virtual void AddChildGTK(wxWindowGTK* child) override;

#ifdef __WXGTK4__
    GtkWidget* m_toolbar;
#else
    GtkToolbar* m_toolbar;
    GtkTooltips* m_tooltips;
#endif

    wxDECLARE_DYNAMIC_CLASS(wxToolBar);
};

#endif
    // _WX_GTK_TOOLBAR_H_
