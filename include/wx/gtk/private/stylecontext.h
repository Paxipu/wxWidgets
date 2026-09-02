///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/stylecontext.h
// Purpose:     GtkStyleContext helper class
// Author:      Paul Cornett
// Created:     2018-06-04
// Copyright:   (c) 2018 Paul Cornett
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_PRIVATE_STYLECONTEXT_H_
#define _WX_GTK_PRIVATE_STYLECONTEXT_H_

#ifdef __WXGTK3__

// The colour queries below take a wxColour&, and this header is included by
// files that don't otherwise need the class.
class WXDLLIMPEXP_FWD_CORE wxColour;

class wxGtkStyleContext
{
public:
    explicit wxGtkStyleContext(double scale = 1);
    ~wxGtkStyleContext();
    wxGtkStyleContext& Add(GType type, const char* objectName, ...) G_GNUC_NULL_TERMINATED;
    wxGtkStyleContext& Add(const char* objectName);
    wxGtkStyleContext& AddButton();
    wxGtkStyleContext& AddCheckButton();
    wxGtkStyleContext& AddHeaderbar();
    wxGtkStyleContext& AddLabel();
    wxGtkStyleContext& AddMenu();
    wxGtkStyleContext& AddMenuItem();
    wxGtkStyleContext& AddTextview(const char* child1 = nullptr, const char* child2 = nullptr);
    wxGtkStyleContext& AddTooltip();
    wxGtkStyleContext& AddTreeview();
#if GTK_CHECK_VERSION(3,20,0) && !defined(__WXGTK4__)
    // Only the GTK+ 3 renderer asks for one: the GTK4 one draws a header from
    // the "button" node of a GtkColumnView instead.
    wxGtkStyleContext& AddTreeviewHeaderButton(int pos);
#endif // GTK >= 3.20 && !GTK4
    wxGtkStyleContext& AddWindow(const char* className2 = nullptr);
    void Bg(wxColour& color, int state = GTK_STATE_FLAG_NORMAL) const;
    void Fg(wxColour& color, int state = GTK_STATE_FLAG_NORMAL) const;
    void Border(wxColour& color) const;
#ifdef __WXGTK4__
    // The node this has descended to, as a widget. GTK4 answers style queries
    // about widgets rather than about contexts, so this is what the queries
    // outside this class are given. Never shown and never realized, so it is
    // safe to relayout for wxGTKGetStyleMetrics() below.
    GtkWidget* GetWidget() const { return m_current; }
#else
    operator GtkStyleContext*() { return m_context; }
#endif

private:
#ifndef __WXGTK4__
    GtkStyleContext* m_context;
#endif
#ifdef __WXGTK4__
    // GTK4 removed GtkWidgetPath, gtk_style_context_new() and
    // gtk_style_context_set_parent(): a GtkStyleContext can only be obtained
    // from a real widget now. So instead of describing a synthetic CSS node
    // path, we build an actual (never shown, never realized) widget hierarchy
    // and walk it. See docs/gtk/gtk4-stylecontext-design.md -- in particular,
    // the hierarchy really has to be parented, because a node's style
    // genuinely resolves differently depending on its ancestors.
    //
    // m_root is owned (ref_sink'd, released in the dtor); m_current is
    // borrowed and points at the node we have descended to so far, and
    // m_context is borrowed from m_current.
    GtkWidget* m_root;
    GtkWidget* m_current;
    // Every widget we created ourselves, in reverse creation order (i.e.
    // deepest first). A widget attached with gtk_widget_set_parent() is NOT
    // released when its parent is destroyed -- only a parent that knows about
    // the child unparents it in dispose, and these parents don't -- so each
    // one has to be unparented explicitly, deepest first. Verified with a weak
    // pointer; see docs/gtk/probes/.
    GSList* m_created;

    // Create a widget of the given type and attach it under m_current
    // (becoming m_root if there is nothing yet), then make it current.
    void AddWidget(GType type);
    // Descend to the nearest descendant of m_current whose CSS name matches,
    // staying put if there is none (see the design doc: GTK4's widget tree
    // doesn't have every node the GTK3 synthetic paths named).
    void Descend(const char* objectName);
    // Give a freshly created widget the minimum content its interior CSS
    // nodes need in order to exist at all (e.g. a GtkNotebook has no "tab"
    // node until it has a page).
    static void PopulateForStyleQuery(GtkWidget* widget);
#else
    GtkWidgetPath* const m_path;
#endif
#ifndef __WXGTK4__
    const int m_scale;
#endif

    wxDECLARE_NO_COPY_CLASS(wxGtkStyleContext);
};

#ifdef __WXGTK4__
// Approximate "what colour does the theme paint behind this node".
//
// GTK4 has no API answering this: backgrounds are painted by
// render_background() and may be a gradient or an image rather than a flat
// colour, which is why the flat-colour query was removed outright instead of
// renamed. Looking up the colour names that Adwaita-derived themes
// conventionally define is the closest available approximation -- a
// convention, not a guarantee. Returns false if the theme defines no such
// colour, leaving 'color' untouched.
//
// Shared with control.cpp so this gap has exactly one implementation and one
// place to improve later.
bool wxGTKLookupThemeColour(GtkWidget* widget, const char* name, wxColour& color);

// Ask what padding and border a widget's style gives it.
//
// GTK4 removed gtk_style_context_get_padding() and get_border() with the rest
// of GtkStyleContext and put nothing in their place. What it does still offer
// is gtk_widget_measure(), and the padding and the border are part of what
// that measures -- so the way to read one of them back is to take it away and
// see by how much the widget shrinks.
//
// That is what this does: one CSS class per side per property, all in a single
// provider installed once for the display. Two things about it are
// load-bearing, and both were measured rather than assumed
// (docs/gtk/probes/gtk4-style-metrics-replacements.c):
//
//  - The provider sits at GTK_STYLE_PROVIDER_PRIORITY_USER, above the theme
//    and above anything the application loads. At equal priority the rule
//    that comes later wins, the property never goes to zero, and every side
//    measures 0 while looking perfectly healthy.
//  - The widget has to be told to requeue its resize, or it answers out of
//    its cached size request and, again, everything measures 0.
//
// Because of that requeue, pass a widget it is safe to relayout -- one of the
// never-shown scratch widgets, not a live one being laid out right now. The
// values are the theme's for that widget type, which is what every caller
// wants; wx never sets padding or border on an individual widget.
//
// Either pointer may be null if only the other is wanted.
void wxGTKGetStyleMetrics(GtkWidget* widget, GtkBorder* padding, GtkBorder* border);
#endif

#endif // __WXGTK3__
#endif // _WX_GTK_PRIVATE_STYLECONTEXT_H_
