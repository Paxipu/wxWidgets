/* gtk_style_context_get_padding() and get_border() are deprecated, and
   gtk4-style-query-replacements.c already showed that gtk_widget_measure() of
   an empty widget equals padding+border.  That is enough for the call sites
   that add the two together, and not enough for the three that don't:

     src/gtk/win_gtk.cpp   wants the border alone (it draws a frame of that
                           thickness, so padding would make it too thick)
     src/gtk/spinbutt.cpp  wants the padding alone (it subtracts it)
     src/gtk/statbox.cpp   wants them one after the other, but only ever adds

   So: can the two be separated with supported API?  The idea measured here is
   a CSS class, registered once display-wide, that zeroes one of them.  Measure
   the widget, add the class, measure again, and the difference is the property
   that went away.  Nothing else on the display carries the class, so the
   provider is inert everywhere else.

   Printed side by side with what the deprecated calls say, because a
   replacement that is merely plausible is what this whole area of the port
   keeps getting wrong. */
#include <gtk/gtk.h>

static void measure(GtkWidget *w, int *hor, int *ver)
{
    gtk_widget_measure(w, GTK_ORIENTATION_HORIZONTAL, -1, hor, NULL, NULL, NULL);
    gtk_widget_measure(w, GTK_ORIENTATION_VERTICAL, -1, ver, NULL, NULL, NULL);
}

/* min size with the named class on, minus min size with it off */
static void delta_for_class(GtkWidget *w, const char *cls, int *dh, int *dv)
{
    int h0, v0, h1, v1;
    measure(w, &h0, &v0);
    gtk_widget_add_css_class(w, cls);
    /* Without this the widget answers out of its cached size request and the
       class appears to do nothing at all.  gtk_widget_get_color() needs no
       such thing -- it reflects the class immediately -- which is exactly the
       kind of difference that makes a plausible-looking replacement wrong. */
    gtk_widget_queue_resize(w);
    measure(w, &h1, &v1);
    gtk_widget_remove_css_class(w, cls);
    gtk_widget_queue_resize(w);
    *dh = h0 - h1;
    *dv = v0 - v1;
}

/* The call sites in statbox.cpp and win_gtk.cpp want the individual sides,
   not just the per-axis sum, so zero one side at a time. */
static void sides_for(GtkWidget *w, const char *prefix, GtkBorder *out)
{
    static const char* const side[] = { "left", "right", "top", "bottom" };
    int v[4];
    for ( int i = 0; i < 4; i++ )
    {
        char cls[64];
        g_snprintf(cls, sizeof(cls), "wxprobe-no-%s-%s", prefix, side[i]);

        int h0, v0, h1, v1;
        measure(w, &h0, &v0);
        gtk_widget_add_css_class(w, cls);
        gtk_widget_queue_resize(w);
        measure(w, &h1, &v1);
        gtk_widget_remove_css_class(w, cls);
        gtk_widget_queue_resize(w);

        v[i] = (i < 2) ? h0 - h1 : v0 - v1;
    }
    out->left = v[0]; out->right = v[1]; out->top = v[2]; out->bottom = v[3];
}

static void check(const char *what, GtkWidget *w)
{
    /* the widget has to be in a window for its style to resolve the way the
       theme means it; nothing here is ever shown */
    GtkWidget *win = gtk_window_new();
    gtk_window_set_child(GTK_WINDOW(win), w);

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkStyleContext *sc = gtk_widget_get_style_context(w);
    GtkBorder pad, bor;
    gtk_style_context_get_padding(sc, &pad);
    gtk_style_context_get_border(sc, &bor);
    G_GNUC_END_IGNORE_DEPRECATIONS

    int padh, padv, borh, borv;
    delta_for_class(w, "wxprobe-no-padding", &padh, &padv);
    delta_for_class(w, "wxprobe-no-border", &borh, &borv);

    g_print("%s\n", what);
    g_print("   deprecated   padding h=%d v=%d   border h=%d v=%d\n",
            pad.left + pad.right, pad.top + pad.bottom,
            bor.left + bor.right, bor.top + bor.bottom);
    g_print("   measured     padding h=%d v=%d   border h=%d v=%d\n",
            padh, padv, borh, borv);
    GtkBorder padSides, borSides;
    sides_for(w, "padding", &padSides);
    sides_for(w, "border", &borSides);
    g_print("   deprecated   padding l%d r%d t%d b%d   border l%d r%d t%d b%d\n",
            pad.left, pad.right, pad.top, pad.bottom,
            bor.left, bor.right, bor.top, bor.bottom);
    g_print("   measured     padding l%d r%d t%d b%d   border l%d r%d t%d b%d\n",
            padSides.left, padSides.right, padSides.top, padSides.bottom,
            borSides.left, borSides.right, borSides.top, borSides.bottom);
    g_print("   -> per side  padding %s, border %s\n",
            (padSides.left == pad.left && padSides.right == pad.right &&
             padSides.top == pad.top && padSides.bottom == pad.bottom)
                ? "EXACT" : "DIFFERS",
            (borSides.left == bor.left && borSides.right == bor.right &&
             borSides.top == bor.top && borSides.bottom == bor.bottom)
                ? "EXACT" : "DIFFERS");
    g_print("   -> per axis  padding %s, border %s\n",
            (padh == pad.left + pad.right && padv == pad.top + pad.bottom)
                ? "EXACT" : "DIFFERS",
            (borh == bor.left + bor.right && borv == bor.top + bor.bottom)
                ? "EXACT" : "DIFFERS");

    gtk_window_set_child(GTK_WINDOW(win), NULL);
    gtk_window_destroy(GTK_WINDOW(win));
}

/* The same question for an interior CSS node rather than the widget itself:
   win_gtk.cpp asks a GtkTreeView/GtkEntry, statbox.cpp asks a GtkFrame's
   "border" node. */
static GtkWidget *find_node(GtkWidget *parent, const char *name)
{
    for ( GtkWidget *c = gtk_widget_get_first_child(parent); c;
          c = gtk_widget_get_next_sibling(c) )
    {
        const char *n = gtk_widget_get_css_name(c);
        if ( n && strcmp(n, name) == 0 )
            return c;
        GtkWidget *found = find_node(c, name);
        if ( found )
            return found;
    }
    return NULL;
}

int main(void)
{
    gtk_init();

    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_string(p,
        ".wxprobe-no-padding { padding: 0; }\n"
        ".wxprobe-no-border { border-width: 0; }\n"
        ".wxprobe-no-padding-left { padding-left: 0; }\n"
        ".wxprobe-no-padding-right { padding-right: 0; }\n"
        ".wxprobe-no-padding-top { padding-top: 0; }\n"
        ".wxprobe-no-padding-bottom { padding-bottom: 0; }\n"
        ".wxprobe-no-border-left { border-left-width: 0; }\n"
        ".wxprobe-no-border-right { border-right-width: 0; }\n"
        ".wxprobe-no-border-top { border-top-width: 0; }\n"
        ".wxprobe-no-border-bottom { border-bottom-width: 0; }\n");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                               GTK_STYLE_PROVIDER(p),
                                               GTK_STYLE_PROVIDER_PRIORITY_USER);

    check("GtkEntry           (control.cpp: GTKGetEntryMargins)", gtk_entry_new());
    check("GtkSpinButton      (spinbutt.cpp: DoGetBestSize)",
          gtk_spin_button_new_with_range(0, 10, 1));
    check("GtkFrame           (statbox.cpp: GetBordersForSizer)", gtk_frame_new(NULL));
    check("GtkScrolledWindow  (win_gtk.cpp: get_border, scrolled case)",
          gtk_scrolled_window_new());

    /* Adversarial: all four sides different, so a mapping that happens to be
       right for the symmetric values every theme uses cannot pass by luck.

       This goes in a *second* provider at APPLICATION priority, which is where
       an application's own CSS lives, and below the USER priority the zeroing
       rules use.  Priority is load-bearing here: in the first version of this
       check both rules sat in one provider, the asymmetric one came later in
       the file, and it therefore beat the zeroing rule at equal specificity --
       every difference measured zero and the check reported that the whole
       approach had failed.  It is the ordering that has to be right, not the
       approach. */
    {
        GtkCssProvider *asym = gtk_css_provider_new();
        gtk_css_provider_load_from_string(asym,
            ".wxprobe-asym { padding: 3px 5px 7px 11px;"
            "                border-style: solid;"
            "                border-width: 2px 4px 6px 8px; }\n");
        gtk_style_context_add_provider_for_display(
            gdk_display_get_default(), GTK_STYLE_PROVIDER(asym),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        GtkWidget *ae = gtk_entry_new();
        gtk_widget_add_css_class(ae, "wxprobe-asym");
        check("GtkEntry with deliberately asymmetric padding and border", ae);
    }

    /* and one interior node, which is what several of the call sites really
       want: an entry's own frame rather than the entry widget */
    GtkWidget *win = gtk_window_new();
    GtkWidget *entry = gtk_entry_new();
    gtk_window_set_child(GTK_WINDOW(win), entry);
    GtkWidget *text = find_node(entry, "text");
    if ( text )
    {
        int eh, ev, th, tv;
        measure(entry, &eh, &ev);
        measure(text, &th, &tv);
        g_print("GtkEntry minus its inner \"text\" node: h=%d v=%d\n",
                eh - th, ev - tv);
    }
    else
        g_print("GtkEntry has no \"text\" child node\n");

    return 0;
}
