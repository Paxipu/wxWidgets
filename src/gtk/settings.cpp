/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/settings.cpp
// Purpose:
// Author:      Robert Roebling
// Modified by: Mart Raudsepp (GetMetric)
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/settings.h"

#ifndef WX_PRECOMP
    #include "wx/log.h"
    #include "wx/toplevel.h"
    #include "wx/module.h"
#endif

#include "wx/display.h"
#include "wx/fontutil.h"
#include "wx/fontenum.h"

#include "wx/gtk/private/wrapgtk.h"
#include "wx/gtk/private/gtk3-compat.h"
#include "wx/gtk/private/win_gtk.h"
#include "wx/gtk/private/stylecontext.h"
#include "wx/gtk/private/value.h"

#ifdef __WXGTK3__
    #include "wx/gtk/private/appearance.h"
    #include "wx/gtk/private/glibptr.h"
    #include "wx/gtk/private/variant.h"
#endif

#ifdef __WXGTK4__
// GdkWindow is gone: this takes the toplevel's GdkSurface now, see toplevel.cpp.
bool wxGetFrameExtents(GdkSurface* window, wxTopLevelWindow::DecorSize* decorSize);
#else
bool wxGetFrameExtents(GdkWindow* window, wxTopLevelWindow::DecorSize* decorSize);
#endif

// ----------------------------------------------------------------------------
// wxSystemSettings implementation
// ----------------------------------------------------------------------------

static wxFont gs_fontSystem;
static int gs_scrollWidth;
static GtkWidget* gs_tlw_parent;

// This is a GtkContainer* under GTK2/3 and a GtkWidget* (of a GtkFixed)
// under GTK4, where GtkContainer doesn't exist any more; callers that need
// to add a child to it should use ContainerWidgetAddChild() below instead
// of gtk_container_add() directly so they work under both.
#ifdef __WXGTK4__
static GtkWidget* ContainerWidget()
{
    static GtkWidget* s_widget;
    if (s_widget == nullptr)
    {
        s_widget = gtk_fixed_new();
        g_object_add_weak_pointer(G_OBJECT(s_widget), (void**)&s_widget);
        gs_tlw_parent = gtk_window_new();
        gtk_window_set_child(GTK_WINDOW(gs_tlw_parent), s_widget);
    }
    return s_widget;
}

static void ContainerWidgetAddChild(GtkWidget* child)
{
    gtk_fixed_put(GTK_FIXED(ContainerWidget()), child, 0, 0);
}
#else
static GtkContainer* ContainerWidget()
{
    static GtkContainer* s_widget;
    if (s_widget == nullptr)
    {
        s_widget = GTK_CONTAINER(gtk_fixed_new());
        g_object_add_weak_pointer(G_OBJECT(s_widget), (void**)&s_widget);
        gs_tlw_parent = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_container_add(GTK_CONTAINER(gs_tlw_parent), GTK_WIDGET(s_widget));
    }
    return s_widget;
}

static void ContainerWidgetAddChild(GtkWidget* child)
{
    gtk_container_add(ContainerWidget(), child);
}
#endif // __WXGTK4__/!__WXGTK4__

static GtkWidget* ScrollBarWidget()
{
    static GtkWidget* s_widget;
    if (s_widget == nullptr)
    {
        s_widget = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, nullptr);
        g_object_add_weak_pointer(G_OBJECT(s_widget), (void**)&s_widget);
        ContainerWidgetAddChild(s_widget);
#ifndef __WXGTK3__
        gtk_widget_ensure_style(s_widget);
#endif
    }
    return s_widget;
}

#ifndef __WXGTK3__

extern "C" {
static void style_set(GtkWidget*, GtkStyle*, void*)
{
    gs_fontSystem = wxNullFont;
    gs_scrollWidth = 0;
}
}

static GtkWidget* ButtonWidget()
{
    static GtkWidget* s_widget;
    if (s_widget == nullptr)
    {
        s_widget = gtk_button_new();
        g_object_add_weak_pointer(G_OBJECT(s_widget), (void**)&s_widget);
        gtk_container_add(ContainerWidget(), s_widget);
        gtk_widget_ensure_style(s_widget);
        g_signal_connect(s_widget, "style_set", G_CALLBACK(style_set), nullptr);
    }
    return s_widget;
}

static GtkWidget* ListWidget()
{
    static GtkWidget* s_widget;
    if (s_widget == nullptr)
    {
        s_widget = gtk_tree_view_new_with_model(
            GTK_TREE_MODEL(gtk_list_store_new(1, G_TYPE_INT)));
        g_object_add_weak_pointer(G_OBJECT(s_widget), (void**)&s_widget);
        gtk_container_add(ContainerWidget(), s_widget);
        gtk_widget_ensure_style(s_widget);
    }
    return s_widget;
}

static GtkWidget* TextCtrlWidget()
{
    static GtkWidget* s_widget;
    if (s_widget == nullptr)
    {
        s_widget = gtk_text_view_new();
        g_object_add_weak_pointer(G_OBJECT(s_widget), (void**)&s_widget);
        gtk_container_add(ContainerWidget(), s_widget);
        gtk_widget_ensure_style(s_widget);
    }
    return s_widget;
}

static GtkWidget* MenuItemWidget()
{
    static GtkWidget* s_widget;
    if (s_widget == nullptr)
    {
        s_widget = gtk_menu_item_new();
        g_object_add_weak_pointer(G_OBJECT(s_widget), (void**)&s_widget);
        gtk_container_add(ContainerWidget(), s_widget);
        gtk_widget_ensure_style(s_widget);
    }
    return s_widget;
}

static GtkWidget* MenuBarWidget()
{
    static GtkWidget* s_widget;
    if (s_widget == nullptr)
    {
        s_widget = gtk_menu_bar_new();
        g_object_add_weak_pointer(G_OBJECT(s_widget), (void**)&s_widget);
        gtk_container_add(ContainerWidget(), s_widget);
        gtk_widget_ensure_style(s_widget);
    }
    return s_widget;
}

static GtkWidget* ToolTipWidget()
{
    static GtkWidget* s_widget;
    if (s_widget == nullptr)
    {
        s_widget = gtk_window_new(GTK_WINDOW_POPUP);
        g_object_add_weak_pointer(G_OBJECT(s_widget), (void**)&s_widget);
        g_signal_connect_swapped(ContainerWidget(), "destroy",
            G_CALLBACK(gtk_widget_destroy), s_widget);
        const char* name = "gtk-tooltip";
        if (!wx_is_at_least_gtk2(11))
            name = "gtk-tooltips";
        gtk_widget_set_name(s_widget, name);
        gtk_widget_ensure_style(s_widget);
    }
    return s_widget;
}
#endif // !__WXGTK3__

#ifdef __WXGTK3__

#if !GTK_CHECK_VERSION(3,12,0)
    #define GTK_STATE_FLAG_LINK (1 << 9)
#endif

static wxColour gs_systemColorCache[wxSYS_COLOUR_MAX + 1];

extern "C" {
static void notify_gtk_theme_name(GObject*, GParamSpec*, void*)
{
    gs_fontSystem.UnRef();
    gs_scrollWidth = 0;
    for (int i = wxSYS_COLOUR_MAX; i--;)
        gs_systemColorCache[i].UnRef();
}

static void notify_gtk_font_name(GObject*, GParamSpec*, void*)
{
    gs_fontSystem.UnRef();
}
}

namespace
{

constexpr const char* TRACE_DARKMODE = "darkmode";

// This corresponds to the current system value.
gboolean gs_systemPrefersDark = FALSE;

// This remembers the last value passed to wxApp::SetAppearance() call.
wxGTKImpl::ColorScheme gs_appScheme = wxGTKImpl::ColorScheme::NoPreference;


// Convert raw value to ColorScheme, return NoPreference for unknown values.
wxGTKImpl::ColorScheme AsColorScheme(guint32 colorScheme)
{
    switch ( colorScheme )
    {
        case static_cast<guint32>(wxGTKImpl::ColorScheme::NoPreference):
        case static_cast<guint32>(wxGTKImpl::ColorScheme::PreferDark):
        case static_cast<guint32>(wxGTKImpl::ColorScheme::PreferLight):
            return static_cast<wxGTKImpl::ColorScheme>(colorScheme);
    }

    wxLogTrace(TRACE_DARKMODE, "Unknown color scheme value %u", colorScheme);
    return wxGTKImpl::ColorScheme::NoPreference;
}

// Convert ColorScheme to raw "prefer-dark" value.
gboolean GetPreferDark(wxGTKImpl::ColorScheme colorScheme)
{
    switch ( colorScheme )
    {
        case wxGTKImpl::ColorScheme::NoPreference:
        case wxGTKImpl::ColorScheme::PreferLight:
            return FALSE;

        case wxGTKImpl::ColorScheme::PreferDark:
            return TRUE;
    }

    wxFAIL_MSG("Invalid color scheme value");
    return FALSE;
}

#ifdef __WXGTK4__

// Values of GtkInterfaceColorScheme, available since GTK 4.20. Keep them
// locally to allow building wxWidgets with older GTK headers while still
// using the new property when running with a newer GTK version.
enum
{
    wxGTK_INTERFACE_COLOR_SCHEME_UNSUPPORTED = 0,
    wxGTK_INTERFACE_COLOR_SCHEME_DEFAULT,
    wxGTK_INTERFACE_COLOR_SCHEME_DARK,
    wxGTK_INTERFACE_COLOR_SCHEME_LIGHT
};

#if GTK_CHECK_VERSION(4,20,0)
static_assert(wxGTK_INTERFACE_COLOR_SCHEME_UNSUPPORTED ==
              static_cast<int>(GTK_INTERFACE_COLOR_SCHEME_UNSUPPORTED));
static_assert(wxGTK_INTERFACE_COLOR_SCHEME_DEFAULT ==
              static_cast<int>(GTK_INTERFACE_COLOR_SCHEME_DEFAULT));
static_assert(wxGTK_INTERFACE_COLOR_SCHEME_DARK ==
              static_cast<int>(GTK_INTERFACE_COLOR_SCHEME_DARK));
static_assert(wxGTK_INTERFACE_COLOR_SCHEME_LIGHT ==
              static_cast<int>(GTK_INTERFACE_COLOR_SCHEME_LIGHT));
#endif

// Return true if GTK supports setting the interface color scheme and put its
// current value in colorScheme. GTK exposes the property even when the desktop
// doesn't support color schemes, in which case the legacy property is still
// needed.
bool GetInterfaceColorScheme(GtkSettings* settings, gint& colorScheme)
{
    constexpr const char* property = "gtk-interface-color-scheme";

    if ( !g_object_class_find_property(G_OBJECT_GET_CLASS(settings), property) )
        return false;

    g_object_get(settings, property, &colorScheme, nullptr);

    return colorScheme != wxGTK_INTERFACE_COLOR_SCHEME_UNSUPPORTED;
}

#endif // __WXGTK4__

// UpdateColorScheme() should normally be used instead of this function to
// avoid changing the preferences unnecessarily and update any
// appearance-dependent cached settings, but it's enough to call this one on
// startup, before we have anything cached.
void UpdatePreferDark(gboolean preferDark)
{
    wxLogTrace(TRACE_DARKMODE, "Turning dark mode preference %s",
               preferDark ? "on" : "off");

    GtkSettings* const settings = gtk_settings_get_default();

#ifdef __WXGTK4__
    gint colorScheme;
    if ( GetInterfaceColorScheme(settings, colorScheme) )
    {
        g_object_set(settings, "gtk-interface-color-scheme",
            preferDark ? wxGTK_INTERFACE_COLOR_SCHEME_DARK
                       : wxGTK_INTERFACE_COLOR_SCHEME_LIGHT,
            nullptr);
        return;
    }
#endif // __WXGTK4__

    g_object_set(settings, "gtk-application-prefer-dark-theme",
        preferDark, nullptr);
}

void DoUpdateColorScheme(wxGTKImpl::ColorScheme colorScheme)
{
    GtkSettings* const settings = gtk_settings_get_default();
    // This shouldn't happen, but don't bother doing anything else if it does.
    if (!settings)
    {
        wxLogTrace(TRACE_DARKMODE, "Failed to get GTK settings");
        return;
    }

    wxGlibPtr<char> themeName;
    gboolean preferDarkPrev = FALSE;
    g_object_get(settings, "gtk-theme-name", themeName.Out(), nullptr);

#ifdef __WXGTK4__
    gint currentColorScheme;
    if ( GetInterfaceColorScheme(settings, currentColorScheme) )
    {
        preferDarkPrev =
            currentColorScheme == wxGTK_INTERFACE_COLOR_SCHEME_DARK;
    }
    else
#endif // __WXGTK4__
    {
        g_object_get(settings, "gtk-application-prefer-dark-theme",
            &preferDarkPrev, nullptr);
    }

    // This is not supposed to happen neither, but don't crash if it does.
    if (!themeName)
    {
        wxLogTrace(TRACE_DARKMODE, "Failed to get GTK theme name");
        return;
    }

    const wxString theme = wxString::FromUTF8(themeName);

    wxLogTrace(TRACE_DARKMODE, "Current GTK theme is \"%s\"", theme);

    // Check if the current theme is a dark variant.
    constexpr const char* darkVariant = "-dark";
    constexpr const char* darkVariantU = "-Dark";
    constexpr size_t lenDark = 5; // strlen(darkVariant) == strlen(darkVariantU)
    auto posDark = theme.find(darkVariant);
    if ( posDark == wxString::npos )
        posDark = theme.find(darkVariantU);

    if ( posDark != wxString::npos )
        preferDarkPrev = TRUE;

    gboolean preferDark = FALSE;
    switch ( colorScheme )
    {
        case wxGTKImpl::ColorScheme::NoPreference:
            preferDark = gs_systemPrefersDark;
            break;

        case wxGTKImpl::ColorScheme::PreferDark:
            preferDark = TRUE;
            break;

        case wxGTKImpl::ColorScheme::PreferLight:
            preferDark = FALSE;
            break;
    }

    if ( preferDark == preferDarkPrev )
    {
        wxLogTrace(TRACE_DARKMODE, "Dark mode preference didn't change");
        return;
    }

    UpdatePreferDark(preferDark);

    if ( posDark != wxString::npos )
    {
        // We need to stop using the dark theme variant when switching to the
        // light application appearance as otherwise it would remain dark.
        wxString themeNew = theme;
        themeNew.erase(posDark, lenDark);

        wxLogTrace(TRACE_DARKMODE, "Switching to theme \"%s\"", themeNew);

        g_object_set(gtk_settings_get_default(),
            "gtk-theme-name", themeNew.utf8_str().data(), nullptr);
    }

    for (int i = wxSYS_COLOUR_MAX; i--;)
        gs_systemColorCache[i].UnRef();

    for (auto* win: wxTopLevelWindows)
    {
        win->SendSysColourChangedEvents();
    }
}

// Global GDBusProxy for org.freedesktop.portal.Settings initialized by
// wxSystemSettingsModule.
GDBusProxy* gs_proxyPortalSettings = nullptr;

} // anonymous namespace

// Functions declared in wx/gtk/private/appearance.h and used by wxApp.
namespace wxGTKImpl
{

bool UpdateColorScheme(ColorScheme colorScheme)
{
    // It's possible that we didn't initialize it because GTK_THEME is
    // explicitly set, so it's not an error -- but we can't change the
    // appearance in this case (it's overridden by the theme), so don't
    // bother doing anything.
    if ( !gs_proxyPortalSettings )
        return false;

    if ( colorScheme == gs_appScheme )
    {
        // Don't do anything if the preference didn't change.
        return true;
    }

    gs_appScheme = colorScheme;

    DoUpdateColorScheme(colorScheme);

    return true;
}

} // namespace wxGTKImpl

// "g-signal" from GDBusProxy
extern "C" {
static void
proxy_g_signal(GDBusProxy*, const char*, const char* signal_name, GVariant* parameters, void*)
{
    if (strcmp(signal_name, "SettingChanged") != 0)
        return;

    const char* nameSpace;
    const char* key;
    wxGtkVariant value;
    g_variant_get(parameters, "(&s&sv)", &nameSpace, &key, value.ByRef());
    if (strcmp(nameSpace, "org.freedesktop.appearance") != 0 ||
        strcmp(key, "color-scheme") != 0)
        return;

    const auto colorScheme = AsColorScheme(value.GetUint32());

    // Update gs_systemPrefersDark in any case as we want to keep track of it
    // even when using app-specified color scheme because this can change later.
    gs_systemPrefersDark = GetPreferDark(colorScheme);

    if ( gs_appScheme == wxGTKImpl::ColorScheme::NoPreference )
    {
        wxLogTrace(TRACE_DARKMODE, "System color scheme changed to %u", colorScheme);

        DoUpdateColorScheme(colorScheme);
    }
    else
    {
        // Application-set scheme should remain in effect even if the system
        // scheme changes.
        wxLogTrace(TRACE_DARKMODE,
                   "Ignoring new system color scheme %u due to app-set scheme %u",
                   colorScheme, gs_appScheme);
    }
}
}

// Some notes on using GtkStyleContext. Style information from a context
// attached to a non-visible GtkWidget is not accurate. The context has an
// internal visibility state, controlled by the widget, which it presumably
// uses to avoid doing unnecessary work. Creating a new style context from the
// GtkWidgetPath in a context attached to a widget also does not work. The path
// does not accurately reproduce the context state with older versions of GTK+,
// and there is no context hierarchy (parent contexts). The hierarchy of parent
// contexts is necessary, even though it would seem that the widget path has
// the same hierarchy in it. So the best way to get style information seems
// to be creating the widget paths and context hierarchy directly.

//-----------------------------------------------------------------------------

#ifndef __WXGTK4__
class wxGtkWidgetPath
{
public:
    wxGtkWidgetPath() : m_path(gtk_widget_path_new()) { }
    ~wxGtkWidgetPath() { gtk_widget_path_free(m_path); }
    operator GtkWidgetPath*() { return m_path; }
private:
    GtkWidgetPath* const m_path;
};
#endif // !__WXGTK4__

//-----------------------------------------------------------------------------
// wxGtkStyleContext
//-----------------------------------------------------------------------------

#ifdef __WXGTK4__

// GTK4 build: back the context with a real (never shown, never realized)
// widget hierarchy instead of a synthetic GtkWidgetPath, which no longer
// exists. See docs/gtk/gtk4-stylecontext-design.md for the reasoning and for
// the probe programs establishing that this actually reproduces the theme's
// values.

bool wxGTKLookupThemeColour(GtkStyleContext* sc, const char* name, wxColour& color)
{
    GdkRGBA rgba;
    if (!sc || !gtk_style_context_lookup_color(sc, name, &rgba))
        return false;

    color = wxColour(rgba);
    return true;
}

// Depth-first search for the nearest descendant with the given CSS name.
//
// This is deliberately a *descendant* search rather than a direct-child one,
// matching CSS descendant-selector semantics -- and, more practically,
// absorbing the places where GTK4 interposes a node the GTK3 synthetic paths
// didn't name (e.g. GtkScrollbar gained a "range" node between "scrollbar"
// and "trough").
static GtkWidget* wxGTKFindCssNode(GtkWidget* parent, const char* name)
{
    for (GtkWidget* child = gtk_widget_get_first_child(parent);
         child; child = gtk_widget_get_next_sibling(child))
    {
        const char* const cssName = gtk_widget_get_css_name(child);
        if (cssName && strcmp(cssName, name) == 0)
            return child;

        if (GtkWidget* const found = wxGTKFindCssNode(child, name))
            return found;
    }

    return nullptr;
}

void wxGtkStyleContext::PopulateForStyleQuery(GtkWidget* widget)
{
    // Some interior CSS nodes don't exist until the widget has content: an
    // empty GtkNotebook has "header" and "tabs" but no "tab" child, so a
    // descent looking for "tab" would silently stop at "tabs" and report that
    // node's padding instead.
    if (GTK_IS_NOTEBOOK(widget))
    {
        gtk_notebook_append_page(GTK_NOTEBOOK(widget),
                                 gtk_label_new(""), gtk_label_new(""));
    }
    else if (GTK_IS_TREE_VIEW(widget))
    {
        // Three columns, to match the three-sibling widget path the GTK3 code
        // built in AddTreeviewHeaderButton(): themes style header buttons with
        // :first-child/:last-child, so the sibling count is significant.
        for (int i = 0; i < 3; i++)
        {
            GtkTreeViewColumn* const column = gtk_tree_view_column_new();
            gtk_tree_view_column_set_title(column, "");
            gtk_tree_view_append_column(GTK_TREE_VIEW(widget), column);
        }
    }
}

void wxGtkStyleContext::AddWidget(GType type)
{
    GtkWidget* const widget = GTK_WIDGET(g_object_new(type, nullptr));
    PopulateForStyleQuery(widget);

    if (m_current == nullptr)
    {
        m_root = widget;
        g_object_ref_sink(m_root);
    }
    else if (GTK_IS_WINDOW(m_current))
    {
        // The exception to the note below. A GtkWindow is a toplevel: GTK
        // tracks it globally and validates its CSS node tree whether or not
        // we ever show it, so this one really does get laid out. Attaching to
        // it with gtk_widget_set_parent() leaves its layout manager unaware of
        // the child, which GTK reports as
        //   Unable to present ... unknown auxiliary child ... GtkWindow
        // and then aborts on, in gtk_css_node_validate(). Since every style
        // query starts at AddWindow(), that made any of them a latent abort.
        gtk_window_set_child(GTK_WINDOW(m_current), widget);
    }
    else
    {
        // gtk_widget_set_parent() is the generic low-level attach; verified to
        // give exactly the same style resolution as the type-specific setters
        // (gtk_button_set_child() etc.), which don't share a common base class
        // under GTK4. Safe here because these widgets are never realized or
        // size-allocated -- we only ever read style values off them.
        gtk_widget_set_parent(widget, m_current);
    }

    // Deepest first, so the destructor can unparent in a safe order.
    m_created = g_slist_prepend(m_created, widget);
    m_current = widget;
}

void wxGtkStyleContext::Descend(const char* objectName)
{
    GtkWidget* const node = wxGTKFindCssNode(m_current, objectName);
    if (node)
    {
        m_current = node;
        return;
    }

    // No such node. This is expected in a few places where GTK4's widget tree
    // genuinely differs from the GTK3 synthetic paths -- GtkFrame has no
    // "border" node, for instance, and carries the border on "frame" itself,
    // so staying put yields the right answer. Staying put is therefore the
    // deliberate behaviour rather than an error, but it turns a structural
    // mismatch into a quietly wrong number, so make it visible in debug
    // builds.
    wxLogTrace("gtk4style", "no CSS node \"%s\" under \"%s\", staying put",
               objectName, gtk_widget_get_css_name(m_current));
}

wxGtkStyleContext::wxGtkStyleContext(double scale)
    : m_scale(int(scale))
{
    m_context = nullptr;
    m_root = nullptr;
    m_current = nullptr;
    m_created = nullptr;
}

wxGtkStyleContext& wxGtkStyleContext::Add(GType type, const char* objectName, ...)
{
    if (m_root == nullptr && type != GTK_TYPE_WINDOW)
        AddWindow();

    // G_TYPE_NONE means "an interior node of the widget we're already on"
    // rather than a new widget of its own.
    if (type == G_TYPE_NONE)
        Descend(objectName);
    else
        AddWidget(type);

    va_list args;
    va_start(args, objectName);
    const char* className;
    while ((className = va_arg(args, char*)))
        gtk_widget_add_css_class(m_current, className);
    va_end(args);

    m_context = gtk_widget_get_style_context(m_current);
    gtk_style_context_set_scale(m_context, m_scale);

    return *this;
}

wxGtkStyleContext& wxGtkStyleContext::Add(const char* objectName)
{
    return Add(G_TYPE_NONE, objectName, nullptr);
}

#else // !__WXGTK4__

wxGtkStyleContext::wxGtkStyleContext(double scale)
    : m_path(gtk_widget_path_new())
    , m_scale(int(scale))
{
    m_context = nullptr;
}

wxGtkStyleContext& wxGtkStyleContext::Add(GType type, const char* objectName, ...)
{
    if (m_context == nullptr && type != GTK_TYPE_WINDOW)
        AddWindow();

    gtk_widget_path_append_type(m_path, type);
#if GTK_CHECK_VERSION(3,20,0)
    if (gtk_check_version(3,20,0) == nullptr)
        gtk_widget_path_iter_set_object_name(m_path, -1, objectName);
#endif
    va_list args;
    va_start(args, objectName);
    const char* className;
    while ((className = va_arg(args, char*)))
        gtk_widget_path_iter_add_class(m_path, -1, className);
    va_end(args);

    GtkStyleContext* sc = gtk_style_context_new();
#if GTK_CHECK_VERSION(3,10,0)
    if (gtk_check_version(3,10,0) == nullptr)
        gtk_style_context_set_scale(sc, m_scale);
#endif
    gtk_style_context_set_path(sc, m_path);
    if (m_context)
    {
#if GTK_CHECK_VERSION(3,4,0)
        if (gtk_check_version(3,4,0) == nullptr)
            gtk_style_context_set_parent(sc, m_context);
#endif
        g_object_unref(m_context);
    }
    m_context = sc;
    return *this;
}

wxGtkStyleContext& wxGtkStyleContext::Add(const char* objectName)
{
    return Add(G_TYPE_NONE, objectName, nullptr);
}

#endif // __WXGTK4__/!__WXGTK4__

#ifdef __WXGTK4__

wxGtkStyleContext::~wxGtkStyleContext()
{
    // m_context and m_current are borrowed; only the widgets we created
    // ourselves need releasing.
    //
    // Widgets attached with gtk_widget_set_parent() are not released when the
    // parent goes away: only a parent that knows about the child unparents it
    // in dispose, and these ones don't. So unparent each explicitly. The list
    // is in reverse creation order, i.e. deepest first, which is the order
    // that leaves no widget holding a dangling child. Unparenting drops the
    // parent's reference, which is the last one, so the widget is finalized
    // there and must not be touched afterwards.
    for (GSList* p = m_created; p; p = p->next)
    {
        GtkWidget* const widget = GTK_WIDGET(p->data);
        if (widget == m_root)
            continue;

        // Detach the way it was attached, see AddWidget().
        GtkWidget* const parent = gtk_widget_get_parent(widget);
        if (parent != nullptr && GTK_IS_WINDOW(parent))
            gtk_window_set_child(GTK_WINDOW(parent), nullptr);
        else
            gtk_widget_unparent(widget);
    }
    g_slist_free(m_created);

    if (m_root)
    {
        if (GTK_IS_WINDOW(m_root))
            gtk_window_destroy(GTK_WINDOW(m_root));
        g_object_unref(m_root);
    }
}

#else // !__WXGTK4__

wxGtkStyleContext::~wxGtkStyleContext()
{
    gtk_widget_path_free(m_path);
    if (m_context == nullptr)
        return;
    if (gtk_check_version(3,16,0) == nullptr || gtk_check_version(3,4,0))
    {
        g_object_unref(m_context);
        return;
    }
#if GTK_CHECK_VERSION(3,4,0)
    // GTK+ < 3.16 does not properly handle freeing child context before parent
    GtkStyleContext* sc = m_context;
    do {
        GtkStyleContext* parent = gtk_style_context_get_parent(sc);
        if (parent)
        {
            g_object_ref(parent);
            gtk_style_context_set_parent(sc, nullptr);
        }
        g_object_unref(sc);
        sc = parent;
    } while (sc);
#endif
}

#endif // __WXGTK4__/!__WXGTK4__

wxGtkStyleContext& wxGtkStyleContext::AddButton()
{
    return Add(GTK_TYPE_BUTTON, "button", "button", nullptr);
}

wxGtkStyleContext& wxGtkStyleContext::AddCheckButton()
{
    return Add(GTK_TYPE_CHECK_BUTTON, "checkbutton", nullptr);
}

#if GTK_CHECK_VERSION(3,10,0)
wxGtkStyleContext& wxGtkStyleContext::AddHeaderbar()
{
    return Add(GTK_TYPE_HEADER_BAR, "headerbar", "titlebar", "header-bar", nullptr);
}
#endif

wxGtkStyleContext& wxGtkStyleContext::AddLabel()
{
    return Add(GTK_TYPE_LABEL, "label", nullptr);
}

wxGtkStyleContext& wxGtkStyleContext::AddMenu()
{
#ifdef __WXGTK4__
    // GtkMenu doesn't exist under GTK4 in any form: menus became GMenuModel
    // plus GtkPopoverMenu. A plain GtkPopover carries the same "menu surface"
    // styling (background, border, shadow) that the queries using this
    // actually want, so it stands in until menu.cpp itself is ported to the
    // popover model -- at which point this should follow whatever that uses.
    return AddWindow("popup").Add(GTK_TYPE_POPOVER, "popover", "background", "menu", nullptr);
#else
    return AddWindow("popup").Add(GTK_TYPE_MENU, "menu", "menu", nullptr);
#endif
}

wxGtkStyleContext& wxGtkStyleContext::AddMenuItem()
{
#ifdef __WXGTK4__
    // Likewise GtkMenuItem: GTK4 popover menus use GtkModelButton, which is
    // not public API, so a GtkButton carrying the same "modelbutton" CSS name
    // is the closest thing that can be built from outside GTK. Approximate --
    // see docs/gtk/gtk4-stylecontext-design.md.
    return AddMenu().Add(GTK_TYPE_BUTTON, "modelbutton", "modelbutton", nullptr);
#else
    return AddMenu().Add(GTK_TYPE_MENU_ITEM, "menuitem", "menuitem", nullptr);
#endif
}

wxGtkStyleContext& wxGtkStyleContext::AddTextview(const char* child1, const char* child2)
{
    Add(GTK_TYPE_TEXT_VIEW, "textview", "view", nullptr);
    if (child1 && gtk_check_version(3,20,0) == nullptr)
    {
        Add(child1);
        if (child2)
            Add(child2);
    }
    return *this;
}

wxGtkStyleContext& wxGtkStyleContext::AddTreeview()
{
    return Add(GTK_TYPE_TREE_VIEW, "treeview", "view", nullptr);
}

#if GTK_CHECK_VERSION(3,20,0)
#ifdef __WXGTK4__

wxGtkStyleContext& wxGtkStyleContext::AddTreeviewHeaderButton(int pos)
{
    // GTK3 described this as "the pos'th of three sibling buttons" with
    // gtk_widget_path_append_with_siblings(), so that themes styling
    // :first-child/:last-child resolved correctly. Here the treeview is a real
    // widget which PopulateForStyleQuery() has already given three columns, so
    // the three header buttons genuinely exist as siblings and we just descend
    // to the right one -- and :first-child/:last-child are real rather than
    // simulated. (GTK4's treeview has no separate "header" node, so the Add()
    // below finds nothing and stays put, which is what we want.)
    AddTreeview().Add("header");

    int index = 0;
    for (GtkWidget* child = gtk_widget_get_first_child(m_current);
         child; child = gtk_widget_get_next_sibling(child))
    {
        const char* const cssName = gtk_widget_get_css_name(child);
        if (cssName && strcmp(cssName, "button") == 0)
        {
            if (index++ == pos)
            {
                m_current = child;
                m_context = gtk_widget_get_style_context(m_current);
                gtk_style_context_set_scale(m_context, m_scale);
                break;
            }
        }
    }

    return *this;
}

#else // !__WXGTK4__

wxGtkStyleContext& wxGtkStyleContext::AddTreeviewHeaderButton(int pos)
{
    AddTreeview().Add("header");
    GtkStyleContext* sc = gtk_style_context_new();

    wxGtkWidgetPath siblings;
    gtk_widget_path_append_type(siblings, GTK_TYPE_BUTTON);
    gtk_widget_path_iter_set_object_name(siblings, -1, "button");
    gtk_widget_path_append_type(siblings, GTK_TYPE_BUTTON);
    gtk_widget_path_iter_set_object_name(siblings, -1, "button");
    gtk_widget_path_append_type(siblings, GTK_TYPE_BUTTON);
    gtk_widget_path_iter_set_object_name(siblings, -1, "button");

    gtk_widget_path_append_with_siblings(m_path, siblings, pos);

    gtk_style_context_set_path(sc, m_path);
    gtk_style_context_set_parent(sc, m_context);
    g_object_unref(m_context);
    m_context = sc;
    return *this;
}

#endif // __WXGTK4__/!__WXGTK4__
#endif // GTK_CHECK_VERSION(3,20,0)

wxGtkStyleContext& wxGtkStyleContext::AddTooltip()
{
#ifdef __WXGTK4__
    wxASSERT(m_root == nullptr);

    // GTK4 paints tooltips with an internal GtkTooltipWindow whose CSS name is
    // "tooltip"; that type isn't public and a CSS name can't be set per
    // instance, so a GtkWindow carrying the tooltip classes is as close as
    // this can get from outside GTK.
    AddWidget(GTK_TYPE_WINDOW);
    gtk_widget_add_css_class(m_current, "background");
    gtk_widget_add_css_class(m_current, "tooltip");
    gtk_widget_set_name(m_current, "gtk-tooltip");

    m_context = gtk_widget_get_style_context(m_current);
    gtk_style_context_set_scale(m_context, m_scale);
    return *this;
#else
    wxASSERT(m_context == nullptr);
    GtkWidgetPath* path = m_path;
    gtk_widget_path_append_type(path, GTK_TYPE_WINDOW);
#if GTK_CHECK_VERSION(3,20,0)
    if (gtk_check_version(3,20,0) == nullptr)
        gtk_widget_path_iter_set_object_name(path, -1, "tooltip");
#endif
    gtk_widget_path_iter_add_class(path, -1, "background");
    gtk_widget_path_iter_add_class(path, -1, "tooltip");
    gtk_widget_path_iter_set_name(path, -1, "gtk-tooltip");
    m_context = gtk_style_context_new();
    gtk_style_context_set_path(m_context, m_path);
    return *this;
#endif
}

wxGtkStyleContext& wxGtkStyleContext::AddWindow(const char* className2)
{
    return Add(GTK_TYPE_WINDOW, "window", "background", className2, nullptr);
}

#ifndef __WXGTK4__
static void bg(GtkStyleContext* sc, wxColour& color, int state)
{
    GdkRGBA* rgba;
    cairo_pattern_t* pattern = nullptr;
    gtk_style_context_set_state(sc, GtkStateFlags(state));
    gtk_style_context_get(sc, GtkStateFlags(state),
        "background-color", &rgba, "background-image", &pattern, nullptr);
    color = wxColour(*rgba);
    gdk_rgba_free(rgba);

    // "background-image" takes precedence over "background-color".
    // If there is an image, try to get a color out of it.
    if (pattern)
    {
        int count;
        switch (cairo_pattern_get_type(pattern))
        {
        default:
            break;
        case CAIRO_PATTERN_TYPE_LINEAR:
        case CAIRO_PATTERN_TYPE_RADIAL:
            cairo_pattern_get_color_stop_count(pattern, &count);
            if (count > 0)
            {
                double r, g, b, a;
                cairo_pattern_get_color_stop_rgba(pattern, 0, nullptr, &r, &g, &b, &a);
                if (count > 1)
                {
                    double r2, g2, b2, a2;
                    cairo_pattern_get_color_stop_rgba(pattern, count - 1, nullptr, &r2, &g2, &b2, &a2);
                    r = (r + r2) / 2;
                    g = (g + g2) / 2;
                    b = (b + b2) / 2;
                    a = (a + a2) / 2;
                }
                color.Set(guchar(r * 255), guchar(g * 255), guchar(b * 255), guchar(a * 255));
            }
            break;
        case CAIRO_PATTERN_TYPE_SURFACE:
            cairo_surface_t* surf;
            cairo_pattern_get_surface(pattern, &surf);
            if (cairo_surface_get_type(surf) == CAIRO_SURFACE_TYPE_IMAGE)
            {
                const guchar* data = cairo_image_surface_get_data(surf);
                const int stride = cairo_image_surface_get_stride(surf);
                // choose a pixel in the middle vertically,
                // images often have a vertical gradient
                const int i = stride * (cairo_image_surface_get_height(surf) / 2);
                const unsigned* p = reinterpret_cast<const unsigned*>(data + i);
                const unsigned pixel = *p;
                guchar r, g, b, a = 0xff;
                switch (cairo_image_surface_get_format(surf))
                {
                case CAIRO_FORMAT_ARGB32:
                    a = guchar(pixel >> 24);
                    if (a == 0)
                        break;
                    wxFALLTHROUGH;
                case CAIRO_FORMAT_RGB24:
                    r = guchar(pixel >> 16);
                    g = guchar(pixel >> 8);
                    b = guchar(pixel);
                    if (a != 0xff)
                    {
                        // un-premultiply
                        r = guchar((r * 0xff) / a);
                        g = guchar((g * 0xff) / a);
                        b = guchar((b * 0xff) / a);
                    }
                    color.Set(r, g, b, a);
                    break;
                default:
                    break;
                }
            }
        }
        cairo_pattern_destroy(pattern);
    }
}
#endif // !__WXGTK4__

#ifdef __WXGTK4__

void wxGtkStyleContext::Bg(wxColour& color, int state) const
{
    // GTK4 removed gtk_style_context_get(), and with it the only way to ask
    // what colour a node's background is painted with. There is no
    // replacement: backgrounds go through render_background() and may be a
    // gradient or an image rather than a flat colour, which is why the query
    // was dropped rather than renamed.
    //
    // What remains is gtk_style_context_lookup_color(), which resolves the
    // named colours a theme defines. Adwaita and the themes derived from it
    // define the three used below, but that is a convention rather than a
    // guarantee, so this is explicitly an approximation -- see
    // docs/gtk/gtk4-stylecontext-design.md. Foreground colours (Fg(), below)
    // are unaffected and remain exact.
    const char* name = "theme_bg_color";

    if (state & GTK_STATE_FLAG_SELECTED)
    {
        name = "theme_selected_bg_color";
    }
    else if (m_current)
    {
        // Widgets that show a document/list surface are themed with the
        // "base" colour rather than the general widget background.
        const char* const cssName = gtk_widget_get_css_name(m_current);
        if (cssName &&
                (strcmp(cssName, "textview") == 0 ||
                 strcmp(cssName, "treeview") == 0 ||
                 strcmp(cssName, "entry") == 0))
        {
            name = "theme_base_color";
        }
    }

    if (!wxGTKLookupThemeColour(m_context, name, color))
    {
        // Theme doesn't define that name; fall back to the most generic one,
        // and leave the colour untouched if even that is missing.
        wxGTKLookupThemeColour(m_context, "theme_bg_color", color);
    }
}

#else // !__WXGTK4__

void wxGtkStyleContext::Bg(wxColour& color, int state) const
{
    for (GtkStyleContext* sc = m_context; sc; )
    {
        bg(sc, color, state);
        if (color.Alpha())
            break;
#if GTK_CHECK_VERSION(3,4,0)
        if (gtk_check_version(3,4,0) == nullptr)
            sc = gtk_style_context_get_parent(sc);
        else
#endif
        {
            // Try TLW as last resort, but not if we're already doing it
            if (gtk_widget_path_length(m_path) > 1)
                wxGtkStyleContext().AddWindow().Bg(color, state);
            break;
        }
    }
}

#endif // __WXGTK4__/!__WXGTK4__

void wxGtkStyleContext::Fg(wxColour& color, int state) const
{
    GdkRGBA rgba;
    gtk_style_context_set_state(m_context, GtkStateFlags(state));
#ifdef __WXGTK4__
    gtk_style_context_get_color(m_context, &rgba);
#else
    gtk_style_context_get_color(m_context, GtkStateFlags(state), &rgba);
#endif
    color = wxColour(rgba);
}

void wxGtkStyleContext::Border(wxColour& color) const
{
#ifdef __WXGTK4__
    // As for Bg(): the "border-color" property query went away with
    // gtk_style_context_get(). "borders" is the name Adwaita-derived themes
    // conventionally use for it. Note this is only the border *colour* --
    // border widths come from gtk_style_context_get_border(), which survives
    // intact and stays exact.
    wxGTKLookupThemeColour(m_context, "borders", color);
#else
    GdkRGBA* rgba;
    gtk_style_context_get(m_context, GTK_STATE_FLAG_NORMAL, "border-color", &rgba, nullptr);
    color = wxColour(*rgba);
    gdk_rgba_free(rgba);
#endif
}

//-----------------------------------------------------------------------------

wxColour wxSystemSettingsNative::GetColour(wxSystemColour index)
{
    if (unsigned(index) > wxSYS_COLOUR_MAX)
        index = wxSYS_COLOUR_MAX;

    wxColour& color = gs_systemColorCache[index];
    if (color.IsOk())
        return color;

    static bool once;
    if (!once)
    {
        once = true;
        g_signal_connect(gtk_settings_get_default(), "notify::gtk-theme-name",
            G_CALLBACK(notify_gtk_theme_name), nullptr);
    }

    wxGtkStyleContext sc;

    switch (index)
    {
    case wxSYS_COLOUR_ACTIVECAPTION:
    case wxSYS_COLOUR_INACTIVECAPTION:
    case wxSYS_COLOUR_GRADIENTACTIVECAPTION:
    case wxSYS_COLOUR_GRADIENTINACTIVECAPTION:
#if GTK_CHECK_VERSION(3,10,0)
        if (gtk_check_version(3,10,0) == nullptr)
        {
            int state = GTK_STATE_FLAG_NORMAL;
            if (index == wxSYS_COLOUR_INACTIVECAPTION ||
                index == wxSYS_COLOUR_GRADIENTINACTIVECAPTION)
            {
                state = GTK_STATE_FLAG_BACKDROP;
            }
            sc.AddHeaderbar().Bg(color, state);
            break;
        }
        wxFALLTHROUGH;
#endif
    case wxSYS_COLOUR_GRIDLINES:
    case wxSYS_COLOUR_3DLIGHT:
    case wxSYS_COLOUR_ACTIVEBORDER:
    case wxSYS_COLOUR_BTNFACE:
    case wxSYS_COLOUR_DESKTOP:
    case wxSYS_COLOUR_INACTIVEBORDER:
    case wxSYS_COLOUR_SCROLLBAR:
    case wxSYS_COLOUR_WINDOWFRAME:
        sc.AddButton().Bg(color);
        break;
    case wxSYS_COLOUR_HIGHLIGHT:
        sc.AddTextview("text", "selection");
        sc.Bg(color, GTK_STATE_FLAG_SELECTED | GTK_STATE_FLAG_FOCUSED);
        break;
    case wxSYS_COLOUR_HIGHLIGHTTEXT:
        sc.AddTextview("text", "selection");
        sc.Fg(color, GTK_STATE_FLAG_SELECTED | GTK_STATE_FLAG_FOCUSED);
        break;
    case wxSYS_COLOUR_WINDOWTEXT:
        sc.AddTextview("text").Fg(color);
        break;
    case wxSYS_COLOUR_BTNHIGHLIGHT:
        sc.AddButton().Bg(color, GTK_STATE_FLAG_PRELIGHT);
        break;
    case wxSYS_COLOUR_BTNSHADOW:
        sc.AddButton().Border(color);
        break;
    case wxSYS_COLOUR_CAPTIONTEXT:
#if GTK_CHECK_VERSION(3,10,0)
        if (gtk_check_version(3,10,0) == nullptr)
        {
            sc.AddHeaderbar().AddLabel().Fg(color);
            break;
        }
        wxFALLTHROUGH;
#endif
    case wxSYS_COLOUR_BTNTEXT:
        sc.AddButton().AddLabel().Fg(color);
        break;
    case wxSYS_COLOUR_INACTIVECAPTIONTEXT:
#if GTK_CHECK_VERSION(3,10,0)
        if (gtk_check_version(3,10,0) == nullptr)
        {
            sc.AddHeaderbar().AddLabel().Fg(color, GTK_STATE_FLAG_BACKDROP);
            break;
        }
        wxFALLTHROUGH;
#endif
    case wxSYS_COLOUR_GRAYTEXT:
        sc.AddLabel().Fg(color, GTK_STATE_FLAG_INSENSITIVE);
        break;
    case wxSYS_COLOUR_HOTLIGHT:
        sc.Add(GTK_TYPE_LINK_BUTTON, "button", "link", nullptr);
        if (wx_is_at_least_gtk3(12))
            sc.Fg(color, GTK_STATE_FLAG_LINK);
#ifndef __WXGTK4__
        else
        {
            wxGCC_WARNING_SUPPRESS(deprecated-declarations)
            wxGtkValue value( GDK_TYPE_COLOR);
            gtk_style_context_get_style_property(sc, "link-color", value);
            GdkColor* link_color = static_cast<GdkColor*>(g_value_get_boxed(value));
            GdkColor gdkColor = { 0, 0, 0, 0xeeee };
            if (link_color)
                gdkColor = *link_color;
            color = wxColour(gdkColor);
            wxGCC_WARNING_RESTORE()
        }
#endif
        break;
    case wxSYS_COLOUR_INFOBK:
        sc.AddTooltip().Bg(color);
        break;
    case wxSYS_COLOUR_INFOTEXT:
        sc.AddTooltip().AddLabel().Fg(color);
        break;
    case wxSYS_COLOUR_LISTBOX:
        sc.AddTreeview().Bg(color);
        break;
    case wxSYS_COLOUR_LISTBOXHIGHLIGHTTEXT:
        sc.AddTreeview().Fg(color, GTK_STATE_FLAG_SELECTED | GTK_STATE_FLAG_FOCUSED);
        break;
    case wxSYS_COLOUR_LISTBOXHIGHLIGHT:
        sc.AddTreeview().Bg(color, GTK_STATE_FLAG_SELECTED | GTK_STATE_FLAG_FOCUSED);
        break;
    case wxSYS_COLOUR_LISTBOXTEXT:
        sc.AddTreeview().Fg(color);
        break;
    case wxSYS_COLOUR_MENU:
        sc.AddMenu().Bg(color);
        break;
    case wxSYS_COLOUR_MENUBAR:
#ifdef __WXGTK4__
        // GtkMenuBar is gone with the rest of the GtkMenu family; GTK4's
        // replacement, GtkPopoverMenuBar, carries the same "menubar" CSS name
        // and is what menu.cpp builds menu bars from.
        sc.Add(GTK_TYPE_POPOVER_MENU_BAR, "menubar", "menubar", nullptr).Bg(color);
#else
        sc.Add(GTK_TYPE_MENU_BAR, "menubar", "menubar", nullptr).Bg(color);
#endif
        break;
    case wxSYS_COLOUR_MENUHILIGHT:
        sc.AddMenuItem().Bg(color, GTK_STATE_FLAG_PRELIGHT);
        break;
    case wxSYS_COLOUR_MENUTEXT:
        sc.AddMenuItem().AddLabel().Fg(color);
        break;
    case wxSYS_COLOUR_APPWORKSPACE:
    case wxSYS_COLOUR_WINDOW:
        sc.AddTextview().Bg(color);
        break;
    case wxSYS_COLOUR_3DDKSHADOW:
        color.Set(0, 0, 0);
        break;
    default:
        wxFAIL_MSG("invalid system colour index");
        color.Set(0, 0, 0, 0);
        break;
    }

    return color;
}
#else // !__WXGTK3__
static const GtkStyle* ButtonStyle()
{
    return gtk_widget_get_style(ButtonWidget());
}

static const GtkStyle* ListStyle()
{
    return gtk_widget_get_style(ListWidget());
}

static const GtkStyle* TextCtrlStyle()
{
    return gtk_widget_get_style(TextCtrlWidget());
}

static const GtkStyle* MenuItemStyle()
{
    return gtk_widget_get_style(MenuItemWidget());
}

static const GtkStyle* MenuBarStyle()
{
    return gtk_widget_get_style(MenuBarWidget());
}

static const GtkStyle* ToolTipStyle()
{
    return gtk_widget_get_style(ToolTipWidget());
}

wxColour wxSystemSettingsNative::GetColour( wxSystemColour index )
{
    wxColor color;
    switch (index)
    {
        case wxSYS_COLOUR_SCROLLBAR:
        case wxSYS_COLOUR_BACKGROUND:
        //case wxSYS_COLOUR_DESKTOP:
        case wxSYS_COLOUR_INACTIVECAPTION:
        case wxSYS_COLOUR_GRADIENTINACTIVECAPTION:
        case wxSYS_COLOUR_MENU:
        case wxSYS_COLOUR_WINDOWFRAME:
        case wxSYS_COLOUR_ACTIVEBORDER:
        case wxSYS_COLOUR_INACTIVEBORDER:
        case wxSYS_COLOUR_BTNFACE:
        case wxSYS_COLOUR_GRIDLINES:
        //case wxSYS_COLOUR_3DFACE:
        case wxSYS_COLOUR_3DLIGHT:
            color = wxColor(ButtonStyle()->bg[GTK_STATE_NORMAL]);
            break;

        case wxSYS_COLOUR_WINDOW:
            color = wxColor(TextCtrlStyle()->base[GTK_STATE_NORMAL]);
            break;

        case wxSYS_COLOUR_MENUBAR:
            color = wxColor(MenuBarStyle()->bg[GTK_STATE_NORMAL]);
            break;

        case wxSYS_COLOUR_3DDKSHADOW:
            color = *wxBLACK;
            break;

        case wxSYS_COLOUR_GRAYTEXT:
        case wxSYS_COLOUR_BTNSHADOW:
        //case wxSYS_COLOUR_3DSHADOW:
            {
                wxColour faceColour(GetColour(wxSYS_COLOUR_3DFACE));
                color =
                   wxColour((unsigned char) (faceColour.Red() * 2 / 3),
                            (unsigned char) (faceColour.Green() * 2 / 3),
                            (unsigned char) (faceColour.Blue() * 2 / 3));
            }
            break;

        case wxSYS_COLOUR_BTNHIGHLIGHT:
        //case wxSYS_COLOUR_BTNHILIGHT:
        //case wxSYS_COLOUR_3DHIGHLIGHT:
        //case wxSYS_COLOUR_3DHILIGHT:
            color = *wxWHITE;
            break;

        case wxSYS_COLOUR_HIGHLIGHT:
            color = wxColor(ButtonStyle()->bg[GTK_STATE_SELECTED]);
            break;

        case wxSYS_COLOUR_LISTBOXHIGHLIGHT:
            color = wxColor(ListStyle()->bg[GTK_STATE_SELECTED]);
            break;

        case wxSYS_COLOUR_LISTBOX:
            color = wxColor(ListStyle()->base[GTK_STATE_NORMAL]);
            break;

        case wxSYS_COLOUR_LISTBOXTEXT:
            color = wxColor(ListStyle()->text[GTK_STATE_NORMAL]);
            break;

        case wxSYS_COLOUR_LISTBOXHIGHLIGHTTEXT:
            // This is for the text in a list control (or tree) when the
            // item is selected, but not focused
            color = wxColor(ListStyle()->text[GTK_STATE_ACTIVE]);
            break;

        case wxSYS_COLOUR_MENUTEXT:
        case wxSYS_COLOUR_WINDOWTEXT:
        case wxSYS_COLOUR_CAPTIONTEXT:
        case wxSYS_COLOUR_INACTIVECAPTIONTEXT:
        case wxSYS_COLOUR_BTNTEXT:
            color = wxColor(ButtonStyle()->fg[GTK_STATE_NORMAL]);
            break;

        case wxSYS_COLOUR_INFOBK:
            color = wxColor(ToolTipStyle()->bg[GTK_STATE_NORMAL]);
            break;

        case wxSYS_COLOUR_INFOTEXT:
            color = wxColor(ToolTipStyle()->fg[GTK_STATE_NORMAL]);
            break;

        case wxSYS_COLOUR_HIGHLIGHTTEXT:
            color = wxColor(ButtonStyle()->fg[GTK_STATE_SELECTED]);
            break;

        case wxSYS_COLOUR_APPWORKSPACE:
            color = *wxWHITE;    // ?
            break;

        case wxSYS_COLOUR_ACTIVECAPTION:
        case wxSYS_COLOUR_GRADIENTACTIVECAPTION:
        case wxSYS_COLOUR_MENUHILIGHT:
            color = wxColor(MenuItemStyle()->bg[GTK_STATE_SELECTED]);
            break;

        case wxSYS_COLOUR_HOTLIGHT:
            {
                GdkColor c = { 0, 0, 0, 0xeeee };
                if (gtk_check_version(2,10,0) == nullptr)
                {
                    GdkColor* linkColor = nullptr;
                    gtk_widget_style_get(ButtonWidget(), "link-color", &linkColor, nullptr);
                    if (linkColor)
                    {
                        c = *linkColor;
                        gdk_color_free(linkColor);
                    }
                }
                color = wxColour(c);
            }
            break;

        case wxSYS_COLOUR_MAX:
        default:
            wxFAIL_MSG( wxT("unknown system colour index") );
            color = *wxWHITE;
            break;
    }

    wxASSERT(color.IsOk());
    return color;
}
#endif // !__WXGTK3__

wxFont wxSystemSettingsNative::GetFont( wxSystemFont index )
{
    wxFont font;
    switch (index)
    {
        case wxSYS_OEM_FIXED_FONT:
        case wxSYS_ANSI_FIXED_FONT:
        case wxSYS_SYSTEM_FIXED_FONT:
            font = *wxNORMAL_FONT;
            break;

        case wxSYS_ANSI_VAR_FONT:
        case wxSYS_SYSTEM_FONT:
        case wxSYS_DEVICE_DEFAULT_FONT:
        case wxSYS_DEFAULT_GUI_FONT:
            if (!gs_fontSystem.IsOk())
            {
                wxNativeFontInfo info;
#ifdef __WXGTK3__
                static bool once;
                if (!once)
                {
                    once = true;
                    g_signal_connect(gtk_settings_get_default(), "notify::gtk-font-name",
                        G_CALLBACK(notify_gtk_font_name), nullptr);
                }
                ContainerWidget();
                int scale = 1;
#if GTK_CHECK_VERSION(3,10,0)
                if (wx_is_at_least_gtk3(10))
                    scale = gtk_widget_get_scale_factor(gs_tlw_parent);
#endif
#ifdef __WXGTK4__
                // gtk_style_context_get() and GTK_STYLE_PROPERTY_FONT are both
                // gone. The UI font is the "gtk-font-name" setting, which is
                // where the style context read it from in the first place and
                // which this code already watches for changes just above.
                wxUnusedVar(scale);

                gchar* fontName = nullptr;
                g_object_get(gtk_settings_get_default(),
                             "gtk-font-name", &fontName, nullptr);

                info.description =
                    pango_font_description_from_string(fontName ? fontName
                                                                : "Sans 10");
                g_free(fontName);
#else
                wxGtkStyleContext sc(scale);
                sc.AddButton().AddLabel();
                gtk_style_context_get(sc, GTK_STATE_FLAG_NORMAL,
                    GTK_STYLE_PROPERTY_FONT, &info.description, nullptr);
#endif
#else
                info.description = ButtonStyle()->font_desc;
#endif
                gs_fontSystem = wxFont(info);

#if wxUSE_FONTENUM
                // (try to) heal the default font (on some common systems e.g. Ubuntu
                // it's "Sans Serif" but the real font is called "Sans"):
                if (!wxFontEnumerator::IsValidFacename(gs_fontSystem.GetFaceName()) &&
                    gs_fontSystem.GetFaceName() == "Sans Serif")
                {
                    gs_fontSystem.SetFaceName("Sans");
                }
#endif // wxUSE_FONTENUM

#ifndef __WXGTK3__
                info.description = nullptr;
#endif
            }
            font = gs_fontSystem;
            break;

        default:
            break;
    }

    wxASSERT( font.IsOk() );

    return font;
}

// helper: return the GtkSettings either for the screen the current window is
// on or for the default screen if window is null
#ifdef __WXGTK4__
// GdkScreen is gone: settings are per-display now, and a window is a surface.
static GtkSettings *GetSettingsForWindowScreen(GdkSurface *window)
{
    return window ? gtk_settings_get_for_display(gdk_surface_get_display(window))
                  : gtk_settings_get_default();
}
#else
static GtkSettings *GetSettingsForWindowScreen(GdkWindow *window)
{
    return window ? gtk_settings_get_for_screen(gdk_window_get_screen(window))
                  : gtk_settings_get_default();
}
#endif

static int GetBorderWidth(wxSystemMetric index, const wxWindow* win)
{
    if (win->m_wxwindow)
    {
        wxPizza* pizza = WX_PIZZA(win->m_wxwindow);
        GtkBorder border;
        pizza->get_border(border);
        switch (index)
        {
            case wxSYS_BORDER_X:
            case wxSYS_EDGE_X:
            case wxSYS_FRAMESIZE_X:
                return border.left;
            default:
                return border.top;
        }
    }
    return -1;
}

#ifdef __WXGTK4__
static GdkRectangle GetMonitorGeom(GdkSurface* window)
{
    GdkRectangle rect = { 0, 0, 0, 0 };

    GdkDisplay* const display = window ? gdk_surface_get_display(window)
                                       : gdk_display_get_default();
    if ( !display )
        return rect;

    GdkMonitor* monitor = nullptr;
    if (window)
    {
        monitor = gdk_display_get_monitor_at_surface(display, window);
    }
    else
    {
        // gdk_display_get_primary_monitor() is gone: GTK4 takes the view that
        // no monitor is more primary than another, so use the first one.
        GListModel* const monitors = gdk_display_get_monitors(display);
        if ( monitors && g_list_model_get_n_items(monitors) )
        {
            monitor = static_cast<GdkMonitor*>(g_list_model_get_item(monitors, 0));
            g_object_unref(monitor); // the list model holds a reference too
        }
    }

    if (monitor)
        gdk_monitor_get_geometry(monitor, &rect);

    return rect;
}
#endif

#if defined(__WXGTK3__) && !defined(__WXGTK4__)
static int GetNodeWidth(wxGtkStyleContext& sc)
{
    int width;
    gtk_style_context_get(sc, GTK_STATE_FLAG_NORMAL, "min-width", &width, nullptr);
    GtkBorder border;
    gtk_style_context_get_padding(sc, GTK_STATE_FLAG_NORMAL, &border);
    width += border.left + border.right;
    gtk_style_context_get_border(sc, GTK_STATE_FLAG_NORMAL, &border);
    width += border.left + border.right;
    gtk_style_context_get_margin(sc, GTK_STATE_FLAG_NORMAL, &border);
    width += border.left + border.right;

    return width < 0 ? 0 : width;
}
#endif // __WXGTK3__ && !__WXGTK4__

static int GetScrollbarWidth()
{
    int width;
#ifdef __WXGTK4__
    // The GTK3 code below sums min-width plus padding/border/margin node by
    // node down the scrollbar's CSS tree. GTK4 removed the "min-width"
    // property query along with gtk_style_context_get(), but it also makes
    // this unnecessary: gtk_widget_measure() on a real scrollbar reports the
    // minimum width with everything CSS contributes at every nesting level
    // already accounted for, which is both simpler and more trustworthy than
    // reconstructing the sum by hand.
    GtkWidget* const sb = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, nullptr);
    g_object_ref_sink(sb);
    gtk_widget_measure(sb, GTK_ORIENTATION_HORIZONTAL, -1,
                       &width, nullptr, nullptr, nullptr);
    g_object_unref(sb);
#else // !__WXGTK4__
#ifdef __WXGTK3__
    if (wx_is_at_least_gtk3(20))
    {
#if GTK_CHECK_VERSION(3,10,0)
        wxGtkStyleContext sc(gtk_widget_get_scale_factor(ScrollBarWidget()));
#else
        wxGtkStyleContext sc;
#endif
        sc.Add(GTK_TYPE_SCROLLBAR, "scrollbar", "scrollbar", "vertical", "right", nullptr);
        width = GetNodeWidth(sc);

        sc.Add("contents");
        width += GetNodeWidth(sc);

        sc.Add("trough");
        width += GetNodeWidth(sc);

        sc.Add("slider");
        width += GetNodeWidth(sc);
    }
    else
#endif // __WXGTK3__
    {
        int slider_width, trough_border;
        gtk_widget_style_get(ScrollBarWidget(),
            "slider-width", &slider_width, "trough-border", &trough_border, nullptr);
        width = slider_width + (2 * trough_border);
    }
#endif // __WXGTK4__/!__WXGTK4__
    return width;
}

int wxSystemSettingsNative::GetMetric( wxSystemMetric index, const wxWindow* win )
{
#ifdef __WXGTK4__
    GdkSurface *window = nullptr;
#else
    GdkWindow *window = nullptr;
#endif
    if (win)
        window = wx_gtk_widget_get_surface_or_window(win->GetHandle());

    switch (index)
    {
        case wxSYS_BORDER_X:
        case wxSYS_BORDER_Y:
        case wxSYS_EDGE_X:
        case wxSYS_EDGE_Y:
        case wxSYS_FRAMESIZE_X:
        case wxSYS_FRAMESIZE_Y:
            if (win)
            {
                wxTopLevelWindow *tlw = wxDynamicCast(win, wxTopLevelWindow);
                if (!tlw)
                    return GetBorderWidth(index, win);
                else if (window)
                {
                    // Get the frame extents from the windowmanager.
                    // In most cases the top extent is the titlebar, so we use the bottom extent
                    // for the heights.
                    wxTopLevelWindow::DecorSize decorSize;
                    if (wxGetFrameExtents(window, &decorSize))
                    {
                        switch (index)
                        {
                            case wxSYS_BORDER_X:
                            case wxSYS_EDGE_X:
                            case wxSYS_FRAMESIZE_X:
                                return decorSize.right;
                            default:
                                return decorSize.bottom;
                        }
                    }
                }
            }

            return -1; // no window specified

        case wxSYS_CURSOR_X:
        case wxSYS_CURSOR_Y:
            {
                gint cursor_size = 0;
                g_object_get(GetSettingsForWindowScreen(window),
                                "gtk-cursor-theme-size", &cursor_size, nullptr);
                if (cursor_size)
                    return cursor_size;

#ifdef __WXGTK4__
                // gdk_display_get_default_cursor_size() is gone; the setting
                // read just above is the only remaining source, so all that is
                // left is the value GTK itself falls back to.
                return 24;
#else
                return gdk_display_get_default_cursor_size(
                            window ? gdk_window_get_display(window)
                                   : gdk_display_get_default());
#endif
            }

        case wxSYS_DCLICK_X:
        case wxSYS_DCLICK_Y:
            gint dclick_distance;
            g_object_get(GetSettingsForWindowScreen(window),
                            "gtk-double-click-distance", &dclick_distance, nullptr);

            return dclick_distance * 2;

        case wxSYS_DCLICK_MSEC:
            gint dclick;
            g_object_get(GetSettingsForWindowScreen(window),
                            "gtk-double-click-time", &dclick, nullptr);
            return dclick;

        case wxSYS_CARET_ON_MSEC:
        case wxSYS_CARET_OFF_MSEC:
            {
                gboolean should_blink = true;
                gint blink_time = -1;
                g_object_get(GetSettingsForWindowScreen(window),
                                "gtk-cursor-blink", &should_blink,
                                "gtk-cursor-blink-time", &blink_time,
                                nullptr);
                if (!should_blink)
                    return 0;

                if (blink_time > 0)
                    return blink_time / 2;

                return -1;
            }

        case wxSYS_CARET_TIMEOUT_MSEC:
            {
                gboolean should_blink = true;
                gint timeout = 0;
                g_object_get(GetSettingsForWindowScreen(window),
                                "gtk-cursor-blink", &should_blink,
                                "gtk-cursor-blink-timeout", &timeout,
                                nullptr);
                if (!should_blink)
                    return 0;

                // GTK+ returns this value in seconds, not milliseconds,
                // Special value of 2147483647 means that the cursor never
                // blinks and we handle any value that would overflow int after
                // multiplication in the same manner as it looks quite
                // unnecessary to support cursor blinking once a month.
                if (timeout > 0 && timeout < 2147483647 / 1000)
                    return timeout * 1000;

                return -1;  // no timeout, blink forever
            }

        case wxSYS_DRAG_X:
        case wxSYS_DRAG_Y:
            gint drag_threshold;
            g_object_get(GetSettingsForWindowScreen(window),
                            "gtk-dnd-drag-threshold", &drag_threshold, nullptr);

            // The correct thing here would be to double the value
            // since that is what the API wants. But the values
            // are much bigger under GNOME than under Windows and
            // just seem to much in many cases to be useful.
            // drag_threshold *= 2;

            return drag_threshold;

        case wxSYS_ICON_X:
        case wxSYS_ICON_Y:
            return 32;

        case wxSYS_SCREEN_X:
#ifdef __WXGTK4__
            return GetMonitorGeom(window).width;
#else
            wxGCC_WARNING_SUPPRESS(deprecated-declarations)
            if (window)
                return gdk_screen_get_width(gdk_window_get_screen(window));
            else
                return gdk_screen_width();
            wxGCC_WARNING_RESTORE()
#endif

        case wxSYS_SCREEN_Y:
#ifdef __WXGTK4__
            return GetMonitorGeom(window).height;
#else
            wxGCC_WARNING_SUPPRESS(deprecated-declarations)
            if (window)
                return gdk_screen_get_height(gdk_window_get_screen(window));
            else
                return gdk_screen_height();
            wxGCC_WARNING_RESTORE()
#endif

        case wxSYS_HSCROLL_Y:
        case wxSYS_VSCROLL_X:
            if (gs_scrollWidth == 0)
                gs_scrollWidth = GetScrollbarWidth();
            return gs_scrollWidth;

        case wxSYS_CAPTION_Y:
            if (!window)
                // No realized window specified, and no implementation for that case yet.
                return -1;

            wxASSERT_MSG( wxDynamicCast(win, wxTopLevelWindow),
                          wxT("Asking for caption height of a non toplevel window") );

            // Get the height of the top windowmanager border.
            // This is the titlebar in most cases. The titlebar might be elsewhere, and
            // we could check which is the thickest wm border to decide on which side the
            // titlebar is, but this might lead to interesting behaviours in used code.
            // Reconsider when we have a way to report to the user on which side it is.
            {
                wxTopLevelWindow::DecorSize decorSize;
                if (wxGetFrameExtents(window, &decorSize))
                {
                    return decorSize.top;
                }
            }

            // Try a default approach without a window pointer, if possible
            // ...

            return -1;

        case wxSYS_PENWINDOWS_PRESENT:
            // No MS Windows for Pen computing extension available in X11 based gtk+.
            return 0;

        default:
            return -1;   // metric is unknown
    }
}

bool wxSystemSettingsNative::HasFeature(wxSystemFeature index)
{
    switch (index)
    {
        case wxSYS_CAN_ICONIZE_FRAME:
            return false;

        case wxSYS_CAN_DRAW_FRAME_DECORATIONS:
            return true;

        default:
            return false;
    }
}

class wxSystemSettingsModule: public wxModule
{
public:
    virtual bool OnInit() override;
    virtual void OnExit() override;

    wxDECLARE_DYNAMIC_CLASS(wxSystemSettingsModule);
};
wxIMPLEMENT_DYNAMIC_CLASS(wxSystemSettingsModule, wxModule);

bool wxSystemSettingsModule::OnInit()
{
#ifdef __WXGTK3__
    // Gnome has gone to a dark style setting rather than a selectable dark
    // theme, available via GSettings as the 'color-scheme' key under the
    // 'org.gnome.desktop.interface' schema. It's also available via a "portal"
    // (https://docs.flatpak.org/en/latest/portal-api-reference.html), which
    // has the advantage of allowing the setting to be accessed from within a
    // virtualized environment such as Flatpak. Since the setting does not
    // change the theme, we propagate it to the corresponding GtkSettings
    // application preference to get a dark theme.

    // If this is not a GUI app
    if (!g_type_class_peek(GTK_TYPE_WIDGET))
        return true;

    // GTK_THEME environment variable overrides other settings
    if (getenv("GTK_THEME") == nullptr)
    {
        gs_proxyPortalSettings = g_dbus_proxy_new_for_bus_sync(
            G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, nullptr,
            "org.freedesktop.portal.Desktop",
            "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.Settings",
            nullptr, nullptr);
    }
    if (gs_proxyPortalSettings)
    {
        g_signal_connect(gs_proxyPortalSettings, "g-signal", G_CALLBACK(proxy_g_signal), nullptr);

        wxGtkVariant ret(g_dbus_proxy_call_sync(gs_proxyPortalSettings, "Read",
            g_variant_new("(ss)", "org.freedesktop.appearance", "color-scheme"),
            G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr));
        if (ret)
        {
            wxGtkVariant child;
            ret.Get("(v)", child.ByRef());

            const auto colorScheme = child.GetVariant().GetUint32();
            wxLogTrace(TRACE_DARKMODE, "Initial color scheme is %u", colorScheme);

            gs_systemPrefersDark = GetPreferDark(AsColorScheme(colorScheme));

            // We only need to do anything here if the color-scheme is dark, as
            // we use the light one by default anyhow.
            if ( gs_systemPrefersDark )
                UpdatePreferDark(TRUE);
        }
    }
#endif // __WXGTK3__
    return true;
}

void wxSystemSettingsModule::OnExit()
{
#ifdef __WXGTK3__
    GtkSettings* settings = gtk_settings_get_default();
    if (settings)
    {
        g_signal_handlers_disconnect_by_func(settings,
            (void*)notify_gtk_theme_name, nullptr);
        g_signal_handlers_disconnect_by_func(settings,
            (void*)notify_gtk_font_name, nullptr);
    }
    if (gs_proxyPortalSettings)
    {
        g_object_unref(gs_proxyPortalSettings);
        gs_proxyPortalSettings = nullptr;
    }
#endif
    if (gs_tlw_parent)
    {
#ifdef __WXGTK4__
        gtk_window_destroy(GTK_WINDOW(gs_tlw_parent));
#else
        gtk_widget_destroy(gs_tlw_parent);
#endif
        gs_tlw_parent = nullptr;
    }
}
