/* What does GtkColorDialogButton/GtkFontDialogButton do that GtkColorButton
   did not?  Two questions decide whether the swap is safe:

     1. does setting the value programmatically emit the change signal?
        (GtkColorButton's "color-set" did not; notify::rgba might)
     2. does the value survive a set/get round trip unchanged? */
#include <gtk/gtk.h>

static int rgba_notifies = 0;
static int font_notifies = 0;

static void on_rgba(GObject *o, GParamSpec *p, gpointer d)
{ (void)o; (void)p; (void)d; rgba_notifies++; }

static void on_font(GObject *o, GParamSpec *p, gpointer d)
{ (void)o; (void)p; (void)d; font_notifies++; }

int main(void)
{
    gtk_init();

    /* ---- colour ---- */
    GtkColorDialog *cd = gtk_color_dialog_new();
    gtk_color_dialog_set_with_alpha(cd, FALSE);
    GtkWidget *cb = gtk_color_dialog_button_new(cd);  /* takes the dialog */
    g_signal_connect(cb, "notify::rgba", G_CALLBACK(on_rgba), NULL);

    GdkRGBA want = { 0.25, 0.5, 0.75, 1.0 };
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(cb), &want);
    const GdkRGBA *got = gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(cb));

    g_print("colour  set(%.2f %.2f %.2f %.2f) -> get(%.2f %.2f %.2f %.2f)  %s\n",
            want.red, want.green, want.blue, want.alpha,
            got->red, got->green, got->blue, got->alpha,
            gdk_rgba_equal(&want, got) ? "ROUND TRIP OK" : "ROUND TRIP LOST");
    g_print("colour  notify::rgba after a programmatic set: %d\n", rgba_notifies);

    /* setting the same value again -- does GObject filter it? */
    int before = rgba_notifies;
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(cb), &want);
    g_print("colour  notify on setting the identical value again: %d\n",
            rgba_notifies - before);

    /* does blocking the handler suppress it, the way wx will need? */
    before = rgba_notifies;
    GdkRGBA other = { 0.9, 0.1, 0.1, 1.0 };
    g_signal_handlers_block_by_func(cb, (gpointer)on_rgba, NULL);
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(cb), &other);
    g_signal_handlers_unblock_by_func(cb, (gpointer)on_rgba, NULL);
    g_print("colour  notify while the handler is blocked: %d\n",
            rgba_notifies - before);

    /* alpha: does with-alpha=FALSE force alpha back to 1? */
    GdkRGBA half = { 0.2, 0.4, 0.6, 0.5 };
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(cb), &half);
    got = gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(cb));
    g_print("colour  alpha 0.5 stored with with-alpha=FALSE -> %.2f\n", got->alpha);

    /* ---- font ---- */
    GtkFontDialog *fd = gtk_font_dialog_new();
    GtkWidget *fb = gtk_font_dialog_button_new(fd);
    g_signal_connect(fb, "notify::font-desc", G_CALLBACK(on_font), NULL);

    const char *wantfont = "Sans Bold Italic 14";
    PangoFontDescription *desc = pango_font_description_from_string(wantfont);
    gtk_font_dialog_button_set_font_desc(GTK_FONT_DIALOG_BUTTON(fb), desc);
    pango_font_description_free(desc);

    const PangoFontDescription *gotdesc =
        gtk_font_dialog_button_get_font_desc(GTK_FONT_DIALOG_BUTTON(fb));
    char *gotfont = pango_font_description_to_string(gotdesc);
    g_print("font    set(%s) -> get(%s)  %s\n", wantfont, gotfont,
            g_strcmp0(wantfont, gotfont) == 0 ? "ROUND TRIP OK" : "ROUND TRIP CHANGED");
    g_free(gotfont);
    g_print("font    notify::font-desc after a programmatic set: %d\n", font_notifies);

    /* the label: GtkFontButton could be told to leave style and size out of
       its own label.  Can the new one?  Report what the label actually says. */
    gtk_font_dialog_button_set_level(GTK_FONT_DIALOG_BUTTON(fb), GTK_FONT_LEVEL_FONT);
    g_print("font    level FONT   -> use_font=%d use_size=%d\n",
            gtk_font_dialog_button_get_use_font(GTK_FONT_DIALOG_BUTTON(fb)),
            gtk_font_dialog_button_get_use_size(GTK_FONT_DIALOG_BUTTON(fb)));

    /* what does the old button report for the same font, for comparison? */
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkWidget *old = gtk_font_button_new();
    gtk_font_chooser_set_font(GTK_FONT_CHOOSER(old), wantfont);
    char *oldfont = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(old));
    g_print("font    old GtkFontButton reports: %s\n", oldfont);
    g_free(oldfont);
    G_GNUC_END_IGNORE_DEPRECATIONS

    return 0;
}
