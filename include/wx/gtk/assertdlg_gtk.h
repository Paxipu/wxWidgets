/* ///////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/assertdlg_gtk.h
// Purpose:     GtkAssertDialog
// Author:      Francesco Montorsi
// Copyright:   (c) 2006 Francesco Montorsi
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////// */

#ifndef _WX_GTK_ASSERTDLG_H_
#define _WX_GTK_ASSERTDLG_H_

#define GTK_TYPE_ASSERT_DIALOG            (gtk_assert_dialog_get_type ())
#define GTK_ASSERT_DIALOG(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), GTK_TYPE_ASSERT_DIALOG, GtkAssertDialog))
#define GTK_ASSERT_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_ASSERT_DIALOG, GtkAssertDialogClass))
#define GTK_IS_ASSERT_DIALOG(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), GTK_TYPE_ASSERT_DIALOG))
#define GTK_IS_ASSERT_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_ASSERT_DIALOG))
#define GTK_ASSERT_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_ASSERT_DIALOG, GtkAssertDialogClass))

typedef struct _GtkAssertDialog        GtkAssertDialog;
typedef struct _GtkAssertDialogClass   GtkAssertDialogClass;
typedef void (*GtkAssertDialogStackFrameCallback)(void *);

struct _GtkAssertDialog
{
#ifdef __WXGTK4__
    /* GtkDialog is deprecated in GTK 4.10, along with the whole of its
       convenience API -- the content area, the action buttons and the
       ::response signal. What replaces it for a dialog of one's own is a plain
       GtkWindow carrying its own content and its own buttons, which is what
       this is, so the parent type differs here. */
    GtkWindow parent_instance;
#else
    GtkDialog parent_instance;
#endif

    /* GtkAssertDialog widgets */
    GtkWidget *expander;
    GtkWidget *message;
    /* Under GTK4 this is a GtkColumnView and the rows live in "frames" below;
       elsewhere it is a GtkTreeView owning its own GtkListStore. */
    GtkWidget *treeview;

#ifdef __WXGTK4__
    /* GListStore of WxAssertFrame, the model behind the column view. */
    GListStore *frames;
#endif

    GtkWidget *shownexttime;

#ifdef __WXGTK4__
    /* What GtkDialog used to hold: where the content goes, and the result of
       the nested loop gtk_assert_dialog_run() spins. */
    GtkWidget *contentArea;
    GMainLoop *loop;
    int response;
#endif

    /* callback for processing the stack frame */
    GtkAssertDialogStackFrameCallback callback;
    void *userdata;
};

struct _GtkAssertDialogClass
{
#ifdef __WXGTK4__
    GtkWindowClass parent_class;
#else
    GtkDialogClass parent_class;
#endif
};

typedef enum
{
    GTK_ASSERT_DIALOG_STOP,
    GTK_ASSERT_DIALOG_CONTINUE,
    GTK_ASSERT_DIALOG_CONTINUE_SUPPRESSING
} GtkAssertDialogResponseID;




GType gtk_assert_dialog_get_type(void);
GtkWidget *gtk_assert_dialog_new(void);

#ifdef __WXGTK4__
/* Show the dialog and block until the user answers, returning one of the
   GtkAssertDialogResponseID values. What gtk_dialog_run() did, for a dialog
   that is no longer a GtkDialog.

   The nested loop is a plain GMainLoop rather than a wxGUIEventLoop, for the
   same reason as the one in gtk3-compat.h: this runs when an assertion has
   already fired and wx's own event loop may not be in a usable state. */
int gtk_assert_dialog_run(GtkAssertDialog *assertdlg);
#endif

/* get the assert message */
gchar *gtk_assert_dialog_get_message(GtkAssertDialog *assertdlg);

/* set the assert message */
void gtk_assert_dialog_set_message(GtkAssertDialog *assertdlg, const gchar *msg);

/* get a string containing all stack frames appended to the dialog */
gchar *gtk_assert_dialog_get_backtrace(GtkAssertDialog *assertdlg);

/* sets the callback to use when the user wants to see the stackframe */
void gtk_assert_dialog_set_backtrace_callback(GtkAssertDialog *assertdlg,
                                              GtkAssertDialogStackFrameCallback callback,
                                              void *userdata);

/* appends a stack frame to the dialog */
void gtk_assert_dialog_append_stack_frame(GtkAssertDialog *dlg,
                                          const gchar *function,
                                          const gchar *sourcefile,
                                          guint line_number);

#endif /* _WX_GTK_ASSERTDLG_H_ */
