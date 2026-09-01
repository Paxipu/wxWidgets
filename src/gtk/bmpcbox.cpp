/////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/bmpcbox.cpp
// Purpose:     wxBitmapComboBox
// Author:      Jaakko Salli
// Created:     2008-05-19
// Copyright:   (c) 2008 Jaakko Salli
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "wx/wxprec.h"


#if wxUSE_BITMAPCOMBOBOX

#include "wx/bmpcbox.h"

#ifndef WX_PRECOMP
    #include "wx/log.h"
#endif

#include "wx/gtk/private.h"
#include "wx/gtk/private/value.h"
#include "wx/gtk/private/gtk3-compat.h"
#if GTK_CHECK_VERSION(3,10,0)
    #include <cairo-gobject.h>
#endif

// ============================================================================
// implementation
// ============================================================================


wxIMPLEMENT_DYNAMIC_CLASS(wxBitmapComboBox, wxComboBox);


// ----------------------------------------------------------------------------
// wxBitmapComboBox creation
// ----------------------------------------------------------------------------

void wxBitmapComboBox::Init()
{
#ifndef __WXGTK4__
    m_bitmapCellIndex = 0;
    m_stringCellIndex = 1;
#endif
    m_bitmapSize = wxSize(-1, -1);
}

wxBitmapComboBox::wxBitmapComboBox(wxWindow *parent,
                                  wxWindowID id,
                                  const wxString& value,
                                  const wxPoint& pos,
                                  const wxSize& size,
                                  const wxArrayString& choices,
                                  long style,
                                  const wxValidator& validator,
                                  const wxString& name)
    : wxComboBox(),
      wxBitmapComboBoxBase()
{
    Init();

    Create(parent,id,value,pos,size,choices,style,validator,name);
}

bool wxBitmapComboBox::Create(wxWindow *parent,
                              wxWindowID id,
                              const wxString& value,
                              const wxPoint& pos,
                              const wxSize& size,
                              const wxArrayString& choices,
                              long style,
                              const wxValidator& validator,
                              const wxString& name)
{
    wxCArrayString chs(choices);
    return Create(parent, id, value, pos, size, chs.GetCount(),
                  chs.GetStrings(), style, validator, name);
}

bool wxBitmapComboBox::Create(wxWindow *parent,
                              wxWindowID id,
                              const wxString& value,
                              const wxPoint& pos,
                              const wxSize& size,
                              int n,
                              const wxString choices[],
                              long style,
                              const wxValidator& validator,
                              const wxString& name)
{
    if ( !wxComboBox::Create(parent, id, value, pos, size,
                             n, choices, style, validator, name) )
        return false;

    // Select 'value' in entry-less mode
    if ( !GetEntry() )
    {
        int i = FindString(value);
        if (i != wxNOT_FOUND)
            SetSelection(i);
    }

    return true;
}

#ifdef __WXGTK4__

extern "C" {

static void
wx_gtk_bmpcombo_setup(GtkSignalListItemFactory*, GtkListItem* item, gpointer)
{
    GtkWidget* const box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* const image = gtk_image_new();
    GtkWidget* const label = gtk_label_new(nullptr);

    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);

    gtk_box_append(GTK_BOX(box), image);
    gtk_box_append(GTK_BOX(box), label);
    gtk_list_item_set_child(item, box);
}

static void
wx_gtk_bmpcombo_bind(GtkSignalListItemFactory*, GtkListItem* item, gpointer data)
{
    wxBitmapComboBox* const combo = static_cast<wxBitmapComboBox*>(data);

    GtkWidget* const box = gtk_list_item_get_child(item);
    GtkWidget* const image = gtk_widget_get_first_child(box);
    GtkWidget* const label = gtk_widget_get_next_sibling(image);

    GtkStringObject* const obj =
        GTK_STRING_OBJECT(gtk_list_item_get_item(item));
    gtk_label_set_label(GTK_LABEL(label),
                        obj ? gtk_string_object_get_string(obj) : "");

    combo->GTKSetRowImage(image, gtk_list_item_get_position(item));
}

} // extern "C"

void wxBitmapComboBox::GTKSetRowImage(void* image, unsigned int n) const
{
    GtkImage* const gtkImage = GTK_IMAGE(image);

    if ( n >= m_bitmaps.size() || !m_bitmaps[n].IsOk() )
    {
        gtk_image_clear(gtkImage);
        return;
    }

    const wxBitmap bmp = m_bitmaps[n].GetBitmapFor(this);
    if ( !bmp.IsOk() )
    {
        gtk_image_clear(gtkImage);
        return;
    }

    GdkTexture* const texture = gdk_texture_new_for_pixbuf(bmp.GetPixbuf());
    gtk_image_set_from_paintable(gtkImage, GDK_PAINTABLE(texture));
    g_object_unref(texture);

    const wxSize size = bmp.GetLogicalSize();
    gtk_widget_set_size_request(GTK_WIDGET(gtkImage), size.x, size.y);
}

void wxBitmapComboBox::GTKCreateComboBoxWidget()
{
    // The entry and the popover list are wxComboBox's; only what a row looks
    // like differs, so this takes that widget and gives its list a factory
    // that puts an image beside the label.
    wxComboBox::GTKCreateComboBoxWidget();

    GtkListItemFactory* const factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup",
                     G_CALLBACK(wx_gtk_bmpcombo_setup), nullptr);
    g_signal_connect(factory, "bind",
                     G_CALLBACK(wx_gtk_bmpcombo_bind), this);

    gtk_list_view_set_factory(GTK_LIST_VIEW(m_listView), factory);
    g_object_unref(factory);
}

int wxBitmapComboBox::DoInsertItems(const wxArrayStringsAdapter& items,
                                    unsigned int pos,
                                    void **clientData,
                                    wxClientDataType type)
{
    const int n = wxComboBox::DoInsertItems(items, pos, clientData, type);

    // Sorted controls put each item where it belongs rather than at pos, so
    // the bitmaps follow the strings rather than the requested position.
    const unsigned int count = GetCount();
    while ( m_bitmaps.size() < count )
        m_bitmaps.insert(m_bitmaps.begin() + wxMin(pos, m_bitmaps.size()),
                         wxBitmapBundle());

    return n;
}

void wxBitmapComboBox::DoDeleteOneItem(unsigned int n)
{
    if ( n < m_bitmaps.size() )
        m_bitmaps.erase(m_bitmaps.begin() + n);

    wxComboBox::DoDeleteOneItem(n);
}

void wxBitmapComboBox::DoClear()
{
    m_bitmaps.clear();

    wxComboBox::DoClear();
}

#else // !__WXGTK4__

void wxBitmapComboBox::GTKCreateComboBoxWidget()
{
    GtkListStore *store;

    GType imageType;
    const char* imageAttr;
#ifdef __WXGTK4__
    // GtkCellRendererPixbuf has no "surface" property any more. Its GTK4
    // replacement is "texture", taking a GdkTexture -- note that it is not
    // "paintable", which the renderer does not have either.
    imageType = GDK_TYPE_TEXTURE;
    imageAttr = "texture";
#elif GTK_CHECK_VERSION(3,10,0)
    if (wx_is_at_least_gtk3(10))
    {
        imageType = CAIRO_GOBJECT_TYPE_SURFACE;
        imageAttr = "surface";
    }
    else
#endif
#ifndef __WXGTK4__
    {
        imageType = G_TYPE_OBJECT;
        imageAttr = "pixbuf";
    }
#endif // !__WXGTK4__
    store = gtk_list_store_new(2, imageType, G_TYPE_STRING);

    if ( HasFlag(wxCB_READONLY) )
    {
        m_widget = gtk_combo_box_new_with_model( GTK_TREE_MODEL(store) );
    }
    else
    {
#ifdef __WXGTK3__
        m_widget = gtk_combo_box_new_with_model_and_entry(GTK_TREE_MODEL(store));
        gtk_combo_box_set_entry_text_column(GTK_COMBO_BOX(m_widget), m_stringCellIndex);
#else
        m_widget = gtk_combo_box_entry_new_with_model( GTK_TREE_MODEL(store), m_stringCellIndex );
#endif
#ifdef __WXGTK4__
        m_entry = GTK_ENTRY(gtk_combo_box_get_child(GTK_COMBO_BOX(m_widget)));
#else
        m_entry = GTK_ENTRY(gtk_bin_get_child(GTK_BIN(m_widget)));
#endif
        g_object_add_weak_pointer(G_OBJECT(m_entry), (void**)&m_entry);
        gtk_editable_set_editable(GTK_EDITABLE(m_entry), true);
    }
    g_object_ref(m_widget);

    // gtk_combo_box_new_with_model() and friends are (transfer none): the
    // combo box takes a reference of its own, so the one gtk_list_store_new()
    // returned above is ours to drop. Without this every wxBitmapComboBox
    // leaked its store, and everything in it.
    g_object_unref(store);

    // This must be called as gtk_combo_box_entry_new_with_model adds
    // automatically adds one text column.
    gtk_cell_layout_clear( GTK_CELL_LAYOUT(m_widget) );

    GtkCellRenderer* imageRenderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT(m_widget),
                                imageRenderer, FALSE);
    gtk_cell_layout_add_attribute( GTK_CELL_LAYOUT(m_widget),
                                   imageRenderer, imageAttr, 0);

    GtkCellRenderer* textRenderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_end( GTK_CELL_LAYOUT(m_widget),
                              textRenderer, TRUE );
    gtk_cell_layout_add_attribute( GTK_CELL_LAYOUT(m_widget),
                                   textRenderer, "text", 1);
}

#endif // __WXGTK4__/!__WXGTK4__

wxBitmapComboBox::~wxBitmapComboBox()
{
}

GtkWidget* wxBitmapComboBox::GetConnectWidget() const
{
    if ( GetEntry() )
        return wxComboBox::GetConnectWidget();

    return wxChoice::GetConnectWidget();
}

#ifndef __WXGTK4__
GdkWindow *wxBitmapComboBox::GTKGetWindow(wxArrayGdkWindows& windows) const
{
    if ( GetEntry() )
        return wxComboBox::GTKGetWindow(windows);

    return wxChoice::GTKGetWindow(windows);
}
#endif // !__WXGTK4__

wxSize wxBitmapComboBox::DoGetBestSize() const
{
    wxSize best = wxComboBox::DoGetBestSize();

    int delta = GetBitmapSize().y - GetCharHeight();
    if ( delta > 0 )
        best.y += delta;

    return best;
}

// ----------------------------------------------------------------------------
// Item manipulation
// ----------------------------------------------------------------------------

#ifdef __WXGTK4__

void wxBitmapComboBox::SetItemBitmap(unsigned int n, const wxBitmapBundle& bitmap)
{
    wxCHECK_RET( n < GetCount(), wxT("invalid index") );

    if ( m_bitmaps.size() < GetCount() )
        m_bitmaps.resize(GetCount());

    m_bitmaps[n] = bitmap;

    const wxBitmap bmp = bitmap.GetBitmapFor(this);
    if ( bmp.IsOk() && m_bitmapSize.x < 0 )
        m_bitmapSize = bmp.GetLogicalSize();

    // The bitmaps are not in the list's model, so changing one tells the list
    // nothing. Writing the row's string back, unchanged, is what makes GTK
    // rebind that row and ask the factory for the image again.
    SetString(n, GetString(n));

    if ( int(n) == GetSelection() )
        GTKUpdateEntryBitmap();
}

wxBitmap wxBitmapComboBox::GetItemBitmap(unsigned int n) const
{
    if ( n >= m_bitmaps.size() )
        return wxBitmap();

    // Unlike the GTK+ 3 build, which round-trips the bitmap through the model
    // and loses its scale factor on the way, this gives back what was set.
    return m_bitmaps[n].GetBitmapFor(this);
}

void wxBitmapComboBox::GTKUpdateEntryBitmap()
{
    GtkEntry* const entry = GetEntry();
    if ( !entry )
        return;

    // A GtkDropDown showed the selected item with the same cell renderers as
    // the list, bitmap included. This control's closed state is an entry, and
    // what an entry has in that place is an icon.
    const int sel = GetSelection();
    const wxBitmap bmp = sel == wxNOT_FOUND ? wxBitmap()
                                            : GetItemBitmap(unsigned(sel));
    if ( !bmp.IsOk() )
    {
        gtk_entry_set_icon_from_paintable(entry, GTK_ENTRY_ICON_PRIMARY,
                                          nullptr);
        return;
    }

    GdkTexture* const texture = gdk_texture_new_for_pixbuf(bmp.GetPixbuf());
    gtk_entry_set_icon_from_paintable(entry, GTK_ENTRY_ICON_PRIMARY,
                                      GDK_PAINTABLE(texture));
    g_object_unref(texture);
}

#else // !__WXGTK4__

void wxBitmapComboBox::SetItemBitmap(unsigned int n, const wxBitmapBundle& bitmap)
{
    wxBitmap bmp = bitmap.GetBitmapFor(this);
    if ( bmp.IsOk() )
    {
        if ( m_bitmapSize.x < 0 )
        {
            m_bitmapSize = bmp.GetLogicalSize();
        }

        GtkComboBox* combobox = GTK_COMBO_BOX( m_widget );
        GtkTreeModel *model = gtk_combo_box_get_model( combobox );
        GtkTreeIter iter;

        if ( gtk_tree_model_iter_nth_child( model, &iter, nullptr, n ) )
        {
            wxGtkValue value0;
#ifdef __WXGTK4__
            // A texture carries no device scale, unlike the cairo surface it
            // replaces, so the scale factor is not round-tripped through the
            // model any more; GetItemBitmap() below says the same.
            {
                g_value_init(value0, GDK_TYPE_TEXTURE);

                GdkTexture* const texture =
                    gdk_texture_new_for_pixbuf(bmp.GetPixbuf());
                g_value_set_object(value0, texture);
                g_object_unref(texture);
            }
#else // !__WXGTK4__
#if GTK_CHECK_VERSION(3,10,0)
            if (wx_is_at_least_gtk3(10))
            {
                g_value_init(value0, CAIRO_GOBJECT_TYPE_SURFACE);
                cairo_surface_t* surface = gdk_cairo_surface_create_from_pixbuf(
                    bmp.GetPixbuf(), 1, gtk_widget_get_window(m_widget));
                const double scaleFactor = bmp.GetScaleFactor();
                cairo_surface_set_device_scale(surface, scaleFactor, scaleFactor);
                g_value_set_boxed(value0, surface);
                cairo_surface_destroy(surface);
            }
            else
#endif
            {
                g_value_init(value0, G_TYPE_OBJECT);
                g_value_set_object( value0, bmp.GetPixbuf() );
            }
#endif // __WXGTK4__/!__WXGTK4__
            gtk_list_store_set_value( GTK_LIST_STORE(model), &iter,
                                      m_bitmapCellIndex, value0 );
        }
    }
}

wxBitmap wxBitmapComboBox::GetItemBitmap(unsigned int n) const
{
    wxBitmap bitmap;

    GtkComboBox* combobox = GTK_COMBO_BOX( m_widget );
    GtkTreeModel *model = gtk_combo_box_get_model( combobox );
    GtkTreeIter iter;

    if (gtk_tree_model_iter_nth_child (model, &iter, nullptr, n))
    {
        wxGtkValue value;
        gtk_tree_model_get_value( model, &iter,
                                  m_bitmapCellIndex, value );
#ifdef __WXGTK4__
        {
            // See SetItemBitmap(): a texture has no device scale, so the
            // bitmap comes back at its default scale factor of 1.
            GdkTexture* const texture = GDK_TEXTURE(g_value_get_object(value));
            if (texture)
            {
                // Not gdk_texture_download() into a pixbuf's own buffer: that
                // writes premultiplied ARGB in native byte order, which is not
                // what a GdkPixbuf holds. This does the conversion.
                //
                // wxBitmap takes ownership of the pixbuf, which is why there
                // is no unref here and why the GTK3 branch below, whose pixbuf
                // is borrowed from the GValue, has to add a reference first.
                bitmap = wxBitmap(gdk_pixbuf_get_from_texture(texture));
            }
        }
#else // !__WXGTK4__
#if GTK_CHECK_VERSION(3,10,0)
        if (wx_is_at_least_gtk3(10))
        {
            cairo_surface_t* surface = static_cast<cairo_surface_t*>(g_value_get_boxed(value));
            if (surface)
            {
                const int w = cairo_image_surface_get_width(surface);
                const int h = cairo_image_surface_get_height(surface);
                bitmap = wxBitmap(gdk_pixbuf_get_from_surface(surface, 0, 0, w, h));
                double sx, sy;
                cairo_surface_get_device_scale(surface, &sx, &sy);
                bitmap.SetScaleFactor(sx);
            }
        }
        else
#endif
        {
            GdkPixbuf* pixbuf = (GdkPixbuf*) g_value_get_object( value );
            if ( pixbuf )
            {
                g_object_ref( pixbuf );
                bitmap = wxBitmap(pixbuf);
            }
        }
#endif // __WXGTK4__/!__WXGTK4__
    }

    return bitmap;
}

#endif // __WXGTK4__/!__WXGTK4__

int wxBitmapComboBox::Append(const wxString& item, const wxBitmapBundle& bitmap)
{
    const int n = wxComboBox::Append(item);
    if ( n != wxNOT_FOUND )
        SetItemBitmap(n, bitmap);
    return n;
}

int wxBitmapComboBox::Append(const wxString& item, const wxBitmapBundle& bitmap,
                             void *clientData)
{
    const int n = wxComboBox::Append(item, clientData);
    if ( n != wxNOT_FOUND )
        SetItemBitmap(n, bitmap);
    return n;
}

int wxBitmapComboBox::Append(const wxString& item, const wxBitmapBundle& bitmap,
                             wxClientData *clientData)
{
    const int n = wxComboBox::Append(item, clientData);
    if ( n != wxNOT_FOUND )
        SetItemBitmap(n, bitmap);
    return n;
}

int wxBitmapComboBox::Insert(const wxString& item,
                             const wxBitmapBundle& bitmap,
                             unsigned int pos)
{
    const int n = wxComboBox::Insert(item, pos);
    if ( n != wxNOT_FOUND )
        SetItemBitmap(n, bitmap);
    return n;
}

int wxBitmapComboBox::Insert(const wxString& item, const wxBitmapBundle& bitmap,
                             unsigned int pos, wxClientData *clientData)
{
    const int n = wxComboBox::Insert(item, pos, clientData);
    if ( n != wxNOT_FOUND )
        SetItemBitmap(n, bitmap);
    return n;
}

int wxBitmapComboBox::Insert(const wxString& item, const wxBitmapBundle& bitmap,
                             unsigned int pos, void *clientData)
{
    const int n = wxComboBox::Insert(item, pos, clientData);
    if ( n != wxNOT_FOUND )
        SetItemBitmap(n, bitmap);
    return n;
}

// ----------------------------------------------------------------------------
// wxTextEntry interface override
// ----------------------------------------------------------------------------

void wxBitmapComboBox::WriteText(const wxString& value)
{
    if ( GetEntry() )
        wxComboBox::WriteText(value);
    else
        SetStringSelection(value);
}

wxString wxBitmapComboBox::GetValue() const
{
    if ( GetEntry() )
        return wxComboBox::GetValue();

    return GetStringSelection();
}

void wxBitmapComboBox::Remove(long from, long to)
{
    if ( GetEntry() )
        wxComboBox::Remove(from, to);
}

void wxBitmapComboBox::SetInsertionPoint(long pos)
{
    if ( GetEntry() )
        wxComboBox::SetInsertionPoint(pos);
}

long wxBitmapComboBox::GetInsertionPoint() const
{
    if ( GetEntry() )
        return wxComboBox::GetInsertionPoint();

    return 0;
 }
long wxBitmapComboBox::GetLastPosition() const
{
    if ( GetEntry() )
        return wxComboBox::GetLastPosition();

    return 0;
 }

void wxBitmapComboBox::SetSelection(long from, long to)
{
    if ( GetEntry() )
        wxComboBox::SetSelection(from, to);
}

void wxBitmapComboBox::GetSelection(long *from, long *to) const
{
    if ( GetEntry() )
        wxComboBox::GetSelection(from, to);
}

bool wxBitmapComboBox::IsEditable() const
{
    if ( GetEntry() )
        return wxTextEntry::IsEditable();

    return false;
}

void wxBitmapComboBox::SetEditable(bool editable)
{
    if ( GetEntry() )
        wxComboBox::SetEditable(editable);
}

#endif // wxUSE_BITMAPCOMBOBOX
