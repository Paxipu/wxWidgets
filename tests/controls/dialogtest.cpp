///////////////////////////////////////////////////////////////////////////////
// Name:        tests/controls/dialogtest.cpp
// Purpose:     wxWindow unit test
// Author:      Vaclav Slavik
// Created:     2012-08-30
// Copyright:   (c) 2012 Vaclav Slavik
///////////////////////////////////////////////////////////////////////////////

#include "testprec.h"


#include "wx/testing.h"

#include "wx/msgdlg.h"
#include "wx/filedlg.h"

#if wxUSE_FILEDLG

// wxFileDialog::SetExtraControlCreator() reports whether the port can show an
// extra control, and an application is entitled to act on the answer. Under
// GTK4 it cannot: gtk_file_chooser_set_extra_widget() is gone and a chooser
// only takes the fixed choices gtk_file_chooser_add_choice() offers.
//
// Saying "yes" anyway ran the application's creator and then owned the control
// it returned without ever showing it, so the application was told it had an
// extra control and had none. This pins the answer to the truth on every
// platform rather than only noticing when someone looks at a dialog.
static wxWindow* CreateExtraControlForTest(wxWindow* parent)
{
    return new wxWindow(parent, wxID_ANY);
}

TEST_CASE("wxFileDialog::ExtraControl", "[filedlg]")
{
    wxFileDialog dlg(nullptr, "Test", "", "",
                     wxFileSelectorDefaultWildcardStr, wxFD_OPEN);

    const bool accepted = dlg.SetExtraControlCreator(&CreateExtraControlForTest);

#ifdef __WXGTK4__
    INFO("GTK4 cannot host an arbitrary widget in a file chooser");
    CHECK( !accepted );
#else
    INFO("this port can host an extra control, so it must accept one");
    CHECK( accepted );
#endif

    // Whatever the answer, it has to be the same one the port reports.
    CHECK( accepted == dlg.SupportsExtraControl() );
}

#endif // wxUSE_FILEDLG

#if wxUSE_WIZARDDLG && defined(__WXGTK4__)
    #include "wx/sizer.h"
    #include "wx/stattext.h"
    #include "wx/wizard.h"
#endif

// The modal tests below exercise helpers from wx/testing.h intended for
// testing code that calls modal dialogs. They don't test the implementation of
// wxWidgets dialogs themselves.

TEST_CASE("Modal::MessageDialog", "[modal]")
{
    int rc;

#if wxUSE_FILEDLG
    #define FILE_DIALOG_TEST ,\
        wxExpectModal<wxFileDialog>(wxGetCwd() + "/test.txt").Optional()
#else
    #define FILE_DIALOG_TEST
#endif

    wxTEST_DIALOG
    (
        rc = wxMessageBox("Should I fail?", "Question", wxYES|wxNO),
        wxExpectModal<wxMessageDialog>(wxNO)
        FILE_DIALOG_TEST
    );

    CHECK( rc == wxNO );
}

#if wxUSE_FILEDLG
TEST_CASE("Modal::FileDialog", "[modal]")
{
#if defined(__WXQT__) && defined(__WINDOWS__)
    WARN("Skipping test known to fail under wxQt for Windows");
    return;
#else
    wxFileDialog dlg(nullptr);
    int rc;

    wxTEST_DIALOG
    (
        rc = dlg.ShowModal(),
        wxExpectModal<wxFileDialog>(wxGetCwd() + "/test.txt")
    );

    CHECK( rc == wxID_OK );

    CHECK( dlg.GetFilename() == "test.txt" );

#ifdef __WXGTK3__
    // The native file dialog in GTK+ 3 launches an async operation which tries
    // to dereference the already deleted dialog object if we don't let it to
    // complete before leaving this function.
    wxYield();
#endif
#endif
}
#endif

class MyDialog : public wxDialog
{
public:
    MyDialog(wxWindow *parent) : wxDialog(parent, wxID_ANY, "Entry"), m_value(-1)
    {
        // Dummy. Imagine it's a real dialog that shows some number-entry
        // controls.
    }

    int m_value;
};


template<>
class wxExpectModal<MyDialog> : public wxExpectModalBase<MyDialog>
{
public:
    wxExpectModal(int valueToSet) : m_valueToSet(valueToSet) {}

protected:
    virtual int OnInvoked(MyDialog *dlg) const override
    {
        // Simulate the user entering the expected number:
        dlg->m_value = m_valueToSet;
        return wxID_OK;
    }

    int m_valueToSet;
};

TEST_CASE("Modal::CustomDialog", "[modal]")
{
    MyDialog dlg(nullptr);

    wxTEST_DIALOG
    (
        dlg.ShowModal(),
        wxExpectModal<MyDialog>(42)
    );

    CHECK( dlg.m_value == 42 );
}


class MyModalDialog : public wxDialog
{
public:
    MyModalDialog() : wxDialog (nullptr, wxID_ANY, "Modal Dialog")
    {
        m_wasModal = false;
        Bind( wxEVT_INIT_DIALOG, &MyModalDialog::OnInit, this );
    }

    void OnInit(wxInitDialogEvent& WXUNUSED(event))
    {
        m_wasModal = IsModal();
        CallAfter( &MyModalDialog::EndModal, wxID_OK );
    }

    bool WasModal() const
    {
        return m_wasModal;
    }

private:
    bool m_wasModal;
};

TEST_CASE("Modal::InitDialog", "[modal]")
{
    MyModalDialog dlg;
    dlg.ShowModal();
    CHECK( dlg.WasModal() );
}

#if wxUSE_WIZARDDLG && defined(__WXGTK4__)

TEST_CASE("Wizard::LayoutAdaptation", "[wizard][layout]")
{
    wxWizard wizard(nullptr, wxID_ANY, "Wizard");
    auto* const firstPage = new wxWizardPageSimple(&wizard);
    auto* const page = new wxWizardPageSimple(&wizard);
    firstPage->Chain(page);

    auto* const text = new wxStaticText(page, wxID_ANY, "Page content");
    auto* const pageSizer = new wxBoxSizer(wxVERTICAL);
    pageSizer->Add(text);
    page->SetSizer(pageSizer);
    wizard.GetPageAreaSizer()->Add(firstPage);

    REQUIRE( wizard.ShowPage(firstPage) );
    REQUIRE( wizard.DoLayoutAdaptation() );

    wxWindow* const scrolledWindow = text->GetParent();
    CHECK( scrolledWindow != page );
    CHECK( scrolledWindow->GetParent() == page );
    CHECK( pageSizer->GetContainingWindow() == scrolledWindow );
}

#endif // wxUSE_WIZARDDLG && defined(__WXGTK4__)
